# VERA Module RBL-A2

<div align="center">

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![Status: Work in progress](https://img.shields.io/badge/status-WORK_IN_PROGRESS_%E2%80%94_NOT_WORKING-red.svg)](#)
[![Apple II](https://img.shields.io/badge/Apple-II%20%2F%20IIe-green.svg)](#)
[![ESP32](https://img.shields.io/badge/ESP32--S3-FN8-yellow.svg)](https://www.espressif.com/)

*🌐 English | [🌐 Italiano](#vera-module-rbl-a2-italiano)*

</div>

> [!WARNING]
> **WORK IN PROGRESS — NOT WORKING.**
> The firmware has been checked on the host and in an emulator only. The Apple II board
> (schematic and PCB) has not been drawn yet. Do not build anything from this repository yet.

## Overview

The **VERA Module RBL-A2** is an Apple II slot card with:

- a **VERA** (Video Enhanced Retro Adapter, the Commander X16 video and audio chip) on a Lattice
  iCE40UP5K FPGA: layers, sprites, 256 colours from 4096, VGA 640×480, PSG and PCM audio, SD card;
- **256 KB of expanded memory compatible with the Apple II Memory Expansion Card ("Slinky")**,
  with a ProDOS slot ROM: ProDOS sees the RAM disk `/SLINKY` at boot, with no driver to load;
- an **ESP32-S3FN8** that runs the slot bus: Slinky registers, slot ROM, VERA chip select.

## Memory map (slot n)

| Range | Function |
|---|---|
| `$C0n0-$C0n3` | Slinky: address bits 0-7 / 8-15 / 16-23 (`$C0n2` reads OR `$F0`), data with auto-increment |
| `$C0n4-$C0nE` | plain registers |
| `$C0nF` | bit 0 = 1: VERA registers at `$Cn00-$Cn1F`; 0: slot ROM (cleared by reset) |
| `$Cn00-$CnFF` | ProDOS slot ROM (block device, driver at `$Cn33`, `"VERAS"` at `$CnF0`) |
| `$Cn00-$Cn1F` | VERA registers, with `$C0nF` bit 0 = 1 |

After a reset the slot ROM is visible, so ProDOS finds the RAM disk. Programs show the VERA with
`$C0nF = 1`: libvera's start-up code and `vera_detect()` do it when they read `"VERAS"` at
`$CnF0`. Details: [A2-SLINKY.md](A2-SLINKY.md).

## Components (planned)

| Component | Part |
|---|---|
| VERA FPGA | Lattice iCE40UP5K-SG48ITR, 25 MHz clock, SPI flash W25Q16 |
| Bus controller | ESP32-S3FN8 (240 MHz, 8 MB flash), 40 MHz crystal |
| Audio | WM8524 codec, 3.5 mm jack |
| Video | VGA connector |
| Level shifting | 5 V ↔ 3.3 V translators on address, data and control lines |

## Firmware

`firmware/MCU` (PlatformIO, ESP32-S3) and the 6502 slot ROM (`firmware/MCU/6502`, cc65):

```bash
cd firmware/MCU
pio run                      # build (the slot ROM is assembled first)
pio run --target upload      # UART0 on GPIO 43/44
```

Host test: `firmware/MCU/test/host/test_a2_slinky.c`. Emulator: **a2emu-vera** with
`A2_VERA_CARD=rbl`. See [firmware/MCU/README.md](firmware/MCU/README.md).

## Repository

```
Vera-Module-RBL-A2/
├── README.md            # this file
├── A2-SLINKY.md         # Slinky memory, slot ROM, bus interface, hardware requirements, tests
├── PIN-MAPPING.md       # ESP32-S3 pins
├── WORKFLOW.md / .pdf   # KiCad design workflow, schematic to JLCPCB order (Italian)
├── firmware/MCU/        # ESP32-S3 firmware and 6502 slot ROM
├── tools/, *.py, *.sh   # KiCad / JLCPCB production scripts (for the board to come)
├── patches/, utilities/ # eeplot patches, STEP → WRL
└── ESP32-S3_Datasheet.pdf, WeACT-Studio-ESP32_S3_*_Sch.pdf
```

## License

GNU General Public License v3.0, see [LICENSE](LICENSE).

---

# VERA Module RBL-A2 (Italiano)

> [!WARNING]
> **LAVORO IN CORSO — NON FUNZIONANTE.**
> Il firmware è stato verificato solo sull'host e in emulatore. La scheda Apple II (schema e PCB)
> non è ancora stata disegnata. Non costruire nulla da questo repository.

## Panoramica

Il **VERA Module RBL-A2** è una scheda per gli slot dell'Apple II con:

- una **VERA** (il chip video e audio del Commander X16) su FPGA Lattice iCE40UP5K: layer, sprite,
  256 colori su 4096, VGA 640×480, audio PSG e PCM, scheda SD;
- **256 KB di memoria espansa compatibile con la Apple II Memory Expansion Card ("Slinky")**, con
  una ROM di slot ProDOS: al boot ProDOS vede il RAM disk `/SLINKY` senza driver da caricare;
- un **ESP32-S3FN8** che gestisce il bus dello slot: registri Slinky, ROM di slot, chip select della
  VERA.

## Mappa della memoria (slot n)

| Indirizzi | Funzione |
|---|---|
| `$C0n0-$C0n3` | Slinky: bit 0-7 / 8-15 / 16-23 dell'indirizzo (`$C0n2` letto OR `$F0`), dato con autoincremento |
| `$C0n4-$C0nE` | registri semplici |
| `$C0nF` | bit 0 = 1: registri VERA in `$Cn00-$Cn1F`; 0: ROM di slot (azzerato dal reset) |
| `$Cn00-$CnFF` | ROM di slot ProDOS (dispositivo a blocchi, driver a `$Cn33`, `"VERAS"` a `$CnF0`) |
| `$Cn00-$Cn1F` | registri VERA, con `$C0nF` bit 0 = 1 |

Dopo un reset è visibile la ROM, così ProDOS trova il RAM disk. I programmi mostrano la VERA con
`$C0nF = 1`: lo fanno il codice d'avvio di libvera e `vera_detect()` quando leggono `"VERAS"` in
`$CnF0`. Dettagli: [A2-SLINKY.md](A2-SLINKY.md).

## Componenti (previsti)

| Componente | Parte |
|---|---|
| FPGA VERA | Lattice iCE40UP5K-SG48ITR, clock 25 MHz, flash SPI W25Q16 |
| Controller di bus | ESP32-S3FN8 (240 MHz, 8 MB di flash), quarzo 40 MHz |
| Audio | codec WM8524, jack 3,5 mm |
| Video | connettore VGA |
| Traslatori di livello | 5 V ↔ 3,3 V su indirizzi, dati e controlli |

## Firmware

`firmware/MCU` (PlatformIO, ESP32-S3) e la ROM di slot 6502 (`firmware/MCU/6502`, cc65):

```bash
cd firmware/MCU
pio run                      # compila (prima assembla la ROM di slot)
pio run --target upload      # UART0 su GPIO 43/44
```

Test host: `firmware/MCU/test/host/test_a2_slinky.c`. Emulatore: **a2emu-vera** con
`A2_VERA_CARD=rbl`. Vedi [firmware/MCU/README.md](firmware/MCU/README.md).

## Licenza

GNU General Public License v3.0, vedi [LICENSE](LICENSE).
