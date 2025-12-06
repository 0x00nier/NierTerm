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
} AnsiState;

typedef struct {
    AnsiState state;
    int params[16];
    int param_count;
    char intermediate[4];
    int intermediate_count;

    // Current text attributes
    uint32_t fg_color;
    uint32_t bg_color;
    bool bold;
    bool italic;
    bool underline;
    bool inverse;
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

#endif
