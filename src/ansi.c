#include "ansi.h"
#include "terminal.h"
#include "colors.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static void handle_sgr(AnsiParser *parser);
static void handle_cursor_movement(AnsiParser *parser, Buffer *buffer, char final);
static void handle_erase(AnsiParser *parser, Buffer *buffer, char final);

void ansi_init(AnsiParser *parser) {
    if (!parser) return;

    parser->state = ANSI_STATE_NORMAL;
    parser->param_count = 0;
    parser->intermediate_count = 0;
    memset(parser->params, 0, sizeof(parser->params));
    memset(parser->intermediate, 0, sizeof(parser->intermediate));

    // Default text attributes (Moonfly colors)
    parser->fg_color = MOONFLY_FG;
    parser->bg_color = MOONFLY_BG;
    parser->bold = false;
    parser->italic = false;
    parser->underline = false;
    parser->inverse = false;
}

void ansi_reset(AnsiParser *parser) {
    if (!parser) return;

    parser->state = ANSI_STATE_NORMAL;
    parser->param_count = 0;
    parser->intermediate_count = 0;
}

uint32_t ansi_get_color(int color_code, bool bright) {
    // Standard ANSI colors mapped to Moonfly palette
    if (bright) {
        switch (color_code) {
            case 0: return MOONFLY_BLACK_BR;
            case 1: return MOONFLY_RED_BR;
            case 2: return MOONFLY_GREEN_BR;
            case 3: return MOONFLY_YELLOW_BR;
            case 4: return MOONFLY_BLUE_BR;
            case 5: return MOONFLY_PURPLE_BR;
            case 6: return MOONFLY_CYAN_BR;
            case 7: return MOONFLY_WHITE_BR;
            default: return MOONFLY_FG;
        }
    } else {
        switch (color_code) {
            case 0: return MOONFLY_BLACK;
            case 1: return MOONFLY_RED;
            case 2: return MOONFLY_GREEN;
            case 3: return MOONFLY_YELLOW;
            case 4: return MOONFLY_BLUE;
            case 5: return MOONFLY_PURPLE;
            case 6: return MOONFLY_CYAN;
            case 7: return MOONFLY_WHITE;
            default: return MOONFLY_FG;
        }
    }
}

static void handle_sgr(AnsiParser *parser) {
    if (!parser) return;

    // If no parameters, reset
    if (parser->param_count == 0) {
        parser->params[0] = 0;
        parser->param_count = 1;
    }

    for (int i = 0; i < parser->param_count; i++) {
        int param = parser->params[i];

        if (param == 0) {
            // Reset all attributes
            parser->fg_color = MOONFLY_FG;
            parser->bg_color = MOONFLY_BG;
            parser->bold = false;
            parser->italic = false;
            parser->underline = false;
            parser->inverse = false;
        } else if (param == 1) {
            parser->bold = true;
        } else if (param == 3) {
            parser->italic = true;
        } else if (param == 4) {
            parser->underline = true;
        } else if (param == 7) {
            parser->inverse = true;
        } else if (param == 22) {
            parser->bold = false;
        } else if (param == 23) {
            parser->italic = false;
        } else if (param == 24) {
            parser->underline = false;
        } else if (param == 27) {
            parser->inverse = false;
        } else if (param >= 30 && param <= 37) {
            // Foreground color
            parser->fg_color = ansi_get_color(param - 30, false);
        } else if (param >= 90 && param <= 97) {
            // Bright foreground color
            parser->fg_color = ansi_get_color(param - 90, true);
        } else if (param >= 40 && param <= 47) {
            // Background color
            parser->bg_color = ansi_get_color(param - 40, false);
        } else if (param >= 100 && param <= 107) {
            // Bright background color
            parser->bg_color = ansi_get_color(param - 100, true);
        } else if (param == 38 || param == 48) {
            // 256-color or RGB color
            // 38;5;N or 48;5;N for 256 colors
            // 38;2;R;G;B or 48;2;R;G;B for RGB
            if (i + 2 < parser->param_count && parser->params[i + 1] == 5) {
                // 256-color mode
                int color_idx = parser->params[i + 2];
                // Simple mapping for 256 colors (TODO: implement full palette)
                if (color_idx < 8) {
                    uint32_t color = ansi_get_color(color_idx, false);
                    if (param == 38) parser->fg_color = color;
                    else parser->bg_color = color;
                } else if (color_idx >= 8 && color_idx < 16) {
                    uint32_t color = ansi_get_color(color_idx - 8, true);
                    if (param == 38) parser->fg_color = color;
                    else parser->bg_color = color;
                }
                i += 2;
            } else if (i + 4 < parser->param_count && parser->params[i + 1] == 2) {
                // RGB mode
                int r = parser->params[i + 2];
                int g = parser->params[i + 3];
                int b = parser->params[i + 4];
                uint32_t color = (r << 16) | (g << 8) | b;
                if (param == 38) parser->fg_color = color;
                else parser->bg_color = color;
                i += 4;
            }
        } else if (param == 39) {
            // Default foreground
            parser->fg_color = MOONFLY_FG;
        } else if (param == 49) {
            // Default background
            parser->bg_color = MOONFLY_BG;
        }
    }
}

static void handle_cursor_movement(AnsiParser *parser, Buffer *buffer, char final) {
    if (!parser || !buffer) return;

    int n = parser->param_count > 0 ? parser->params[0] : 1;
    if (n < 1) n = 1;

    switch (final) {
        case 'A': // CUU - Cursor Up
            buffer->cursor_row -= n;
            if (buffer->cursor_row < 0) buffer->cursor_row = 0;
            break;
        case 'B': // CUD - Cursor Down
            buffer->cursor_row += n;
            if (buffer->cursor_row >= buffer->rows) buffer->cursor_row = buffer->rows - 1;
            break;
        case 'C': // CUF - Cursor Forward
            buffer->cursor_col += n;
            if (buffer->cursor_col >= buffer->cols) buffer->cursor_col = buffer->cols - 1;
            break;
        case 'D': // CUB - Cursor Back
            buffer->cursor_col -= n;
            if (buffer->cursor_col < 0) buffer->cursor_col = 0;
            break;
        case 'H': // CUP - Cursor Position
        case 'f': // HVP - Horizontal and Vertical Position
            {
                int row = parser->param_count > 0 ? parser->params[0] : 1;
                int col = parser->param_count > 1 ? parser->params[1] : 1;
                buffer->cursor_row = row - 1;
                buffer->cursor_col = col - 1;
                if (buffer->cursor_row < 0) buffer->cursor_row = 0;
                if (buffer->cursor_row >= buffer->rows) buffer->cursor_row = buffer->rows - 1;
                if (buffer->cursor_col < 0) buffer->cursor_col = 0;
                if (buffer->cursor_col >= buffer->cols) buffer->cursor_col = buffer->cols - 1;
            }
            break;
        case 'G': // CHA - Cursor Horizontal Absolute
            {
                int col = parser->param_count > 0 ? parser->params[0] : 1;
                buffer->cursor_col = col - 1;
                if (buffer->cursor_col < 0) buffer->cursor_col = 0;
                if (buffer->cursor_col >= buffer->cols) buffer->cursor_col = buffer->cols - 1;
            }
            break;
        case 'd': // VPA - Vertical Position Absolute
            {
                int row = parser->param_count > 0 ? parser->params[0] : 1;
                buffer->cursor_row = row - 1;
                if (buffer->cursor_row < 0) buffer->cursor_row = 0;
                if (buffer->cursor_row >= buffer->rows) buffer->cursor_row = buffer->rows - 1;
            }
            break;
    }
}

static void handle_erase(AnsiParser *parser, Buffer *buffer, char final) {
    if (!parser || !buffer) return;

    int n = parser->param_count > 0 ? parser->params[0] : 0;

    if (final == 'J') {
        // Erase in Display
        if (n == 0) {
            // Clear from cursor to end of screen
            int start = buffer->cursor_row * buffer->cols + buffer->cursor_col;
            for (int i = start; i < buffer->rows * buffer->cols; i++) {
                buffer->cells[i].ch = 0;
                buffer->cells[i].fg_color = parser->fg_color;
                buffer->cells[i].bg_color = parser->bg_color;
            }
        } else if (n == 1) {
            // Clear from cursor to beginning of screen
            int end = buffer->cursor_row * buffer->cols + buffer->cursor_col;
            for (int i = 0; i <= end; i++) {
                buffer->cells[i].ch = 0;
                buffer->cells[i].fg_color = parser->fg_color;
                buffer->cells[i].bg_color = parser->bg_color;
            }
        } else if (n == 2 || n == 3) {
            // Clear entire screen (3 also clears scrollback)
            for (int i = 0; i < buffer->rows * buffer->cols; i++) {
                buffer->cells[i].ch = 0;
                buffer->cells[i].fg_color = parser->fg_color;
                buffer->cells[i].bg_color = parser->bg_color;
            }
            if (n == 2) {
                buffer->cursor_row = 0;
                buffer->cursor_col = 0;
            }
        }
    } else if (final == 'K') {
        // Erase in Line
        int row_start = buffer->cursor_row * buffer->cols;
        if (n == 0) {
            // Clear from cursor to end of line
            for (int i = buffer->cursor_col; i < buffer->cols; i++) {
                buffer->cells[row_start + i].ch = 0;
                buffer->cells[row_start + i].fg_color = parser->fg_color;
                buffer->cells[row_start + i].bg_color = parser->bg_color;
            }
        } else if (n == 1) {
            // Clear from cursor to beginning of line
            for (int i = 0; i <= buffer->cursor_col; i++) {
                buffer->cells[row_start + i].ch = 0;
                buffer->cells[row_start + i].fg_color = parser->fg_color;
                buffer->cells[row_start + i].bg_color = parser->bg_color;
            }
        } else if (n == 2) {
            // Clear entire line
            for (int i = 0; i < buffer->cols; i++) {
                buffer->cells[row_start + i].ch = 0;
                buffer->cells[row_start + i].fg_color = parser->fg_color;
                buffer->cells[row_start + i].bg_color = parser->bg_color;
            }
        }
    }
}

bool ansi_process_char(AnsiParser *parser, Buffer *buffer, char c) {
    if (!parser || !buffer) return false;

    switch (parser->state) {
        case ANSI_STATE_NORMAL:
            if (c == '\x1b') { // ESC
                parser->state = ANSI_STATE_ESCAPE;
                parser->param_count = 0;
                parser->intermediate_count = 0;
                memset(parser->params, 0, sizeof(parser->params));
                return false;
            }
            return true; // Normal character, should be rendered

        case ANSI_STATE_ESCAPE:
            if (c == '[') {
                parser->state = ANSI_STATE_CSI;
                return false;
            } else if (c == ']') {
                parser->state = ANSI_STATE_OSC;
                return false;
            } else {
                // Unknown escape sequence, ignore
                parser->state = ANSI_STATE_NORMAL;
                return false;
            }

        case ANSI_STATE_CSI:
            if (isdigit(c)) {
                parser->state = ANSI_STATE_CSI_PARAM;
                parser->params[0] = c - '0';
                parser->param_count = 1;
                return false;
            } else if (c == ';') {
                parser->state = ANSI_STATE_CSI_PARAM;
                parser->params[0] = 0;
                parser->param_count = 1;
                return false;
            } else if (c >= 0x20 && c <= 0x2F) {
                // Intermediate byte
                parser->state = ANSI_STATE_CSI_INTERMEDIATE;
                if (parser->intermediate_count < 3) {
                    parser->intermediate[parser->intermediate_count++] = c;
                }
                return false;
            } else if (c >= 0x40 && c <= 0x7E) {
                // Final byte
                if (c == 'm') {
                    handle_sgr(parser);
                } else if (c == 'A' || c == 'B' || c == 'C' || c == 'D' ||
                           c == 'H' || c == 'f' || c == 'G' || c == 'd') {
                    handle_cursor_movement(parser, buffer, c);
                } else if (c == 'J' || c == 'K') {
                    handle_erase(parser, buffer, c);
                }
                parser->state = ANSI_STATE_NORMAL;
                return false;
            } else {
                // Invalid, reset
                parser->state = ANSI_STATE_NORMAL;
                return false;
            }

        case ANSI_STATE_CSI_PARAM:
            if (isdigit(c)) {
                if (parser->param_count > 0 && parser->param_count <= 16) {
                    parser->params[parser->param_count - 1] =
                        parser->params[parser->param_count - 1] * 10 + (c - '0');
                }
                return false;
            } else if (c == ';') {
                if (parser->param_count < 16) {
                    parser->params[parser->param_count++] = 0;
                }
                return false;
            } else if (c >= 0x20 && c <= 0x2F) {
                // Intermediate byte
                parser->state = ANSI_STATE_CSI_INTERMEDIATE;
                if (parser->intermediate_count < 3) {
                    parser->intermediate[parser->intermediate_count++] = c;
                }
                return false;
            } else if (c >= 0x40 && c <= 0x7E) {
                // Final byte
                if (c == 'm') {
                    handle_sgr(parser);
                } else if (c == 'A' || c == 'B' || c == 'C' || c == 'D' ||
                           c == 'H' || c == 'f' || c == 'G' || c == 'd') {
                    handle_cursor_movement(parser, buffer, c);
                } else if (c == 'J' || c == 'K') {
                    handle_erase(parser, buffer, c);
                }
                parser->state = ANSI_STATE_NORMAL;
                return false;
            } else {
                // Invalid, reset
                parser->state = ANSI_STATE_NORMAL;
                return false;
            }

        case ANSI_STATE_CSI_INTERMEDIATE:
            if (c >= 0x20 && c <= 0x2F) {
                // Another intermediate byte
                if (parser->intermediate_count < 3) {
                    parser->intermediate[parser->intermediate_count++] = c;
                }
                return false;
            } else if (c >= 0x40 && c <= 0x7E) {
                // Final byte - ignore for now
                parser->state = ANSI_STATE_NORMAL;
                return false;
            } else {
                // Invalid, reset
                parser->state = ANSI_STATE_NORMAL;
                return false;
            }

        case ANSI_STATE_OSC:
            // OSC sequences end with BEL or ESC backslash
            if (c == '\x07' || (c == '\\' && parser->intermediate_count > 0 &&
                parser->intermediate[parser->intermediate_count - 1] == '\x1b')) {
                parser->state = ANSI_STATE_NORMAL;
            } else {
                // Store for potential processing
                if (parser->intermediate_count < 3) {
                    parser->intermediate[parser->intermediate_count++] = c;
                }
            }
            return false;
    }

    return false;
}
