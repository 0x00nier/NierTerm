#ifndef ANSI_H
#define ANSI_H

#include <stdbool.h>
#include <stdint.h>

// Forward declaration
struct Buffer;
typedef struct Buffer Buffer;

// ANSI parser state
typedef enum {
    ANSI_STATE_NORMAL,
    ANSI_STATE_ESCAPE,
    ANSI_STATE_CSI,
    ANSI_STATE_OSC,
    ANSI_STATE_CSI_PARAM,
    ANSI_STATE_CSI_INTERMEDIATE,
    ANSI_STATE_DCS,        // Device Control String
    ANSI_STATE_CHARSET,    // Character set selection (ESC ( or ESC ))
} AnsiState;

// Terminal modes (DEC private modes)
typedef struct {
    bool cursor_visible;       // DECTCEM (25)
    bool app_cursor_keys;      // DECCKM (1)
    bool app_keypad;           // DECNKM
    bool auto_wrap;            // DECAWM (7)
    bool insert_mode;          // IRM (4)
    bool origin_mode;          // DECOM (6)
    bool alt_screen;           // Alternate screen buffer (1049, 47, 1047)
    bool bracketed_paste;      // Bracketed paste mode (2004)
    bool focus_events;         // Focus events (1004)
    bool mouse_tracking;       // Mouse tracking modes
    int mouse_mode;            // 0=off, 1000=normal, 1002=button, 1003=any
    int mouse_encoding;        // 0=normal, 1005=utf8, 1006=sgr, 1015=urxvt
} TerminalModes;

// Saved cursor state
typedef struct {
    int row;
    int col;
    uint32_t fg_color;
    uint32_t bg_color;
    bool bold;
    bool italic;
    bool underline;
    bool inverse;
} SavedCursor;

typedef struct {
    AnsiState state;
    int params[16];
    int param_count;
    char intermediate[8];
    int intermediate_count;

    // OSC string buffer
    char osc_buf[256];
    int osc_len;

    // Current text attributes
    uint32_t fg_color;
    uint32_t bg_color;
    bool bold;
    bool italic;
    bool underline;
    bool inverse;
    bool dim;
    bool blink;
    bool hidden;
    bool strikethrough;

    // Terminal modes
    TerminalModes modes;

    // Saved cursor (ESC 7 / ESC 8)
    SavedCursor saved_cursor;

    // Scroll region (DECSTBM)
    int scroll_top;
    int scroll_bottom;

    // Tabstops
    bool tabstops[256];
} AnsiParser;

// Initialize ANSI parser
void ansi_init(AnsiParser *parser);

// Process a single character through the ANSI parser
// Returns true if character should be rendered, false if it was part of escape sequence
bool ansi_process_char(AnsiParser *parser, Buffer *buffer, char c);

// Reset parser state
void ansi_reset(AnsiParser *parser);

// Helper to get color from ANSI color code
uint32_t ansi_get_color(int color_code, bool bright);

// Get color from 256-color palette index
uint32_t ansi_get_256_color(int idx);

// Set scroll region
void ansi_set_scroll_region(AnsiParser *parser, Buffer *buffer, int top, int bottom);

// Scroll buffer content within scroll region
void ansi_scroll_up(AnsiParser *parser, Buffer *buffer, int lines);
void ansi_scroll_down(AnsiParser *parser, Buffer *buffer, int lines);

#endif
