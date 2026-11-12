/**
 * main.cpp  --  Apple II slot card bus controller (VERA Module RBL-A2)
 *
 * Target MCU: ESP32-S3FN8 (QFN56)
 *
 * The ESP32 sits on the Apple II slot bus and serves:
 *   - /DEVSEL $C0n0-$C0nF : expanded memory, Apple II Memory Expansion Card
 *                           ("Slinky") registers, 256 KB (a2_slinky.h);
 *                           $C0nF bit 0 maps the VERA over $Cn00-$Cn1F
 *   - /IOSEL  $Cn00-$CnFF : slot ROM with the ProDOS block driver
 *                           (6502/src/slinky_rom.s), or, with $C0nF bit 0
 *                           set, the VERA registers at $Cn00-$Cn1F
 *                           (DEV_SEL_N = VERA chip select)
 * The slot number is never needed: the Apple decodes /DEVSEL and /IOSEL for
 * the card's slot, and the slot ROM is position independent.  The card does
 * not use $C800-$CFFF (/IOSTROBE).
 *
 * At power-on the 256 KB are formatted as an empty ProDOS volume (/SLINKY);
 * an Apple reset clears the Slinky registers (and unmaps the VERA), never the
 * memory.
 *
 * Slot signals (PIN-MAPPING.md, A2-SLINKY.md):
 *   GPIO 1   PHI0       (slot pin 40)
 *   GPIO 41  CARD_RD_N: the card drives the data bus (data transceiver ~OE)
 *   GPIO 42  /DEVSEL input (slot pin 41)
 *   GPIO 48  /IOSEL  input (slot pin 1)
 *   GPIO 37  Apple /RESET (slot pin 31): must be open drain
 *
 * Style: Allman
 * Language: English
 */

#include <Arduino.h>
#include <driver/gpio.h>
#include <soc/gpio_struct.h>
#include <soc/timer_group_struct.h>
#include <hal/wdt_hal.h>
#include <stddef.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <driver/dedic_gpio.h>
#include <hal/cpu_ll.h>
#include "a2_slinky.h"
#include "prodos_format.h"
#include "slinky_rom.h"

/* ===========================================================================
 * Hardware Pin Mapping (ESP32-S3FN8 QFN56)
 * =========================================================================== */

/* --- Bank 0 Signals (GPIO 0-31) --- */
#define PIN_PHI0        1    /* Apple PHI0 (Pin 6) */
#define PIN_RW          2    /* Read/Write, High = Read (Pin 7) */

/* D0-D7: GPIO 4 to 11 (Pins 9-16, contiguous in Bank 0): D0 = GPIO 4 */
#define DBUS_SHIFT      4
#define DBUS_MASK       (0xFFUL << DBUS_SHIFT)

/* A0-A9: GPIO 12 to 21 (Pins 17-27, pin 20 = VDD3P3_RTC, not GPIO) */
#define ABUS_LO_SHIFT   12

/* --- Bank 1 Signals (GPIO 32-48) --- */
#define PIN_A10         33   /* Address Bit 10 (Pin 38) */
#define PIN_A11         34   /* Address Bit 11 (Pin 39) */
#define PIN_A12         35   /* Address Bit 12 (Pin 40) */
#define PIN_A13         36   /* Address Bit 13 (Pin 41) */
#define PIN_ARESET      37   /* Apple /RESET output, held LOW until the bus loop runs (Pin 42) */
#define PIN_CRESET      38   /* Vera Chip Reset Output (Pin 43) */
#define PIN_CDONE       39   /* Vera Programmed Status Input (Pin 44) */
#define PIN_DEV_SEL_N   40   /* VERA chip select output (Pin 45, active LOW) */
#define PIN_CARD_RD_N   41   /* Card drives the data bus: data transceiver ~OE gating (Pin 47, active LOW) */
#define PIN_DEVSEL_N    42   /* Apple /DEVSEL input (Pin 48, active LOW) */
#define PIN_A14         45   /* Address Bit 14 (Pin 51) */
#define PIN_A15         46   /* Address Bit 15 (Pin 52) */
#define PIN_ABUS_OE_N   47   /* ~OE of the address translators (A0-A15), 10 kΩ pull-up (Pin 37, active LOW) */
#define PIN_IOSEL_N     48   /* Apple /IOSEL input (Pin 36, active LOW) */

/* Serial Debug (UART0 default pins on ESP32-S3 QFN56) */
#define PIN_UART_TX     43   /* UART0 TX (Pin 49) */
#define PIN_UART_RX     44   /* UART0 RX (Pin 50) */

/*
 * Strapping pins: see PIN-MAPPING.md.
 * GPIO45/46 (A14/A15) are LOW at chip reset because PIN_ABUS_OE_N keeps the
 * address translators off until setup() enables them.
 */

/* ===========================================================================
 * Dedicated GPIO channels (ee.get_gpio_in / ee.wr_mask_gpio_out, ~1 cycle)
 *
 *   Input  ch 0 = PHI0, ch 1 = R/W, ch 2 = /IOSEL, ch 3 = /DEVSEL
 *   Output ch 0 = DEV_SEL_N (VERA CS), ch 1 = CARD_RD_N   (both active LOW)
 * =========================================================================== */
#define DIN_PHI0        (1u << 0)
#define DIN_RW          (1u << 1)
#define DIN_IOSEL_N     (1u << 2)
#define DIN_DEVSEL_N    (1u << 3)

#define DOUT_VERA_CS_N  (1u << 0)
#define DOUT_CARD_RD_N  (1u << 1)
#define DOUT_MASK       0x03u
#define DOUT_IDLE       0x03u                       /* both deasserted */
#define DOUT_VERA       (DOUT_IDLE & ~DOUT_VERA_CS_N)
#define DOUT_DRIVE      (DOUT_IDLE & ~DOUT_CARD_RD_N)

/* ===========================================================================
 * Bus Timing (Apple II: PHI0 ~1.02 MHz, 489 ns high, 489 ns low; the long
 * cycle every 65 adds 140 ns)
 *
 * BUS_ADDR_SETTLE_NS  after PHI0 falls: address and R/W valid (6502 1 MHz
 *                     t_ADS = 300 ns, plus the motherboard and address buffers)
 * BUS_SEL_SAMPLE_NS   after PHI0 rises: /DEVSEL and /IOSEL valid (decoded on
 *                     the motherboard from the address and PHI0)
 * BUS_WR_SAMPLE_NS    after PHI0 rises: 6502 write data valid (t_MDS 200 ns),
 *                     held until PHI0 falls
 * BUS_PHI0_HIGH_MAX_NS  longest PHI0-high phase, used as a ceiling for the
 *                     falling edge when the loop sees it late
 * Read data must be on the bus ~100 ns (6502 1 MHz t_DSU) before PHI0 falls.
 * All values can be tuned with -D after measuring the real bus.
 * =========================================================================== */
#ifndef BUS_ADDR_SETTLE_NS
#define BUS_ADDR_SETTLE_NS   350u
#endif
#ifndef BUS_SEL_SAMPLE_NS
#define BUS_SEL_SAMPLE_NS    120u
#endif
#ifndef BUS_WR_SAMPLE_NS
#define BUS_WR_SAMPLE_NS     300u
#endif
#ifndef BUS_PHI0_HIGH_MAX_NS
#define BUS_PHI0_HIGH_MAX_NS 640u
#endif

#define CPU_MHZ             (F_CPU / 1000000UL)
#define NS_TO_CYCLES(ns)    (((ns) * CPU_MHZ + 999u) / 1000u)

/* Delay between CDONE high and the release of the Apple reset (VERA POR). */
#ifndef VERA_POST_CDONE_DELAY_US
#define VERA_POST_CDONE_DELAY_US 1000u
#endif

/* ===========================================================================
 * Log Ring (Core 1 bus loop → Core 0 LogTask, which prints on Serial)
 * Single producer / single consumer, lock-free; drops when full.
 * =========================================================================== */
#define LOG_RING_SIZE 64u   /* power of two */

#define EVT_RESET     0     /* Apple reset seen (vector fetch) */
#define EVT_VERA_MAP  1     /* $C0nF written: data = new value */

typedef struct
{
    uint8_t type;
    uint8_t data;
} LogEvt;

static DRAM_ATTR LogEvt   log_ring[LOG_RING_SIZE];
static DRAM_ATTR uint32_t log_head = 0;     /* written by the bus loop only */
static DRAM_ATTR uint32_t log_tail = 0;     /* written by LogTask only */
static DRAM_ATTR uint32_t log_dropped = 0;  /* written by the bus loop only */

static volatile bool log_ready = false;     /* Serial started by LogTask */

/* ===========================================================================
 * Shared Data
 * =========================================================================== */

/*
 * 256 KB expanded memory: ordinary .bss in DRAM.  It must not go in IRAM:
 * the bus loop accesses it with 8-bit loads/stores.
 */
static uint8_t slinky_mem[SLINKY_SIZE];
static DRAM_ATTR Slinky slinky;

static_assert(sizeof(slinky_rom) == 256u, "slot ROM must be 256 bytes ($Cn00-$CnFF)");

/* ===========================================================================
 * Helpers
 * =========================================================================== */

static inline __attribute__((always_inline)) uint8_t decode_data(uint32_t lo)
{
    return (uint8_t)((lo >> DBUS_SHIFT) & 0xFFu);
}

/* A0-A15 from GPIO.in (A0-A9 on bits 12-21) and GPIO.in1 (A10-A13 on bits
 * 1-4 = GPIO 33-36, A14/A15 on bits 13/14 = GPIO 45/46). */
static inline __attribute__((always_inline)) uint32_t decode_addr(uint32_t lo, uint32_t hi)
{
    return ((lo >> ABUS_LO_SHIFT) & 0x3FFu) |
           (((hi >> 1) & 0x0Fu) << 10) |
           (((hi >> 13) & 0x03u) << 14);
}

static inline __attribute__((always_inline)) uint32_t dedic_in(void)
{
    return cpu_ll_read_dedic_gpio_in();
}

static inline __attribute__((always_inline)) void wait_phi0_high(void)
{
    uint32_t t;
    asm volatile("1: ee.get_gpio_in %0\n\t"
                 "   bbci %0, 0, 1b"
                 : "=&r"(t));
}

static inline __attribute__((always_inline)) void wait_phi0_low(void)
{
    uint32_t t;
    asm volatile("1: ee.get_gpio_in %0\n\t"
                 "   bbsi %0, 0, 1b"
                 : "=&r"(t));
}

static inline __attribute__((always_inline)) void wait_until(uint32_t t)
{
    while ((int32_t)(cpu_ll_get_cycle_count() - t) < 0)
        ;
}

/* Release the data bus (High-Z).  Posted store, no memw. */
static inline __attribute__((always_inline)) void bus_release(void)
{
    asm volatile("s32i  %0, %1, %2"
                 :
                 : "r"(DBUS_MASK), "r"(&GPIO), "n"(offsetof(gpio_dev_t, enable_w1tc))
                 : "memory");
}

/*
 * Drive a byte: GPIO.out covers Bank 0, whose other pins are all inputs, so
 * writing the shifted byte is enough; then enable the outputs, then open the
 * data transceiver towards the Apple (CARD_RD_N).  Xtensa keeps stores in
 * program order.
 */
static inline __attribute__((always_inline)) void bus_drive(uint8_t v)
{
    GPIO.out = (uint32_t)v << DBUS_SHIFT;
    GPIO.enable_w1ts = DBUS_MASK;
    cpu_ll_write_dedic_gpio_mask(DOUT_MASK, DOUT_DRIVE);
}

static inline __attribute__((always_inline)) void log_send(uint8_t type, uint8_t data)
{
    uint32_t h = log_head;
    if (h - __atomic_load_n(&log_tail, __ATOMIC_ACQUIRE) >= LOG_RING_SIZE)
    {
        log_dropped++;
        return;
    }
    LogEvt *e = &log_ring[h & (LOG_RING_SIZE - 1u)];
    e->type = type;
    e->data = data;
    __atomic_store_n(&log_head, h + 1u, __ATOMIC_RELEASE);
}

/*
 * The bus loop runs with interrupts masked, so Core 1 no longer feeds the
 * interrupt watchdog (MWDT1): stop it, or it resets the chip after 300 ms.
 */
static void bus_int_wdt_disable(void)
{
    wdt_hal_context_t ctx = {};
    ctx.inst = WDT_MWDT1;
    ctx.mwdt_dev = &TIMERG1;
    wdt_hal_write_protect_disable(&ctx);
    wdt_hal_disable(&ctx);
    wdt_hal_write_protect_enable(&ctx);
}

/* ===========================================================================
 * MonitorTask -- Core 1, IRAM-resident, interrupts masked
 *
 * One bus cycle per iteration:
 *   PHI1 (PHI0 low):  release the bus, wait for a stable address, read it,
 *                     prepare the slot ROM byte;
 *   PHI0 high:        sample /DEVSEL and /IOSEL, then serve the access:
 *                     Slinky register, slot ROM byte or VERA chip select.
 * =========================================================================== */
static void IRAM_ATTR MonitorTask(void *arg)
{
    static const int din_pins[]  = { PIN_PHI0, PIN_RW, PIN_IOSEL_N, PIN_DEVSEL_N };
    static const int dout_pins[] = { PIN_DEV_SEL_N, PIN_CARD_RD_N };
    dedic_gpio_bundle_handle_t din_bundle, dout_bundle;
    const dedic_gpio_bundle_config_t in_cfg =
    {
        .gpio_array = din_pins,
        .array_size = 4,
        .flags      = { .in_en = 1 }
    };
    const dedic_gpio_bundle_config_t out_cfg =
    {
        .gpio_array = dout_pins,
        .array_size = 2,
        .flags      = { .out_en = 1 }
    };
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&in_cfg,  &din_bundle));
    ESP_ERROR_CHECK(dedic_gpio_new_bundle(&out_cfg, &dout_bundle));
    cpu_ll_write_dedic_gpio_mask(DOUT_MASK, DOUT_IDLE);

    uint32_t in_mask = 0, out_mask = 0;
    dedic_gpio_get_in_mask(din_bundle, &in_mask);
    dedic_gpio_get_out_mask(dout_bundle, &out_mask);
    if (in_mask != 0x0Fu || out_mask != DOUT_MASK)
    {
        Serial.printf("[VeraA2] FATAL: dedicated GPIO channels in=%02lX out=%02lX\n",
                      (unsigned long)in_mask, (unsigned long)out_mask);
        Serial.flush();
        abort();
    }

    bus_release();

    const uint32_t settle    = NS_TO_CYCLES(BUS_ADDR_SETTLE_NS);
    const uint32_t sel_wait  = NS_TO_CYCLES(BUS_SEL_SAMPLE_NS);
    const uint32_t wr_sample = NS_TO_CYCLES(BUS_WR_SAMPLE_NS);
    const uint32_t phi0_max  = NS_TO_CYCLES(BUS_PHI0_HIGH_MAX_NS);
    uint32_t       vec_prev  = 0x10000u;    /* reset detector state */

    /* From here on Core 1 belongs to the bus: no tick, no interrupts. */
    bus_int_wdt_disable();
    portDISABLE_INTERRUPTS();

    /* Release the Apple: the bus goes live; its first cycles fetch the
     * reset vector, which the detector below also sees. */
    GPIO.out1_w1ts.val = (1UL << (PIN_ARESET - 32));

    wait_phi0_high();
    uint32_t t_fall_max = cpu_ll_get_cycle_count() + phi0_max;
    wait_phi0_low();

    for (;;)
    {
        /* ---- PHI1: PHI0 has just fallen -------------------------------- */
        uint32_t t_fall = cpu_ll_get_cycle_count();
        if ((int32_t)(t_fall - t_fall_max) > 0)
            t_fall = t_fall_max;            /* edge seen late: use the prediction */

        cpu_ll_write_dedic_gpio_mask(DOUT_MASK, DOUT_IDLE);
        bus_release();

        wait_until(t_fall + settle);
        const uint32_t lo   = GPIO.in;
        const uint32_t hi   = GPIO.in1.val;
        const uint32_t addr = decode_addr(lo, hi);
        const bool     rd   = (dedic_in() & DIN_RW) != 0;

        if (a2_reset_seen(&vec_prev, addr, rd))
        {
            slinky_reset(&slinky);
            log_send(EVT_RESET, 0);
        }

        /* Prepared during PHI1: what /IOSEL would select. */
        const uint32_t off8    = addr & 0xFFu;
        const bool     io_vera = slinky_vera_mapped(&slinky) && off8 < 0x20u;
        const uint8_t  io_byte = slinky_rom[off8];

        /* ---- PHI0 high ------------------------------------------------- */
        wait_phi0_high();
        const uint32_t t_rise = cpu_ll_get_cycle_count();
        t_fall_max = t_rise + phi0_max;
        wait_until(t_rise + sel_wait);
        const uint32_t sel = dedic_in();

        if (!(sel & DIN_DEVSEL_N))
        {
            const uint32_t reg = addr & 0x0Fu;
            if (rd)
            {
                bus_drive(slinky_read(&slinky, reg));
            }
            else
            {
                wait_until(t_rise + wr_sample);
                const uint8_t v = decode_data(GPIO.in);
                slinky_write(&slinky, reg, v);
                if (reg == SLINKY_REG_VERA)
                    log_send(EVT_VERA_MAP, v);
            }
        }
        else if (!(sel & DIN_IOSEL_N))
        {
            if (io_vera)
                cpu_ll_write_dedic_gpio_mask(DOUT_MASK, DOUT_VERA);   /* VERA answers */
            else if (rd)
                bus_drive(io_byte);                                   /* slot ROM */
            /* writes to the ROM are ignored */
        }

        wait_phi0_low();
    }
}

/* ===========================================================================
 * LogTask -- Core 0: owns Serial and drains log_ring.
 * =========================================================================== */
static void log_drain(void)
{
    static uint32_t dropped_seen = 0;

    uint32_t t = log_tail;
    while (t != __atomic_load_n(&log_head, __ATOMIC_ACQUIRE))
    {
        LogEvt evt = log_ring[t & (LOG_RING_SIZE - 1u)];
        __atomic_store_n(&log_tail, ++t, __ATOMIC_RELEASE);

        uint32_t now = millis();
        if (evt.type == EVT_RESET)
            Serial.printf("[%5lu.%03lu] [A2  ] reset: Slinky registers cleared, slot ROM at $Cn00\n",
                          now / 1000u, now % 1000u);
        else
            Serial.printf("[%5lu.%03lu] [A2  ] $C0nF = $%02X: %s at $Cn00-$Cn1F\n",
                          now / 1000u, now % 1000u, evt.data,
                          (evt.data & SLINKY_VERA_MAP) ? "VERA" : "slot ROM");
    }

    uint32_t dropped = log_dropped;
    if (dropped != dropped_seen)
    {
        Serial.printf("[VeraA2] log: %lu events dropped (ring full)\n",
                      (unsigned long)(dropped - dropped_seen));
        dropped_seen = dropped;
    }
}

static void LogTask(void *arg)
{
    Serial.begin(115200, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
    log_ready = true;
    for (;;)
    {
        log_drain();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ===========================================================================
 * System Setup -- runs on Core 1 until MonitorTask takes the core over.
 * =========================================================================== */
void setup(void)
{
    /* Reclaim JTAG pins (GPIO 39-42) for regular GPIO usage */
    gpio_reset_pin((gpio_num_t)PIN_CDONE);     /* GPIO 39 */
    gpio_reset_pin((gpio_num_t)PIN_DEV_SEL_N); /* GPIO 40 */
    gpio_reset_pin((gpio_num_t)PIN_CARD_RD_N); /* GPIO 41 */
    gpio_reset_pin((gpio_num_t)PIN_DEVSEL_N);  /* GPIO 42 */

    /* Hold the Apple and the VERA in reset: the VERA is released below, the
     * Apple only by MonitorTask once the bus loop is ready. */
    pinMode(PIN_ARESET, OUTPUT);
    digitalWrite(PIN_ARESET, LOW);
    pinMode(PIN_CRESET, OUTPUT);
    digitalWrite(PIN_CRESET, LOW);

    xTaskCreatePinnedToCore(LogTask, "BusLog", 4096, NULL, 1, NULL, 0);
    while (!log_ready)
        delay(1);

    Serial.println("[VeraA2] Apple II slot card: Slinky 256 KB at $C0n0-$C0n3, "
                   "ProDOS slot ROM at $Cn00, VERA at $Cn00-$Cn1F with $C0nF bit 0");

    slinky_init(&slinky, slinky_mem);
    prodos_format(slinky_mem, SLINKY_BLOCKS, "SLINKY");
    Serial.printf("[VeraA2] RAM disk /SLINKY formatted: %u blocks\n", (unsigned)SLINKY_BLOCKS);

    /* Inputs: PHI0, R/W, D0-D7, A0-A9 (Bank 0); A10-A15, CDONE, /DEVSEL,
     * /IOSEL (Bank 1). */
    gpio_config_t cfg = {};
    cfg.intr_type    = GPIO_INTR_DISABLE;
    cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
    cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    cfg.mode         = GPIO_MODE_INPUT;
    cfg.pin_bit_mask = (1ULL << PIN_PHI0) | (1ULL << PIN_RW) | DBUS_MASK |
                       (0x3FFULL << ABUS_LO_SHIFT);
    gpio_config(&cfg);
    cfg.pin_bit_mask = (1ULL << PIN_A10) | (1ULL << PIN_A11) |
                       (1ULL << PIN_A12) | (1ULL << PIN_A13) |
                       (1ULL << PIN_A14) | (1ULL << PIN_A15) |
                       (1ULL << PIN_CDONE) | (1ULL << PIN_DEVSEL_N) |
                       (1ULL << PIN_IOSEL_N);
    gpio_config(&cfg);

    /* Strapping pins latched: enable the address translators. */
    pinMode(PIN_ABUS_OE_N, OUTPUT);
    digitalWrite(PIN_ABUS_OE_N, LOW);

    pinMode(PIN_DEV_SEL_N, OUTPUT);
    digitalWrite(PIN_DEV_SEL_N, HIGH);
    pinMode(PIN_CARD_RD_N, OUTPUT);
    digitalWrite(PIN_CARD_RD_N, HIGH);

    /* VERA bring-up: release CRESET, wait for CDONE, then its POR. */
    digitalWrite(PIN_CRESET, HIGH);
    Serial.println("[VeraA2] VERA: released from reset, waiting CDONE...");
    uint32_t elapsed_ms = 0u;
    while (!digitalRead(PIN_CDONE) && elapsed_ms < 5000u)
    {
        delay(1);
        elapsed_ms++;
    }
    if (elapsed_ms < 5000u)
        Serial.printf("[VeraA2] VERA: CDONE OK (~%lu ms)\n", (unsigned long)elapsed_ms);
    else
        Serial.println("[VeraA2] VERA: CDONE timeout — FPGA not configured!");
    delayMicroseconds(VERA_POST_CDONE_DELAY_US);

    Serial.printf("[VeraA2] Bus loop on Core 1 (settle %u ns, select %u ns, write %u ns); "
                  "Apple leaves reset\n",
                  BUS_ADDR_SETTLE_NS, BUS_SEL_SAMPLE_NS, BUS_WR_SAMPLE_NS);
    Serial.flush();

    xTaskCreatePinnedToCore(MonitorTask, "BusMonitor", 4096, NULL,
                            configMAX_PRIORITIES - 1, NULL, 1);
    Serial.println("[VeraA2] FATAL: cannot start the bus loop");
}

void loop(void)
{
    /* Not reached: MonitorTask owns Core 1. */
    vTaskDelay(portMAX_DELAY);
}
