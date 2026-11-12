/**
 * test_a2_slinky.c  --  host test for include/a2_slinky.h, prodos_format.h
 * and the slot ROM image (include/slinky_rom.h)
 *
 *   cc -O2 -Wall -I../../include test_a2_slinky.c -o test_a2_slinky && ./test_a2_slinky
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "a2_slinky.h"
#include "prodos_format.h"
#include "slinky_rom.h"

static uint8_t mem[SLINKY_SIZE];
static int errors = 0;

#define CHECK(cond, ...) do { if (!(cond)) { errors++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                                              printf(__VA_ARGS__); printf("\n"); } } while (0)

static void set_addr(Slinky *s, uint32_t a)
{
    slinky_write(s, 0, (uint8_t)a);
    slinky_write(s, 1, (uint8_t)(a >> 8));
    slinky_write(s, 2, (uint8_t)(a >> 16));
}

static void test_registers(void)
{
    Slinky s;
    slinky_init(&s, mem);
    memset(mem, 0, sizeof(mem));

    set_addr(&s, 0x012345);
    CHECK(slinky_read(&s, 0) == 0x45, "addr lo");
    CHECK(slinky_read(&s, 1) == 0x23, "addr mid");
    CHECK(slinky_read(&s, 2) == 0xF1, "addr hi reads OR $F0: %02X", slinky_read(&s, 2));

    /* data writes auto-increment, with carry across the bytes */
    set_addr(&s, 0x00FFFE);
    for (int i = 0; i < 4; i++)
        slinky_write(&s, 3, (uint8_t)(0xA0 + i));
    CHECK(s.addr == 0x010002, "carry: %06X", (unsigned)s.addr);
    CHECK(mem[0xFFFE] == 0xA0 && mem[0xFFFF] == 0xA1 && mem[0x10000] == 0xA2 && mem[0x10001] == 0xA3,
          "data written across the 64K boundary");

    /* reads auto-increment as well */
    set_addr(&s, 0x00FFFE);
    for (int i = 0; i < 4; i++)
        CHECK(slinky_read(&s, 3) == 0xA0 + i, "read back %d", i);

    /* one byte load keeps the counted value of the others */
    set_addr(&s, 0x0000FF);
    (void)slinky_read(&s, 3);                       /* -> $000100 */
    slinky_write(&s, 0, 0x10);                      /* -> $000110 */
    CHECK(s.addr == 0x000110, "byte load: %06X", (unsigned)s.addr);

    /* the memory repeats every 256 KB; the counter is 24 bits */
    set_addr(&s, 0x040000 + 0x1234);
    slinky_write(&s, 3, 0x5A);
    CHECK(mem[0x1234] == 0x5A, "mirror above 256 KB");
    set_addr(&s, 0xFFFFFF);
    (void)slinky_read(&s, 3);
    CHECK(s.addr == 0, "24-bit wrap");

    /* $C0n4-$C0nF plain registers; $C0nF bit 0 = VERA map */
    for (uint32_t r = 4; r < 16; r++)
        slinky_write(&s, r, (uint8_t)(r * 7));
    for (uint32_t r = 4; r < 16; r++)
        CHECK(slinky_read(&s, r) == (uint8_t)(r * 7), "plain register %u", (unsigned)r);
    slinky_write(&s, 15, 0x01);
    CHECK(slinky_vera_mapped(&s), "VERA mapped");
    slinky_write(&s, 15, 0x00);
    CHECK(!slinky_vera_mapped(&s), "VERA unmapped");

    /* reset clears registers and address, not memory */
    slinky_write(&s, 15, 0x01);
    set_addr(&s, 0x020000);
    slinky_reset(&s);
    CHECK(!slinky_vera_mapped(&s) && s.addr == 0, "reset");
    CHECK(mem[0x1234] == 0x5A, "memory survives reset");
}

static void test_reset_detector(void)
{
    uint32_t prev = 0x10000u;
    CHECK(!a2_reset_seen(&prev, 0xFFFC, true), "FFFC alone");
    CHECK(a2_reset_seen(&prev, 0xFFFD, true), "FFFC then FFFD");
    CHECK(!a2_reset_seen(&prev, 0xFFFD, true), "FFFD again");
    a2_reset_seen(&prev, 0xFFFC, true);
    CHECK(!a2_reset_seen(&prev, 0xFFFD, false), "write to FFFD");
    a2_reset_seen(&prev, 0xFFFC, false);
    CHECK(!a2_reset_seen(&prev, 0xFFFD, true), "write to FFFC");
    a2_reset_seen(&prev, 0xFFFC, true);
    a2_reset_seen(&prev, 0x1234, true);
    CHECK(!a2_reset_seen(&prev, 0xFFFD, true), "not consecutive");
}

static void test_format(void)
{
    prodos_format(mem, SLINKY_BLOCKS, "SLINKY");
    const uint8_t *k = mem + 2 * 512;
    CHECK(k[0] == 0 && k[1] == 0 && k[2] == 3 && k[3] == 0, "key block links");
    CHECK(k[4] == 0xF6 && memcmp(k + 5, "SLINKY", 6) == 0, "volume name");
    CHECK(k[0x23] == 0x27 && k[0x24] == 0x0D, "entry length / per block");
    CHECK(k[0x27] == 6 && k[0x28] == 0, "bitmap pointer");
    CHECK(k[0x29] == (SLINKY_BLOCKS & 0xFF) && k[0x2A] == (SLINKY_BLOCKS >> 8), "total blocks");
    const uint8_t *b5 = mem + 5 * 512;
    CHECK(b5[0] == 4 && b5[2] == 0 && b5[3] == 0, "last directory block");
    const uint8_t *bm = mem + 6 * 512;
    CHECK(bm[0] == 0x01, "blocks 0-6 used, 7 free: %02X", bm[0]);
    for (uint32_t i = 1; i < SLINKY_BLOCKS / 8; i++)
        CHECK(bm[i] == 0xFF, "bitmap byte %u", (unsigned)i);
    CHECK(bm[SLINKY_BLOCKS / 8] == 0x00, "no bits past the end");
}

static void test_rom(void)
{
    /* ProDOS / autostart signature, block device (not SmartPort) */
    CHECK(slinky_rom[1] == 0x20 && slinky_rom[3] == 0x00 && slinky_rom[5] == 0x03 && slinky_rom[7] == 0x3C,
          "signature");
    CHECK(slinky_rom[0xFF] >= 0x20, "driver entry above the VERA window: %02X", slinky_rom[0xFF]);
    CHECK(slinky_rom[0xFF] != 0x00, "$CnFF = 0 means Disk II to ProDOS");
    CHECK((slinky_rom[0xFC] | slinky_rom[0xFD] << 8) == SLINKY_BLOCKS, "block count");
    CHECK(memcmp(&slinky_rom[0xF0], "VERA", 4) == 0, "VERA id");
}

int main(void)
{
    test_registers();
    test_reset_detector();
    test_format();
    test_rom();
    printf("%s: %d errors\n", errors ? "FAILED" : "OK", errors);
    return errors != 0;
}
