#include "renderer.h"
#include "terminal.h"
#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#ifdef LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#ifdef HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif
#elif defined(MACOS)
#include <Cocoa/Cocoa.h>
#include <OpenGL/gl.h>
#include <OpenGL/OpenGL.h>
#ifdef HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif
#endif

static void init_fonts(Renderer *renderer);
static void render_text_cpu(Renderer *renderer, Buffer *buffer);
static void render_text_gpu(Renderer *renderer, Buffer *buffer);

#ifdef LINUX
static Display *g_display = NULL;
static Window g_window = 0;
#endif

Renderer *renderer_create(int width, int height) {
    fprintf(stderr, "[DEBUG] renderer_create: Starting, size=%dx%d\n", width, height);
    Renderer *renderer = calloc(1, sizeof(Renderer));
    if (!renderer) {
        fprintf(stderr, "[ERROR] renderer_create: Failed to allocate renderer\n");
        return NULL;
    }
    
    renderer->width = width;
    renderer->height = height;
    renderer->font_size = DEFAULT_FONT_SIZE;
    
#ifdef LINUX
    fprintf(stderr, "[DEBUG] renderer_create: Opening X display\n");
    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        fprintf(stderr, "[ERROR] renderer_create: Failed to open X display\n");
        free(renderer);
        return NULL;
    }
    fprintf(stderr, "[DEBUG] renderer_create: X display opened\n");
    
    int screen = DefaultScreen(g_display);
    unsigned long black = BlackPixel(g_display, screen);
    fprintf(stderr, "[DEBUG] renderer_create: Screen %d, black pixel=%lu\n", screen, black);
    
    // Create simple X11 window (no OpenGL for now)
    fprintf(stderr, "[DEBUG] renderer_create: Creating X window\n");
    g_window = XCreateSimpleWindow(g_display, RootWindow(g_display, screen),
                                   0, 0, width, height, 0,
                                   black, black);
    if (g_window == 0) {
        fprintf(stderr, "[ERROR] renderer_create: Failed to create window\n");
        XCloseDisplay(g_display);
        free(renderer);
        return NULL;
    }
    fprintf(stderr, "[DEBUG] renderer_create: Window created, id=%lu\n", g_window);
    
    XSetWindowAttributes swa;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask | 
                     ButtonPressMask | ButtonReleaseMask | 
                     PointerMotionMask | StructureNotifyMask;
    XChangeWindowAttributes(g_display, g_window, CWEventMask, &swa);
    fprintf(stderr, "[DEBUG] renderer_create: Window attributes set\n");
    
    XStoreName(g_display, g_window, "NierTerm");
    fprintf(stderr, "[DEBUG] renderer_create: Window name set\n");
    
    // Set up WM_DELETE_WINDOW protocol for proper window closing
    Atom wm_delete_window = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, g_window, &wm_delete_window, 1);
    fprintf(stderr, "[DEBUG] renderer_create: WM protocols set\n");
    
    fprintf(stderr, "[DEBUG] renderer_create: Mapping window\n");
    XMapWindow(g_display, g_window);
    XMapRaised(g_display, g_window); // Raise window to front
    XFlush(g_display);
    XSync(g_display, False); // Wait for window to actually appear
    fprintf(stderr, "[DEBUG] renderer_create: Window mapped, raised, and synced\n");
    
    // For now, disable GPU rendering - use pure X11
    renderer->gpu_available = false;
    renderer->window = (void *)g_window;
    renderer->context = NULL;

    // Initialize X11 font cache
    renderer->x11_font = NULL;
    renderer->x11_font_struct = NULL;
    renderer->x11_gc = NULL;

    fprintf(stderr, "[DEBUG] renderer_create: Renderer setup complete\n");

#elif defined(MACOS)
    // macOS implementation would go here
    renderer->gpu_available = false;
    renderer->x11_font = NULL;
    renderer->x11_font_struct = NULL;
    renderer->x11_gc = NULL;
#endif

    // Initialize fonts
    init_fonts(renderer);

    return renderer;
}

void renderer_destroy(Renderer *renderer) {
    if (!renderer) return;

    if (renderer->render_thread_running) {
        renderer->render_thread_running = false;
        pthread_join(renderer->render_thread, NULL);
    }

#ifdef LINUX
    // Clean up cached X11 resources
    if (g_display) {
        if (renderer->x11_font_struct) {
            XFreeFont(g_display, (XFontStruct *)renderer->x11_font_struct);
            renderer->x11_font_struct = NULL;
        }
        if (renderer->x11_gc) {
            XFreeGC(g_display, (GC)renderer->x11_gc);
            renderer->x11_gc = NULL;
        }
        // Note: XFreeFont above already unloaded the font
        renderer->x11_font = NULL;
    }

    if (g_window) {
        XDestroyWindow(g_display, g_window);
        g_window = 0;
    }
    if (g_display) {
        XCloseDisplay(g_display);
        g_display = NULL;
    }
#endif

#ifdef HAVE_FREETYPE
    if (renderer->font_face) {
        FT_Done_Face((FT_Face)renderer->font_face);
    }
    if (renderer->font_library) {
        FT_Done_FreeType((FT_Library)renderer->font_library);
    }
#endif

    free(renderer);
}

void renderer_resize(Renderer *renderer, int width, int height) {
    if (!renderer) return;

    renderer->width = width;
    renderer->height = height;

#ifdef LINUX
    if (g_window) {
        XResizeWindow(g_display, g_window, width, height);
        XFlush(g_display);
    }
#endif
}

void renderer_change_font_size(Renderer *renderer, int delta) {
    if (!renderer) return;

    // Update font size (clamp between 6 and 72)
    renderer->font_size += delta;
    if (renderer->font_size < 6) renderer->font_size = 6;
    if (renderer->font_size > 72) renderer->font_size = 72;

    fprintf(stderr, "[DEBUG] renderer_change_font_size: New size = %d\n", renderer->font_size);

#ifdef LINUX
    if (!g_display || !g_window) return;

    // Free old font resources
    if (renderer->x11_font_struct) {
        XFreeFont(g_display, (XFontStruct *)renderer->x11_font_struct);
        renderer->x11_font_struct = NULL;
    }
    renderer->x11_font = NULL;

    // Try to load font at new size
    // For X11 core fonts, we'll try different sizes
    Font font = None;

    // Try standard fixed fonts with approximate size
    const char *base_fonts[] = {
        renderer->font_size >= 14 ? "9x15" : (renderer->font_size >= 12 ? "8x13" : "7x13"),
        "fixed",
        NULL
    };

    for (int i = 0; base_fonts[i] && font == None; i++) {
        font = XLoadFont(g_display, base_fonts[i]);
        if (font != None) {
            fprintf(stderr, "[DEBUG] Loaded font '%s' for size %d\n", base_fonts[i], renderer->font_size);
            break;
        }
    }

    if (font != None) {
        renderer->x11_font = (void *)(uintptr_t)font;
        XSetFont(g_display, (GC)renderer->x11_gc, font);

        // Update font struct
        XFontStruct *font_struct = XQueryFont(g_display, font);
        if (font_struct) {
            renderer->x11_font_struct = (void *)font_struct;

            int char_w = font_struct->max_bounds.width;
            int char_h = font_struct->ascent + font_struct->descent;

            if (char_w == 0) {
                char_w = font_struct->max_bounds.rbearing - font_struct->min_bounds.lbearing;
            }
            if (char_w == 0) char_w = 8;

            renderer->char_width = char_w;
            renderer->char_height = char_h;

            fprintf(stderr, "[DEBUG] New char dimensions: %dx%d\n", char_w, char_h);
        }
    }
#endif

#ifdef HAVE_FREETYPE
    // Update FreeType font size if available
    if (renderer->font_face) {
        FT_Set_Pixel_Sizes((FT_Face)renderer->font_face, 0, renderer->font_size);
    }
#endif
}

void renderer_draw_buffer(Renderer *renderer, Buffer *buffer) {
    if (!renderer || !buffer) return;
    
    if (renderer->gpu_available) {
        render_text_gpu(renderer, buffer);
    } else {
        render_text_cpu(renderer, buffer);
    }
}

void renderer_swap_buffers(Renderer *renderer) {
    if (!renderer) return;
    
#ifdef LINUX
    // For X11 rendering, we just need to flush
    if (g_display) {
        XFlush(g_display);
    }
#endif
}

bool renderer_should_close(Renderer *renderer) {
    if (!renderer) return true;
#ifdef LINUX
    // Check if window still exists
    if (g_window == 0) return true;
    
    // Check if display connection is still valid
    if (g_display) {
        XEvent event;
        if (XCheckTypedWindowEvent(g_display, g_window, ClientMessage, &event)) {
            // Window close event detected
            return true;
        }
    }
#endif
    return false;
}

void renderer_poll_events(Renderer *renderer) {
#ifdef LINUX
    if (g_display) {
        XEvent event;
        while (XPending(g_display)) {
            XNextEvent(g_display, &event);
            if (event.type == Expose) {
                // Window was exposed - trigger a redraw
                if (renderer && renderer->window) {
                    // The next draw call will refresh the window
                }
            } else if (event.type == ClientMessage) {
                // Window close event - check for WM_DELETE_WINDOW
                Atom wm_protocols = XInternAtom(g_display, "WM_PROTOCOLS", False);
                Atom wm_delete_window = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
                
                if (event.xclient.message_type == wm_protocols &&
                    (Atom)event.xclient.data.l[0] == wm_delete_window) {
                    // Window close requested - destroy window
                    XDestroyWindow(g_display, g_window);
                    g_window = 0;
                }
            }
        }
    }
#else
    (void)renderer;
#endif
}

void *renderer_get_display(Renderer *renderer) {
    (void)renderer;
#ifdef LINUX
    return (void *)g_display;
#else
    return NULL;
#endif
}

void *renderer_get_window(Renderer *renderer) {
    (void)renderer;
#ifdef LINUX
    if (g_window == 0) return NULL;
    return (void *)(uintptr_t)g_window;
#else
    return NULL;
#endif
}

static void init_fonts(Renderer *renderer) {
    // Default character dimensions - will be updated when font is loaded
    renderer->char_width = 8;
    renderer->char_height = 16;
    renderer->font_library = NULL;
    renderer->font_face = NULL;

#ifdef LINUX
    // Load and cache X11 font
    if (g_display && g_window) {
        // Create persistent GC
        GC gc = XCreateGC(g_display, g_window, 0, NULL);
        renderer->x11_gc = (void *)gc;

        // Try to load a fixed-width font
        Font font = None;
        const char *font_names[] = {
            "fixed",
            "6x13",
            "7x13",
            "8x13",
            "9x15",
            "-misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso10646-1",
            NULL
        };

        for (int i = 0; font_names[i] && font == None; i++) {
            font = XLoadFont(g_display, font_names[i]);
            if (font != None) {
                fprintf(stderr, "[DEBUG] init_fonts: Loaded font '%s'\n", font_names[i]);
                break;
            }
        }

        if (font != None) {
            renderer->x11_font = (void *)(uintptr_t)font;
            XSetFont(g_display, gc, font);

            // Query font metrics
            XFontStruct *font_struct = XQueryFont(g_display, font);
            if (font_struct) {
                renderer->x11_font_struct = (void *)font_struct;

                int char_w = font_struct->max_bounds.width;
                int char_h = font_struct->ascent + font_struct->descent;

                if (char_w == 0) {
                    char_w = font_struct->max_bounds.rbearing - font_struct->min_bounds.lbearing;
                }
                if (char_w == 0) char_w = 8;

                renderer->char_width = char_w;
                renderer->char_height = char_h;

                fprintf(stderr, "[DEBUG] init_fonts: Cached font metrics - w=%d, h=%d\n", char_w, char_h);
            }
        } else {
            fprintf(stderr, "[ERROR] init_fonts: Failed to load any font\n");
        }
    }
#endif

#ifdef HAVE_FREETYPE
    FT_Library library;
    if (FT_Init_FreeType(&library) != 0) {
        return;
    }

    renderer->font_library = (void *)library;

    FT_Face face;
    if (FT_New_Face(library, FONT_PATH, 0, &face) != 0) {
        // Try fallback font
        const char *fallback = "/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf";
        if (FT_New_Face(library, fallback, 0, &face) != 0) {
            FT_Done_FreeType(library);
            renderer->font_library = NULL;
            return;
        }
    }

    FT_Set_Pixel_Sizes(face, 0, renderer->font_size);
    renderer->font_face = (void *)face;

    // Calculate character dimensions (FreeType may override X11 font metrics)
    FT_GlyphSlot slot = face->glyph;
    int ft_width = (slot->metrics.horiAdvance >> 6);
    int ft_height = (face->size->metrics.height >> 6);
    if (ft_width > 0) renderer->char_width = ft_width;
    if (ft_height > 0) renderer->char_height = ft_height;
#endif
}

static void render_text_cpu(Renderer *renderer, Buffer *buffer) {
    static int render_count = 0;
    render_count++;
    if (render_count % 60 == 0) {
        fprintf(stderr, "[DEBUG] render_text_cpu: Render call %d\n", render_count);
    }

    if (!renderer || !buffer) {
        if (render_count == 1) fprintf(stderr, "[ERROR] render_text_cpu: renderer or buffer is NULL\n");
        return;
    }

#ifdef LINUX
    if (!g_display || !g_window) {
        if (render_count == 1) fprintf(stderr, "[ERROR] render_text_cpu: g_display=%p, g_window=%lu\n", g_display, g_window);
        return;
    }

    // Use cached GC and font
    if (!renderer->x11_gc || !renderer->x11_font_struct) {
        if (render_count == 1) fprintf(stderr, "[ERROR] render_text_cpu: Cached font not initialized\n");
        return;
    }

    GC gc = (GC)renderer->x11_gc;
    XFontStruct *font_struct = (XFontStruct *)renderer->x11_font_struct;

    int char_w = renderer->char_width;
    int char_h = renderer->char_height;
    
    // Set background color (black)
    XSetBackground(g_display, gc, BlackPixel(g_display, DefaultScreen(g_display)));
    
    // Clear the window to black
    XSetForeground(g_display, gc, BlackPixel(g_display, DefaultScreen(g_display)));
    XFillRectangle(g_display, g_window, gc, 0, 0, renderer->width, renderer->height);
    
    // Set foreground color to white
    XSetForeground(g_display, gc, WhitePixel(g_display, DefaultScreen(g_display)));
    
    // Count non-empty cells
    int cell_count = 0;
    int non_empty_count = 0;

    // Render each character, starting from scroll_offset
    for (int visible_row = 0; visible_row < buffer->rows && visible_row * char_h < renderer->height; visible_row++) {
        // Calculate actual row in the total buffer (including scrollback)
        int actual_row = buffer->scroll_offset + visible_row;
        if (actual_row >= buffer->total_rows) break;

        for (int col = 0; col < buffer->cols && col * char_w < renderer->width; col++) {
            int idx = actual_row * buffer->cols + col;
            if (idx < 0 || idx >= buffer->total_rows * buffer->cols) continue;

            cell_count++;
            Cell *cell = &buffer->cells[idx];

            // Calculate position (use visible_row for Y coordinate)
            int x = col * char_w;
            int y = visible_row * char_h;

            // Draw background color
            unsigned long bg = ((cell->bg_color >> 16) & 0xFF) << 16 |
                              ((cell->bg_color >> 8) & 0xFF) << 8 |
                              (cell->bg_color & 0xFF);
            XSetForeground(g_display, gc, bg);
            XFillRectangle(g_display, g_window, gc, x, y, char_w, char_h);

            // Draw character if not empty
            if (cell->ch != 0 && cell->ch != ' ') {
                non_empty_count++;

                // Set foreground color
                unsigned long fg = ((cell->fg_color >> 16) & 0xFF) << 16 |
                                  ((cell->fg_color >> 8) & 0xFF) << 8 |
                                  (cell->fg_color & 0xFF);
                XSetForeground(g_display, gc, fg);

                // TODO: Handle bold (could load bold font or draw twice with offset)
                // TODO: Handle italic (requires italic font)
                // TODO: Handle underline (draw line below character)

                // Draw character
                char ch_str[2] = {(char)cell->ch, '\0'};
                XDrawString(g_display, g_window, gc, x, y + font_struct->ascent, ch_str, 1);

                // Draw underline if needed
                if (cell->underline) {
                    XDrawLine(g_display, g_window, gc, x, y + char_h - 1, x + char_w - 1, y + char_h - 1);
                }
            }
        }
    }
    
    // Draw cursor (block cursor)
    if (buffer->cursor_visible) {
        int cursor_x = buffer->cursor_col * char_w;
        int cursor_y = buffer->cursor_row * char_h;
        
        XSetForeground(g_display, gc, WhitePixel(g_display, DefaultScreen(g_display)));
        XFillRectangle(g_display, g_window, gc, cursor_x, cursor_y, char_w, char_h);
    }
    
    static int render_debug_count = 0;
    render_debug_count++;
    if (render_debug_count <= 5 || render_debug_count % 60 == 0) {
        fprintf(stderr, "[DEBUG] render_text_cpu: Rendered %d cells, %d non-empty, cursor at (%d,%d)\n",
                cell_count, non_empty_count, buffer->cursor_col, buffer->cursor_row);
    }

    // Don't free GC or font - they're cached in renderer and will be freed in renderer_destroy()

    XFlush(g_display);
#endif
}

static void render_text_gpu(Renderer *renderer, Buffer *buffer) {
    // For now, use CPU rendering even if GPU is available
    // This ensures consistent rendering
    render_text_cpu(renderer, buffer);
}

