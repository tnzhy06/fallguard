#include "bsp_lcd.h"
#include "app_config.h"
#include "harmonyos_sans.h"
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

static const ui_font_bitmap_t *find_glyph(uint32_t code, uint8_t size)
{
    const app_ui_glyph_t *table;
    unsigned count;
    if (size == 14U) {
        table = ui_glyphs14;
        count = sizeof(ui_glyphs14) / sizeof(ui_glyphs14[0]);
    } else if (size == 16U) {
        table = ui_glyphs16;
        count = sizeof(ui_glyphs16) / sizeof(ui_glyphs16[0]);
    } else {
        table = ui_glyphs20;
        count = sizeof(ui_glyphs20) / sizeof(ui_glyphs20[0]);
    }
    unsigned lo = 0U, hi = count;
    while (lo < hi)
    {
        unsigned mid = lo + (hi - lo) / 2U;
        if (table[mid].code < code) lo = mid + 1U;
        else hi = mid;
    }
    return (lo < count && table[lo].code == code) ? &table[lo].bitmap : 0;
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

uint8_t BSP_LCD_TextHeight(uint8_t size)
{
    switch (size)
    {
    case 14U: return UI_FONT14_HEIGHT;
    case 16U: return UI_FONT16_HEIGHT;
    case 20U: return UI_FONT20_HEIGHT;
    case 40U: return UI_FONT40_HEIGHT;
    default: return 0U;
    }
}

static const ui_font_bitmap_t *glyph_bitmap(uint32_t code, uint8_t size, int *offset_y)
{
    *offset_y = 0;
    if (size == 40U)
    {
        for (unsigned i = 0U; i < sizeof(ui_large_glyphs) / sizeof(ui_large_glyphs[0]); ++i)
            if (ui_large_glyphs[i].code == code) return &ui_large_glyphs[i].bitmap;
        *offset_y = (UI_FONT40_HEIGHT - UI_FONT20_HEIGHT) / 2;
    }
    const ui_font_bitmap_t *glyph = find_glyph(code, size);
    if (!glyph) {
        glyph = find_glyph('?', size);
    }
    return glyph;
}

static int span_width(const char *text, const char *end, uint8_t size)
{
    int width = 0, offset_y;
    while (*text && (!end || text < end))
        width += glyph_bitmap(next_code(&text), size, &offset_y)->advance;
    return width;
}

static void draw_span(int x, int y, const char *text, const char *end,
                       uint8_t size, uint8_t scale, uint16_t color, uint16_t background)
{
    int width = span_width(text, end, size) * scale;
    int height = BSP_LCD_TextHeight(size) * scale;
    BSP_LCD_Rect(x, y, width, height, background);
    while (*text && (!end || text < end))
    {
        int offset_y;
        const ui_font_bitmap_t *glyph = glyph_bitmap(next_code(&text), size, &offset_y);
        if (glyph->width && glyph->height)
            draw_mask(x + glyph->x * scale, y + (glyph->y + offset_y) * scale,
                       ui_font_pixels + glyph->offset, glyph->width, glyph->height,
                       glyph->width, scale, color, background, 1U);
        x += glyph->advance * scale;
    }
}

void BSP_LCD_Text(int x, int y, const char *text, uint8_t size, uint8_t scale,
                  uint16_t color, uint16_t background)
{
    if (text && scale && BSP_LCD_TextHeight(size))
        draw_span(x, y, text, NULL, size, scale, color, background);
}

int BSP_LCD_TextWidth(const char *text, uint8_t size, uint8_t scale)
{
    return text && BSP_LCD_TextHeight(size) ? span_width(text, NULL, size) * scale : 0;
}

static const char *page_end(const char *text, int width, uint8_t size)
{
    int used = 0, offset_y;
    const char *end = text;
    while (*end)
    {
        const char *next = end;
        int advance = glyph_bitmap(next_code(&next), size, &offset_y)->advance;
        if (used + advance > width) break;
        used += advance;
        end = next;
    }
    return end;
}

uint16_t BSP_LCD_TextPage(int x, int y, int width, const char *text, uint8_t size,
                           uint32_t page, uint16_t color, uint16_t background)
{
    if (!text || width <= 0 || !BSP_LCD_TextHeight(size)) return 0U;
    uint16_t pages = 0U;
    const char *start = text;
    while (*start)
    {
        const char *end = page_end(start, width, size);
        if (end == start) return 0U;
        ++pages;
        start = end;
    }
    if (!pages) return 0U;
    page %= pages;
    start = text;
    while (page--) start = page_end(start, width, size);
    draw_span(x, y, start, page_end(start, width, size), size, 1U, color, background);
    return pages;
}

void BSP_LCD_Clock(int x, int y, const char *text, uint16_t color, uint16_t background)
{
    static const char clock_chars[] = UI_CLOCK_CHARS;
    if (!text) return;
    BSP_LCD_Rect(x, y, (int)strlen(text) * UI_CLOCK_WIDTH, UI_CLOCK_HEIGHT, background);
    while (text && *text && x < (int)ST7789_WIDTH)
    {
        const char *match = strchr(clock_chars, *text++);
        unsigned index = match ? (unsigned)(match - clock_chars) : sizeof(clock_chars) - 2U;
        const ui_font_bitmap_t *glyph = &ui_clock_glyphs[index];
        draw_mask(x + glyph->x, y + glyph->y, ui_font_pixels + glyph->offset,
                  glyph->width, glyph->height, glyph->width, 1U, color, background, 1U);
        x += UI_CLOCK_WIDTH;
    }
}

void BSP_LCD_Icon(int x, int y, const uint8_t *mask, uint8_t size, uint16_t color, uint16_t background)
{
    if (mask && size)
    {
        draw_mask(x, y, mask, size, size, size, 1U, color, background, 1U);
    }
}
