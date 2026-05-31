/* =================================================================
 * display_oled.c — SSD1306 128×64 OLED driver (framebuffer-based)
 *
 * This driver renders into an off-screen 1024-byte framebuffer,
 * then flushes entire pages to the display via bulk I2C writes.
 * This eliminates the flicker and tearing caused by writing
 * individual bytes in separate I2C transactions.
 * ================================================================= */

#include "display_oled.h"
#include "i2c_bus.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include <string.h>

/* -----------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------- */
#define OLED_ADDR   0x3C
#define OLED_W      128
#define OLED_H      64
#define OLED_PAGES  (OLED_H / 8)   /* 8 pages */

/* -----------------------------------------------------------------
 * Framebuffer  —  8 pages × 128 columns = 1024 bytes
 *
 * Page layout (each page = 128 columns × 8 vertical pixels):
 *   bit 0 = topmost pixel of the page row
 *   bit 7 = bottommost pixel of the page row
 * ----------------------------------------------------------------- */
static uint8_t fb[OLED_PAGES][OLED_W];

/* -----------------------------------------------------------------
 * 5×7 Bitmap Font  —  ASCII 32 (' ') through 90 ('Z')
 *
 * Each glyph is 5 bytes; each byte encodes one column of 7 pixels.
 * Lowercase letters are mapped to uppercase before lookup.
 * ----------------------------------------------------------------- */
static const uint8_t font5x7[59][5] = {
    {0x00, 0x00, 0x00, 0x00, 0x00}, /* 32 ' ' */
    {0x00, 0x00, 0x5F, 0x00, 0x00}, /* 33 '!' */
    {0x00, 0x07, 0x00, 0x07, 0x00}, /* 34 '"' */
    {0x14, 0x7F, 0x14, 0x7F, 0x14}, /* 35 '#' */
    {0x24, 0x2A, 0x7F, 0x2A, 0x12}, /* 36 '$' */
    {0x23, 0x13, 0x08, 0x64, 0x62}, /* 37 '%' */
    {0x36, 0x49, 0x55, 0x22, 0x50}, /* 38 '&' */
    {0x00, 0x05, 0x03, 0x00, 0x00}, /* 39 ''' */
    {0x00, 0x1C, 0x22, 0x41, 0x00}, /* 40 '(' */
    {0x00, 0x41, 0x22, 0x1C, 0x00}, /* 41 ')' */
    {0x14, 0x08, 0x3E, 0x08, 0x14}, /* 42 '*' */
    {0x08, 0x08, 0x3E, 0x08, 0x08}, /* 43 '+' */
    {0x00, 0x50, 0x30, 0x00, 0x00}, /* 44 ',' */
    {0x08, 0x08, 0x08, 0x08, 0x08}, /* 45 '-' */
    {0x00, 0x60, 0x60, 0x00, 0x00}, /* 46 '.' */
    {0x20, 0x10, 0x08, 0x04, 0x02}, /* 47 '/' */
    {0x3E, 0x51, 0x49, 0x45, 0x3E}, /* 48 '0' */
    {0x00, 0x42, 0x7F, 0x40, 0x00}, /* 49 '1' */
    {0x42, 0x61, 0x51, 0x49, 0x46}, /* 50 '2' */
    {0x21, 0x41, 0x45, 0x4B, 0x31}, /* 51 '3' */
    {0x18, 0x14, 0x12, 0x7F, 0x10}, /* 52 '4' */
    {0x27, 0x45, 0x45, 0x45, 0x39}, /* 53 '5' */
    {0x3C, 0x4A, 0x49, 0x49, 0x30}, /* 54 '6' */
    {0x01, 0x71, 0x09, 0x05, 0x03}, /* 55 '7' */
    {0x36, 0x49, 0x49, 0x49, 0x36}, /* 56 '8' */
    {0x06, 0x49, 0x49, 0x29, 0x1E}, /* 57 '9' */
    {0x00, 0x36, 0x36, 0x00, 0x00}, /* 58 ':' */
    {0x00, 0x56, 0x36, 0x00, 0x00}, /* 59 ';' */
    {0x08, 0x14, 0x22, 0x41, 0x00}, /* 60 '<' */
    {0x14, 0x14, 0x14, 0x14, 0x14}, /* 61 '=' */
    {0x00, 0x41, 0x22, 0x14, 0x08}, /* 62 '>' */
    {0x02, 0x01, 0x51, 0x09, 0x06}, /* 63 '?' */
    {0x32, 0x49, 0x79, 0x41, 0x3E}, /* 64 '@' */
    {0x7E, 0x11, 0x11, 0x11, 0x7E}, /* 65 'A' */
    {0x7F, 0x49, 0x49, 0x49, 0x36}, /* 66 'B' */
    {0x3E, 0x41, 0x41, 0x41, 0x22}, /* 67 'C' */
    {0x7F, 0x41, 0x41, 0x22, 0x1C}, /* 68 'D' */
    {0x7F, 0x49, 0x49, 0x49, 0x41}, /* 69 'E' */
    {0x7F, 0x09, 0x09, 0x09, 0x01}, /* 70 'F' */
    {0x3E, 0x41, 0x49, 0x49, 0x7A}, /* 71 'G' */
    {0x7F, 0x08, 0x08, 0x08, 0x7F}, /* 72 'H' */
    {0x00, 0x41, 0x7F, 0x41, 0x00}, /* 73 'I' */
    {0x20, 0x40, 0x41, 0x3F, 0x01}, /* 74 'J' */
    {0x7F, 0x08, 0x14, 0x22, 0x41}, /* 75 'K' */
    {0x7F, 0x40, 0x40, 0x40, 0x40}, /* 76 'L' */
    {0x7F, 0x02, 0x0C, 0x02, 0x7F}, /* 77 'M' */
    {0x7F, 0x04, 0x08, 0x10, 0x7F}, /* 78 'N' */
    {0x3E, 0x41, 0x41, 0x41, 0x3E}, /* 79 'O' */
    {0x7F, 0x09, 0x09, 0x09, 0x06}, /* 80 'P' */
    {0x3E, 0x41, 0x51, 0x21, 0x5E}, /* 81 'Q' */
    {0x7F, 0x09, 0x19, 0x29, 0x46}, /* 82 'R' */
    {0x46, 0x49, 0x49, 0x49, 0x31}, /* 83 'S' */
    {0x01, 0x01, 0x7F, 0x01, 0x01}, /* 84 'T' */
    {0x3F, 0x40, 0x40, 0x40, 0x3F}, /* 85 'U' */
    {0x1F, 0x20, 0x40, 0x20, 0x1F}, /* 86 'V' */
    {0x3F, 0x40, 0x38, 0x40, 0x3F}, /* 87 'W' */
    {0x63, 0x14, 0x08, 0x14, 0x63}, /* 88 'X' */
    {0x07, 0x08, 0x70, 0x08, 0x07}, /* 89 'Y' */
    {0x61, 0x51, 0x49, 0x45, 0x43}, /* 90 'Z' */
};

/* =================================================================
 * LOW-LEVEL I2C HELPERS
 * ================================================================= */

/* Send a single command byte to the SSD1306 */
static void oled_cmd(uint8_t c) {
    i2c_write_reg(OLED_ADDR, 0x00, c);
}

/*
 * Flush one full page (128 bytes) to the SSD1306 in a single
 * atomic I2C transaction.  This is critical for clean rendering:
 *
 *   - The i2c_mutex is held for the entire operation so no other
 *     sensor task can interleave on the bus mid-page.
 *   - The page address + column 0 cursor is set first.
 *   - Then all 128 data bytes are streamed in one START…STOP.
 *
 * This replaces 128 individual i2c_write_reg() calls with ONE
 * bulk write, eliminating visible tearing.
 */
static void oled_flush_page(uint8_t page) {
    if (i2c_mutex == NULL) return;
    xSemaphoreTake(i2c_mutex, portMAX_DELAY);

    /* --- Set cursor: page address, column 0 --- */
    {
        uint8_t cmd_buf[4];
        cmd_buf[0] = 0x00;                /* Control byte: command stream */
        cmd_buf[1] = 0xB0 | (page & 0x07);/* Page address */
        cmd_buf[2] = 0x00;                /* Column low nibble  = 0 */
        cmd_buf[3] = 0x10;                /* Column high nibble = 0 */

        i2c_master_write_to_device(
            I2C_BUS_PORT, OLED_ADDR,
            cmd_buf, sizeof(cmd_buf),
            pdMS_TO_TICKS(50)
        );
    }

    /* --- Write 128 bytes of pixel data --- */
    {
        /*
         * We need to prepend the data-mode control byte (0x40)
         * before the 128 pixel bytes.  Use a small local buffer
         * of 129 bytes: [0x40, pixel0, pixel1, … pixel127].
         */
        uint8_t tx[1 + OLED_W];
        tx[0] = 0x40;  /* Control byte: data stream */
        memcpy(&tx[1], fb[page], OLED_W);

        i2c_master_write_to_device(
            I2C_BUS_PORT, OLED_ADDR,
            tx, sizeof(tx),
            pdMS_TO_TICKS(100)
        );
    }

    xSemaphoreGive(i2c_mutex);
}

/* Flush the entire framebuffer (all 8 pages) to the display */
static void oled_flush(void) {
    for (int p = 0; p < OLED_PAGES; p++) {
        oled_flush_page((uint8_t)p);
    }
}

/* =================================================================
 * FRAMEBUFFER DRAWING PRIMITIVES
 * ================================================================= */

/* Clear the entire framebuffer to black */
static void fb_clear(void) {
    memset(fb, 0x00, sizeof(fb));
}

/*
 * Draw a single character into the framebuffer at 1× size.
 *   x    = pixel column (0–127)
 *   page = page row     (0–7)
 * Each glyph occupies 5 columns + 1 blank spacing = 6 px wide.
 */
static void fb_putchar(int x, int page, char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;   /* force uppercase */
    if (ch < 32 || ch > 90)    ch  = 32;    /* clamp to font range */
    int idx = ch - 32;

    for (int i = 0; i < 5; i++) {
        int col = x + i;
        if (col >= 0 && col < OLED_W) {
            fb[page][col] = font5x7[idx][i];
        }
    }
    /* 1-pixel inter-character gap */
    if (x + 5 >= 0 && x + 5 < OLED_W) {
        fb[page][x + 5] = 0x00;
    }
}

/* Draw a null-terminated string at 1× size */
static void fb_puts(int x, int page, const char *s) {
    while (*s && x < OLED_W) {
        fb_putchar(x, page, *s++);
        x += 6;   /* 5 glyph + 1 space */
    }
}

/*
 * Double each bit in a byte: input 8 bits → output 16 bits.
 *   bit 0 → bits 0,1
 *   bit 1 → bits 2,3
 *   …
 *   bit 7 → bits 14,15
 *
 * The low byte goes into the top page, the high byte into
 * the bottom page, producing a 2× vertical stretch.
 */
static uint16_t scale_col_2x(uint8_t b) {
    uint16_t r = 0;
    for (int i = 0; i < 8; i++) {
        if (b & (1 << i)) {
            r |= ((uint16_t)3 << (i * 2));
        }
    }
    return r;
}

/*
 * Draw a single character at 2× size.
 *   x    = pixel column (0–127)
 *   page = top page row (must be ≤ 6, since it spans page and page+1)
 * Each 2× glyph occupies 10 columns + 2 blank spacing = 12 px wide.
 */
static void fb_putchar_2x(int x, int page, char ch) {
    if (ch >= 'a' && ch <= 'z') ch -= 32;
    if (ch < 32 || ch > 90)    ch  = 32;
    int idx = ch - 32;

    if (page < 0 || page + 1 >= OLED_PAGES) return;

    for (int i = 0; i < 5; i++) {
        uint16_t sc  = scale_col_2x(font5x7[idx][i]);
        uint8_t  top = (uint8_t)(sc & 0xFF);
        uint8_t  bot = (uint8_t)((sc >> 8) & 0xFF);

        int px = x + (i * 2);
        /* Each font column is drawn twice horizontally (2× width) */
        if (px >= 0 && px < OLED_W) {
            fb[page][px]     = top;
            fb[page + 1][px] = bot;
        }
        if (px + 1 >= 0 && px + 1 < OLED_W) {
            fb[page][px + 1]     = top;
            fb[page + 1][px + 1] = bot;
        }
    }

    /* 2-pixel inter-character gap */
    int sx = x + 10;
    for (int j = 0; j < 2; j++) {
        if (sx + j >= 0 && sx + j < OLED_W) {
            fb[page][sx + j]     = 0x00;
            fb[page + 1][sx + j] = 0x00;
        }
    }
}

/* Draw a null-terminated string at 2× size */
static void fb_puts_2x(int x, int page, const char *s) {
    while (*s && x < OLED_W) {
        fb_putchar_2x(x, page, *s++);
        x += 12;   /* 10 glyph + 2 space */
    }
}

/* Draw a full-width horizontal line at a specific bit within a page */
static void fb_hline(int page, int bit) {
    if (page < 0 || page >= OLED_PAGES) return;
    uint8_t mask = (uint8_t)(1 << bit);
    for (int x = 0; x < OLED_W; x++) {
        fb[page][x] |= mask;
    }
}

/* =================================================================
 * PUBLIC API
 * ================================================================= */

void display_oled_init(void) {
    /* Let the SSD1306 finish its internal power-on-reset */
    vTaskDelay(pdMS_TO_TICKS(100));

    /* ---- Full SSD1306 128×64 Initialization Sequence ---- */

    oled_cmd(0xAE);         /* Display OFF                              */

    oled_cmd(0xD5);         /* Set Display Clock Divide / Osc Freq      */
    oled_cmd(0x80);         /* Default: divide=1, freq=8                */

    oled_cmd(0xA8);         /* Set Multiplex Ratio                      */
    oled_cmd(0x3F);         /* 64 lines (0x3F = 63)                     */

    oled_cmd(0xD3);         /* Set Display Offset                       */
    oled_cmd(0x00);         /* No offset                                */

    oled_cmd(0x40);         /* Set Display Start Line = 0               */

    oled_cmd(0x8D);         /* Charge Pump Setting                      */
    oled_cmd(0x14);         /* Enable charge pump (internal VCC)        */

    oled_cmd(0x20);         /* Set Memory Addressing Mode               */
    oled_cmd(0x02);         /* Page addressing mode                     */

    oled_cmd(0xA1);         /* Segment Re-map: col 127 → SEG0           */
    oled_cmd(0xC8);         /* COM Output Scan Direction: remapped      */

    oled_cmd(0xDA);         /* Set COM Pins Hardware Configuration      */
    oled_cmd(0x12);         /* Alternative COM pin, no L/R remap        */

    oled_cmd(0x81);         /* Set Contrast Control                     */
    oled_cmd(0xCF);         /* High contrast (internal VCC)             */

    oled_cmd(0xD9);         /* Set Pre-charge Period                    */
    oled_cmd(0xF1);         /* Phase1=1 DCLK, Phase2=15 DCLKs           */

    oled_cmd(0xDB);         /* Set VCOMH Deselect Level                 */
    oled_cmd(0x40);         /* ~0.89 × VCC                              */

    oled_cmd(0xA4);         /* Entire Display ON (follows RAM)          */
    oled_cmd(0xA6);         /* Normal display (not inverted)            */

    oled_cmd(0xAF);         /* Display ON                               */

    /* Paint the screen black */
    fb_clear();
    oled_flush();

    printf("[OLED] Display initialized (128x64, SSD1306)\n");
}

void display_update_status(bool online, bool crash, int battery_pct) {
    char buf[22];   /* max 21 chars at 1×  (128 / 6 = 21) */

    /* --- Start with a completely blank framebuffer --- */
    fb_clear();

    /* ---- Page 0: Wi-Fi status (1× text) ---- */
    snprintf(buf, sizeof(buf), "STATUS: %s", online ? "ONLINE" : "OFFLINE");
    fb_puts(0, 0, buf);

    /* ---- Page 1: thin separator line at bottom edge ---- */
    fb_hline(1, 7);

    /* ---- Pages 2–3: Crash status (2× text) ---- */
    snprintf(buf, sizeof(buf), "CRASH:%s", crash ? "YES" : "NO");
    fb_puts_2x(0, 2, buf);

    /* ---- Pages 5–6: Battery percentage (2× text) ---- */
    snprintf(buf, sizeof(buf), "BATT:%d%%", battery_pct);
    fb_puts_2x(0, 5, buf);

    /* ---- Flush everything to the physical display ---- */
    oled_flush();
}
