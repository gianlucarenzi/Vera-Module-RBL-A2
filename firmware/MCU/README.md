# Vera-Module-RBL-A2 — Firmware MCU

## 1. Introduzione

Firmware per l'**ESP32-S3FN8** (QFN-56, 8 MB di flash integrata, dual-core Xtensa LX7 a 240 MHz)
della scheda VERA per **Apple II**. La scheda va in uno slot di espansione; l'ESP32 sta sul bus
dello slot e serve:

- **`$C0n0-$C0nF` (/DEVSEL):** memoria espansa compatibile **Apple II Memory Expansion Card
  ("Slinky")**, 256 KB nella DRAM dell'ESP32; `$C0nF` bit 0 mappa la VERA;
- **`$Cn00-$CnFF` (/IOSEL):** ROM di slot con il driver ProDOS (il RAM disk `/SLINKY` funziona
  senza driver da caricare), oppure, con la VERA mappata, i registri VERA in `$Cn00-$Cn1F`
  (`DEV_SEL_N` = chip select della VERA).

Interfaccia, registri e prove: [`../../A2-SLINKY.md`](../../A2-SLINKY.md). Pin: [`../../PIN-MAPPING.md`](../../PIN-MAPPING.md).

> **Requisito hardware:** quarzo esterno da 40 MHz su XTAL_P (pin 54) e XTAL_N (pin 53), vedi
> `PIN-MAPPING.md` § 5.

---

## 2. Architettura

### Avvio (`setup()`, core 1)

1. Recupera GPIO 39–42 dal JTAG; abbassa **ARESET** (Apple in reset) e **CRESET** (VERA in reset).
2. Avvia `LogTask` sul core 0 (Serial a 115200 baud su GPIO 43/44).
3. Inizializza la memoria Slinky e la formatta come volume ProDOS vuoto `/SLINKY` (512 blocchi).
4. Configura gli ingressi, abilita i traslatori degli indirizzi (GPIO 47 basso, dopo gli strapping).
5. Rilascia CRESET, attende CDONE (bitstream VERA caricato, timeout 5 s), poi 1 ms per il
   power-on reset della VERA.
6. Crea `MonitorTask` sul core 1: prende il core e rilascia l'Apple.

### Divisione dei core

| Core | Task | Compito |
|------|------|---------|
| 1 | `MonitorTask` (IRAM, interruzioni mascherate, watchdog MWDT1 fermato) | loop di bus, un'iterazione per ciclo 6502 |
| 0 | `LogTask` | stampa gli eventi (reset dell'Apple, scritture su `$C0nF`) dal ring lock-free |

### Loop di bus

Per ogni ciclo:

1. **PHI1** (PHI0 basso): rilascia il bus e i segnali, attende `BUS_ADDR_SETTLE_NS` dalla discesa
   di PHI0, legge A0–A15 (`GPIO.in`, `GPIO.in1`) e R/W, aggiorna il rilevatore del reset
   (lettura di `$FFFC` seguita da `$FFFD`: azzera i registri Slinky), prepara il byte di ROM.
2. **PHI0 alto:** dopo `BUS_SEL_SAMPLE_NS` legge /DEVSEL e /IOSEL (Dedicated GPIO):
   - /DEVSEL: lettura → `bus_drive(slinky_read())`; scrittura → campiona il dato a
     `BUS_WR_SAMPLE_NS` e chiama `slinky_write()`;
   - /IOSEL: VERA mappata e offset < `$20` → `DEV_SEL_N` basso (risponde la VERA); altrimenti, in
     lettura, `bus_drive()` del byte di ROM.
3. Attende la discesa di PHI0.

Il numero di slot non serve. `$C800-$CFFF` (/IOSTROBE) non è usato. Nel loop non ci sono chiamate
né tabelle in flash: modello Slinky (`a2_slinky.h`) e ROM sono inline o in DRAM.

| Macro | Default | Significato |
|---|---|---|
| `BUS_ADDR_SETTLE_NS` | 350 | indirizzo valido dopo la discesa di PHI0 |
| `BUS_SEL_SAMPLE_NS` | 120 | /DEVSEL e /IOSEL validi dopo la salita di PHI0 |
| `BUS_WR_SAMPLE_NS` | 300 | dato in scrittura valido dopo la salita di PHI0 |
| `BUS_PHI0_HIGH_MAX_NS` | 640 | durata massima di PHI0 alto |

Valori dai datasheet, da misurare sul bus reale e regolare con `-D`.

### Dedicated GPIO

| Pool | Ch | GPIO | Segnale |
|------|----|------|---------|
| Input  | 0 | 1  | PHI0 |
| Input  | 1 | 2  | R/W |
| Input  | 2 | 48 | /IOSEL |
| Input  | 3 | 42 | /DEVSEL |
| Output | 0 | 40 | DEV_SEL_N (VERA) |
| Output | 1 | 41 | CARD_RD_N (transceiver dati) |

---

## 3. File

```
firmware/MCU/
├── platformio.ini            # ambiente esp32s3fn8-a2
├── pre_build_6502.py         # lancia make in 6502/ prima della build
├── produce-pdf-from-tex.sh   # PDF da LaTeX (usato da WORKFLOW)
├── src/main.cpp              # setup, LogTask, MonitorTask (loop di bus)
├── include/
│   ├── a2_slinky.h           # registri Slinky, mappa VERA, rilevatore del reset
│   ├── prodos_format.h       # volume ProDOS vuoto
│   └── slinky_rom.h          # ROM di slot (generata da 6502/, tenuta nel repository)
├── 6502/
│   ├── Makefile              # ca65/ld65 → slinky_rom.bin → include/slinky_rom.h
│   ├── slinky_rom.ld
│   └── src/slinky_rom.s      # firma ProDOS, boot, driver a blocchi, ID "VERAS"
└── test/host/test_a2_slinky.c
```

---

## 4. Compilazione e flashing

Serve PlatformIO e cc65 (ca65/ld65) per la ROM.

```bash
cd firmware/MCU
pio run                                  # compila (ROM 6502 inclusa)
pio run --target upload                  # UART0: GPIO43 = TX, GPIO44 = RX
pio device monitor -b 115200
```

USB CDC è disabilitato: GPIO 19/20 (USB D−/D+) sono A7/A8. Usare un adattatore UART.

Solo la ROM: `make -C 6502` (rigenera `include/slinky_rom.h`).

---

## 5. Test

```bash
cd test/host
cc -O2 -Wall -I../../include test_a2_slinky.c -o test_a2_slinky && ./test_a2_slinky
```

Registri, carry dell'indirizzo, mirror, reset, rilevatore del vettore, formattazione ProDOS, byte
della ROM. Le prove con ProDOS si fanno in **a2emu-vera** (`A2_VERA_CARD=rbl`), che usa copie di
`a2_slinky.h`, `prodos_format.h` e `slinky_rom.h`: vedi `A2-SLINKY.md`.
