#include "renderer.h"
#include "terminal.h"
#include "config.h"
#include "colors.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

#ifdef LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#ifdef HAVE_XFT
#include <X11/Xft/Xft.h>
#endif
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

// Window padding (pixels from edge)
#define WINDOW_PADDING 6

// Font scaling factors (like st terminal's cwscale/chscale)
// Reduce these slightly to fix Nerd Font spacing issues
static float cwscale = 1.0f;   // Character width scale
static float chscale = 1.0f;   // Character height scale

#ifdef LINUX
static Display *g_display = NULL;
static Window g_window = 0;
static Colormap g_colormap = 0;
static int g_screen = 0;
static Visual *g_visual = NULL;
static int g_depth = 0;
#endif

// Convert RGB to X11 pixel value
#ifdef LINUX
static unsigned long rgb_to_pixel(uint32_t rgb) {
    if (!g_display) return 0;

    // For TrueColor visuals (depth >= 24), we can use RGB directly
    if (g_depth >= 24) {
        unsigned char r = (rgb >> 16) & 0xFF;
        unsigned char g = (rgb >> 8) & 0xFF;
        unsigned char b = rgb & 0xFF;
        return (r << 16) | (g << 8) | b;
    }

    // For other visuals, use XAllocColor
    XColor xcolor;
    xcolor.red = ((rgb >> 16) & 0xFF) * 257;   // Scale 0-255 to 0-65535
    xcolor.green = ((rgb >> 8) & 0xFF) * 257;
    xcolor.blue = (rgb & 0xFF) * 257;
    xcolor.flags = DoRed | DoGreen | DoBlue;

    if (XAllocColor(g_display, g_colormap, &xcolor)) {
        return xcolor.pixel;
    }

    // Fallback to black or white
    return (rgb > 0x808080) ? WhitePixel(g_display, g_screen) : BlackPixel(g_display, g_screen);
}
#endif

// Motif WM hints for borderless window
typedef struct {
    unsigned long flags;
    unsigned long functions;
    unsigned long decorations;
    long input_mode;
    unsigned long status;
} MotifWmHints;

#define MWM_HINTS_DECORATIONS (1L << 1)

Renderer *renderer_create(int width, int height) {
    Renderer *renderer = calloc(1, sizeof(Renderer));
    if (!renderer) {
        fprintf(stderr, "[ERROR] Failed to allocate renderer\n");
        return NULL;
    }

    renderer->width = width;
    renderer->height = height;
    renderer->font_size = DEFAULT_FONT_SIZE;

#ifdef LINUX
    g_display = XOpenDisplay(NULL);
    if (!g_display) {
        fprintf(stderr, "[ERROR] Failed to open X display\n");
        free(renderer);
        return NULL;
    }

    g_screen = DefaultScreen(g_display);
    g_visual = DefaultVisual(g_display, g_screen);
    g_colormap = DefaultColormap(g_display, g_screen);
    g_depth = DefaultDepth(g_display, g_screen);

    // Use pure black background
    unsigned long black = rgb_to_pixel(0x000000);

    // Create window
    g_window = XCreateSimpleWindow(g_display, RootWindow(g_display, g_screen),
                                   0, 0, width, height, 0,
                                   black, black);
    if (g_window == 0) {
        fprintf(stderr, "[ERROR] Failed to create window\n");
        XCloseDisplay(g_display);
        free(renderer);
        return NULL;
    }

    // Request borderless window (removes ugly decorations)
    Atom motif_hints = XInternAtom(g_display, "_MOTIF_WM_HINTS", False);
    MotifWmHints hints = {0};
    hints.flags = MWM_HINTS_DECORATIONS;
    hints.decorations = 0;  // No decorations = borderless
    XChangeProperty(g_display, g_window, motif_hints, motif_hints, 32,
                    PropModeReplace, (unsigned char *)&hints, 5);

    XSetWindowAttributes swa;
    swa.event_mask = ExposureMask | KeyPressMask | KeyReleaseMask |
                     ButtonPressMask | ButtonReleaseMask |
                     PointerMotionMask | StructureNotifyMask;
    swa.background_pixel = black;
    swa.border_pixel = black;
    XChangeWindowAttributes(g_display, g_window, CWEventMask | CWBackPixel | CWBorderPixel, &swa);

    XStoreName(g_display, g_window, "NierTerm");

    // Set up WM_DELETE_WINDOW protocol for proper window closing
    Atom wm_delete_window = XInternAtom(g_display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(g_display, g_window, &wm_delete_window, 1);

    XMapWindow(g_display, g_window);
    XMapRaised(g_display, g_window);
    XFlush(g_display);
    XSync(g_display, False);

    renderer->gpu_available = false;
    renderer->window = (void *)g_window;
    renderer->context = NULL;

    // Initialize X11 font cache
    renderer->x11_font = NULL;
    renderer->x11_font_struct = NULL;
    renderer->x11_gc = NULL;

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
    // Default character dimensions
    renderer->char_width = 10;
    renderer->char_height = 20;
    renderer->font_library = NULL;
    renderer->font_face = NULL;
    renderer->xft_font = NULL;
    renderer->xft_draw = NULL;

#ifdef LINUX
    if (!g_display || !g_window) return;

    // Create persistent GC
    GC gc = XCreateGC(g_display, g_window, 0, NULL);
    renderer->x11_gc = (void *)gc;

#ifdef HAVE_XFT
    // Xft support enabled - load TrueType/Nerd Fonts!
    fprintf(stderr, "[INFO] Xft enabled - loading Nerd Fonts...\n");

    const char *patterns[] = {
        "MesloLGS Nerd Font Mono:size=14:antialias=true:hinting=true",
        "MesloLGS NF:size=14:antialias=true:hinting=true",
        "JetBrainsMono Nerd Font:size=14:antialias=true:hinting=true",
        "monospace:size=14:antialias=true",
        NULL
    };

    XftFont *xft_font = NULL;
    for (int i = 0; patterns[i]; i++) {
        xft_font = XftFontOpenName(g_display, DefaultScreen(g_display), patterns[i]);
        if (xft_font) {
            fprintf(stderr, "[SUCCESS] Loaded: %s\n", patterns[i]);
            break;
        }
    }

    if (xft_font) {
        renderer->xft_font = xft_font;
        renderer->xft_draw = XftDrawCreate(g_display, g_window,
                                          DefaultVisual(g_display, DefaultScreen(g_display)),
                                          DefaultColormap(g_display, DefaultScreen(g_display)));

        // Measure character width using XftTextExtents (like st terminal does)
        // This gives more accurate width than max_advance_width for monospace fonts
        XGlyphInfo extents;
        const char test_char = 'M';  // Use 'M' as reference (widest common char)
        XftTextExtentsUtf8(g_display, xft_font, (XftChar8 *)&test_char, 1, &extents);

        int measured_width = extents.xOff;  // xOff is the advance width
        if (measured_width <= 0) {
            measured_width = xft_font->max_advance_width;
        }

        // Apply cwscale factor (st terminal style) for fine-tuning
        renderer->char_width = (int)(measured_width * cwscale);

        // Use font's actual line height with chscale
        renderer->char_height = (int)((xft_font->ascent + xft_font->descent) * chscale);

        // Sanity checks
        if (renderer->char_width <= 0 || renderer->char_width > 30) {
            renderer->char_width = 10;
        }
        if (renderer->char_height <= 0 || renderer->char_height > 50) {
            renderer->char_height = 20;
        }

        fprintf(stderr, "[INFO] Font: width=%d, height=%d (measured=%d, max_advance=%d)\n",
                renderer->char_width, renderer->char_height, measured_width, xft_font->max_advance_width);
        return;
    }
    fprintf(stderr, "[WARN] No Xft fonts - falling back to bitmap\n");
#else
    fprintf(stderr, "[INFO] Xft not available - install libxft-dev and rebuild\n");
#endif

    // Fallback: bitmap fonts
    Font font = None;
    const char *names[] = {"10x20", "9x15", "fixed", NULL};
    for (int i = 0; names[i]; i++) {
        font = XLoadFont(g_display, names[i]);
        if (font != None) break;
    }
    if (font != None) {
        renderer->x11_font = (void *)(uintptr_t)font;
        XSetFont(g_display, gc, font);
        XFontStruct *fs = XQueryFont(g_display, font);
        if (fs) {
            renderer->x11_font_struct = fs;
            renderer->char_width = fs->max_bounds.width ?: 10;
            renderer->char_height = fs->ascent + fs->descent;
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
    if (!renderer || !buffer) return;

#ifdef LINUX
    if (!g_display || !g_window) return;

    // Check if we have any font available
    bool use_xft = false;
#ifdef HAVE_XFT
    if (renderer->xft_font && renderer->xft_draw) {
        use_xft = true;
    }
#endif

    if (!use_xft && (!renderer->x11_gc || !renderer->x11_font_struct)) return;

    GC gc = (GC)renderer->x11_gc;
    int char_w = renderer->char_width;
    int char_h = renderer->char_height;

#ifdef HAVE_XFT
    XftFont *xft_font = (XftFont *)renderer->xft_font;
    XftDraw *xft_draw = (XftDraw *)renderer->xft_draw;
    int xft_ascent = use_xft ? xft_font->ascent : 0;
#endif

    // Clear the window to pure black background
    if (gc) {
        unsigned long bg_pixel = rgb_to_pixel(MOONFLY_BG);
        XSetForeground(g_display, gc, bg_pixel);
        XFillRectangle(g_display, g_window, gc, 0, 0, renderer->width, renderer->height);
    }

    // Render each character, starting from scroll_offset (with padding)
    for (int visible_row = 0; visible_row < buffer->rows && (visible_row * char_h + WINDOW_PADDING) < renderer->height; visible_row++) {
        int actual_row = buffer->scroll_offset + visible_row;
        if (actual_row >= buffer->total_rows) break;

        for (int col = 0; col < buffer->cols && (col * char_w + WINDOW_PADDING) < renderer->width; col++) {
            int idx = actual_row * buffer->cols + col;
            if (idx < 0 || idx >= buffer->total_rows * buffer->cols) continue;

            Cell *cell = &buffer->cells[idx];
            int x = col * char_w + WINDOW_PADDING;
            int y = visible_row * char_h + WINDOW_PADDING;

            // Draw background
            if (gc) {
                unsigned long bg = rgb_to_pixel(cell->bg_color);
                XSetForeground(g_display, gc, bg);
                XFillRectangle(g_display, g_window, gc, x, y, char_w, char_h);
            }

            // Draw character if not empty
            if (cell->ch != 0 && cell->ch != ' ') {
#ifdef HAVE_XFT
                if (use_xft) {
                    // Use Xft for nice antialiased rendering
                    XftColor xft_color;
                    XRenderColor render_color;
                    render_color.red = ((cell->fg_color >> 16) & 0xFF) * 257;
                    render_color.green = ((cell->fg_color >> 8) & 0xFF) * 257;
                    render_color.blue = (cell->fg_color & 0xFF) * 257;
                    render_color.alpha = 0xFFFF;
                    XftColorAllocValue(g_display, g_visual, g_colormap, &render_color, &xft_color);

                    char ch_str[2] = {(char)cell->ch, '\0'};
                    XftDrawStringUtf8(xft_draw, &xft_color, xft_font, x, y + xft_ascent,
                                     (XftChar8 *)ch_str, 1);
                    XftColorFree(g_display, g_visual, g_colormap, &xft_color);

                    // Draw underline
                    if (cell->underline && gc) {
                        XSetForeground(g_display, gc, rgb_to_pixel(cell->fg_color));
                        XDrawLine(g_display, g_window, gc, x, y + char_h - 1, x + char_w - 1, y + char_h - 1);
                    }
                } else
#endif
                {
                    // Use X11 bitmap fonts
                    XFontStruct *font_struct = (XFontStruct *)renderer->x11_font_struct;
                    unsigned long fg = rgb_to_pixel(cell->fg_color);
                    XSetForeground(g_display, gc, fg);

                    char ch_str[2] = {(char)cell->ch, '\0'};
                    XDrawString(g_display, g_window, gc, x, y + font_struct->ascent, ch_str, 1);

                    if (cell->underline) {
                        XDrawLine(g_display, g_window, gc, x, y + char_h - 1, x + char_w - 1, y + char_h - 1);
                    }
                }
            }
        }
    }

    // Draw cursor (block cursor) with padding offset
    if (buffer->cursor_visible && gc) {
        int cursor_x = buffer->cursor_col * char_w + WINDOW_PADDING;
        int cursor_y = buffer->cursor_row * char_h + WINDOW_PADDING;
        XSetForeground(g_display, gc, rgb_to_pixel(MOONFLY_FG));
        XFillRectangle(g_display, g_window, gc, cursor_x, cursor_y, char_w, char_h);
    }

    XFlush(g_display);
#endif
}

static void render_text_gpu(Renderer *renderer, Buffer *buffer) {
    // For now, use CPU rendering even if GPU is available
    // This ensures consistent rendering
    render_text_cpu(renderer, buffer);
}

void renderer_draw_selection(Renderer *renderer, int start_row, int start_col,
                             int end_row, int end_col, int scroll_offset) {
    if (!renderer) return;

    // Normalize selection (ensure start is before end)
    if (start_row > end_row || (start_row == end_row && start_col > end_col)) {
        int tmp = start_row; start_row = end_row; end_row = tmp;
        tmp = start_col; start_col = end_col; end_col = tmp;
    }

#ifdef LINUX
    if (!g_display || !g_window || !renderer->x11_gc) return;

    GC gc = (GC)renderer->x11_gc;
    int char_w = renderer->char_width;
    int char_h = renderer->char_height;

    // Selection highlight color (semi-transparent blue effect via XOR)
    // Use a blue-ish color for selection
    unsigned long sel_color = rgb_to_pixel(0x4488CC);
    XSetForeground(g_display, gc, sel_color);
    XSetFunction(g_display, gc, GXxor);  // XOR mode for highlight effect

    for (int row = start_row; row <= end_row; row++) {
        int col_start = (row == start_row) ? start_col : 0;
        int col_end = (row == end_row) ? end_col : 1000;  // Will be clamped by window width

        int x = col_start * char_w + WINDOW_PADDING;
        int y = row * char_h + WINDOW_PADDING;
        int width = (col_end - col_start + 1) * char_w;

        XFillRectangle(g_display, g_window, gc, x, y, width, char_h);
    }

    // Restore normal drawing mode
    XSetFunction(g_display, gc, GXcopy);
    XFlush(g_display);
#endif
}

void renderer_draw_fuzzy_finder(Renderer *renderer, void *term_ptr) {
    Terminal *term = (Terminal *)term_ptr;
    if (!renderer || !term || !term->fuzzy_finder_active) return;

#ifdef LINUX
    if (!g_display || !g_window || !renderer->x11_gc) return;

    // Check if we have any font available (Xft or bitmap)
    bool use_xft = false;
#ifdef HAVE_XFT
    XftFont *xft_font = (XftFont *)renderer->xft_font;
    XftDraw *xft_draw = (XftDraw *)renderer->xft_draw;
    if (xft_font && xft_draw) {
        use_xft = true;
    }
#endif
    if (!use_xft && !renderer->x11_font_struct) return;

    GC gc = (GC)renderer->x11_gc;
    int char_w = renderer->char_width;
    int char_h = renderer->char_height;

    // Draw fuzzy finder box at the bottom of the screen
    int box_height = (term->suggestion_count + 2) * char_h;
    int box_y = renderer->height - box_height;
    int box_width = renderer->width;

    // Draw background
    XSetForeground(g_display, gc, rgb_to_pixel(0x1c1c1c));  // Dark background
    XFillRectangle(g_display, g_window, gc, 0, box_y, box_width, box_height);

    // Draw border
    XSetForeground(g_display, gc, rgb_to_pixel(MOONFLY_BLUE));
    XDrawRectangle(g_display, g_window, gc, 0, box_y, box_width - 1, box_height - 1);

    // Draw query line
    char prompt[256];
    snprintf(prompt, sizeof(prompt), "(search): %s", term->fuzzy_query ? term->fuzzy_query : "");
    int y = box_y + char_h;

#ifdef HAVE_XFT
    if (use_xft) {
        XftColor xft_color;
        XRenderColor render_color;

        // Draw prompt in green
        render_color.red = ((MOONFLY_GREEN >> 16) & 0xFF) * 257;
        render_color.green = ((MOONFLY_GREEN >> 8) & 0xFF) * 257;
        render_color.blue = (MOONFLY_GREEN & 0xFF) * 257;
        render_color.alpha = 0xFFFF;
        XftColorAllocValue(g_display, g_visual, g_colormap, &render_color, &xft_color);
        XftDrawStringUtf8(xft_draw, &xft_color, xft_font, char_w, y, (XftChar8 *)prompt, strlen(prompt));
        XftColorFree(g_display, g_visual, g_colormap, &xft_color);

        // Draw suggestions
        y += char_h;
        for (int i = 0; i < term->suggestion_count && i < 10; i++) {
            if (i == term->fuzzy_selection) {
                // Highlight selected
                XSetForeground(g_display, gc, rgb_to_pixel(MOONFLY_BLUE));
                XFillRectangle(g_display, g_window, gc, 0, y - char_h + 4, box_width, char_h);
                render_color.red = 0xFFFF;
                render_color.green = 0xFFFF;
                render_color.blue = 0xFFFF;
            } else {
                render_color.red = ((MOONFLY_FG >> 16) & 0xFF) * 257;
                render_color.green = ((MOONFLY_FG >> 8) & 0xFF) * 257;
                render_color.blue = (MOONFLY_FG & 0xFF) * 257;
            }
            render_color.alpha = 0xFFFF;
            XftColorAllocValue(g_display, g_visual, g_colormap, &render_color, &xft_color);

            if (term->suggestions[i].text) {
                XftDrawStringUtf8(xft_draw, &xft_color, xft_font, char_w * 2, y,
                                 (XftChar8 *)term->suggestions[i].text, strlen(term->suggestions[i].text));
            }
            XftColorFree(g_display, g_visual, g_colormap, &xft_color);
            y += char_h;
        }
    } else
#endif
    {
        // Fallback to X11 bitmap fonts
        XFontStruct *font_struct = (XFontStruct *)renderer->x11_font_struct;
        XSetForeground(g_display, gc, rgb_to_pixel(MOONFLY_GREEN));
        XDrawString(g_display, g_window, gc, char_w, y, prompt, strlen(prompt));

        // Draw suggestions
        y += char_h;
        for (int i = 0; i < term->suggestion_count && i < 10; i++) {
            if (i == term->fuzzy_selection) {
                XSetForeground(g_display, gc, rgb_to_pixel(MOONFLY_BLUE));
                XFillRectangle(g_display, g_window, gc, 0, y - char_h + 4, box_width, char_h);
                XSetForeground(g_display, gc, rgb_to_pixel(0xFFFFFF));
            } else {
                XSetForeground(g_display, gc, rgb_to_pixel(MOONFLY_FG));
            }

            if (term->suggestions[i].text) {
                XDrawString(g_display, g_window, gc, char_w * 2, y,
                           term->suggestions[i].text, strlen(term->suggestions[i].text));
            }
            y += char_h;
        }
        (void)font_struct;  // Suppress unused warning
    }

    XFlush(g_display);
#endif
}

