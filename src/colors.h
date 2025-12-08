#ifndef COLORS_H
#define COLORS_H

// Moonfly color scheme (from https://github.com/bluz71/vim-moonfly-colors)
// These will be used for syntax highlighting later

// Background - pure black for cleaner look
#define MOONFLY_BG         0x000000
#define MOONFLY_BG_ALT     0x0a0a0a

// Foreground
#define MOONFLY_FG         0xb2b2b2
#define MOONFLY_FG_ALT     0x9e9e9e

// Cursor & Selection
#define MOONFLY_CURSOR     0x9e9e9e
#define MOONFLY_CURSOR_BG  0x080808
#define MOONFLY_SELECTION  0xb2ceee
#define MOONFLY_SELECTION_BG 0x080808

// ANSI Colors (normal)
#define MOONFLY_BLACK      0x323437
#define MOONFLY_RED        0xff5d5d
#define MOONFLY_GREEN      0x8cc85f
#define MOONFLY_YELLOW     0xe3c78a
#define MOONFLY_BLUE       0x80a0ff
#define MOONFLY_PURPLE     0xcf87e8
#define MOONFLY_CYAN       0x79dac8
#define MOONFLY_WHITE      0xc6c6c6

// ANSI Colors (bright)
#define MOONFLY_BLACK_BR   0x949494
#define MOONFLY_RED_BR     0xff5189
#define MOONFLY_GREEN_BR   0x36c692
#define MOONFLY_YELLOW_BR  0xc6c684
#define MOONFLY_BLUE_BR    0x74b2ff
#define MOONFLY_PURPLE_BR  0xae81ff
#define MOONFLY_CYAN_BR    0x85dc85
#define MOONFLY_WHITE_BR   0xe4e4e4

// Syntax highlighting colors (for future use)
#define MOONFLY_COMMENT    0x6c6c6c
#define MOONFLY_STRING     0x8cc85f
#define MOONFLY_NUMBER     0xe3c78a
#define MOONFLY_KEYWORD    0x80a0ff
#define MOONFLY_FUNCTION   0x79dac8
#define MOONFLY_VARIABLE   0xc6c6c6
#define MOONFLY_TYPE       0xcf87e8
#define MOONFLY_CONSTANT   0xff5d5d

#endif

