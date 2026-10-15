# Verdrahtung

Beide Seiten sind identisch aufgebaut, nur die Ein- und Ausgänge unterscheiden sich.

## Signalweg

```
ESP32  --3,3 V TTL-->  MAX3232  --RS232-->  E90-DTU  ))) 868 MHz (((
```

Das E90-DTU hat **keinen TTL-Eingang**. Es bringt RS232 (DB9) und RS485 (Klemme) mit und
will 8-28 V Versorgung. Deshalb sitzt je Seite ein MAX3232 zwischen ESP32 und Modem.

## ESP32 zu MAX3232 (TTL-Seite)

| ESP32 | MAX3232 | Hinweis |
|---|---|---|
| GPIO17 (TX2) | T1IN | Daten zum Modem |
| GPIO16 (RX2) | R1OUT | Daten vom Modem |
| 3V3 | VCC | 3,3 V, nicht 5 V, sonst stimmen die TTL-Pegel nicht |
| GND | GND | gemeinsame Masse |

## MAX3232 zu E90-DTU (RS232-Seite)

| MAX3232 | DTU DB9 |
|---|---|
| T1OUT | RXD |
| R1IN | TXD |
| GND | GND |

TXD und RXD werden gekreuzt verbunden (Nullmodem-Belegung): was der ESP32 sendet, muss am
RXD des Modems ankommen. Werden sie gerade durchverbunden, bleibt es still. Das ist der
häufigste Fehler beim ersten Aufbau.

RS232 und RS485 am DTU nicht gleichzeitig belegen.

## Versorgung

| Verbraucher | Spannung | Quelle |
|---|---|---|
| E90-DTU | 12 V DC | Netzteil |
| ESP32 | 5 V an VIN | Step-Down aus den 12 V |
| MAX3232 | 3,3 V | 3V3-Pin des ESP32 |

Alle Massen müssen zusammengeführt werden: Netzteil, Step-Down, ESP32, MAX3232 und DTU.
Ohne gemeinsame Masse kommen entweder keine oder nur verstümmelte Bytes an.

Beim Flashen über USB versorgt der PC den ESP32 mit. Dann entweder den Step-Down abklemmen
oder das Netzteil ausstecken, nicht beides gleichzeitig einspeisen.

## Ein- und Ausgänge Controller 1

| Pin | Funktion |
|---|---|
| GPIO32 | Zustand 1, Schalter nach GND, interner Pull-up |
| GPIO33 | Zustand 2, Schalter nach GND, interner Pull-up |
| GPIO34 | Messwert, Potentiometer oder Sensor 0-3,3 V (ADC1, nur Eingang) |
| GPIO25 | Ausgang für den empfangenen Schaltbefehl, über Relaismodul |
| GPIO2 | Status-LED |

## Ein- und Ausgänge Controller 2

| Pin | Funktion |
|---|---|
| GPIO27 | Taster nach GND, schaltet den Befehl um |
| GPIO25 | LED, zeigt Zustand 1 von Controller 1 |
| GPIO26 | LED, zeigt Zustand 2 von Controller 1 |
| GPIO2 | Status-LED |

## Wichtig bei WROVER-Boards

Auf ESP32-WROVER-Modulen sind GPIO16 und GPIO17 vom PSRAM belegt. Dort muss UART2 auf
andere Pins gelegt werden, zum Beispiel GPIO26 (RX) und GPIO27 (TX). Dann wandern die
LEDs und der Taster entsprechend auf freie Pins. Die betroffenen Zeilen stehen am Anfang
beider Sketches.

Ein ESP32-WROOM-32 ist der unkomplizierte Fall: dort sind 16 und 17 frei.
