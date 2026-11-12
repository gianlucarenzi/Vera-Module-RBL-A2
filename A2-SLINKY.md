# Memoria espansa Slinky — VERA Module RBL-A2

La scheda Apple II ha 256 KB di memoria espansa compatibile con la **Apple II Memory Expansion
Card ("Slinky")**. La serve l'ESP32 dalla sua DRAM, senza componenti aggiuntivi. Una ROM di
slot la rende un **dispositivo a blocchi ProDOS che funziona senza driver**: al boot ProDOS la trova
in `Sn,D1` come volume `/SLINKY`, già formattato.

Decisioni prese (2026-11-12):

- **ROM al reset, poi VERA.** Dopo un reset dell'Apple, `$Cn00-$Cn1F` mostrano la ROM di slot, che
  contiene la firma ProDOS. Scrivendo 1 nel bit 0 di `$C0nF`, al loro posto compaiono i registri
  VERA. Il driver ProDOS sta sopra `$Cn1F` e quindi resta sempre visibile.
- **256 KB serviti dall'ESP32**, senza componenti nuovi.

## Registri (`$C0n0-$C0nF`, /DEVSEL dello slot n)

| Indirizzo | Lettura | Scrittura |
|---|---|---|
| `$C0n0` | bit 0-7 dell'indirizzo | carica i bit 0-7 |
| `$C0n1` | bit 8-15 | carica i bit 8-15 |
| `$C0n2` | bit 16-23 **OR `$F0`** (come la scheda Apple) | carica i bit 16-23 |
| `$C0n3` | byte all'indirizzo, poi indirizzo +1 | scrive il byte, poi indirizzo +1 |
| `$C0n4-$C0nE` | valore scritto | registro semplice |
| `$C0nF` | valore scritto | **bit 0 = 1: VERA in `$Cn00-$Cn1F`**, 0: ROM di slot |

- L'indirizzo è un contatore a 24 bit; la memoria si ripete ogni 256 KB.
- Il reset dell'Apple azzera tutti i registri e l'indirizzo, quindi la VERA torna nascosta. **La
  memoria non viene mai cancellata.**
- Il comportamento è quello del modello MAME (`a2memexp.cpp`, scheda Apple: `$F0` in `$C0n2`,
  registri semplici in `$C0n4-$C0nF`). L'unica differenza è che il contatore è uno solo: scrivere un
  solo byte d'indirizzo dopo alcuni accessi a `$C0n3` lascia gli altri byte al valore raggiunto
  contando. MAME invece li ricarica con l'ultimo valore scritto.
- Sulla scheda Apple `$C0nF` non ha effetto; sulla RamFactor commuta il banco della ROM. Il software
  Slinky usa solo `$C0n0-$C0n3`.
- Codice: `firmware/MCU/include/a2_slinky.h`.

## ROM di slot (`$Cn00-$CnFF`, /IOSEL)

Sorgente: `firmware/MCU/6502/src/slinky_rom.s`. Si assembla con `make -C firmware/MCU/6502` (ca65/ld65),
che genera `firmware/MCU/include/slinky_rom.h`; la build PlatformIO lo lancia da sola. Il codice è
indipendente dalla posizione, quindi la stessa immagine funziona in qualunque slot.

| Byte | Valore | Significato |
|---|---|---|
| `$Cn01/03/05/07` | `$20/$00/$03/$3C` | dispositivo a blocchi avviabile, non SmartPort |
| `$CnF0-$CnF5` | `"VERAS"`, `$01` | identificazione per il software VERA (tipo `S`, versione 1) |
| `$CnFC-$CnFD` | `$0200` | 512 blocchi |
| `$CnFE` | `$0F` | 1 volume; status, read, write, format |
| `$CnFF` | `$33` | entry del driver = `$Cn33`, sopra la finestra VERA |

- **Driver:** STATUS, READ, WRITE e FORMAT (FORMAT non fa nulla, è RAM). Restituisce l'errore `$27`
  per blocchi oltre il 511 e `$01` per comandi sconosciuti. Le scritture usano `STA ($3A,X)`, con
  `$3A/$3B` salvati e ripristinati: su NMOS 6502 un `STA $C083,X` farebbe prima una lettura
  fantasma di `$C0n3` e farebbe avanzare l'indirizzo.
- **Boot** (`PR#n`, scansione autostart): legge il blocco 0 in `$0800`. Se trova un boot block ProDOS
  (`$0800 = $01`) salta a `$0801` con X = n·16. Altrimenti ripristina CSW/KSW (SETVID/SETKBD) e
  prosegue la scansione dallo slot n-1 (`$FABA`, uguale su II+, IIe e IIe enhanced). Il ripristino
  serve perché da BASIC `PR#n` usa lo slot come uscita, e ogni carattere stampato rientrerebbe nel
  boot.
- **Volume:** all'accensione l'ESP32 formatta i 256 KB come volume ProDOS vuoto `/SLINKY`
  (`firmware/MCU/include/prodos_format.h`). I blocchi 0-1 restano a zero (non avviabile). Per
  avviare dal RAM disk bisogna copiarci un boot block ProDOS e il file PRODOS.

## Firmware ESP32 (`firmware/MCU/src/main.cpp`)

Un solo ambiente PlatformIO, `esp32s3fn8-a2`. Il core 1 esegue il loop di bus a interrupt
disabilitati; il core 0 gestisce il log seriale (eventi di reset e scritture su `$C0nF`). Per ogni
ciclo:

1. **PHI1:** rilascia il bus, attende l'indirizzo stabile, lo legge e prepara il byte di ROM.
2. **PHI0 alto:** campiona /DEVSEL e /IOSEL.
   - /DEVSEL: registro Slinky (in lettura pilota il dato; in scrittura campiona il dato).
   - /IOSEL: se la VERA è mappata e l'offset è < `$20`, attiva il chip select della VERA;
     altrimenti, in lettura, pilota il byte di ROM.

Il numero di slot non serve: lo decodifica l'Apple. La scheda non usa `$C800-$CFFF` (/IOSTROBE).
Siccome manca un ingresso /RESET, la scheda riconosce il reset dalla lettura del vettore (`$FFFC`
seguito da `$FFFD`); anche un `JMP ($FFFC)` conta come reset.

Tempi, tutti regolabili con `-D` dopo la misura sul bus reale:

| Macro | Default | Riferimento |
|---|---|---|
| `BUS_ADDR_SETTLE_NS` | 350 | dopo la discesa di PHI0: t_ADS 6502 a 1 MHz = 300 ns, più i buffer |
| `BUS_SEL_SAMPLE_NS` | 120 | dopo la salita di PHI0: /DEVSEL e /IOSEL validi |
| `BUS_WR_SAMPLE_NS` | 300 | dopo la salita di PHI0: dato in scrittura valido (t_MDS 200 ns) |
| `BUS_PHI0_HIGH_MAX_NS` | 640 | durata massima di PHI0 alto (489 ns più il ciclo lungo) |

Il dato in lettura serve entro circa 100 ns prima della discesa di PHI0, cioè circa 370 ns dopo la
salita. Il firmware lo mette sul bus poco dopo `BUS_SEL_SAMPLE_NS`. **Questi tempi non sono stati
verificati su hardware.**

## Hardware (schema: foglio `a2slotInterface.sch`)

Lo schema è quello della scheda VERA Module RBL-XE con l'interfaccia Atari sostituita dallo slot
Apple (connettore a pettine 2×25, **J8**, `RetroBitLab:Bus_Apple_II_Card_Side`, footprint
`RetroBitLab:APPLE2-PERIPHERAL-BUS-EDGE`). Pinout ESP32 completo in `PIN-MAPPING.md`.

| Segnale Apple (J8) | Verso | Percorso | ESP32 |
|---|---|---|---|
| A0-A15 (pin 2-17) | → | `U4` (A0-A7), `U3` (A8-A15), `~OE` = GPIO 47 | GPIO 12-21, 33-36, 45, 46 |
| D0-D7 (pin 49-42) | ↔ | `U19`, DIR da R/W (`U23`) | GPIO 4-11 |
| R/W (pin 18) | → | `U6` A3→B3 | GPIO 2 |
| PHI0 (pin 40) | → | `U6` A4→B4 | GPIO 1 |
| /DEVSEL (pin 41) | → | `U6` A5→B5, LED `LD1` | GPIO 42 |
| /IOSEL (pin 1) | → | `U6` A0→B0, LED `LD5`, test point `mIOSEL1` | GPIO 48 |
| /RES (pin 31) | ← | `U27` SN74LVC1G07, **open drain**; `R70` 10 kΩ verso GND sull'ingresso | GPIO 37 |
| /IRQ (pin 30) | ← | `U26` SN74LVC1G07, open drain | (VERA `~mVIRQ`) |
| +5V (pin 25), GND (pin 26) | | rete `5V`, `GND` | |
| INT IN→OUT (28→23), DMA IN→OUT (27→24) | | collegati in passante sulla scheda | |

- **Reset:** con l'ESP32 in reset o nel bootloader GPIO37 è flottante, `R70` tiene basso l'ingresso
  di `U27`, quindi l'Apple resta in reset. Il firmware lo tiene basso in `setup()` e lo rilascia
  quando il loop di bus parte; l'alto lo dà il pull-up dell'Apple. `U27` è open drain perché il tasto
  RESET dell'Apple porta la linea a massa.
- **`~OE` di `U19`** = `NAND(PHI0, NAND4(R/W, DEV_SEL_N, CARD_RD_N, 3V3))` (`U24`, `U25`): acceso nelle
  scritture e nelle letture a cui la scheda risponde (VERA o ESP32), spento in PHI1. Il quarto
  ingresso (prima MPD) è a 3V3.
- `U6`: gli ingressi non usati (A1, A2, A6) sono a GND, le uscite B1, B2, B6 non connesse.
- Tolti rispetto alla scheda Atari: CART1, ECI1, `U16` (traslatore MCU → 6502 per EXTSEL, MPD,
  RESET, non più necessario), il mixer audio verso il connettore (R2, R6, C66: l'Apple non ha
  audio sullo slot, si usa il jack 3,5 mm `J7`), i test point dei segnali Atari.
- Alimentazione dallo slot (+5 V, pin 25).
- **PCB:** `VERA-MODULE-RBL.kicad_pcb` è ancora quello Atari: va aggiornato dallo schema (J8, U27, C70
  da aggiungere; CART1, ECI1, U16 e le parti tolte da eliminare) e va ridisegnato il bordo (Edge.Cuts).

## Software VERA (aggiornato il 2026-11-12: VERA_COMMON 549539f, a2emu-vera a749511)

Con la VERA nascosta dopo il reset, il software la attiva quando trova la firma `"VERAS"` in
`$CnF0-$CnF4`. Prima legge soltanto, quindi le altre schede (per esempio il Disk II) non vengono
mai scritte.

- **libvera** (`VERA_COMMON/src/apple2/plat_apple2.s`): il costruttore `plat_startup` scrive 1 in
  `$C0nF` dello slot di `VERA_BASE`. Ogni programma libvera Apple funziona sulla RBL-A2 dopo la
  ricompilazione (RastanRIP, PacManRIP, ...).
- **`vera_detect()`** (`a2emu-vera/examples/vera/vera.c`): uno slot con la firma ProDOS viene ancora
  saltato, tranne quando contiene `"VERAS"`; in quel caso scrive 1 in `$C0nF` e fa il controllo
  solito.
- **a2emu-vera**: `A2_VERA_CARD=rbl` (in `a2run` anche `--vera-card rbl`, `--rbl-image`,
  `--rbl-dump`). Usa le copie di `a2_slinky.h`, `prodos_format.h` e `slinky_rom.h` in `rbla2/`, da
  ricopiare quando cambiano qui.
- Dopo un reset dell'Apple, il programma deve rimappare la VERA (rilanciare `vera_detect()` o
  riavviare il programma).
- `PR#n` con la VERA mappata eseguirebbe i registri VERA: dopo un reset non succede.

## Prove fatte (2026-11-12)

- Test host `firmware/MCU/test/host/test_a2_slinky.c`: registri, carry, mirror, reset,
  rilevamento del vettore, formattazione, byte della ROM. **0 errori.**
  `cc -O2 -Wall -I../../include test_a2_slinky.c -o test_a2_slinky && ./test_a2_slinky`
- `pio run`: compila; RAM 85,9 %. Il loop di bus è in IRAM, senza chiamate né tabelle di salto in
  flash.
- Emulatore (`a2run` patchato con lo stesso `a2_slinky.h` e `slinky_rom.h`, letture fantasma NMOS
  attive), ProDOS 2.4.3:
  - Bitsy Bye mostra `S5,D1:/SLINKY` senza driver caricati.
  - BASIC.SYSTEM: `SAVE`, `CAT`, `RUN` su `/SLINKY`. Un `BSAVE` di 10 240 byte riletto dal dump è
    identico all'originale su **II+, IIe e IIe enhanced**.
  - Boot da `$C500` con un'immagine ProDOS nella scheda: ProDOS parte, prefisso `/PRODOS.2.4.3/`
    (II+, IIe, IIe enhanced).
  - `PR#5` con il volume vuoto: torna al prompt Applesoft (II+, IIe, IIe enhanced).
  - VERA e scheda nello stesso slot: con `$C0DF` = 1 la VERA risponde in `$C500` (ID `'V'`,
    versione 48), e ProDOS continua a leggere e scrivere `/SLINKY`. Con `$C0DF` = 0 torna la ROM.
- **Non provato:** hardware reale, Apple IIgs, AppleWorks e altri programmi che usano lo Slinky
  direttamente.

## Punti aperti

- Aggiornare il PCB dallo schema e ridisegnare il bordo per lo slot Apple.
- Misurare sul bus reale i ritardi di /DEVSEL e /IOSEL rispetto a PHI0, poi regolare i tempi.
