#ifndef INPUT_H
#define INPUT_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

typedef struct {
    bool ctrl;
    bool alt;
    bool shift;
    bool super;
    char key;
    uint32_t unicode;
    bool is_special;
    int special_key; // Arrow keys, function keys, etc.
    int scroll_delta; // Mouse wheel: positive = up, negative = down
} KeyEvent;

typedef void (*KeyCallback)(KeyEvent *event, void *userdata);

struct InputHandler {
    KeyCallback callback;
    void *userdata;
    bool running;
    pthread_t thread;
    void *display;
    unsigned long window;
};

typedef struct InputHandler InputHandler;

InputHandler *input_create(KeyCallback callback, void *userdata);
void input_destroy(InputHandler *input);
void input_start(InputHandler *input);
void input_stop(InputHandler *input);
void input_set_window(InputHandler *input, void *display, void *window);

#endif

