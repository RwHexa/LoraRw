// Gemeinsames Funkprotokoll fuer die E90-DTU-Strecke.
// Wird von controller1 und controller2 eingebunden -- Frameaufbau und CRC
// stehen nur hier, damit beide Seiten nicht auseinanderlaufen koennen.
#ifndef LORA_PROTOCOL_H
#define LORA_PROTOCOL_H

#include <Arduino.h>

// --- Sendezeit-Budget -------------------------------------------------------
// Die Intervalle weiter unten sind so gewaehlt, dass der Duty-Cycle eingehalten
// wird. Wiederholungen und ein zappelnder Analogeingang koennen die Rechnung
// aber sprengen, deshalb zaehlt diese Klasse die verbrauchte Luftzeit mit und
// verweigert weitere Frames, wenn das Budget im laufenden Fenster erschoepft ist.
// 1 Prozent von 60 s waeren 600 ms -- 250 ms sind bewusst konservativ, damit
// auch zwei ungluecklich benachbarte Fenster unter dem Grenzwert bleiben.
static const uint32_t LORA_AIRTIME_MS        = 25;     // Luftzeit je Frame bei 9,6 kbps
static const uint32_t LORA_AIRTIME_WINDOW_MS = 60000;
static const uint32_t LORA_AIRTIME_BUDGET_MS = 250;

class LoraAirtime {
 public:
  // Darf jetzt noch ein Frame raus?
  bool available(uint32_t now) {
    rollWindow(now);
    return (usedMs_ + LORA_AIRTIME_MS) <= LORA_AIRTIME_BUDGET_MS;
  }

  // Nach jedem tatsaechlich gesendeten Frame aufrufen. Quittungen werden
  // mitgezaehlt, aber nie blockiert -- eine verweigerte Quittung wuerde die
  // Gegenseite nur zu weiteren Wiederholungen treiben.
  void account(uint32_t now) {
    rollWindow(now);
    usedMs_ += LORA_AIRTIME_MS;
  }

  uint32_t usedMs() const { return usedMs_; }

 private:
  void rollWindow(uint32_t now) {
    if (!started_ || (uint32_t)(now - windowStartMs_) >= LORA_AIRTIME_WINDOW_MS) {
      windowStartMs_ = now;
      usedMs_        = 0;
      started_       = true;
    }
  }

  uint32_t windowStartMs_ = 0;
  uint32_t usedMs_        = 0;
  bool     started_       = false;
};

// --- Frameaufbau (7 Byte) ---------------------------------------------------
// 0    Startbyte 0xAA
// 1    Typ
// 2    Sequenznummer
// 3    Flags
// 4    Messwert 0..100 (0xFF = ungueltig)
// 5..6 CRC16 (Modbus) ueber Offset 0..4
static const uint8_t LORA_START_BYTE = 0xAA;
static const uint8_t LORA_FRAME_LEN  = 7;
static const uint8_t LORA_CRC_OFFSET = 5;

static const uint8_t LORA_TYPE_TELEMETRY = 0x01;
static const uint8_t LORA_TYPE_COMMAND   = 0x02;
static const uint8_t LORA_TYPE_ACK       = 0x03;

static const uint8_t LORA_FLAG_STATE1  = 0x01;
static const uint8_t LORA_FLAG_STATE2  = 0x02;
static const uint8_t LORA_FLAG_COMMAND = 0x04;

static const uint8_t LORA_VALUE_INVALID = 0xFF;

// --- Zeiten und Grenzen -----------------------------------------------------
static const uint32_t LORA_BAUD            = 9600;   // muss zur DTU-Einstellung passen
static const uint32_t LORA_HEARTBEAT_MS    = 10000;  // Lebenszeichen von Controller 1
static const uint32_t LORA_LINK_TIMEOUT_MS = 30000;  // danach Failsafe
static const uint32_t LORA_HEARTBEAT_DOWN_MS = 30000;  // Lebenszeichen bei gestoerter Strecke:
                                                       // Heartbeat plus Wiederholungen wuerden
                                                       // im 10-s-Takt den Duty-Cycle ausreizen
static const uint32_t LORA_MIN_GAP_MS      = 300;    // Sendebremse fuer den Duty-Cycle
static const uint32_t LORA_ACK_WAIT_MS     = 250;    // Wartezeit auf die Quittung
static const uint32_t LORA_ACK_JITTER_MS   = 150;    // Zufallsanteil gegen Dauerkollisionen
static const uint8_t  LORA_MAX_RETRIES     = 3;
static const uint32_t LORA_PARSER_IDLE_MS  = 100;    // danach gilt ein Teilframe als verloren
static const uint8_t  LORA_VALUE_HYSTERESE = 2;      // ab dieser Aenderung wird gesendet
static const uint32_t LORA_VALUE_MIN_MS    = 5000;    // Mindestabstand messwertgetriebener Frames:
                                                     // 25 ms Luftzeit je 5 s sind 0,5 % Duty-Cycle

struct LoraFrame {
  uint8_t type;
  uint8_t seq;
  uint8_t flags;
  uint8_t value;
};

// CRC16/Modbus -- klein, verbreitet, erkennt die typischen Funkfehler zuverlaessig.
static uint16_t loraCrc16(const uint8_t *data, uint8_t len) {
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (crc & 0x0001) {
        crc = (uint16_t)((crc >> 1) ^ 0xA001);
      } else {
        crc >>= 1;
      }
    }
  }
  return crc;
}

static void loraEncode(const LoraFrame &frame, uint8_t *out) {
  out[0] = LORA_START_BYTE;
  out[1] = frame.type;
  out[2] = frame.seq;
  out[3] = frame.flags;
  out[4] = frame.value;
  uint16_t crc = loraCrc16(out, LORA_CRC_OFFSET);
  out[5] = (uint8_t)(crc & 0xFF);
  out[6] = (uint8_t)(crc >> 8);
}

static void loraSend(Stream &port, const LoraFrame &frame) {
  uint8_t buf[LORA_FRAME_LEN];
  loraEncode(frame, buf);
  port.write(buf, LORA_FRAME_LEN);
}

// Zustandsmaschine fuer den Empfang. Die Funkstrecke liefert Bytes haeufig
// zerstueckelt, deshalb wird byteweise gesammelt statt zeilenweise gelesen.
class LoraParser {
 public:
  // Gibt true zurueck, sobald ein vollstaendiger Frame mit gueltigem CRC vorliegt.
  bool feed(uint8_t byte, uint32_t now, LoraFrame &out) {
    if (count_ > 0 && (uint32_t)(now - lastByteMs_) > LORA_PARSER_IDLE_MS) {
      count_ = 0;  // Rest eines zerrissenen Frames verwerfen
    }
    lastByteMs_ = now;

    if (count_ == 0 && byte != LORA_START_BYTE) {
      return false;  // auf das Startbyte synchronisieren
    }
    buf_[count_++] = byte;
    if (count_ < LORA_FRAME_LEN) {
      return false;
    }

    count_ = 0;
    uint16_t crc = loraCrc16(buf_, LORA_CRC_OFFSET);
    if (buf_[5] != (uint8_t)(crc & 0xFF) || buf_[6] != (uint8_t)(crc >> 8)) {
      crcErrors++;
      return false;
    }
    out.type  = buf_[1];
    out.seq   = buf_[2];
    out.flags = buf_[3];
    out.value = buf_[4];
    frames++;
    return true;
  }

  uint32_t frames    = 0;
  uint32_t crcErrors = 0;

 private:
  uint8_t  buf_[LORA_FRAME_LEN];
  uint8_t  count_      = 0;
  uint32_t lastByteMs_ = 0;
};

#endif  // LORA_PROTOCOL_H
