// Stufe 2 der Verifikation: rohe Funkstrecke pruefen, ohne Protokoll.
//
// Auf beide ESP32 flashen. Jedes Board sendet jede Sekunde "TEST" ueber das
// DTU und gibt alles Empfangene als Hex auf dem seriellen Monitor aus.
// Erst wenn hier Bytes ankommen, lohnt sich der Protokollcode.

static const int PIN_LORA_RX = 16;
static const int PIN_LORA_TX = 17;

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, PIN_LORA_RX, PIN_LORA_TX);
  Serial.println();
  Serial.println("Linktest bereit -- sendet jede Sekunde TEST");
}

void loop() {
  static uint32_t lastMs = 0;
  uint32_t now = millis();

  if ((uint32_t)(now - lastMs) >= 1000) {
    lastMs = now;
    Serial2.print("TEST");
    Serial.println("-> TEST gesendet");
  }

  while (Serial2.available() > 0) {
    int byteIn = Serial2.read();
    Serial.printf("<- 0x%02X %c\n", byteIn,
                  (byteIn >= 32 && byteIn < 127) ? (char)byteIn : '.');
  }
}
