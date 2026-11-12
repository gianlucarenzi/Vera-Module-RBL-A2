# Pin Mapping: VERA Module RBL-A2 (ESP32-S3FN8)

Target MCU: **ESP32-S3FN8** — QFN56, 45 GPIO, 8 MB di flash Quad SPI in-package, 512 KB di SRAM,
dual-core Xtensa LX7 a 240 MHz.

La scheda va in uno slot di espansione dell'Apple II (connettore a 50 pin). Il firmware
(`firmware/MCU/src/main.cpp`) usa i GPIO elencati qui; lo schema della scheda Apple è ancora da
disegnare, quindi i collegamenti sono **requisiti**, non la descrizione di un circuito esistente.
Interfaccia di bus e memoria Slinky: [`A2-SLINKY.md`](A2-SLINKY.md).

---

## ⚠ Strapping pin e pin riservati

| GPIO | QFN56 pin | Segnale | Requisito |
|------|-----------|---------|-----------|
| **GPIO 0** | 5 | BOOT0 | Strapping (modo di boot). Pull-up a 3V3, pulsante BOOT verso massa, auto-reset dal programmatore. LOW al reset = download (richiede anche GPIO 46 LOW). La costante di tempo di CHIP_PU deve essere più lunga di quella di BOOT0, così GPIO 0 è già alto quando il reset viene rilasciato. |
| **GPIO 3** | 8 | — | Strapping (sorgente JTAG), senza pull interne: **non usato**, pull-up 10 kΩ a 3V3 (datasheet §3.4: non può restare flottante). |
| **GPIO 26–32** | 28, 30–35 | — | Memoria in-package (datasheet Table 2-14): GPIO 27–32 = flash della FN8, **mai connettere**; GPIO 26 = CS# della PSRAM (assente sulla FN8), sconsigliato. |
| **GPIO 45** | 51 | A14 | Strapping VDD_SPI, pull-down interno. Il 6502 pilota gli indirizzi anche in reset, quindi i traslatori di A0–A15 devono essere **spenti** mentre l'ESP32 è in reset: `~OE` su GPIO 47 con pull-up 10 kΩ. Così GPIO 45 legge LOW → VDD_SPI = 3,3 V (Table 3-4), la tensione della flash della FN8. |
| **GPIO 46** | 52 | A15 | Strapping (messaggi ROM / download), pull-down interno, stesso gating di GPIO 45 → LOW: download boot valido con GPIO 0 LOW, quindi la programmazione funziona con l'Apple acceso. |

> **GPIO 39–42 (JTAG):** il firmware chiama `gpio_reset_pin()` su GPIO 39, 40, 41, 42 per toglierli
> al controller JTAG e usarli come GPIO.

> **GPIO 19–20 (USB D−/D+):** USB CDC disabilitato (`ARDUINO_USB_CDC_ON_BOOT=0`); i pin sono A7 e A8.
> Il monitor seriale usa la UART0 (GPIO 43/44).

> **GPIO 15–16 (XTAL_32K):** nessun quarzo da 32 kHz; sono A3 e A4.

---

## 1. Bus dati D0–D7 (bidirezionale, Bank 0 bit 4–11)

Transceiver bidirezionale 5 V ↔ 3,3 V (per esempio 74LVC4245A). DIR da R/W (verso l'Apple nelle
letture, verso la scheda nelle scritture). `~OE` attivo solo quando serve:

> **`~OE` = `NAND(PHI0, NAND3(R/W, DEV_SEL_N, CARD_RD_N))`**: acceso in ogni scrittura e nelle
> letture a cui la scheda risponde (VERA con `DEV_SEL_N`, ESP32 con `CARD_RD_N`), spento in PHI1 e
> in tutte le altre letture, per non entrare in contesa con le altre schede. Sul lato 3,3 V il bus è
> condiviso da ESP32 e VERA.

| Segnale | GPIO | QFN56 pin | Bank-0 bit | Note |
|---|---|---|---|---|
| D0 | 4  | 9  | 4  | glitch LOW di 60 µs all'accensione |
| D1 | 5  | 10 | 5  | |
| D2 | 6  | 11 | 6  | |
| D3 | 7  | 12 | 7  | |
| D4 | 8  | 13 | 8  | |
| D5 | 9  | 14 | 9  | |
| D6 | 10 | 15 | 10 | |
| D7 | 11 | 16 | 11 | |

Firmware: `DBUS_MASK = 0x00000FF0`, `data = (GPIO.in >> 4) & 0xFF`.

---

## 2. Bus indirizzi A0–A15 (ingressi)

Traslatori a direzione fissa (Apple → ESP32), `~OE` comune su GPIO 47 (§ strapping).

| Segnale | GPIO | QFN56 pin | Bank bit | Note |
|---|---|---|---|---|
| A0  | 12 | 17 | Bank 0 bit 12 | |
| A1  | 13 | 18 | Bank 0 bit 13 | |
| A2  | 14 | 19 | Bank 0 bit 14 | |
| A3  | 15 | 21 | Bank 0 bit 15 | XTAL_32K_P usato come GPIO; pin 20 = VDD3P3_RTC |
| A4  | 16 | 22 | Bank 0 bit 16 | XTAL_32K_N usato come GPIO |
| A5  | 17 | 23 | Bank 0 bit 17 | |
| A6  | 18 | 24 | Bank 0 bit 18 | |
| A7  | 19 | 25 | Bank 0 bit 19 | USB D− |
| A8  | 20 | 26 | Bank 0 bit 20 | USB D+ |
| A9  | 21 | 27 | Bank 0 bit 21 | |
| A10 | 33 | 38 | Bank 1 bit 1  | |
| A11 | 34 | 39 | Bank 1 bit 2  | |
| A12 | 35 | 40 | Bank 1 bit 3  | |
| A13 | 36 | 41 | Bank 1 bit 4  | |
| A14 | 45 | 51 | Bank 1 bit 13 | strapping, vedi sopra |
| A15 | 46 | 52 | Bank 1 bit 14 | strapping, vedi sopra |

Firmware (`decode_addr()`): `A0–A9 = (GPIO.in >> 12) & 0x3FF`, `A10–A13 = (GPIO.in1 >> 1) & 0xF`,
`A14–A15 = (GPIO.in1 >> 13) & 3`. Il numero di slot non serve: le selezioni le decodifica l'Apple
(/DEVSEL, /IOSEL); l'indirizzo completo serve per l'offset nei registri e nella ROM e per
riconoscere il reset (lettura del vettore `$FFFC`/`$FFFD`).

---

## 3. Segnali di controllo

| Segnale | GPIO | QFN56 pin | Verso | Attivo | Collegamento | Descrizione |
|---|---|---|---|---|---|---|
| PHI0 | 1 | 6 | ingresso | HIGH | traslatore | Clock dello slot (pin 40), ~1,02 MHz |
| R/W | 2 | 7 | ingresso | HIGH = lettura | traslatore | Slot pin 18 |
| /IOSEL | 48 | 36 | ingresso | LOW | traslatore | Slot pin 1: `$Cn00-$CnFF` (ROM di slot o VERA). Dominio VDD_SPI (3,3 V sulla FN8) |
| /DEVSEL | 42 | 48 | ingresso | LOW | traslatore | Slot pin 41: `$C0n0-$C0nF` (registri Slinky, `$C0nF`) |
| DEV_SEL_N | 40 | 45 | uscita | LOW | diretto (3,3 V) | Chip select della VERA per `$Cn00-$Cn1F` quando mappata |
| CARD_RD_N | 41 | 47 | uscita | LOW | diretto | L'ESP32 pilota il bus dati (gating di `~OE` del transceiver) |
| ARESET | 37 | 42 | uscita | LOW | **open drain** | Slot pin 31 /RESET: tiene l'Apple in reset finché il loop di bus non parte. Open drain obbligatorio: il tasto RESET dell'Apple porta la linea a massa |
| CRESET | 38 | 43 | uscita | LOW | diretto | Reset della FPGA VERA |
| CDONE | 39 | 44 | ingresso | HIGH | diretto | FPGA VERA configurata |
| ABUS_OE_N | 47 | 37 | uscita | LOW | diretto, pull-up 10 kΩ | `~OE` dei traslatori di A0–A15; alto durante il reset dell'ESP32 (§ strapping), portato basso in `setup()`. Dominio VDD_SPI |

L'IRQ della VERA va allo slot pin 30 attraverso un buffer open drain (non passa dall'ESP32).

### Dedicated GPIO — canali usati

Letti e scritti con le istruzioni TIE `ee.get_gpio_in` / `ee.wr_mask_gpio_out` (1 ciclo).

| Pool | Ch | GPIO | Segnale |
|------|----|------|---------|
| Input  | 0 | 1  | PHI0 |
| Input  | 1 | 2  | R/W |
| Input  | 2 | 48 | /IOSEL |
| Input  | 3 | 42 | /DEVSEL |
| Output | 0 | 40 | DEV_SEL_N |
| Output | 1 | 41 | CARD_RD_N |

I pool di ingresso e di uscita sono indipendenti sull'ESP32-S3.

---

## 4. Debug / programmazione

| Funzione | GPIO | QFN56 pin | Note |
|---|---|---|---|
| UART TX | 43 | 49 | U0TXD, monitor seriale 115200 baud |
| UART RX | 44 | 50 | U0RXD |

> Se la scheda ha un proprio ingresso 5 V (USB o jack) oltre al +5 V dello slot, serve un diodo o
> un interruttore di alimentazione: altrimenti, programmandola dentro l'Apple, la scheda
> alimenterebbe l'Apple spento o lavorerebbe in parallelo al suo alimentatore.

---

## 5. Quarzo 40 MHz

L'ESP32-S3FN8 richiede un **quarzo esterno da 40 MHz** (riferimento del PLL per CPU, flash e UART);
senza non parte. I pin sono analogici dedicati.

| QFN56 pin | Segnale | Note |
|-----------|---------|------|
| 53 | XTAL_N | terminale − |
| 54 | XTAL_P | terminale + |

| Componente | Valore | Note |
|---|---|---|
| Quarzo | 40 MHz, SMD 3225 o 2016 | ±10 ppm o meglio |
| Condensatori di carico | 10 pF 0402 ±5 % | C_ext = 2 × C_L − C_stray (C_stray ≈ 3–5 pF) |
| Resistenza serie | 0–33 Ω (opzionale) | limita il drive level |

PCB: tracce XTAL < 5 mm, condensatori ai pin del chip, piano di massa pieno sotto il quarzo,
lontano da PHI0 e da D0–D7.

---

## 6. GPIO liberi ed esclusi

Tutti i GPIO non riservati sono assegnati. GPIO 3 è libero ma è uno strapping pin (pull-up 10 kΩ).

| GPIO / Funzione | QFN56 pin | Motivo dell'esclusione |
|---|---|---|
| GPIO 26–32 | 28, 30–35 | memoria in-package (§ strapping) |
| XTAL_N / XTAL_P | 53, 54 | quarzo 40 MHz |
| VDDA1 / VDDA2 | 55, 56 | alimentazione analogica |
| GPIO 22–25 | — | non esistono sull'ESP32-S3 |

---

## 7. QFN56 — pinout completo

Nomi e numeri da *ESP32-S3 Series Datasheet* v2.2, Table 2-1 (pag. 17) e Table 2-14 (pag. 31).

| Pin | GPIO / Funzione | Uso |
|-----|-----------------|-----|
| 1   | LNA_IN | RF (Wi-Fi/Bluetooth), non usato |
| 2–3 | VDD3P3 | alimentazione analogica 3,3 V |
| 4   | CHIP_PU (EN) | RC verso 3V3 (costante più lunga di quella di BOOT0), pulsante reset, auto-reset |
| 5   | GPIO 0 | BOOT0 — strapping |
| 6   | GPIO 1 | PHI0 |
| 7   | GPIO 2 | R/W |
| 8   | GPIO 3 | non usato — strapping, pull-up 10 kΩ |
| 9–16 | GPIO 4–11 | D0–D7 |
| 17–19 | GPIO 12–14 | A0–A2 |
| 20  | VDD3P3_RTC | alimentazione RTC |
| 21–22 | GPIO 15–16 | A3–A4 (XTAL_32K) |
| 23–24 | GPIO 17–18 | A5–A6 |
| 25  | GPIO 19 | A7 (USB D−) |
| 26  | GPIO 20 | A8 (USB D+) |
| 27  | GPIO 21 | A9 |
| 28  | GPIO 26 | CS# PSRAM (assente) — non usato |
| 29  | VDD_SPI | alimentazione flash |
| 30–35 | GPIO 27–32 | **flash in-package — NC** |
| 36  | GPIO 48 | /IOSEL |
| 37  | GPIO 47 | ABUS_OE_N |
| 38–41 | GPIO 33–36 | A10–A13 |
| 42  | GPIO 37 | ARESET (open drain verso /RESET) |
| 43  | GPIO 38 | CRESET (VERA) |
| 44  | GPIO 39 | CDONE (VERA) — ex JTAG |
| 45  | GPIO 40 | DEV_SEL_N (VERA) — ex JTAG |
| 46  | VDD3P3_CPU | alimentazione CPU |
| 47  | GPIO 41 | CARD_RD_N — ex JTAG |
| 48  | GPIO 42 | /DEVSEL — ex JTAG |
| 49  | GPIO 43 | UART0 TX |
| 50  | GPIO 44 | UART0 RX |
| 51  | GPIO 45 | A14 — strapping VDD_SPI |
| 52  | GPIO 46 | A15 — strapping download |
| 53–54 | XTAL_N / XTAL_P | quarzo 40 MHz |
| 55–56 | VDDA1 / VDDA2 | alimentazione analogica |
| EP  | GND | pad centrale |
