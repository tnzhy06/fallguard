#include "bsp_lcd.h"
#include "app_config.h"
#include "noto_sans_sc.h"
#include <string.h>
#include <stdlib.h>

static uint8_t band[ST7789_WIDTH * LCD_BAND_ROWS * 2U];
static int band_y;

static void pixel(int x, int y, uint16_t color)
{
    if ((x < 0) || (x >= (int)ST7789_WIDTH) || (y < band_y) ||
        (y >= band_y + (int)LCD_BAND_ROWS) || (y >= (int)ST7789_HEIGHT)) return;
    unsigned i = ((unsigned)(y - band_y) * ST7789_WIDTH + (unsigned)x) * 2U;
    band[i] = (uint8_t)(color >> 8);
    band[i + 1U] = (uint8_t)color;
}

void BSP_LCD_Rect(int x, int y, int w, int h, uint16_t color)
{
    int right = x + w, bottom = y + h;
    if (x < 0) x = 0;
    if (y < band_y) y = band_y;
    if (right > (int)ST7789_WIDTH) right = ST7789_WIDTH;
    if (bottom > band_y + (int)LCD_BAND_ROWS) bottom = band_y + LCD_BAND_ROWS;
    if (bottom > (int)ST7789_HEIGHT) bottom = ST7789_HEIGHT;
    if (x >= right) return;
    for (int row = y; row < bottom; ++row)
    {
        uint8_t *p = &band[((unsigned)(row - band_y) * ST7789_WIDTH + (unsigned)x) * 2U];
        for (int col = x; col < right; ++col)
        {
            *p++ = (uint8_t)(color >> 8);
            *p++ = (uint8_t)color;
        }
    }
}

void BSP_LCD_BeginBand(uint16_t y, uint16_t background)
{
    band_y = y;
    BSP_LCD_Rect(0, y, ST7789_WIDTH, LCD_BAND_ROWS, background);
}

const uint8_t *BSP_LCD_GetBand(void) { return band; }

void BSP_LCD_RoundRect(int x, int y, int w, int h, int r, uint16_t color)
{
    if ((r < 0) || (w <= 0) || (h <= 0)) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    int top = y > band_y ? y : band_y;
    int bottom = y + h < band_y + (int)LCD_BAND_ROWS ? y + h : band_y + (int)LCD_BAND_ROWS;
    for (int row = top; row < bottom; ++row)
    {
        int dy = row < y + r ? y + r - row - 1 : (row >= y + h - r ? row - (y + h - r) : 0);
        int inset = 0;
        while ((inset < r) && ((r - inset) * (r - inset) + dy * dy > r * r)) ++inset;
        BSP_LCD_Rect(x + inset, row, w - 2 * inset, 1, color);
    }
}

void BSP_LCD_Line(int x0, int y0, int x1, int y1, uint16_t color)
{
    if (((y0 < band_y) && (y1 < band_y)) ||
        ((y0 >= band_y + (int)LCD_BAND_ROWS) && (y1 >= band_y + (int)LCD_BAND_ROWS))) return;
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    for (;;)
    {
        pixel(x0, y0, color);
        if ((x0 == x1) && (y0 == y1)) break;
        int e = 2 * err;
        if (e >= dy) { err += dy; x0 += sx; }
        if (e <= dx) { err += dx; y0 += sy; }
    }
}

void BSP_LCD_Circle(int x, int y, int r, uint16_t color)
{
    for (int row = band_y; row < band_y + (int)LCD_BAND_ROWS; ++row)
    {
        int dy = row - y;
        if (abs(dy) > r) continue;
        int dx = r;
        while ((dx > 0) && (dx * dx + dy * dy > r * r)) --dx;
        BSP_LCD_Rect(x - dx, row, dx * 2 + 1, 1, color);
    }
}

/* Decode valid UTF-8; malformed bytes become '?' without reading past NUL. */
static uint32_t next_code(const char **text)
{
    const unsigned char *s = (const unsigned char *)*text;
    uint32_t code = *s++;
    if ((code >= 0xC2U) && (code <= 0xDFU) && s[0] && ((s[0] & 0xC0U) == 0x80U))
        code = ((code & 31U) << 6) | (*s++ & 63U);
    else if ((code >= 0xE0U) && (code <= 0xEFU) && s[0] && s[1] &&
             ((s[0] & 0xC0U) == 0x80U) && ((s[1] & 0xC0U) == 0x80U))
    {
        code = ((code & 15U) << 12) | ((s[0] & 63U) << 6) | (s[1] & 63U);
        s += 2;
    }
    else if (code >= 128U) code = '?';
    *text = (const char *)s;
    return code;
}

static const app_ui_glyph_t *find_glyph(uint32_t code)
{
    unsigned lo = 0U, hi = sizeof(ui_glyphs) / sizeof(ui_glyphs[0]);
    while (lo < hi)
    {
        unsigned mid = lo + (hi - lo) / 2U;
        if (ui_glyphs[mid].code < code) lo = mid + 1U;
        else hi = mid;
    }
    return (lo < sizeof(ui_glyphs) / sizeof(ui_glyphs[0]) && ui_glyphs[lo].code == code) ? &ui_glyphs[lo] : 0;
}

static uint16_t blend(uint16_t fg, uint16_t bg, unsigned a)
{
    return (uint16_t)((((((fg >> 11) & 31U) * a + ((bg >> 11) & 31U) * (3U - a)) / 3U) << 11) |
                      (((((fg >> 5) & 63U) * a + ((bg >> 5) & 63U) * (3U - a)) / 3U) << 5) |
                      (((fg & 31U) * a + (bg & 31U) * (3U - a)) / 3U));
}

static void draw_mask(int x, int y, const uint8_t *bits, int w, int h, int stride,
                      uint8_t scale, uint16_t color, uint16_t background, uint8_t transparent)
{
    if (y >= band_y + (int)LCD_BAND_ROWS || y + h * scale <= band_y) return;
    uint16_t palette[4] = {background, blend(color, background, 1U), blend(color, background, 2U), color};
    for (int row = 0; row < h; ++row)
    {
        if (y + (row + 1) * scale <= band_y || y + row * scale >= band_y + (int)LCD_BAND_ROWS) continue;
        for (int col = 0; col < w; ++col)
        {
            unsigned i = (unsigned)(row * stride + col);
            unsigned a = bits ? ((bits[i / 4U] >> (6U - 2U * (i % 4U))) & 3U) :
                         ((row == 1 || row == h - 2 || col == 1 || col == w - 2) ? 3U : 0U);
            if (!transparent || a)
                BSP_LCD_Rect(x + col * scale, y + row * scale, scale, scale, palette[a]);
        }
    }
}

static const app_ui_ascii_glyph_t *ascii_glyph(uint32_t code)
{
    if (code < 32U || code > 126U) code = '?';
    return &ui_ascii_glyphs[code - 32U];
}

void BSP_LCD_Text(int x, int y, const char *text, uint8_t size, uint8_t scale,
                  uint16_t color, uint16_t background)
{
    if (!text || !scale || (size != 16U && size != 20U && size != 40U)) return;
    while (*text && x < (int)ST7789_WIDTH)
    {
        uint32_t code = next_code(&text);
        int w = size, h = size, stride = size;
        const uint8_t *bits = 0;
        if (code <= 127U)
        {
            const app_ui_ascii_glyph_t *glyph = ascii_glyph(code);
            h = stride = size == 16U ? 16 : 20;
            w = size == 16U ? glyph->advance16 : glyph->advance20;
            bits = size == 16U ? glyph->px16 : glyph->px20;
        }
        else if (size == 40U)
        {
            for (unsigned i = 0; i < sizeof(ui_large_glyphs) / sizeof(ui_large_glyphs[0]); ++i)
                if (ui_large_glyphs[i].code == code) { bits = ui_large_glyphs[i].pixels; break; }
        }
        else
        {
            const app_ui_glyph_t *glyph = find_glyph(code);
            if (glyph) bits = size == 20U ? glyph->px20 : glyph->px16;
        }
        draw_mask(x, y + (size - h) * scale / 2, bits, w, h, stride, scale, color, background, 0U);
        x += w * scale;
    }
}

int BSP_LCD_TextWidth(const char *text, uint8_t size, uint8_t scale)
{
    int w = 0;
    if (!text || (size != 16U && size != 20U && size != 40U)) return 0;
    while (*text)
    {
        uint32_t code = next_code(&text);
        const app_ui_ascii_glyph_t *glyph = ascii_glyph(code);
        w += code > 127U ? size : (size == 16U ? glyph->advance16 : glyph->advance20);
    }
    return w * scale;
}

void BSP_LCD_Clock(int x, int y, const char *text, uint16_t color, uint16_t background)
{
    static const char clock_chars[] = UI_CLOCK_CHARS;
    while (text && *text && x < (int)ST7789_WIDTH)
    {
        const char *glyph = strchr(clock_chars, *text++);
        unsigned index = glyph ? (unsigned)(glyph - clock_chars) : sizeof(clock_chars) - 2U;
        draw_mask(x, y, ui_clock_glyphs[index], UI_CLOCK_WIDTH, UI_CLOCK_HEIGHT,
                  UI_CLOCK_WIDTH, 1U, color, background, 0U);
        x += UI_CLOCK_WIDTH;
    }
}

void BSP_LCD_Icon(int x, int y, const uint8_t *mask, uint8_t size, uint16_t color, uint16_t background)
{
    if (mask && size)
        draw_mask(x, y, mask, size, size, size, 1U, color, background, 1U);
}
