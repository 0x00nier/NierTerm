#include "terminal.h"
#include "pty.h"
#include "renderer.h"
#include "input.h"
#include "config.h"
#include "ansi.h"
#include <stdint.h>
#include "autocomplete.h"
#include "autosuggest.h"
#include "fuzzy.h"
#include "clipboard.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <pwd.h>
#include <time.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

static void key_callback(KeyEvent *event, void *userdata);
static void update_prompt(Terminal *term);
static void process_output(Terminal *term, const char *data, size_t len);
static void load_bash_history(Terminal *term);
static void save_to_bash_history(Terminal *term, const char *command);

// Load bash history from ~/.bash_history
static void load_bash_history(Terminal *term) {
    if (!term) return;

    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) {
        fprintf(stderr, "[WARN] load_bash_history: Could not determine home directory\n");
        return;
    }

    char history_path[1024];
    snprintf(history_path, sizeof(history_path), "%s/.bash_history", home);

    FILE *f = fopen(history_path, "r");
    if (!f) {
        fprintf(stderr, "[INFO] load_bash_history: No .bash_history file found at %s\n", history_path);
        return;
    }

    char line[4096];
    int loaded = 0;
    while (fgets(line, sizeof(line), f) && term->history_count < term->history_capacity) {
        // Remove trailing newline
        size_t len = strlen(line);
        if (len > 0 && line[len - 1] == '\n') {
            line[len - 1] = '\0';
            len--;
        }

        // Skip empty lines
        if (len == 0) continue;

        // Add to history
        term->history[term->history_count].command = strdup(line);
        term->history[term->history_count].length = len;
        term->history[term->history_count].timestamp = time(NULL);
        term->history_count++;
        loaded++;
    }

    fclose(f);
    fprintf(stderr, "[INFO] load_bash_history: Loaded %d commands from %s\n", loaded, history_path);
}

// Save a command to ~/.bash_history
static void save_to_bash_history(Terminal *term, const char *command) {
    if (!term || !command) return;

    // Skip empty commands and duplicates
    size_t len = strlen(command);
    if (len == 0) return;

    // Check if it's a duplicate of the last command
    if (term->history_count > 0) {
        const char *last = term->history[term->history_count - 1].command;
        if (last && strcmp(last, command) == 0) {
            return; // Skip duplicate
        }
    }

    const char *home = getenv("HOME");
    if (!home) {
        struct passwd *pw = getpwuid(getuid());
        if (pw) home = pw->pw_dir;
    }
    if (!home) return;

    char history_path[1024];
    snprintf(history_path, sizeof(history_path), "%s/.bash_history", home);

    // Append to .bash_history
    FILE *f = fopen(history_path, "a");
    if (f) {
        fprintf(f, "%s\n", command);
        fclose(f);
        fprintf(stderr, "[DEBUG] save_to_bash_history: Saved '%s' to %s\n", command, history_path);
    }

    // Add to in-memory history
    if (term->history_count < term->history_capacity) {
        term->history[term->history_count].command = strdup(command);
        term->history[term->history_count].length = len;
        term->history[term->history_count].timestamp = time(NULL);
        term->history_count++;
    }
}

Terminal *terminal_create(void) {
    fprintf(stderr, "[DEBUG] terminal_create: Starting\n");
    Terminal *term = calloc(1, sizeof(Terminal));
    if (!term) {
        fprintf(stderr, "[ERROR] terminal_create: Failed to allocate terminal\n");
        return NULL;
    }
    fprintf(stderr, "[DEBUG] terminal_create: Terminal allocated\n");
    
    // Initialize PTY
    fprintf(stderr, "[DEBUG] terminal_create: Creating PTY\n");
    PTY *pty = pty_create();
    if (!pty || pty_spawn_shell(pty) < 0) {
        fprintf(stderr, "[ERROR] terminal_create: Failed to create/spawn PTY\n");
        if (pty) pty_destroy(pty);
        free(term);
        return NULL;
    }

    term->master_fd = pty->master_fd;
    term->shell_pid = pty->pid;
    fprintf(stderr, "[DEBUG] terminal_create: PTY created, master_fd=%d, pid=%d\n", term->master_fd, term->shell_pid);

    // Don't free PTY, just extract the values we need
    // The PTY struct itself can be freed, but we keep the FDs and PID
    free(pty);
    
    // Get terminal size
    struct winsize ws;
    ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws);
    term->size.rows = ws.ws_row > 0 ? ws.ws_row : 24;
    term->size.cols = ws.ws_col > 0 ? ws.ws_col : 80;
    fprintf(stderr, "[DEBUG] terminal_create: Terminal size: %dx%d\n", term->size.cols, term->size.rows);
    
    // Create renderer
    fprintf(stderr, "[DEBUG] terminal_create: Creating renderer (%dx%d)\n", term->size.cols * 8, term->size.rows * 16);
    term->renderer = renderer_create(term->size.cols * 8, term->size.rows * 16);
    if (!term->renderer) {
        fprintf(stderr, "[ERROR] terminal_create: Failed to create renderer\n");
        close(term->master_fd);
        free(term);
        return NULL;
    }
    fprintf(stderr, "[DEBUG] terminal_create: Renderer created\n");
    
    // Create input handler
    fprintf(stderr, "[DEBUG] terminal_create: Creating input handler\n");
    term->input = input_create(key_callback, term);
    if (!term->input) {
        fprintf(stderr, "[ERROR] terminal_create: Failed to create input handler\n");
        renderer_destroy(term->renderer);
        close(term->master_fd);
        free(term);
        return NULL;
    }
    fprintf(stderr, "[DEBUG] terminal_create: Input handler created\n");
    
    // Set input window to renderer's window
    fprintf(stderr, "[DEBUG] terminal_create: Setting input window\n");
    void *display = renderer_get_display(term->renderer);
    void *window = renderer_get_window(term->renderer);
    fprintf(stderr, "[DEBUG] terminal_create: display=%p, window=%p\n", display, window);
    input_set_window(term->input, display, window);
    fprintf(stderr, "[DEBUG] terminal_create: Input window set\n");

    // Get username and hostname BEFORE creating buffer
    struct passwd *pw = getpwuid(getuid());
    term->username = pw ? strdup(pw->pw_name) : strdup("user");

    char hostname_buf[256];
    gethostname(hostname_buf, sizeof(hostname_buf));
    // Abbreviate hostname (first part before dot)
    char *dot = strchr(hostname_buf, '.');
    if (dot) *dot = '\0';
    term->hostname = strdup(hostname_buf);

    update_prompt(term);

    // Initialize buffer
    Pane *pane = terminal_create_pane(term, 0, 0, term->size.rows, term->size.cols);
    if (!pane) {
        input_destroy(term->input);
        renderer_destroy(term->renderer);
        close(term->master_fd);
        free(term->username);
        free(term->hostname);
        free(term->prompt);
        free(term);
        return NULL;
    }
    term->active_pane = pane;
    term->panes = pane;

    // Initialize buffer with prompt
    Buffer *buf = pane->buffer;
    if (buf && term->prompt) {
        int len = strlen(term->prompt);
        fprintf(stderr, "[DEBUG] terminal_create: Initializing buffer with prompt '%s' (len=%d)\n", term->prompt, len);
        fprintf(stderr, "[DEBUG] terminal_create: Buffer size: %dx%d, cursor at (%d,%d)\n", 
                buf->cols, buf->rows, buf->cursor_col, buf->cursor_row);
        for (int i = 0; i < len && i < buf->cols; i++) {
            int idx = buf->cursor_row * buf->cols + buf->cursor_col;
            if (idx >= 0 && idx < buf->rows * buf->cols) {
                buf->cells[idx].ch = term->prompt[i];
                buf->cells[idx].fg_color = DEFAULT_FG_COLOR;
                buf->cells[idx].bg_color = DEFAULT_BG_COLOR;
                buf->cells[idx].bold = false;
                buf->cells[idx].italic = false;
                buf->cells[idx].underline = false;
            }
            buf->cursor_col++;
        }
        fprintf(stderr, "[DEBUG] terminal_create: Prompt written, cursor now at (%d,%d)\n", buf->cursor_col, buf->cursor_row);
    } else {
        fprintf(stderr, "[WARN] terminal_create: buf=%p, prompt=%p\n", buf, term->prompt);
    }
    
    // Initialize history
    term->history_capacity = MAX_HISTORY;
    term->history = calloc(term->history_capacity, sizeof(HistoryEntry));
    term->history_count = 0;

    // Load bash history from ~/.bash_history
    load_bash_history(term);

    // Initialize suggestions
    term->suggestions = calloc(MAX_SUGGESTIONS, sizeof(Suggestion));
    term->suggestion_count = 0;

    // Initialize ANSI parser
    term->ansi_parser = calloc(1, sizeof(AnsiParser));
    if (term->ansi_parser) {
        ansi_init(term->ansi_parser);
        fprintf(stderr, "[DEBUG] terminal_create: ANSI parser initialized\n");
    } else {
        fprintf(stderr, "[ERROR] terminal_create: Failed to allocate ANSI parser\n");
    }

    // Username and hostname already set above

    term->running = true;
    term->gpu_available = term->renderer->gpu_available;
    
    return term;
}

void terminal_destroy(Terminal *term) {
    if (!term) return;
    
    input_stop(term->input);
    input_destroy(term->input);
    
    // Close all panes
    Pane *pane = term->panes;
    while (pane) {
        Pane *next = pane->next;
        terminal_close_pane(term, pane);
        pane = next;
    }
    
    if (term->master_fd >= 0) {
        close(term->master_fd);
    }
    if (term->shell_pid > 0) {
        kill(term->shell_pid, SIGHUP);
    }
    
    renderer_destroy(term->renderer);
    
    // Free history
    for (int i = 0; i < term->history_count; i++) {
        free(term->history[i].command);
    }
    free(term->history);
    
    // Free suggestions
    for (int i = 0; i < term->suggestion_count; i++) {
        free(term->suggestions[i].text);
    }
    free(term->suggestions);

    // Free ANSI parser
    free(term->ansi_parser);

    free(term->current_line);
    free(term->fuzzy_query);
    free(term->prompt);
    free(term->username);
    free(term->hostname);

    free(term);
}

void terminal_run(Terminal *term) {
    if (!term) {
        fprintf(stderr, "[ERROR] terminal_run: term is NULL\n");
        return;
    }
    fprintf(stderr, "[DEBUG] terminal_run: Starting\n");
    
    // Make sure window is visible before starting input
    fprintf(stderr, "[DEBUG] terminal_run: Polling events and drawing initial frame\n");
    renderer_poll_events(term->renderer);
    renderer_draw_buffer(term->renderer, term->active_pane->buffer);
    renderer_swap_buffers(term->renderer);
    
    // Small delay to ensure window is mapped
    fprintf(stderr, "[DEBUG] terminal_run: Waiting for window to be ready\n");
    usleep(100000); // 100ms
    
    fprintf(stderr, "[DEBUG] terminal_run: Starting input handler\n");
    input_start(term->input);
    fprintf(stderr, "[DEBUG] terminal_run: Input handler started, entering main loop\n");
    
    char buffer[4096];
    fd_set readfds;
    int loop_count = 0;
    
    while (term->running && !renderer_should_close(term->renderer)) {
        loop_count++;
        if (loop_count % 100 == 0) {
            fprintf(stderr, "[DEBUG] terminal_run: Loop iteration %d\n", loop_count);
        }
        FD_ZERO(&readfds);
        FD_SET(term->master_fd, &readfds);
        
        struct timeval timeout;
        timeout.tv_sec = 0;
        timeout.tv_usec = 16000; // ~60 FPS
        
        int ret = select(term->master_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (ret > 0 && FD_ISSET(term->master_fd, &readfds)) {
            ssize_t n = read(term->master_fd, buffer, sizeof(buffer) - 1);
            if (n > 0) {
                buffer[n] = '\0';
                process_output(term, buffer, n);
            } else if (n == 0) {
                // Shell exited
                break;
            }
        }
        
        renderer_poll_events(term->renderer);
        renderer_draw_buffer(term->renderer, term->active_pane->buffer);
        renderer_swap_buffers(term->renderer);
    }
}

void terminal_resize(Terminal *term, int rows, int cols) {
    if (!term) return;
    
    term->size.rows = rows;
    term->size.cols = cols;
    
    // Resize renderer
    renderer_resize(term->renderer, cols * 8, rows * 16);
    
    // Resize PTY (create temporary PTY struct for resize)
    PTY tmp_pty;
    tmp_pty.master_fd = term->master_fd;
    pty_resize(&tmp_pty, rows, cols);
    
    // Resize active pane buffer
    if (term->active_pane && term->active_pane->buffer) {
        Buffer *buf = term->active_pane->buffer;
        buf->rows = rows;
        buf->cols = cols;
        buf->cells = realloc(buf->cells, rows * cols * sizeof(Cell));
        memset(buf->cells, 0, rows * cols * sizeof(Cell));
    }
}

Pane *terminal_create_pane(Terminal *term, int row, int col, int rows, int cols) {
    if (!term) return NULL;

    Pane *pane = calloc(1, sizeof(Pane));
    if (!pane) return NULL;

    pane->buffer = calloc(1, sizeof(Buffer));
    if (!pane->buffer) {
        free(pane);
        return NULL;
    }

    // Allocate scrollback buffer (10000 total lines)
    int total_rows = 10000;
    pane->buffer->rows = rows;
    pane->buffer->cols = cols;
    pane->buffer->total_rows = total_rows;
    pane->buffer->scroll_offset = 0;
    pane->buffer->cells = calloc(total_rows * cols, sizeof(Cell));
    pane->buffer->cursor_visible = true;
    
    pane->row = row;
    pane->col = col;
    pane->rows = rows;
    pane->cols = cols;
    pane->active = true;
    
    pane->next = term->panes;
    term->panes = pane;
    term->pane_count++;
    
    return pane;
}

void terminal_switch_pane(Terminal *term, Pane *pane) {
    if (!term || !pane) return;
    
    if (term->active_pane) {
        term->active_pane->active = false;
    }
    
    term->active_pane = pane;
    pane->active = true;
}

void terminal_split_pane(Terminal *term, bool vertical) {
    if (!term || !term->active_pane) return;
    
    Pane *active = term->active_pane;
    int new_row = active->row;
    int new_col = active->col;
    int new_rows = active->rows;
    int new_cols = active->cols;
    
    if (vertical) {
        new_cols /= 2;
        active->cols = new_cols;
        new_col = active->col + new_cols;
    } else {
        new_rows /= 2;
        active->rows = new_rows;
        new_row = active->row + new_rows;
    }
    
    terminal_create_pane(term, new_row, new_col, new_rows, new_cols);
}

void terminal_close_pane(Terminal *term, Pane *pane) {
    if (!term || !pane) return;
    
    // Remove from list
    if (term->panes == pane) {
        term->panes = pane->next;
    } else {
        Pane *p = term->panes;
        while (p && p->next != pane) {
            p = p->next;
        }
        if (p) {
            p->next = pane->next;
        }
    }
    
    if (term->active_pane == pane) {
        term->active_pane = term->panes;
        if (term->active_pane) {
            term->active_pane->active = true;
        }
    }
    
    if (pane->buffer) {
        free(pane->buffer->cells);
        free(pane->buffer);
    }
    
    if (pane->master_fd >= 0) {
        close(pane->master_fd);
    }
    if (pane->pid > 0) {
        kill(pane->pid, SIGHUP);
    }
    
    free(pane);
    term->pane_count--;
}

static void update_prompt(Terminal *term) {
    if (!term) return;
    
    free(term->prompt);
    int len = strlen(term->username) + strlen(term->hostname) + 4;
    term->prompt = malloc(len);
    snprintf(term->prompt, len, "%s@%s > ", term->username, term->hostname);
}

static void process_output(Terminal *term, const char *data, size_t len) {
    if (!term || !term->active_pane || !term->active_pane->buffer || !term->ansi_parser) return;

    Buffer *buf = term->active_pane->buffer;
    AnsiParser *parser = term->ansi_parser;

    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        // Process through ANSI parser
        bool should_render = ansi_process_char(parser, buf, c);

        if (should_render) {
            // Regular control characters
            if (c == '\n') {
                buf->cursor_row++;
                buf->cursor_col = 0;
            } else if (c == '\r') {
                buf->cursor_col = 0;
            } else if (c == '\t') {
                buf->cursor_col = (buf->cursor_col + 8) & ~7;
            } else if (c == '\b' || c == 127) {
                if (buf->cursor_col > 0) {
                    buf->cursor_col--;
                    // Erase character - calculate actual row in total buffer
                    int actual_row = buf->scroll_offset + buf->cursor_row;
                    int idx = actual_row * buf->cols + buf->cursor_col;
                    if (idx >= 0 && idx < buf->total_rows * buf->cols) {
                        buf->cells[idx].ch = ' ';
                    }
                }
            } else if (c >= 32) {
                // Printable character - render with current attributes
                // Calculate actual row in total buffer
                int actual_row = buf->scroll_offset + buf->cursor_row;
                int idx = actual_row * buf->cols + buf->cursor_col;
                if (idx >= 0 && idx < buf->total_rows * buf->cols) {
                    buf->cells[idx].ch = (unsigned char)c;
                    buf->cells[idx].fg_color = parser->inverse ? parser->bg_color : parser->fg_color;
                    buf->cells[idx].bg_color = parser->inverse ? parser->fg_color : parser->bg_color;
                    buf->cells[idx].bold = parser->bold;
                    buf->cells[idx].italic = parser->italic;
                    buf->cells[idx].underline = parser->underline;
                }
                buf->cursor_col++;
            }
        }

        // Handle line wrapping
        if (buf->cursor_col >= buf->cols) {
            buf->cursor_col = 0;
            buf->cursor_row++;
        }

        // Handle scrolling with scrollback buffer
        if (buf->cursor_row >= buf->rows) {
            // Move scroll_offset down to make room for new content
            buf->scroll_offset++;

            // If we've hit the end of the scrollback buffer, shift everything up
            if (buf->scroll_offset + buf->rows > buf->total_rows) {
                // Shift entire buffer up by one line
                memmove(buf->cells, buf->cells + buf->cols,
                        (buf->total_rows - 1) * buf->cols * sizeof(Cell));

                // Clear the last line
                for (int j = 0; j < buf->cols; j++) {
                    int idx = (buf->total_rows - 1) * buf->cols + j;
                    buf->cells[idx].ch = 0;
                    buf->cells[idx].fg_color = parser->fg_color;
                    buf->cells[idx].bg_color = parser->bg_color;
                    buf->cells[idx].bold = false;
                    buf->cells[idx].italic = false;
                    buf->cells[idx].underline = false;
                }

                // Keep scroll_offset at the bottom
                buf->scroll_offset = buf->total_rows - buf->rows;
            }

            // Clear the new visible bottom line
            int clear_row = buf->scroll_offset + buf->rows - 1;
            for (int j = 0; j < buf->cols; j++) {
                int idx = clear_row * buf->cols + j;
                if (idx >= 0 && idx < buf->total_rows * buf->cols) {
                    buf->cells[idx].ch = 0;
                    buf->cells[idx].fg_color = parser->fg_color;
                    buf->cells[idx].bg_color = parser->bg_color;
                }
            }

            buf->cursor_row = buf->rows - 1;
        }
    }
}

static void key_callback(KeyEvent *event, void *userdata) {
    Terminal *term = (Terminal *)userdata;
    if (!term || !event) return;

    // Handle font resizing with Ctrl+scroll
    if (event->scroll_delta != 0 && event->ctrl) {
        extern void renderer_change_font_size(Renderer *renderer, int delta);
        renderer_change_font_size(term->renderer, event->scroll_delta);
        fprintf(stderr, "[DEBUG] Font size changed by %d\n", event->scroll_delta);
        return;
    }

    // Handle scrolling without Ctrl
    if (event->scroll_delta != 0 && !event->ctrl && term->active_pane && term->active_pane->buffer) {
        Buffer *buf = term->active_pane->buffer;

        // Scroll by 3 lines per wheel tick
        int scroll_lines = event->scroll_delta * 3;
        buf->scroll_offset -= scroll_lines; // negative delta scrolls up (shows older content)

        // Clamp scroll_offset
        int max_scroll = buf->total_rows - buf->rows;
        if (buf->scroll_offset < 0) buf->scroll_offset = 0;
        if (buf->scroll_offset > max_scroll) buf->scroll_offset = max_scroll;

        fprintf(stderr, "[DEBUG] Scrolled by %d lines, offset now %d (max %d)\n",
                scroll_lines, buf->scroll_offset, max_scroll);
        return;
    }

    // Handle fuzzy finder
    if (term->fuzzy_finder_active) {
        fuzzy_handle_key(term, event);
        return;
    }

    // Handle autocomplete
    if (term->autocomplete_active && event->key == '\t') {
        autocomplete_next(term);
        return;
    }

    // Handle special key combinations
    if (event->ctrl) {
        if (event->key == 'c') {
            // Send SIGINT to shell
            if (term->shell_pid > 0) {
                kill(term->shell_pid, SIGINT);
            }
            return;
        } else if (event->key == 'v') {
            // Paste from clipboard
            char *clip = clipboard_get();
            if (clip) {
                write(term->master_fd, clip, strlen(clip));
                free(clip);
            }
            return;
        } else if (event->key == 'f') {
            // Open fuzzy finder
            fuzzy_start(term);
            return;
        } else if (event->alt) {
            if (event->key == 'h' || event->key == 'v') {
                // Split pane
                terminal_split_pane(term, event->key == 'v');
                return;
            }
        }
    }
    
    // Write to PTY
    if (event->unicode > 0) {
        char utf8[4];
        int len = 0;
        if (event->unicode < 0x80) {
            utf8[0] = event->unicode;
            len = 1;
        } else if (event->unicode < 0x800) {
            utf8[0] = 0xC0 | (event->unicode >> 6);
            utf8[1] = 0x80 | (event->unicode & 0x3F);
            len = 2;
        } else if (event->unicode < 0x10000) {
            utf8[0] = 0xE0 | (event->unicode >> 12);
            utf8[1] = 0x80 | ((event->unicode >> 6) & 0x3F);
            utf8[2] = 0x80 | (event->unicode & 0x3F);
            len = 3;
        } else {
            utf8[0] = 0xF0 | (event->unicode >> 18);
            utf8[1] = 0x80 | ((event->unicode >> 12) & 0x3F);
            utf8[2] = 0x80 | ((event->unicode >> 6) & 0x3F);
            utf8[3] = 0x80 | (event->unicode & 0x3F);
            len = 4;
        }
        write(term->master_fd, utf8, len);
    } else if (event->key > 0) {
        char c = event->key;
        write(term->master_fd, &c, 1);
    }
}

