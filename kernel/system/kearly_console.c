#include "system/kearly_console.h"
#include "terminal/terminal_api.h"
#include "kwrappers/kgfx.h"
#include "kwrappers/colors.h"

#define KEARLY_MAX_ROWS 64u
#define KEARLY_MAX_COLS 160u
#define KEARLY_GLYPH_SCALE 1u
#define KEARLY_CELL_W 7u
#define KEARLY_LINE_H 10u
#define KEARLY_COLUMN_COUNT 3u
#define KEARLY_COLUMN_GAP 12u

typedef struct
{
    const kfont *font;
    int x;
    int y;
    int start_x;
    int start_y;
    uint32_t line_h;
    uint32_t max_x;
    uint32_t max_y;
    uint32_t cols;
    uint32_t rows;
    uint32_t column_width;
    uint32_t cursor_column;
    uint32_t cursor_col;
    uint32_t cursor_row;
    char text[KEARLY_MAX_ROWS][KEARLY_MAX_COLS];
} kearly_console_state;

static kearly_console_state G_early_console;

static void kearly_put_px(int x, int y, uint32_t px)
{
    const kfb *fb = kgfx_info();
    volatile uint32_t *dst = 0;

    if (!fb || !fb->base || x < 0 || y < 0 ||
        x >= (int)fb->width || y >= (int)fb->height)
        return;

    dst = (volatile uint32_t *)(fb->base + (uint32_t)y * fb->pitch + (uint32_t)x * 4u);
    *dst = px;
}

static uint8_t kearly_glyph_row(char ch, uint32_t row)
{
    static const uint8_t blank[7] = {0, 0, 0, 0, 0, 0, 0};
    static const uint8_t unknown[7] = {0x1Eu, 0x11u, 0x01u, 0x06u, 0x04u, 0x00u, 0x04u};
    const uint8_t *g = unknown;

    if (ch >= 'a' && ch <= 'z')
        ch = (char)(ch - 'a' + 'A');

    switch (ch)
    {
    case ' ': g = blank; break;
    case 'A': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u}; g = v; break; }
    case 'B': { static const uint8_t v[7] = {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x11u, 0x11u, 0x1Eu}; g = v; break; }
    case 'C': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x10u, 0x10u, 0x10u, 0x11u, 0x0Eu}; g = v; break; }
    case 'D': { static const uint8_t v[7] = {0x1Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x1Eu}; g = v; break; }
    case 'E': { static const uint8_t v[7] = {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x1Fu}; g = v; break; }
    case 'F': { static const uint8_t v[7] = {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x10u, 0x10u, 0x10u}; g = v; break; }
    case 'G': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x10u, 0x17u, 0x11u, 0x11u, 0x0Fu}; g = v; break; }
    case 'H': { static const uint8_t v[7] = {0x11u, 0x11u, 0x11u, 0x1Fu, 0x11u, 0x11u, 0x11u}; g = v; break; }
    case 'I': { static const uint8_t v[7] = {0x0Eu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu}; g = v; break; }
    case 'J': { static const uint8_t v[7] = {0x01u, 0x01u, 0x01u, 0x01u, 0x11u, 0x11u, 0x0Eu}; g = v; break; }
    case 'K': { static const uint8_t v[7] = {0x11u, 0x12u, 0x14u, 0x18u, 0x14u, 0x12u, 0x11u}; g = v; break; }
    case 'L': { static const uint8_t v[7] = {0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x10u, 0x1Fu}; g = v; break; }
    case 'M': { static const uint8_t v[7] = {0x11u, 0x1Bu, 0x15u, 0x15u, 0x11u, 0x11u, 0x11u}; g = v; break; }
    case 'N': { static const uint8_t v[7] = {0x11u, 0x19u, 0x15u, 0x13u, 0x11u, 0x11u, 0x11u}; g = v; break; }
    case 'O': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu}; g = v; break; }
    case 'P': { static const uint8_t v[7] = {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x10u, 0x10u, 0x10u}; g = v; break; }
    case 'Q': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x11u, 0x11u, 0x15u, 0x12u, 0x0Du}; g = v; break; }
    case 'R': { static const uint8_t v[7] = {0x1Eu, 0x11u, 0x11u, 0x1Eu, 0x14u, 0x12u, 0x11u}; g = v; break; }
    case 'S': { static const uint8_t v[7] = {0x0Fu, 0x10u, 0x10u, 0x0Eu, 0x01u, 0x01u, 0x1Eu}; g = v; break; }
    case 'T': { static const uint8_t v[7] = {0x1Fu, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u, 0x04u}; g = v; break; }
    case 'U': { static const uint8_t v[7] = {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Eu}; g = v; break; }
    case 'V': { static const uint8_t v[7] = {0x11u, 0x11u, 0x11u, 0x11u, 0x11u, 0x0Au, 0x04u}; g = v; break; }
    case 'W': { static const uint8_t v[7] = {0x11u, 0x11u, 0x11u, 0x15u, 0x15u, 0x15u, 0x0Au}; g = v; break; }
    case 'X': { static const uint8_t v[7] = {0x11u, 0x11u, 0x0Au, 0x04u, 0x0Au, 0x11u, 0x11u}; g = v; break; }
    case 'Y': { static const uint8_t v[7] = {0x11u, 0x11u, 0x0Au, 0x04u, 0x04u, 0x04u, 0x04u}; g = v; break; }
    case 'Z': { static const uint8_t v[7] = {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x1Fu}; g = v; break; }
    case '0': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x13u, 0x15u, 0x19u, 0x11u, 0x0Eu}; g = v; break; }
    case '1': { static const uint8_t v[7] = {0x04u, 0x0Cu, 0x04u, 0x04u, 0x04u, 0x04u, 0x0Eu}; g = v; break; }
    case '2': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x01u, 0x02u, 0x04u, 0x08u, 0x1Fu}; g = v; break; }
    case '3': { static const uint8_t v[7] = {0x1Eu, 0x01u, 0x01u, 0x0Eu, 0x01u, 0x01u, 0x1Eu}; g = v; break; }
    case '4': { static const uint8_t v[7] = {0x02u, 0x06u, 0x0Au, 0x12u, 0x1Fu, 0x02u, 0x02u}; g = v; break; }
    case '5': { static const uint8_t v[7] = {0x1Fu, 0x10u, 0x10u, 0x1Eu, 0x01u, 0x01u, 0x1Eu}; g = v; break; }
    case '6': { static const uint8_t v[7] = {0x0Eu, 0x10u, 0x10u, 0x1Eu, 0x11u, 0x11u, 0x0Eu}; g = v; break; }
    case '7': { static const uint8_t v[7] = {0x1Fu, 0x01u, 0x02u, 0x04u, 0x08u, 0x08u, 0x08u}; g = v; break; }
    case '8': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x11u, 0x0Eu, 0x11u, 0x11u, 0x0Eu}; g = v; break; }
    case '9': { static const uint8_t v[7] = {0x0Eu, 0x11u, 0x11u, 0x0Fu, 0x01u, 0x01u, 0x0Eu}; g = v; break; }
    case ':': { static const uint8_t v[7] = {0x00u, 0x04u, 0x04u, 0x00u, 0x04u, 0x04u, 0x00u}; g = v; break; }
    case '-': { static const uint8_t v[7] = {0x00u, 0x00u, 0x00u, 0x1Fu, 0x00u, 0x00u, 0x00u}; g = v; break; }
    case '/': { static const uint8_t v[7] = {0x01u, 0x01u, 0x02u, 0x04u, 0x08u, 0x10u, 0x10u}; g = v; break; }
    case '.': { static const uint8_t v[7] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u, 0x0Cu, 0x0Cu}; g = v; break; }
    }

    return row < 7u ? g[row] : 0;
}

static void kearly_draw_builtin(char ch, int x, int y, uint32_t scale)
{
    uint32_t px = 0x00FFFFFFu;

    for (uint32_t row = 0; row < 7u; ++row)
    {
        uint8_t bits = kearly_glyph_row(ch, row);
        for (uint32_t col = 0; col < 5u; ++col)
        {
            if (!(bits & (uint8_t)(1u << (4u - col))))
                continue;
            for (uint32_t sy = 0; sy < scale; ++sy)
                for (uint32_t sx = 0; sx < scale; ++sx)
                    kearly_put_px(x + (int)(col * scale + sx),
                                  y + (int)(row * scale + sy),
                                  px);
        }
    }
}

static void kearly_console_reset_region(void)
{
    const kfb *fb = kgfx_info();
    if (!fb || !fb->width || !fb->height)
        return;

    kgfx_rect(0, (uint32_t)G_early_console.start_y,
              fb->width,
              fb->height - (uint32_t)G_early_console.start_y,
              black);
}

static int kearly_console_column_x(void)
{
    return G_early_console.start_x +
           (int)(G_early_console.cursor_column *
                 (G_early_console.column_width + KEARLY_COLUMN_GAP));
}

static void kearly_console_advance_column(void)
{
    ++G_early_console.cursor_column;
    if (G_early_console.cursor_column >= KEARLY_COLUMN_COUNT)
    {
        G_early_console.cursor_column = 0u;
        kearly_console_reset_region();
    }
    G_early_console.cursor_row = 0u;
    G_early_console.cursor_col = 0u;
    G_early_console.x = kearly_console_column_x();
    G_early_console.y = G_early_console.start_y;
}

static void kearly_console_newline(void)
{
    G_early_console.cursor_col = 0;
    if (G_early_console.cursor_row + 1u >= G_early_console.rows)
    {
        kearly_console_advance_column();
        return;
    }
    else
        ++G_early_console.cursor_row;

    G_early_console.x = kearly_console_column_x();
    G_early_console.y = G_early_console.start_y + (int)(G_early_console.cursor_row * G_early_console.line_h);
}

static void kearly_console_sink(const char *text, uint32_t len, void *user)
{
    (void)user;

    if (!text)
        return;

    for (uint32_t i = 0; i < len; ++i)
    {
        char ch = text[i];
        if (ch == '\r')
            continue;
        if (ch == '\n')
        {
            kearly_console_newline();
            continue;
        }

        if (G_early_console.cursor_col + 1u >= G_early_console.cols)
            kearly_console_newline();

        G_early_console.text[G_early_console.cursor_row][G_early_console.cursor_col] = ch;

        if (ch != ' ')
            kearly_draw_builtin(ch, G_early_console.x, G_early_console.y, KEARLY_GLYPH_SCALE);
        ++G_early_console.cursor_col;
        if (G_early_console.cursor_col < G_early_console.cols)
            G_early_console.text[G_early_console.cursor_row][G_early_console.cursor_col] = 0;
        G_early_console.x += (int)KEARLY_CELL_W;
    }

    kgfx_flush();
}

void kearly_console_begin(const kfont *font)
{
    const kfb *fb = kgfx_info();
    if (!fb || !fb->width || !fb->height)
        return;

    G_early_console.font = font;
    G_early_console.start_x = 8;
    G_early_console.start_y = 16;
    G_early_console.x = G_early_console.start_x;
    G_early_console.y = G_early_console.start_y;
    G_early_console.line_h = KEARLY_LINE_H;
    G_early_console.max_x = fb->width > 8u ? fb->width - 8u : fb->width;
    G_early_console.max_y = fb->height > 8u ? fb->height - 8u : fb->height;
    G_early_console.column_width =
        (G_early_console.max_x > (uint32_t)G_early_console.start_x +
                                     (KEARLY_COLUMN_COUNT - 1u) * KEARLY_COLUMN_GAP)
            ? ((G_early_console.max_x -
                (uint32_t)G_early_console.start_x -
                (KEARLY_COLUMN_COUNT - 1u) * KEARLY_COLUMN_GAP) /
               KEARLY_COLUMN_COUNT)
            : KEARLY_CELL_W;
    G_early_console.cols = G_early_console.column_width / KEARLY_CELL_W;
    G_early_console.rows = (G_early_console.max_y > (uint32_t)G_early_console.start_y)
                               ? ((G_early_console.max_y - (uint32_t)G_early_console.start_y) / G_early_console.line_h)
                               : 1u;
    if (G_early_console.cols == 0)
        G_early_console.cols = 1u;
    if (G_early_console.rows == 0)
        G_early_console.rows = 1u;
    if (G_early_console.cols > KEARLY_MAX_COLS)
        G_early_console.cols = KEARLY_MAX_COLS;
    if (G_early_console.cols > 1u)
        --G_early_console.cols;
    if (G_early_console.rows > KEARLY_MAX_ROWS)
        G_early_console.rows = KEARLY_MAX_ROWS;
    G_early_console.cursor_column = 0u;
    G_early_console.cursor_col = 0;
    G_early_console.cursor_row = 0;
    for (uint32_t row = 0; row < KEARLY_MAX_ROWS; ++row)
        for (uint32_t col = 0; col < KEARLY_MAX_COLS; ++col)
            G_early_console.text[row][col] = 0;

    terminal_capture_begin(0u, kearly_console_sink, 0);
}

void kearly_console_end(void)
{
    terminal_capture_end();
    G_early_console.font = 0;
}

void kearly_console_write(const char *text)
{
    const char *p = text;
    uint32_t len = 0;

    if (!p)
        return;

    while (p[len])
        ++len;
    kearly_console_sink(text, len, 0);
    kearly_console_sink("\n", 1, 0);
}
