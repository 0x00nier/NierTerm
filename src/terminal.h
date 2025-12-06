#ifndef TERMINAL_H
#define TERMINAL_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>
#include "ansi.h"

// Forward declarations
typedef struct Renderer Renderer;
typedef struct InputHandler InputHandler;

#define MAX_PANES 256
#define MAX_HISTORY 10000
#define MAX_SUGGESTIONS 100

typedef struct {
    int rows;
    int cols;
} TerminalSize;

typedef struct {
    uint32_t ch;
    uint32_t fg_color;
    uint32_t bg_color;
    bool bold;
    bool italic;
    bool underline;
} Cell;

struct Buffer {
    Cell *cells;
    int rows;              // Visible rows
    int cols;
    int total_rows;        // Total rows including scrollback
    int scroll_offset;     // Current scroll position
    int cursor_row;
    int cursor_col;
    bool cursor_visible;
};

typedef struct Buffer Buffer;

typedef struct {
    char *text;
    int length;
    int score;
} Suggestion;

typedef struct {
    char *command;
    int length;
    time_t timestamp;
} HistoryEntry;

typedef struct Pane {
    Buffer *buffer;
    int pid;
    int master_fd;
    int row;
    int col;
    int rows;
    int cols;
    bool active;
    struct Pane *next;
} Pane;

typedef struct {
    // Core
    Pane *panes;
    Pane *active_pane;
    int pane_count;
    
    // PTY
    int shell_pid;
    int master_fd;
    
    // Rendering
    Renderer *renderer;
    TerminalSize size;
    
    // Input
    InputHandler *input;

    // ANSI parser
    AnsiParser *ansi_parser;

    // History & suggestions
    HistoryEntry *history;
    int history_count;
    int history_capacity;
    Suggestion *suggestions;
    int suggestion_count;
    
    // Autocomplete state
    char *current_line;
    int cursor_pos;
    bool autocomplete_active;
    int autocomplete_index;
    
    // Fuzzy finder state
    bool fuzzy_finder_active;
    char *fuzzy_query;
    int fuzzy_selection;
    
    // Prompt
    char *prompt;
    char *username;
    char *hostname;
    
    // Config
    bool running;
    bool gpu_available;
} Terminal;

Terminal *terminal_create(void);
void terminal_destroy(Terminal *term);
void terminal_run(Terminal *term);
void terminal_resize(Terminal *term, int rows, int cols);
Pane *terminal_create_pane(Terminal *term, int row, int col, int rows, int cols);
void terminal_switch_pane(Terminal *term, Pane *pane);
void terminal_split_pane(Terminal *term, bool vertical);
void terminal_close_pane(Terminal *term, Pane *pane);

#endif

