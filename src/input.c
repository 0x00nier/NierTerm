#include "input.h"
#include "terminal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <unistd.h>

#ifdef LINUX
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/XKBlib.h>
#elif defined(MACOS)
#include <ApplicationServices/ApplicationServices.h>
#endif

static void *input_thread_func(void *arg);

InputHandler *input_create(KeyCallback callback, void *userdata) {
    InputHandler *input = calloc(1, sizeof(InputHandler));
    if (!input) return NULL;
    
    input->callback = callback;
    input->userdata = userdata;
    input->running = false;
    input->display = NULL;
    input->window = 0;
    
    return input;
}

void input_destroy(InputHandler *input) {
    if (!input) return;
    
    input_stop(input);
    free(input);
}

void input_start(InputHandler *input) {
    if (!input) {
        fprintf(stderr, "[ERROR] input_start: input is NULL\n");
        return;
    }
    if (input->running) {
        fprintf(stderr, "[WARN] input_start: Input already running\n");
        return;
    }
    fprintf(stderr, "[DEBUG] input_start: Starting input thread\n");
    fprintf(stderr, "[DEBUG] input_start: display=%p, window=%lu\n", input->display, input->window);
    input->running = true;
    int ret = pthread_create(&input->thread, NULL, input_thread_func, input);
    if (ret != 0) {
        fprintf(stderr, "[ERROR] input_start: Failed to create thread: %d\n", ret);
        input->running = false;
    } else {
        fprintf(stderr, "[DEBUG] input_start: Thread created successfully\n");
    }
}

void input_stop(InputHandler *input) {
    if (!input || !input->running) return;
    
    input->running = false;
    pthread_join(input->thread, NULL);
}

void input_set_window(InputHandler *input, void *display, void *window) {
    if (!input) return;
    input->display = display;
    input->window = (unsigned long)(uintptr_t)window;
}

static void *input_thread_func(void *arg) {
    fprintf(stderr, "[DEBUG] input_thread_func: Starting\n");
    InputHandler *input = (InputHandler *)arg;
    if (!input) {
        fprintf(stderr, "[ERROR] input_thread_func: input is NULL\n");
        return NULL;
    }
    
#ifdef LINUX
    fprintf(stderr, "[DEBUG] input_thread_func: Getting display and window\n");
    Display *display = (Display *)input->display;
    if (!display) {
        fprintf(stderr, "[DEBUG] input_thread_func: display is NULL, waiting...\n");
        // Wait a bit for display to be set
        for (int i = 0; i < 100 && !display; i++) {
            usleep(10000);
            display = (Display *)input->display;
        }
        if (!display) {
            fprintf(stderr, "[ERROR] input_thread_func: display still NULL after waiting\n");
            return NULL;
        }
    }
    fprintf(stderr, "[DEBUG] input_thread_func: display=%p\n", display);
    
    Window window = (Window)input->window;
    if (!window) {
        fprintf(stderr, "[DEBUG] input_thread_func: window is NULL, waiting...\n");
        // Wait a bit for window to be set
        for (int i = 0; i < 100 && !window; i++) {
            usleep(10000);
            window = (Window)input->window;
        }
        if (!window) {
            fprintf(stderr, "[ERROR] input_thread_func: window still NULL after waiting (window=%lu)\n", input->window);
            return NULL;
        }
    }
    fprintf(stderr, "[DEBUG] input_thread_func: window=%lu\n", window);
    
    fprintf(stderr, "[DEBUG] input_thread_func: Selecting input events\n");
    XSelectInput(display, window, KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask | FocusChangeMask);
    fprintf(stderr, "[DEBUG] input_thread_func: Entering event loop\n");

    XEvent event;
    KeyEvent key_event;

    int event_count = 0;
    while (input->running) {
        // Use non-blocking event check
        if (!XPending(display)) {
            usleep(1000); // Sleep 1ms if no events (fast response)
            continue;
        }

        XNextEvent(display, &event);
        event_count++;

        if (event.type == ButtonPress || event.type == ButtonRelease) {
            memset(&key_event, 0, sizeof(KeyEvent));
            key_event.ctrl = (event.xbutton.state & ControlMask) != 0;
            key_event.alt = (event.xbutton.state & Mod1Mask) != 0;
            key_event.shift = (event.xbutton.state & ShiftMask) != 0;

            // Handle mouse wheel (button 4 = scroll up, button 5 = scroll down)
            if (event.xbutton.button == 4 || event.xbutton.button == 5) {
                key_event.scroll_delta = (event.xbutton.button == 4) ? 1 : -1;
                if (input->callback) {
                    input->callback(&key_event, input->userdata);
                }
            } else if (event.xbutton.button == 1) {
                // Left mouse button - for text selection
                key_event.is_mouse = true;
                key_event.mouse_button = 1;
                key_event.mouse_pressed = (event.type == ButtonPress);
                key_event.mouse_x = event.xbutton.x;
                key_event.mouse_y = event.xbutton.y;
                if (input->callback) {
                    input->callback(&key_event, input->userdata);
                }
            }
        } else if (event.type == MotionNotify) {
            // Mouse motion - for drag selection
            // Only report if button 1 is held
            if (event.xmotion.state & Button1Mask) {
                memset(&key_event, 0, sizeof(KeyEvent));
                key_event.is_mouse = true;
                key_event.mouse_button = 1;
                key_event.mouse_motion = true;
                key_event.mouse_x = event.xmotion.x;
                key_event.mouse_y = event.xmotion.y;
                if (input->callback) {
                    input->callback(&key_event, input->userdata);
                }
            }
        } else if (event.type == KeyPress || event.type == KeyRelease) {
            memset(&key_event, 0, sizeof(KeyEvent));

            // Use XLookupString to properly handle shift and other modifiers
            char buf[32];
            KeySym keysym;
            int len = XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);

            // Modifiers
            key_event.ctrl = (event.xkey.state & ControlMask) != 0;
            key_event.alt = (event.xkey.state & Mod1Mask) != 0;
            key_event.shift = (event.xkey.state & ShiftMask) != 0;
            key_event.super = (event.xkey.state & Mod4Mask) != 0;

            // Special keys (use keysym for these)
            if (keysym == XK_Up) {
                key_event.special_key = 1;
                key_event.is_special = true;
            } else if (keysym == XK_Down) {
                key_event.special_key = 2;
                key_event.is_special = true;
            } else if (keysym == XK_Left) {
                key_event.special_key = 3;
                key_event.is_special = true;
            } else if (keysym == XK_Right) {
                key_event.special_key = 4;
                key_event.is_special = true;
            } else if (keysym == XK_Return || keysym == XK_KP_Enter) {
                key_event.key = '\n';
            } else if (keysym == XK_BackSpace) {
                key_event.key = '\b';
            } else if (keysym == XK_Tab) {
                key_event.key = '\t';
            } else if (keysym == XK_Escape) {
                key_event.key = 27;
            } else if (len > 0 && buf[0] >= 32 && buf[0] < 127) {
                // Use the actual character from XLookupString (handles shift!)
                key_event.key = buf[0];
                key_event.unicode = (unsigned char)buf[0];
            }

            if (event.type == KeyPress && input->callback) {
                input->callback(&key_event, input->userdata);
            }
        }
    }
    
    // Don't close display - it's owned by renderer
    fprintf(stderr, "[DEBUG] input_thread_func: Exiting event loop\n");
    
#elif defined(MACOS)
    // macOS input handling would go here
    // Would use CGEventTap or NSEvent monitoring
#endif
    
    fprintf(stderr, "[DEBUG] input_thread_func: Thread exiting\n");
    return NULL;
}

