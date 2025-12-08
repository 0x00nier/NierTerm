#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <pthread.h>

// Forward declaration
struct Buffer;
typedef struct Buffer Buffer;

struct Renderer {
    void *window;
    void *context;
    bool gpu_available;
    int width;
    int height;

    // Font rendering
    void *font_library;
    void *font_face;
    int font_size;
    int char_width;
    int char_height;

    // X11 font cache (Linux)
    void *x11_font;          // Font (X11 core fonts - bitmap)
    void *x11_font_struct;   // XFontStruct* (X11)
    void *x11_gc;            // GC (X11)

    // Xft rendering (Linux - for TrueType/Nerd Fonts)
    void *xft_font;          // XftFont*
    void *xft_draw;          // XftDraw*

    // Threading
    bool render_thread_running;
    pthread_t render_thread;
};

typedef struct Renderer Renderer;

Renderer *renderer_create(int width, int height);
void renderer_destroy(Renderer *renderer);
void renderer_resize(Renderer *renderer, int width, int height);
void renderer_draw_buffer(Renderer *renderer, Buffer *buffer);
void renderer_swap_buffers(Renderer *renderer);
bool renderer_should_close(Renderer *renderer);
void renderer_poll_events(Renderer *renderer);
void *renderer_get_display(Renderer *renderer);
void *renderer_get_window(Renderer *renderer);
void renderer_change_font_size(Renderer *renderer, int delta);

// Render fuzzy finder overlay (uses void* to avoid circular dependency)
void renderer_draw_fuzzy_finder(Renderer *renderer, void *term);

// Render text selection highlight
void renderer_draw_selection(Renderer *renderer, int start_row, int start_col,
                             int end_row, int end_col, int scroll_offset);

#endif

