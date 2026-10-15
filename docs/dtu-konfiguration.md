# E90-DTU konfigurieren

Beide Modems müssen identisch eingestellt sein, sonst hören sie sich nicht.

## Eingesetzte Modems

**E90-DTU(900SL22)**, auf beiden Seiten.

| Eigenschaft | Wert |
|---|---|
| Frequenzbereich | 850,125–930,125 MHz, Werkseinstellung 868,125 MHz |
| Sendeleistung | bis 22 dBm |
| Schnittstelle | RS232 (DB9) und RS485, 8–28 V Versorgung |

Das ist die in der EU betreibbare Variante: sie trifft das 868-MHz-SRD-Band und lässt sich
weit genug herunterregeln. Eine 400er-Variante (433 MHz) oder eine 30-dBm-Variante hätte eine
andere Frequenz- und Leistungsplanung gebraucht.

## Sendeleistung — die Einstellung, die man nicht übersehen darf

Im Band 868,0–868,6 MHz sind 25 mW ERP erlaubt, das sind 14 dBm ERP, bei 1 Prozent
Duty-Cycle. Werkseitig funkt das Modem mit 22 dBm und liegt damit deutlich darüber.

Umrechnung: **ERP = Leistung am Antennenanschluss + Antennengewinn in dBi − 2,15 dB**

Mit einer typischen Stabantenne von etwa 2 dBi:

| Einstellung | ERP | Bewertung |
|---|---|---|
| 22 dBm | ca. 21,9 dBm = 155 mW | weit über dem Limit |
| 17 dBm | ca. 16,9 dBm = 49 mW | über dem Limit |
| 13 dBm | ca. 12,9 dBm = 19 mW | eingehalten, gute Wahl |
| 10 dBm | ca. 9,9 dBm = 10 mW | eingehalten, kostet Reichweite |

**Regel:** die höchste angebotene Stufe wählen, die bei 13 dBm oder darunter liegt. Welche
Stufen zur Auswahl stehen, zeigt die Auswahlliste im Konfigurationstool — Ebyte dokumentiert
die Abstufung nicht einheitlich, deshalb hier keine feste Tabelle. Niemals 17 dBm oder höher.
Bei einer Antenne mit mehr als 2 dBi Gewinn eine Stufe tiefer gehen und die Rechnung oben mit
dem tatsächlichen Gewinn aus dem Antennendatenblatt wiederholen.

## Ablauf

1. M0/M1 am Gehäuse in den Konfigurationsmodus stellen. Die Belegung unterscheidet sich
   zwischen den Serien, also im Handbuch nachsehen und nicht aus dem Gedächtnis schalten.
2. DTU per USB-RS232-Adapter an den PC.
3. Ebyte-Tool RF_Setting starten (Downloadbereich der E90-DTU-Produktseite bei cdebyte.com),
   Parameter erst auslesen, dann setzen.
4. Nach dem Schreiben zurück in den Normalmodus (transparente Übertragung).
5. Mit dem zweiten Modem genauso verfahren und die Werte vergleichen.

## Parameter

| Parameter | Wert | Begründung |
|---|---|---|
| Frequenz / Kanal | 868,125 MHz | Werkseinstellung, liegt im EU-SRD-Band |
| Sendeleistung | 13 dBm oder niedriger | siehe Rechnung oben |
| Luftrate | 9,6 kbps | ein 7-Byte-Frame braucht so rund 25 ms statt 80 ms bei 2,4 kbps |
| UART | 9600 8N1 | muss zu LORA_BAUD in shared/lora_protocol.h passen |
| Modus | transparent | Punkt-zu-Punkt, keine Adressierung nötig |
| Adresse / Netz-ID | bei beiden gleich | beliebig, aber gleich |
| Sub-Paketlänge | 32 Byte | kleinster Wert, kürzeste Latenz |
| Listen before talk | ein, falls vorhanden | entschärft gleichzeitiges Senden |

## Duty-Cycle

Im Ruhebetrieb sendet jede Seite einen Frame je 10 Sekunden, jeder etwa 25 ms lang: rund
0,25 Prozent Sendezeit bei erlaubtem 1 Prozent. Der ungünstigste Fall ist ein dauernd
schwankender Messwert; dann greift die 5-s-Bremse und es bleibt bei etwa 0,5 Prozent. Die
Reserve ist für Wiederholungen da.

Nachprüfen lässt sich das direkt am seriellen Monitor von Controller 1: jede Telemetriezeile
endet mit Luft=x/250ms, also der in der letzten Minute verbrauchten Sendezeit gegen das
Budget. Zusätzlich wacht im Code ein Sendezeit-Budget darüber (LORA_AIRTIME_BUDGET_MS in
shared/lora_protocol.h): ist es erschöpft, werden weitere Frames unterdrückt, bis das
60-Sekunden-Fenster weiterläuft. Quittungen sind davon ausgenommen, weil eine verweigerte
Quittung die Gegenseite nur zu weiteren Wiederholungen treiben würde.
