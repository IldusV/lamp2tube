// // #include <stdio.h>
// // #include "freertos/FreeRTOS.h"
// // #include "freertos/task.h"
// // #include "esp_log.h"
// // #include "led_strip.h"

// // #define LED_GPIO      10      // WS2812 on ESP32-C3-Zero
// // #define BLINK_MS      500
// // #define BRIGHTNESS    32      // 0-255, WS2812s are very bright

// // static const char *TAG = "blink";

// // void app_main(void)
// // {
// //     led_strip_handle_t strip;

// //     led_strip_config_t strip_cfg = {
// //         .strip_gpio_num = LED_GPIO,
// //         .max_leds = 1,
// //     };
// //     led_strip_rmt_config_t rmt_cfg = {
// //         .resolution_hz = 10 * 1000 * 1000,  // 10 MHz
// //     };
// //     ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &strip));
// //     led_strip_clear(strip);

// //     bool on = false;
// //     while (1) {
// //         if (on) {
// //             led_strip_set_pixel(strip, 0, 0, BRIGHTNESS, 0);  // green (R,G,B)
// //             led_strip_refresh(strip);
// //         } else {
// //             led_strip_clear(strip);
// //         }
// //         ESP_LOGI(TAG, "LED %s", on ? "ON" : "OFF");
// //         on = !on;
// //         vTaskDelay(pdMS_TO_TICKS(BLINK_MS));
// //     }
// // }

// #include <stdio.h>
// #include <string.h>
// #include <ctype.h>
// #include "freertos/FreeRTOS.h"
// #include "freertos/task.h"
// #include "driver/spi_master.h"
// #include "driver/gpio.h"
// #include "esp_heap_caps.h"
// #include "esp_log.h"
// #include "esp_timer.h"

// // ---------- Pins (ESP32-C3-Zero) ----------
// #define PIN_SCLK   4
// #define PIN_MOSI   6
// #define PIN_CS     7
// #define PIN_DC     8
// #define PIN_RST    3
// #define PIN_BLK    (-1)    // set to a GPIO number if your module's BLK pin is wired to the MCU

// // ---------- Panel ----------
// #define LCD_W      240
// #define LCD_H      280
// #define X_OFFSET   0
// #define Y_OFFSET   20      // 240x280 panel sits at row 20 of the ST7789's 240x320 RAM
// #define SPI_HZ     (40 * 1000 * 1000)

// #define BUF_PIXELS (LCD_W * 20)
// #define BUF_BYTES  (BUF_PIXELS * 2)

// // RGB565
// #define BLACK   0x0000
// #define WHITE   0xFFFF
// #define RED     0xF800
// #define GREEN   0x07E0
// #define BLUE    0x001F
// #define YELLOW  0xFFE0
// #define CYAN    0x07FF
// #define MAGENTA 0xF81F
// #define ORANGE  0xFD20

// static const char *TAG = "lcd";
// static spi_device_handle_t spi;
// static uint8_t *dma_buf;   // DMA-capable scratch buffer

// // ---------- 5x7 font (columns, bit0 = top row) ----------
// typedef struct { char c; uint8_t d[5]; } glyph_t;
// static const glyph_t font[] = {
//     {' ', {0x00,0x00,0x00,0x00,0x00}}, {'-', {0x08,0x08,0x08,0x08,0x08}},
//     {'.', {0x00,0x60,0x60,0x00,0x00}}, {':', {0x00,0x36,0x36,0x00,0x00}},
//     {'0', {0x3E,0x51,0x49,0x45,0x3E}}, {'1', {0x00,0x42,0x7F,0x40,0x00}},
//     {'2', {0x42,0x61,0x51,0x49,0x46}}, {'3', {0x21,0x41,0x45,0x4B,0x31}},
//     {'4', {0x18,0x14,0x12,0x7F,0x10}}, {'5', {0x27,0x45,0x45,0x45,0x39}},
//     {'6', {0x3C,0x4A,0x49,0x49,0x30}}, {'7', {0x01,0x71,0x09,0x05,0x03}},
//     {'8', {0x36,0x49,0x49,0x49,0x36}}, {'9', {0x06,0x49,0x49,0x29,0x1E}},
//     {'A', {0x7E,0x11,0x11,0x11,0x7E}}, {'B', {0x7F,0x49,0x49,0x49,0x36}},
//     {'C', {0x3E,0x41,0x41,0x41,0x22}}, {'D', {0x7F,0x41,0x41,0x22,0x1C}},
//     {'E', {0x7F,0x49,0x49,0x49,0x41}}, {'F', {0x7F,0x09,0x09,0x09,0x01}},
//     {'G', {0x3E,0x41,0x49,0x49,0x7A}}, {'H', {0x7F,0x08,0x08,0x08,0x7F}},
//     {'I', {0x00,0x41,0x7F,0x41,0x00}}, {'J', {0x20,0x40,0x41,0x3F,0x01}},
//     {'K', {0x7F,0x08,0x14,0x22,0x41}}, {'L', {0x7F,0x40,0x40,0x40,0x40}},
//     {'M', {0x7F,0x02,0x0C,0x02,0x7F}}, {'N', {0x7F,0x04,0x08,0x10,0x7F}},
//     {'O', {0x3E,0x41,0x41,0x41,0x3E}}, {'P', {0x7F,0x09,0x09,0x09,0x06}},
//     {'Q', {0x3E,0x41,0x51,0x21,0x5E}}, {'R', {0x7F,0x09,0x19,0x29,0x46}},
//     {'S', {0x46,0x49,0x49,0x49,0x31}}, {'T', {0x01,0x01,0x7F,0x01,0x01}},
//     {'U', {0x3F,0x40,0x40,0x40,0x3F}}, {'V', {0x1F,0x20,0x40,0x20,0x1F}},
//     {'W', {0x3F,0x40,0x38,0x40,0x3F}}, {'X', {0x63,0x14,0x08,0x14,0x63}},
//     {'Y', {0x07,0x08,0x70,0x08,0x07}}, {'Z', {0x61,0x51,0x49,0x45,0x43}},
// };

// static const uint8_t *glyph_for(char c)
// {
//     c = toupper((unsigned char)c);
//     for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
//         if (font[i].c == c) return font[i].d;
//     return font[0].d;  // unknown -> space
// }

// // ---------- Low-level SPI ----------
// static void IRAM_ATTR dc_pre_cb(spi_transaction_t *t)
// {
//     gpio_set_level(PIN_DC, (int)(intptr_t)t->user);
// }

// static void lcd_write(const void *data, size_t len, int dc)
// {
//     if (len == 0) return;
//     spi_transaction_t t = {
//         .length = len * 8,
//         .tx_buffer = data,
//         .user = (void *)(intptr_t)dc,
//     };
//     ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
// }

// static void lcd_cmd(uint8_t cmd) { lcd_write(&cmd, 1, 0); }
// static void lcd_data(const void *d, size_t n) { lcd_write(d, n, 1); }

// static void lcd_cmd_data(uint8_t cmd, const uint8_t *d, size_t n)
// {
//     lcd_cmd(cmd);
//     if (n) lcd_data(d, n);
// }

// static void set_window(int x0, int y0, int x1, int y1)
// {
//     x0 += X_OFFSET; x1 += X_OFFSET;
//     y0 += Y_OFFSET; y1 += Y_OFFSET;
//     uint8_t ca[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
//     uint8_t ra[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
//     lcd_cmd_data(0x2A, ca, 4);
//     lcd_cmd_data(0x2B, ra, 4);
//     lcd_cmd(0x2C);  // RAMWR
// }

// // ---------- Drawing ----------
// static void fill_rect(int x, int y, int w, int h, uint16_t color)
// {
//     if (x < 0 || y < 0 || w <= 0 || h <= 0 || x + w > LCD_W || y + h > LCD_H) return;

//     uint8_t hi = color >> 8, lo = color & 0xFF;
//     int total = w * h;
//     int chunk = total < BUF_PIXELS ? total : BUF_PIXELS;
//     for (int i = 0; i < chunk; i++) { dma_buf[2 * i] = hi; dma_buf[2 * i + 1] = lo; }

//     set_window(x, y, x + w - 1, y + h - 1);
//     while (total > 0) {
//         int n = total < chunk ? total : chunk;
//         lcd_data(dma_buf, n * 2);
//         total -= n;
//     }
// }

// static void draw_char(int x, int y, char c, int scale, uint16_t fg, uint16_t bg)
// {
//     const uint8_t *g = glyph_for(c);
//     int w = 6 * scale, h = 7 * scale;   // 5 columns + 1 spacing column
//     if (x < 0 || y < 0 || x + w > LCD_W || y + h > LCD_H) return;

//     for (int row = 0; row < h; row++) {
//         for (int col = 0; col < w; col++) {
//             int gc = col / scale, gr = row / scale;
//             bool on = (gc < 5) && ((g[gc] >> gr) & 1);
//             uint16_t px = on ? fg : bg;
//             int idx = (row * w + col) * 2;
//             dma_buf[idx] = px >> 8;
//             dma_buf[idx + 1] = px & 0xFF;
//         }
//     }
//     set_window(x, y, x + w - 1, y + h - 1);
//     lcd_data(dma_buf, w * h * 2);
// }

// static void draw_text(int x, int y, const char *s, int scale, uint16_t fg, uint16_t bg)
// {
//     for (; *s; s++, x += 6 * scale)
//         draw_char(x, y, *s, scale, fg, bg);
// }

// static int text_width(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }

// // Draws a horizontally scrolling line. Returns the scroll period in pixels.
// static int draw_marquee(int y, const char *s, int scale, int offset,
//                         uint16_t fg, uint16_t bg)
// {
//     static uint8_t cols[6 * 32];       // 5 glyph columns + 1 blank per char
//     int n = (int)strlen(s);
//     if (n > 32) n = 32;
//     for (int i = 0; i < n; i++) {
//         memcpy(&cols[i * 6], glyph_for(s[i]), 5);
//         cols[i * 6 + 5] = 0;
//     }

//     int text_w = n * 6 * scale;
//     int period = text_w + LCD_W / 2;   // gap between repeats
//     int h = 7 * scale;
//     offset %= period;

//     set_window(0, y, LCD_W - 1, y + h - 1);

//     const int max_rows = BUF_PIXELS / LCD_W;   // rows that fit in dma_buf
//     for (int row = 0; row < h; ) {
//         int rows = (h - row < max_rows) ? (h - row) : max_rows;
//         int idx = 0;
//         for (int r = 0; r < rows; r++) {
//             int gr = (row + r) / scale;
//             for (int x = 0; x < LCD_W; x++) {
//                 int tx = (x + offset) % period;
//                 uint16_t px = bg;
//                 if (tx < text_w && ((cols[tx / scale] >> gr) & 1)) px = fg;
//                 dma_buf[idx++] = px >> 8;
//                 dma_buf[idx++] = px & 0xFF;
//             }
//         }
//         lcd_data(dma_buf, rows * LCD_W * 2);
//         row += rows;
//     }
//     return period;
// }

// // ---------- Init ----------
// static void lcd_init(void)
// {
//     gpio_config_t io = {
//         .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST),
//         .mode = GPIO_MODE_OUTPUT,
//     };
//     ESP_ERROR_CHECK(gpio_config(&io));

// #if PIN_BLK >= 0
//     gpio_config_t bl = {
//         .pin_bit_mask = 1ULL << PIN_BLK,
//         .mode = GPIO_MODE_OUTPUT,
//     };
//     ESP_ERROR_CHECK(gpio_config(&bl));
//     gpio_set_level(PIN_BLK, 1);
// #endif

//     spi_bus_config_t bus = {
//         .sclk_io_num = PIN_SCLK,
//         .mosi_io_num = PIN_MOSI,
//         .miso_io_num = -1,
//         .quadwp_io_num = -1,
//         .quadhd_io_num = -1,
//         .max_transfer_sz = BUF_BYTES,
//     };
//     ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

//     spi_device_interface_config_t dev = {
//         .clock_speed_hz = SPI_HZ,
//         .mode = 0,                  // if colors/garbage look wrong, try mode 3
//         .spics_io_num = PIN_CS,
//         .queue_size = 1,
//         .pre_cb = dc_pre_cb,
//     };
//     ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spi));

//     dma_buf = heap_caps_malloc(BUF_BYTES, MALLOC_CAP_DMA);
//     assert(dma_buf);

//     // Hardware reset
//     gpio_set_level(PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
//     gpio_set_level(PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));

//     lcd_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));   // software reset
//     lcd_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));   // sleep out

//     lcd_cmd_data(0x36, (uint8_t[]){0x00}, 1);        // MADCTL: portrait, RGB order
//     lcd_cmd_data(0x3A, (uint8_t[]){0x05}, 1);        // 16-bit color (RGB565)

//     lcd_cmd_data(0xB2, (uint8_t[]){0x0C,0x0C,0x00,0x33,0x33}, 5);
//     lcd_cmd_data(0xB7, (uint8_t[]){0x35}, 1);
//     lcd_cmd_data(0xBB, (uint8_t[]){0x1F}, 1);
//     lcd_cmd_data(0xC0, (uint8_t[]){0x2C}, 1);
//     lcd_cmd_data(0xC2, (uint8_t[]){0x01}, 1);
//     lcd_cmd_data(0xC3, (uint8_t[]){0x12}, 1);
//     lcd_cmd_data(0xC4, (uint8_t[]){0x20}, 1);
//     lcd_cmd_data(0xC6, (uint8_t[]){0x0F}, 1);
//     lcd_cmd_data(0xD0, (uint8_t[]){0xA4,0xA1}, 2);
//     lcd_cmd_data(0xE0, (uint8_t[]){0xD0,0x08,0x11,0x08,0x0C,0x15,0x39,0x33,0x50,0x36,0x13,0x14,0x29,0x2D}, 14);
//     lcd_cmd_data(0xE1, (uint8_t[]){0xD0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,0x51,0x0B,0x16,0x14,0x2F,0x31}, 14);

//     lcd_cmd(0x21);                                   // display inversion ON (needed for IPS)
//     lcd_cmd(0x13);                                   // normal display mode
//     vTaskDelay(pdMS_TO_TICKS(10));
//     lcd_cmd(0x29);                                   // display ON
//     vTaskDelay(pdMS_TO_TICKS(20));
// }

// // ---------- App ----------
// void app_main(void)
// {
//     lcd_init();
//     ESP_LOGI(TAG, "LCD initialised");

//     fill_rect(0, 0, LCD_W, LCD_H, BLACK);

//     // Color bars
//     const uint16_t bars[] = { RED, ORANGE, YELLOW, GREEN, CYAN, BLUE, MAGENTA, WHITE };
//     int bar_w = LCD_W / 8;
//     for (int i = 0; i < 8; i++)
//         fill_rect(i * bar_w, 0, bar_w, 50, bars[i]);

//     const char *sub = "ST7789 240X280 IPS";
//     draw_text((LCD_W - text_width(sub, 2)) / 2, 110, sub, 2, CYAN, BLACK);

//     // Frame
//     fill_rect(10, 150, LCD_W - 20, 2, YELLOW);
//     fill_rect(10, 258, LCD_W - 20, 2, YELLOW);

//     draw_text(20, 165, "UPTIME", 2, GREEN, BLACK);

//     const char *title = "HELLO ESP32-C3";
//     int offset = 0;
//     int period = LCD_W;
//     uint32_t last_s = UINT32_MAX;
//     char buf[16];

//     while (1) {
//         // Scrolling title (3 px per frame)
//         period = draw_marquee(70, title, 3, offset, WHITE, BLACK);
//         offset = (offset + 3) % period;

//         // Uptime: only redraw when the second changes, so scrolling stays smooth
//         uint32_t s = (uint32_t)(esp_timer_get_time() / 1000000);
//         if (s != last_s) {
//             last_s = s;
//             snprintf(buf, sizeof(buf), "%02u:%02u:%02u",
//                      (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
//             draw_text((LCD_W - text_width(buf, 4)) / 2, 200, buf, 4, ORANGE, BLACK);
//         }

//         vTaskDelay(pdMS_TO_TICKS(50));
//     }
// }

/*
 * Cyberpunk glitch UI - ESP32-C3-Zero + ST7789 240x280
 * Effects adapted from dsalaj/cybr_lmp: random glitch jitter, TV-static noise
 * bursts, diagonal scanline with pause, heartbeat indicator, glitchy scrolling
 * terminal, power-up animation, blinking hex counter.
 * Whole frame is rendered into RAM (134 KB), post-processed, then DMA'd out.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"

// ---------- Pins (ESP32-C3-Zero) ----------
#define PIN_SCLK   4
#define PIN_MOSI   6
#define PIN_CS     7
#define PIN_DC     8
#define PIN_RST    3
#define PIN_BLK    (-1)    // set to a GPIO number if BLK is wired to the MCU

// ---------- Panel ----------
#define LCD_W      240
#define LCD_H      280
#define X_OFFSET   0
#define Y_OFFSET   20
#define SPI_HZ     (40 * 1000 * 1000)

#define FB_PIXELS   (LCD_W * LCD_H)
#define FB_BYTES    (FB_PIXELS * 2)
#define CHUNK_ROWS  40
#define CHUNK_BYTES (LCD_W * CHUNK_ROWS * 2)
_Static_assert((FB_BYTES % CHUNK_BYTES) == 0, "chunk must divide framebuffer");

// ---------- Colors (RGB565, host order) ----------
#define RGB565(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
#define C_BG      RGB565(4, 6, 18)
#define C_CYAN    RGB565(0, 240, 255)
#define C_PINK    RGB565(255, 0, 170)
#define C_GREEN   RGB565(0, 255, 120)
#define C_ORANGE  RGB565(255, 150, 0)
#define C_WHITE   0xFFFF
#define C_DIM     RGB565(60, 85, 120)

static const char *TAG = "cyber";
static spi_device_handle_t spi;
static uint16_t *fb;                 // framebuffer, stored byte-swapped for the panel
static uint16_t tmp_row[LCD_W];

static inline uint16_t sw(uint16_t v) { return __builtin_bswap16(v); }
static inline uint32_t millis(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// ---------- Fast PRNG ----------
static uint32_t rng_state = 0x1234ABCD;
static inline uint32_t rnd(void)
{
    uint32_t x = rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return rng_state = x;
}
static inline int rnd_range(int a, int b) { return a + (int)(rnd() % (uint32_t)(b - a + 1)); }
static inline bool chance(int pct) { return (int)(rnd() % 100) < pct; }

// ---------- 5x7 font (columns, bit0 = top row) ----------
typedef struct { char c; uint8_t d[5]; } glyph_t;
static const glyph_t font[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}}, {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'.', {0x00,0x60,0x60,0x00,0x00}}, {':', {0x00,0x36,0x36,0x00,0x00}},
    {'+', {0x08,0x08,0x3E,0x08,0x08}}, {'/', {0x20,0x10,0x08,0x04,0x02}},
    {'%', {0x23,0x13,0x08,0x64,0x62}}, {'>', {0x00,0x41,0x22,0x14,0x08}},
    {'<', {0x08,0x14,0x22,0x41,0x00}}, {'[', {0x00,0x7F,0x41,0x41,0x00}},
    {']', {0x00,0x41,0x41,0x7F,0x00}}, {'_', {0x40,0x40,0x40,0x40,0x40}},
    {'=', {0x14,0x14,0x14,0x14,0x14}}, {'!', {0x00,0x00,0x5F,0x00,0x00}},
    {'#', {0x14,0x7F,0x14,0x7F,0x14}}, {'*', {0x14,0x08,0x3E,0x08,0x14}},
    {'0', {0x3E,0x51,0x49,0x45,0x3E}}, {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}}, {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}}, {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}}, {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}}, {'9', {0x06,0x49,0x49,0x29,0x1E}},
    {'A', {0x7E,0x11,0x11,0x11,0x7E}}, {'B', {0x7F,0x49,0x49,0x49,0x36}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}}, {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}}, {'F', {0x7F,0x09,0x09,0x09,0x01}},
    {'G', {0x3E,0x41,0x49,0x49,0x7A}}, {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'I', {0x00,0x41,0x7F,0x41,0x00}}, {'J', {0x20,0x40,0x41,0x3F,0x01}},
    {'K', {0x7F,0x08,0x14,0x22,0x41}}, {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'M', {0x7F,0x02,0x0C,0x02,0x7F}}, {'N', {0x7F,0x04,0x08,0x10,0x7F}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}}, {'P', {0x7F,0x09,0x09,0x09,0x06}},
    {'Q', {0x3E,0x41,0x51,0x21,0x5E}}, {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'S', {0x46,0x49,0x49,0x49,0x31}}, {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {'U', {0x3F,0x40,0x40,0x40,0x3F}}, {'V', {0x1F,0x20,0x40,0x20,0x1F}},
    {'W', {0x3F,0x40,0x38,0x40,0x3F}}, {'X', {0x63,0x14,0x08,0x14,0x63}},
    {'Y', {0x07,0x08,0x70,0x08,0x07}}, {'Z', {0x61,0x51,0x49,0x45,0x43}},
};

static const uint8_t *glyph_tbl[128];

static void font_init(void)
{
    for (int i = 0; i < 128; i++) glyph_tbl[i] = font[0].d;   // unknown -> space
    for (size_t i = 0; i < sizeof(font) / sizeof(font[0]); i++)
        glyph_tbl[(uint8_t)font[i].c] = font[i].d;
}

static inline const uint8_t *glyph_for(char c)
{
    unsigned char u = (unsigned char)toupper((unsigned char)c);
    return glyph_tbl[u < 128 ? u : ' '];
}

// ---------- Low-level SPI / panel ----------
static void IRAM_ATTR dc_pre_cb(spi_transaction_t *t)
{
    gpio_set_level(PIN_DC, (int)(intptr_t)t->user);
}

static void lcd_write(const void *data, size_t len, int dc)
{
    if (len == 0) return;
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
        .user = (void *)(intptr_t)dc,
    };
    ESP_ERROR_CHECK(spi_device_polling_transmit(spi, &t));
}

static void lcd_cmd(uint8_t cmd) { lcd_write(&cmd, 1, 0); }
static void lcd_data(const void *d, size_t n) { lcd_write(d, n, 1); }

static void lcd_cmd_data(uint8_t cmd, const uint8_t *d, size_t n)
{
    lcd_cmd(cmd);
    if (n) lcd_data(d, n);
}

static void set_window(int x0, int y0, int x1, int y1)
{
    x0 += X_OFFSET; x1 += X_OFFSET;
    y0 += Y_OFFSET; y1 += Y_OFFSET;
    uint8_t ca[4] = { x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF };
    uint8_t ra[4] = { y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF };
    lcd_cmd_data(0x2A, ca, 4);
    lcd_cmd_data(0x2B, ra, 4);
    lcd_cmd(0x2C);  // RAMWR
}

// Push the whole framebuffer to the panel (7 DMA chunks, CPU sleeps meanwhile)
static void lcd_present(void)
{
    set_window(0, 0, LCD_W - 1, LCD_H - 1);
    const uint8_t *p = (const uint8_t *)fb;
    for (size_t off = 0; off < FB_BYTES; off += CHUNK_BYTES) {
        spi_transaction_t t = {
            .length = CHUNK_BYTES * 8,
            .tx_buffer = p + off,
            .user = (void *)1,
        };
        ESP_ERROR_CHECK(spi_device_transmit(spi, &t));
    }
}

static void lcd_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_DC) | (1ULL << PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

#if PIN_BLK >= 0
    gpio_config_t bl = {
        .pin_bit_mask = 1ULL << PIN_BLK,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&bl));
    gpio_set_level(PIN_BLK, 1);
#endif

    spi_bus_config_t bus = {
        .sclk_io_num = PIN_SCLK,
        .mosi_io_num = PIN_MOSI,
        .miso_io_num = -1,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = CHUNK_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t dev = {
        .clock_speed_hz = SPI_HZ,
        .mode = 0,
        .spics_io_num = PIN_CS,
        .queue_size = 1,
        .pre_cb = dc_pre_cb,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &spi));

    gpio_set_level(PIN_RST, 0); vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1); vTaskDelay(pdMS_TO_TICKS(120));

    lcd_cmd(0x01); vTaskDelay(pdMS_TO_TICKS(150));   // software reset
    lcd_cmd(0x11); vTaskDelay(pdMS_TO_TICKS(120));   // sleep out

    lcd_cmd_data(0x36, (uint8_t[]){0x00}, 1);        // MADCTL
    lcd_cmd_data(0x3A, (uint8_t[]){0x05}, 1);        // RGB565

    lcd_cmd_data(0xB2, (uint8_t[]){0x0C,0x0C,0x00,0x33,0x33}, 5);
    lcd_cmd_data(0xB7, (uint8_t[]){0x35}, 1);
    lcd_cmd_data(0xBB, (uint8_t[]){0x1F}, 1);
    lcd_cmd_data(0xC0, (uint8_t[]){0x2C}, 1);
    lcd_cmd_data(0xC2, (uint8_t[]){0x01}, 1);
    lcd_cmd_data(0xC3, (uint8_t[]){0x12}, 1);
    lcd_cmd_data(0xC4, (uint8_t[]){0x20}, 1);
    lcd_cmd_data(0xC6, (uint8_t[]){0x0F}, 1);
    lcd_cmd_data(0xD0, (uint8_t[]){0xA4,0xA1}, 2);
    lcd_cmd_data(0xE0, (uint8_t[]){0xD0,0x08,0x11,0x08,0x0C,0x15,0x39,0x33,0x50,0x36,0x13,0x14,0x29,0x2D}, 14);
    lcd_cmd_data(0xE1, (uint8_t[]){0xD0,0x08,0x10,0x08,0x06,0x06,0x39,0x44,0x51,0x0B,0x16,0x14,0x2F,0x31}, 14);

    lcd_cmd(0x21);                                   // inversion ON (IPS)
    lcd_cmd(0x13);
    vTaskDelay(pdMS_TO_TICKS(10));
    lcd_cmd(0x29);                                   // display ON
    vTaskDelay(pdMS_TO_TICKS(20));
}

// ---------- Framebuffer drawing primitives ----------
static int clip_y0 = 0, clip_y1 = LCD_H;     // vertical clip window for fb_rect

static void bg_fill(void)
{
    uint16_t c = sw(C_BG);
    for (int i = 0; i < FB_PIXELS; i++) fb[i] = c;
}

static void fb_rect(int x, int y, int w, int h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < clip_y0) { h -= clip_y0 - y; y = clip_y0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > clip_y1) h = clip_y1 - y;
    if (w <= 0 || h <= 0) return;
    uint16_t c = sw(color);
    for (int r = y; r < y + h; r++) {
        uint16_t *p = fb + r * LCD_W + x;
        for (int i = 0; i < w; i++) p[i] = c;
    }
}

static void fb_frame(int x, int y, int w, int h, int t, uint16_t color)
{
    fb_rect(x, y, w, t, color);
    fb_rect(x, y + h - t, w, t, color);
    fb_rect(x, y, t, h, color);
    fb_rect(x + w - t, y, t, h, color);
}

static void fb_line(int x0, int y0, int x1, int y1, int t, uint16_t color)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        fb_rect(x0, y0, t, t, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

// Saturating additive glow (used by the scanline)
static void fb_add(int x, int y, int w, int h, int dr, int dg, int db)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    if (w <= 0 || h <= 0) return;
    for (int row = y; row < y + h; row++) {
        uint16_t *p = fb + row * LCD_W + x;
        for (int i = 0; i < w; i++) {
            uint16_t c = sw(p[i]);
            int r5 = (c >> 11) + dr;          if (r5 > 31) r5 = 31;
            int g6 = ((c >> 5) & 63) + dg;    if (g6 > 63) g6 = 63;
            int b5 = (c & 31) + db;           if (b5 > 31) b5 = 31;
            p[i] = sw((uint16_t)((r5 << 11) | (g6 << 5) | b5));
        }
    }
}

static void draw_char(int x, int y, char ch, int scale, uint16_t color)
{
    if (x >= LCD_W || x + 6 * scale < 0) return;
    const uint8_t *g = glyph_for(ch);
    for (int col = 0; col < 5; col++) {
        uint8_t bits = g[col];
        int row = 0;
        while (row < 7) {
            if ((bits >> row) & 1) {
                int start = row;
                while (row < 7 && ((bits >> row) & 1)) row++;
                fb_rect(x + col * scale, y + start * scale, scale, (row - start) * scale, color);
            } else {
                row++;
            }
        }
    }
}

static void draw_text(int x, int y, const char *s, int scale, uint16_t color)
{
    for (; *s; s++, x += 6 * scale) draw_char(x, y, *s, scale, color);
}

static int text_w(const char *s, int scale) { return (int)strlen(s) * 6 * scale; }

// Chromatic-aberration neon text: pink fringe right, cyan fringe left, core on top
static void draw_text_neon(int x, int y, const char *s, int scale, uint16_t core, int off)
{
    if (off > 0) {
        draw_text(x + off, y, s, scale, C_PINK);
        draw_text(x - off, y, s, scale, C_CYAN);
    }
    draw_text(x, y, s, scale, core);
}

// ---------- Post-process glitch effects ----------
static void fx_row_shift(int y, int h, int dx)
{
    int s = ((dx % LCD_W) + LCD_W) % LCD_W;
    for (int r = y; r < y + h && r < LCD_H; r++) {
        if (r < 0) continue;
        uint16_t *row = fb + r * LCD_W;
        memcpy(tmp_row, row, sizeof(tmp_row));
        memcpy(row + s, tmp_row, (LCD_W - s) * 2);
        memcpy(row, tmp_row + LCD_W - s, s * 2);
    }
}

static void fx_rgb_split(int y, int h, int d)
{
    for (int r = y; r < y + h && r < LCD_H; r++) {
        if (r < 0) continue;
        uint16_t *row = fb + r * LCD_W;
        memcpy(tmp_row, row, sizeof(tmp_row));
        for (int x = 0; x < LCD_W; x++) {
            int xr = x + d; if (xr >= LCD_W) xr = LCD_W - 1;
            int xb = x - d; if (xb < 0) xb = 0;
            uint16_t out = (sw(tmp_row[xr]) & 0xF800) |
                           (sw(tmp_row[x])  & 0x07E0) |
                           (sw(tmp_row[xb]) & 0x001F);
            row[x] = sw(out);
        }
    }
}

static void fx_invert(int x, int y, int w, int h)
{
    if (x + w > LCD_W) w = LCD_W - x;
    if (y + h > LCD_H) h = LCD_H - y;
    for (int r = y; r < y + h; r++)
        for (int i = 0; i < w; i++) fb[r * LCD_W + x + i] ^= 0xFFFF;
}

static void fx_bands(int n, int max_shift)
{
    for (int i = 0; i < n; i++) {
        int h = rnd_range(3, 22);
        fx_row_shift(rnd_range(0, LCD_H - h), h, rnd_range(-max_shift, max_shift));
    }
}

// CRT scanlines: darken every other row by 25%
static void fx_crt(void)
{
    for (int y = 1; y < LCD_H; y += 2) {
        uint16_t *p = fb + y * LCD_W;
        for (int x = 0; x < LCD_W; x++) {
            uint16_t c = sw(p[x]);
            c -= (c >> 2) & 0x18E3;
            p[x] = sw(c);
        }
    }
}

// ---------- Effect state (timing from the reference project) ----------
static bool     glitch_on = false;
static uint32_t glitch_t0 = 0, glitch_next = 5000;
static int      glitch_dx = 0;

static bool     noise_on = true;
static uint32_t noise_t0 = 0, noise_next = 3500, noise_dur = 150;

static bool     scan_paused = false;
static uint32_t scan_t0 = 0;

static void update_fx_state(uint32_t now)
{
    // Glitch every 2-3.5 s, lasts 180 ms
    if (!glitch_on && now >= glitch_next) {
        glitch_on = true;
        glitch_t0 = now;
        glitch_dx = rnd_range(-9, 9);
        glitch_next = now + (uint32_t)rnd_range(1000, 5000);
    } else if (glitch_on && now - glitch_t0 > 180) {
        glitch_on = false;
    }
    // Static noise burst every 4-7 s, lasts 150-250 ms
    if (!noise_on && now >= noise_next) {
        noise_on = true;
        noise_t0 = now;
        noise_dur = (uint32_t)rnd_range(150, 250);
        noise_next = now + (uint32_t)rnd_range(7000, 14000);
    } else if (noise_on && now - noise_t0 > noise_dur) {
        noise_on = false;
    }
}

static void apply_fx(void)
{
    if (glitch_on) {
        fx_bands(3, 32);
        fx_rgb_split(rnd_range(0, LCD_H - 40), rnd_range(12, 40), rnd_range(3, 8));
        if (chance(30))
            fx_invert(rnd_range(0, 100), rnd_range(0, LCD_H - 12), rnd_range(40, 140), rnd_range(2, 10));
    }
    if (noise_on) fx_bands(2, 18);
    if (chance(4)) fx_bands(1, 8);                                            // tiny random tear
    if (chance(2)) fx_rgb_split(rnd_range(0, LCD_H - 20), rnd_range(4, 16), rnd_range(2, 4));
}

// TV static
static void draw_noise(void)
{
    static const uint16_t pal[4] = { C_WHITE, C_CYAN, C_PINK, C_DIM };
    for (int i = 0; i < 3500; i++)
        fb_rect(rnd_range(0, LCD_W - 3), rnd_range(0, LCD_H - 1), rnd_range(1, 3), 1, pal[rnd() & 3]);
    for (int i = 0; i < 6; i++)
        fb_rect(0, rnd_range(0, LCD_H - 1), LCD_W, 1, C_DIM);
}

// Slightly diagonal glowing scanline sweeping down, then a 1 s pause
static void draw_scanline(uint32_t now)
{
    if (scan_paused) {
        if (now - scan_t0 < 1000) return;
        scan_paused = false;
        scan_t0 = now;
    }
    int pos = (int)((now - scan_t0) * 260 / 1000) - 10;
    if (pos > LCD_H + 12) {
        scan_paused = true;
        scan_t0 = now;
        return;
    }
    for (int x = 0; x < LCD_W; x += 4) {
        int y = pos + x / 40;
        // fb_add(x, y, 4, 2, 2, 30, 28);       // bright core
        // fb_add(x, y - 8, 4, 8, 0, 6, 6);     // soft trail
        fb_add(x, y, 4, 2, 1, 12, 11);       // dimmer core
        fb_add(x, y - 8, 4, 8, 0, 2, 2);     // fainter trail
    }
}

// ---------- Scene ----------
static const char *terminal_lines[] = {
    "SYS.INIT...", "LOAD KERN 0X4F2A", "MEM CHECK OK", "GPIO SCAN...",
    "section .text",
    "    global _start",
    "",
    "_start:",
    " mov  eax, 0x1F4",
    " mov  ebx, 0x42",
    " add  eax, ebx",
    " push rax",
    " pop  rcx",
    " cmp  rcx, 0x200",
    " jge  .label_greater",
    " xor  rdx, rdx",
    " jmp  .label_end",
    "",
    ".label_greater:",
    " mov rdx, 0x1",
    " shl rdx, 0x3",
    " sub rax, rdx",
    "",
    ".label_end:",
    " nop",
    " ret"
    "CORE TEMP 42C", "VBUS 5.02V", "3V3 RAIL OK", "FLASH CRC OK",
    "NEURAL LINK UP"
};
#define N_TERM ((int)(sizeof(terminal_lines) / sizeof(terminal_lines[0])))

static void draw_corners(void)
{
    const int L = 16, T = 2, M = 2;
    fb_rect(M, M, L, T, C_PINK);                       fb_rect(M, M, T, L, C_PINK);
    fb_rect(LCD_W - M - L, M, L, T, C_PINK);          fb_rect(LCD_W - M - T, M, T, L, C_PINK);
    fb_rect(M, LCD_H - M - T, L, T, C_PINK);          fb_rect(M, LCD_H - M - L, T, L, C_PINK);
    fb_rect(LCD_W - M - L, LCD_H - M - T, L, T, C_PINK);
    fb_rect(LCD_W - M - T, LCD_H - M - L, T, L, C_PINK);
}

// Cyber heartbeat: brackets + pulsing core (cycle ~720 ms)
static void draw_heartbeat(uint32_t now)
{
    const int cx = 34, cy = 114-50;
    fb_rect(10, 90-50, 3, 48, C_CYAN); fb_rect(10, 90-50, 10, 3, C_CYAN); fb_rect(10, 135-50, 10, 3, C_CYAN);
    fb_rect(55, 90-50, 3, 48, C_CYAN); fb_rect(48, 90-50, 10, 3, C_CYAN); fb_rect(48, 135-50, 10, 3, C_CYAN);

    int pulse = (int)((now / 30) % 24);
    if (pulse < 3) {                 // beat 1: big box + crosshair
        fb_rect(cx - 9, cy - 9, 18, 18, C_PINK);
        fb_rect(cx - 14, cy - 1, 29, 2, C_PINK);
        fb_rect(cx - 1, cy - 14, 2, 29, C_PINK);
    } else if (pulse < 6) {          // recoil
        fb_rect(cx - 3, cy - 3, 6, 6, C_PINK);
    } else if (pulse < 11) {         // beat 2: medium box + ring
        fb_rect(cx - 6, cy - 6, 12, 12, C_PINK);
        fb_frame(cx - 13, cy - 13, 26, 26, 2, C_CYAN);
    } else {                         // rest
        fb_rect(cx - 3, cy - 3, 6, 6, C_PINK);
    }
}

static void draw_terminal(uint32_t now)
{
    const int LH = 16, TOP = 164-50, BOT = 234;
    clip_y0 = TOP; clip_y1 = BOT;

    int scroll = (int)(((uint64_t)now * 45 / 1000) % (uint32_t)(N_TERM * LH));
    int base = scroll / LH, off = scroll % LH;

    for (int i = 0; i < 6; i++) {
        int idx = (base + i) % N_TERM;
        int y = TOP + i * LH - off;

        int frac = (y + LH / 2 - TOP) * 255 / (BOT - TOP);       // newest (bottom) = brightest
        if (frac < 0) frac = 0;
        if (frac > 255) frac = 255;
        int g = 60 + frac * 195 / 255;
        uint16_t col = RGB565(0, g, g / 3);

        int xg = chance(10) ? rnd_range(-6, 6) : 0;               // line jitter
        draw_text(10 + xg, y, terminal_lines[idx], 2, col);

        if (chance(5)) {                                          // corruption blocks
            for (int j = 0; j < 3; j++)
                fb_rect(rnd_range(10, 230), y + rnd_range(0, 13), 4, 3, chance(50) ? C_PINK : C_CYAN);
        }
    }
    clip_y0 = 0; clip_y1 = LCD_H;

    // prompt + blinking cursor
    draw_text(10, 238, ">", 2, C_GREEN);
    if ((now / 500) % 2 == 0) fb_rect(28, 238, 12, 14, C_GREEN);
    draw_text(48, 242, "AWAITING INPUT", 1, C_DIM);
}

static void draw_scene(uint32_t now)
{
    char buf[24];
    bg_fill();
    draw_corners();

    int jx  = glitch_on ? glitch_dx : 0;
    int jy  = glitch_on ? rnd_range(-2, 2) : 0;
    int cab = glitch_on ? rnd_range(3, 8) : 0;

    // Header
    draw_text_neon(10 + jx, 10 + jy, "CYBR.NET", 2, C_CYAN, cab);
    draw_text_neon(LCD_W - 10 - text_w("V2.077", 2) - jx, 10, "V2.077", 2, C_PINK, 0);
    fb_rect(10, 28, LCD_W - 20, 2, C_CYAN);
    if (glitch_on) fb_rect(rnd_range(10, 120), 31, rnd_range(30, 110), 2, C_PINK);

    // Scrolling neon title
    // const char *title = "HELLO ESP32-C3";
    // int tw = text_w(title, 4);
    // int period = tw + 120;
    // int scroll = (int)(((uint64_t)now * 90 / 1000) % (uint32_t)period);
    // int tcab = glitch_on ? rnd_range(4, 9) : 2;
    // for (int k = 0; k < 2; k++) {
    //     int x = -scroll + k * period;
    //     if (x >= LCD_W || x + tw < 0) continue;
    //     draw_text_neon(x, 40 + jy, title, 4, C_WHITE, tcab);
    // }
    // fb_rect(10, 76, LCD_W - 20, 1, C_PINK);
    // for (int x = 10; x < LCD_W - 10; x += 20) fb_rect(x, 77, 1, 4, C_PINK);   // ruler ticks

    // Status block
    draw_heartbeat(now);
    draw_text(70 + jx, 42, "PWR ONLINE", 2, C_GREEN);
    if ((now / 150) % 2 == 0) fb_rect(198-50, 96, 6, 6, C_GREEN);
    uint32_t s = now / 1000;
    snprintf(buf, sizeof(buf), "T+%02u:%02u:%02u",
             (unsigned)(s / 3600), (unsigned)((s / 60) % 60), (unsigned)(s % 60));
    draw_text(70, 114-50, buf, 2, C_ORANGE);
    draw_text(70, 136-50, "NODE 0X3F2A  LINK 98%", 1, C_DIM);

    // Terminal
    fb_rect(10, 148-50, LCD_W - 20, 1, C_CYAN);
    draw_text(10, 153-50, "DIAG.MODE", 1, C_DIM);
    draw_terminal(now);

    // Footer
    fb_rect(10, 256, LCD_W - 20, 2, C_CYAN);
    draw_text(10, 262, "SYS.OK", 2, C_GREEN);
    if ((now / 500) % 2 == 0) {
        snprintf(buf, sizeof(buf), "0X%02X", (unsigned)((now / 100) & 0xFF));
        draw_text(LCD_W - 10 - text_w(buf, 2), 262, buf, 2, C_ORANGE);
    }
}

// ---------- Power-up animation ----------
#define BOOT_MS 1100

static void draw_boot(uint32_t t)
{
    int p = (int)(t * 1000 / BOOT_MS);
    if (p > 1000) p = 1000;
    bg_fill();

    int cx = LCD_W / 2, cy = LCD_H / 2;
    int w = LCD_W * p / 1000, h = LCD_H * p / 1000;
    fb_frame(cx - w / 2, cy - h / 2, w, h, 2, C_CYAN);

    if (p > 300) {                                       // radiating lines
        int len = 110 * (p - 300) / 700;
        float base = p * 0.00628f;
        for (int i = 0; i < 8; i++) {
            float a = base + i * 0.7854f;
            fb_line(cx + (int)(cosf(a) * 16), cy + (int)(sinf(a) * 16),
                    cx + (int)(cosf(a) * (16 + len)), cy + (int)(sinf(a) * (16 + len)),
                    2, (i & 1) ? C_PINK : C_CYAN);
        }
    }
    if (p > 700 && ((t / 60) % 3) != 0) {                // flickering ONLINE
        const char *s = "ONLINE";
        fb_rect(cx - 80, cy - 22, 160, 44, C_BG);
        draw_text((LCD_W - text_w(s, 4)) / 2, cy - 14, s, 4, C_WHITE);
    }
}

// ---------- App ----------
void app_main(void)
{
    rng_state = esp_random() | 1;
    font_init();
    lcd_init();

    fb = heap_caps_malloc(FB_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!fb) {
        ESP_LOGE(TAG, "framebuffer alloc failed (%d bytes)", FB_BYTES);
        return;
    }
    ESP_LOGI(TAG, "LCD ready, framebuffer %d bytes", FB_BYTES);

    // Power-up animation
    uint32_t t0 = millis();
    for (;;) {
        uint32_t t = millis() - t0;
        if (t >= BOOT_MS) break;
        draw_boot(t);
        if (chance(35)) fx_bands(2, 24);
        if (chance(25)) fx_rgb_split(rnd_range(0, LCD_H - 30), rnd_range(8, 30), rnd_range(3, 7));
        fx_crt();
        lcd_present();
    }

    uint32_t frames = 0, fps_t0 = millis();
    scan_t0 = fps_t0;
    while (1) {
        uint32_t now = millis();
        update_fx_state(now);
        draw_scene(now);
        draw_scanline(now);
        if (noise_on) draw_noise();
        apply_fx();
        fx_crt();
        lcd_present();      // blocks on DMA, so the idle task / watchdog still run

        frames++;
        if (now - fps_t0 >= 5000) {
            ESP_LOGI(TAG, "%u fps", (unsigned)(frames * 1000 / (now - fps_t0)));
            frames = 0;
            fps_t0 = now;
        }
    }
}