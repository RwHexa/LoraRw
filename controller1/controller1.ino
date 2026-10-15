// Controller 1 -- Sensorseite der LoRa-Funkstrecke.
//
// Sendet zwei Schaltzustaende und einen Messwert (0..100) an Controller 2 und
// schaltet seinerseits einen Ausgang nach dem Schaltbefehl, den Controller 2
// zurueckmeldet. Der Schaltbefehl reist in der Quittung auf die Telemetrie mit,
// deshalb braucht der Rueckkanal kein eigenes Lebenszeichen.
//
// Verdrahtung und DTU-Einstellungen: siehe docs/ im Projektordner.

#include "../shared/lora_protocol.h"

// --- Pinbelegung ------------------------------------------------------------
static const int PIN_STATE1  = 32;  // Zustand 1, Schalter nach GND
static const int PIN_STATE2  = 33;  // Zustand 2, Schalter nach GND
static const int PIN_VALUE   = 34;  // Messwert, ADC1, nur Eingang
static const int PIN_RELAY   = 25;  // Ausgang fuer den empfangenen Schaltbefehl
static const int PIN_LINK    = 2;   // Status-LED: dauerhaft = Link ok, blinkend = Failsafe
static const int PIN_LORA_RX = 16;  // an den TTL-Ausgang des MAX3232
static const int PIN_LORA_TX = 17;  // an den TTL-Eingang des MAX3232

static const uint32_t DEBOUNCE_MS = 50;

// --- Eingangsentprellung ----------------------------------------------------
struct Debounced {
  int      pin;
  bool     state;
  bool     raw;
  uint32_t changedMs;
};

static Debounced inState1 = {PIN_STATE1, false, false, 0};
static Debounced inState2 = {PIN_STATE2, false, false, 0};

static void debounceUpdate(Debounced &d, uint32_t now) {
  bool raw = (digitalRead(d.pin) == LOW);  // geschlossen nach GND = ein
  if (raw != d.raw) {
    d.raw = raw;
    d.changedMs = now;
  }
  if (raw != d.state && (uint32_t)(now - d.changedMs) >= DEBOUNCE_MS) {
    d.state = raw;
  }
}

static uint8_t readValue() {
  uint32_t sum = 0;
  for (uint8_t i = 0; i < 8; i++) {
    sum += (uint32_t)analogRead(PIN_VALUE);
  }
  uint32_t scaled = ((sum / 8) * 100UL) / 4095UL;
  if (scaled > 100) {
    scaled = 100;
  }
  // Traeges IIR-Filter: daempft das ADC-Rauschen, damit nicht jedes Zittern
  // des Eingangs einen Funkframe ausloest.
  static uint32_t acc    = 0;
  static bool     primed = false;
  if (!primed) {
    acc    = scaled * 16UL;
    primed = true;
  }
  acc = acc - (acc / 16UL) + scaled;
  return (uint8_t)(acc / 16UL);
}

// --- Funkzustand ------------------------------------------------------------
static LoraParser  parser;
static LoraAirtime airtime;

static uint8_t   txSeq        = 0;
static bool      ackPending   = false;
static uint8_t   ackRetries   = 0;
static uint32_t  ackDeadline  = 0;
static LoraFrame pendingFrame = {0, 0, 0, 0};

static uint32_t lastTxMs        = 0;
static uint32_t lastTelemetryMs = 0;
static uint32_t lastRxMs        = 0;
static bool     linkUp          = false;
static bool     linkEverUp      = false;

static uint8_t sentFlags = 0xFF;  // 0xFF = noch nichts gesendet, erzwingt den ersten Frame
static uint8_t sentValue = 0;

static bool    cmdState    = false;  // vom Partner empfangener Schaltbefehl
static bool    cmdSeqValid = false;
static uint8_t lastCmdSeq  = 0;

static uint32_t ackWindow() {
  return LORA_ACK_WAIT_MS + (uint32_t)random((long)LORA_ACK_JITTER_MS + 1);
}

static void sendFrame(const LoraFrame &frame) {
  loraSend(Serial2, frame);
  lastTxMs = millis();
  airtime.account(lastTxMs);
}

static void applyCommand(bool on) {
  if (on == cmdState) {
    return;
  }
  cmdState = on;
  digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  Serial.printf("[cmd] Schaltbefehl %s\n", on ? "EIN" : "AUS");
}

static void sendTelemetry(uint32_t now, uint8_t flags, uint8_t value) {
  txSeq++;
  pendingFrame.type  = LORA_TYPE_TELEMETRY;
  pendingFrame.seq   = txSeq;
  pendingFrame.flags = flags;
  pendingFrame.value = value;

  sendFrame(pendingFrame);
  ackPending      = true;
  ackRetries      = 0;
  ackDeadline     = now + ackWindow();
  lastTelemetryMs = now;
  sentFlags       = flags;
  sentValue       = value;

  Serial.printf("[tx] Telemetrie seq=%u Z1=%u Z2=%u Wert=%u Luft=%u/%ums\n",
                pendingFrame.seq,
                (flags & LORA_FLAG_STATE1) ? 1 : 0,
                (flags & LORA_FLAG_STATE2) ? 1 : 0,
                value,
                (unsigned)airtime.usedMs(),
                (unsigned)LORA_AIRTIME_BUDGET_MS);
}

static void handleRx(uint32_t now) {
  LoraFrame frame;
  while (Serial2.available() > 0) {
    if (!parser.feed((uint8_t)Serial2.read(), now, frame)) {
      continue;
    }
    lastRxMs = now;

    if (frame.type == LORA_TYPE_ACK) {
      if (ackPending && frame.seq == pendingFrame.seq) {
        ackPending = false;
        // Der aktuelle Schaltbefehl reist in der Quittung mit.
        applyCommand((frame.flags & LORA_FLAG_COMMAND) != 0);
      }
    } else if (frame.type == LORA_TYPE_COMMAND) {
      // Immer quittieren, auch Wiederholungen -- sonst sendet die Gegenseite weiter.
      LoraFrame ack = {LORA_TYPE_ACK, frame.seq, 0, LORA_VALUE_INVALID};
      sendFrame(ack);
      if (!cmdSeqValid || frame.seq != lastCmdSeq) {
        lastCmdSeq  = frame.seq;
        cmdSeqValid = true;
        applyCommand((frame.flags & LORA_FLAG_COMMAND) != 0);
      }
    }
  }
}

static void updateLink(uint32_t now) {
  bool up = linkEverUp && ((uint32_t)(now - lastRxMs) < LORA_LINK_TIMEOUT_MS);
  if (up != linkUp) {
    linkUp = up;
    if (up) {
      Serial.println("[link] verbunden");
    } else {
      Serial.println("[link] Timeout -- Failsafe, Ausgang wird abgeschaltet");
      applyCommand(false);
    }
  }
  digitalWrite(PIN_LINK, linkUp ? HIGH : (((now / 500) % 2) ? HIGH : LOW));
}

void setup() {
  pinMode(PIN_STATE1, INPUT_PULLUP);
  pinMode(PIN_STATE2, INPUT_PULLUP);
  pinMode(PIN_RELAY, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_LINK, OUTPUT);
  digitalWrite(PIN_LINK, LOW);
  analogReadResolution(12);

  Serial.begin(115200);
  Serial2.begin(LORA_BAUD, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
  randomSeed((unsigned long)(ESP.getEfuseMac() & 0xFFFFFFFFUL) ^ (unsigned long)micros());

  Serial.println();
  Serial.println("Controller 1 (Sensorseite) bereit");
}

void loop() {
  uint32_t now = millis();

  debounceUpdate(inState1, now);
  debounceUpdate(inState2, now);
  uint8_t value = readValue();

  handleRx(now);
  if (lastRxMs != 0) {
    linkEverUp = true;
  }

  uint8_t flags = 0;
  if (inState1.state) {
    flags |= LORA_FLAG_STATE1;
  }
  if (inState2.state) {
    flags |= LORA_FLAG_STATE2;
  }

  uint8_t delta = (value > sentValue) ? (uint8_t)(value - sentValue)
                                     : (uint8_t)(sentValue - value);
  // Zustandswechsel gehen sofort raus, Messwertaenderungen nur gebremst --
  // sonst kann ein zappelnder Analogeingang den Duty-Cycle sprengen.
  bool flagsChanged = (flags != sentFlags);
  bool valueChanged = (delta >= LORA_VALUE_HYSTERESE) &&
                      ((uint32_t)(now - lastTelemetryMs) >= LORA_VALUE_MIN_MS);
  bool changed   = flagsChanged || valueChanged;
  // Bei gestoerter Strecke seltener senden -- die Wiederholungen kosten Sendezeit,
  // und schneller als der Failsafe-Zeitraum muss die Erholung nicht sein.
  uint32_t heartbeatMs = linkUp ? LORA_HEARTBEAT_MS : LORA_HEARTBEAT_DOWN_MS;
  bool heartbeat = (uint32_t)(now - lastTelemetryMs) >= heartbeatMs;
  bool gapOk     = (uint32_t)(now - lastTxMs) >= LORA_MIN_GAP_MS;

  if (ackPending) {
    if ((int32_t)(now - ackDeadline) >= 0) {
      if (ackRetries < LORA_MAX_RETRIES && airtime.available(now)) {
        ackRetries++;
        sendFrame(pendingFrame);
        ackDeadline = now + ackWindow();
        Serial.printf("[tx] Wiederholung %u fuer seq=%u\n", ackRetries, pendingFrame.seq);
      } else {
        ackPending = false;  // aufgeben, der naechste Heartbeat versucht es erneut
        Serial.printf("[tx] keine Quittung fuer seq=%u\n", pendingFrame.seq);
      }
    }
  } else if (gapOk && (changed || heartbeat) && airtime.available(now)) {
    sendTelemetry(now, flags, value);
  }

  updateLink(now);
}
