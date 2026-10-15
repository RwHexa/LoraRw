// Controller 2 -- Schalterseite der LoRa-Funkstrecke.
//
// Empfaengt die zwei Zustaende und den Messwert von Controller 1 und sendet
// einen Schaltbefehl zurueck. Jede Telemetrie wird quittiert, und die Quittung
// traegt den aktuellen Schaltbefehl gleich mit -- dadurch holt sich die Strecke
// den richtigen Befehl auch dann zurueck, wenn ein eigener Befehlsframe
// verloren gegangen ist.
//
// Verdrahtung und DTU-Einstellungen: siehe docs/ im Projektordner.

#include "../shared/lora_protocol.h"

// --- Pinbelegung ------------------------------------------------------------
static const int PIN_BUTTON  = 27;  // Taster nach GND, schaltet den Befehl um
static const int PIN_LED1    = 25;  // zeigt Zustand 1 von Controller 1
static const int PIN_LED2    = 26;  // zeigt Zustand 2 von Controller 1
static const int PIN_LINK    = 2;   // Status-LED: dauerhaft = Link ok, blinkend = Failsafe
static const int PIN_LORA_RX = 16;  // an den TTL-Ausgang des MAX3232
static const int PIN_LORA_TX = 17;  // an den TTL-Eingang des MAX3232

static const uint32_t DEBOUNCE_MS = 50;

// --- Taster -----------------------------------------------------------------
static bool     btnState     = false;
static bool     btnRaw       = false;
static uint32_t btnChangedMs = 0;

// --- Funkzustand ------------------------------------------------------------
static LoraParser  parser;
static LoraAirtime airtime;

static uint8_t   txSeq        = 0;
static bool      ackPending   = false;
static uint8_t   ackRetries   = 0;
static uint32_t  ackDeadline  = 0;
static LoraFrame pendingFrame = {0, 0, 0, 0};

static uint32_t lastTxMs   = 0;
static uint32_t lastRxMs   = 0;
static bool     linkUp     = false;
static bool     linkEverUp = false;

static bool cmdState = false;  // eigener Schaltbefehl
static bool sentCmd  = false;  // Stand, der bereits auf die Reise geschickt wurde

static bool    telemSeqValid = false;
static uint8_t lastTelemSeq  = 0;

static uint32_t ackWindow() {
  return LORA_ACK_WAIT_MS + (uint32_t)random((long)LORA_ACK_JITTER_MS + 1);
}

static void sendFrame(const LoraFrame &frame) {
  loraSend(Serial2, frame);
  lastTxMs = millis();
  airtime.account(lastTxMs);
}

static void showTelemetry(const LoraFrame &frame) {
  bool state1 = (frame.flags & LORA_FLAG_STATE1) != 0;
  bool state2 = (frame.flags & LORA_FLAG_STATE2) != 0;
  digitalWrite(PIN_LED1, state1 ? HIGH : LOW);
  digitalWrite(PIN_LED2, state2 ? HIGH : LOW);
  Serial.printf("[rx] seq=%u Zustand1=%s Zustand2=%s Messwert=%u\n",
                frame.seq,
                state1 ? "EIN" : "AUS",
                state2 ? "EIN" : "AUS",
                frame.value);
}

static void sendCommand(uint32_t now) {
  txSeq++;
  pendingFrame.type  = LORA_TYPE_COMMAND;
  pendingFrame.seq   = txSeq;
  pendingFrame.flags = cmdState ? LORA_FLAG_COMMAND : 0;
  pendingFrame.value = LORA_VALUE_INVALID;

  sendFrame(pendingFrame);
  ackPending  = true;
  ackRetries  = 0;
  ackDeadline = now + ackWindow();
  sentCmd     = cmdState;

  Serial.printf("[tx] Schaltbefehl %s seq=%u\n", cmdState ? "EIN" : "AUS", pendingFrame.seq);
}

static void handleRx(uint32_t now) {
  LoraFrame frame;
  while (Serial2.available() > 0) {
    if (!parser.feed((uint8_t)Serial2.read(), now, frame)) {
      continue;
    }
    lastRxMs = now;

    if (frame.type == LORA_TYPE_TELEMETRY) {
      // Immer quittieren, auch Wiederholungen -- und den Befehl mitgeben.
      LoraFrame ack = {LORA_TYPE_ACK,
                       frame.seq,
                       (uint8_t)(cmdState ? LORA_FLAG_COMMAND : 0),
                       LORA_VALUE_INVALID};
      sendFrame(ack);
      if (!telemSeqValid || frame.seq != lastTelemSeq) {
        lastTelemSeq  = frame.seq;
        telemSeqValid = true;
        showTelemetry(frame);
      }
    } else if (frame.type == LORA_TYPE_ACK) {
      if (ackPending && frame.seq == pendingFrame.seq) {
        ackPending = false;
        Serial.printf("[tx] quittiert seq=%u\n", frame.seq);
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
      Serial.println("[link] Timeout -- Failsafe, Anzeige wird abgeschaltet");
      digitalWrite(PIN_LED1, LOW);
      digitalWrite(PIN_LED2, LOW);
    }
  }
  digitalWrite(PIN_LINK, linkUp ? HIGH : (((now / 500) % 2) ? HIGH : LOW));
}

void setup() {
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  pinMode(PIN_LED1, OUTPUT);
  pinMode(PIN_LED2, OUTPUT);
  pinMode(PIN_LINK, OUTPUT);
  digitalWrite(PIN_LED1, LOW);
  digitalWrite(PIN_LED2, LOW);
  digitalWrite(PIN_LINK, LOW);

  Serial.begin(115200);
  Serial2.begin(LORA_BAUD, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
  randomSeed((unsigned long)(ESP.getEfuseMac() & 0xFFFFFFFFUL) ^ (unsigned long)micros());

  Serial.println();
  Serial.println("Controller 2 (Schalterseite) bereit");
}

void loop() {
  uint32_t now = millis();

  bool raw = (digitalRead(PIN_BUTTON) == LOW);
  if (raw != btnRaw) {
    btnRaw = raw;
    btnChangedMs = now;
  }
  if (raw != btnState && (uint32_t)(now - btnChangedMs) >= DEBOUNCE_MS) {
    btnState = raw;
    if (btnState) {  // nur die fallende Flanke schaltet um
      cmdState = !cmdState;
      Serial.printf("[in] Taster -- Schaltbefehl jetzt %s\n", cmdState ? "EIN" : "AUS");
    }
  }

  handleRx(now);
  if (lastRxMs != 0) {
    linkEverUp = true;
  }

  bool gapOk = (uint32_t)(now - lastTxMs) >= LORA_MIN_GAP_MS;

  if (ackPending) {
    if ((int32_t)(now - ackDeadline) >= 0) {
      if (ackRetries < LORA_MAX_RETRIES && airtime.available(now)) {
        ackRetries++;
        sendFrame(pendingFrame);
        ackDeadline = now + ackWindow();
        Serial.printf("[tx] Wiederholung %u fuer seq=%u\n", ackRetries, pendingFrame.seq);
      } else {
        ackPending = false;
        // Nicht weiter senden: die naechste Quittung auf eine Telemetrie
        // traegt den aktuellen Befehl ohnehin mit.
        Serial.printf("[tx] keine Quittung fuer seq=%u\n", pendingFrame.seq);
      }
    }
  } else if (cmdState != sentCmd && gapOk && airtime.available(now)) {
    sendCommand(now);
  }

  updateLink(now);
}
