#include "terminal.h"
#include "renderer.h"
#include "input.h"
#include "pty.h"
#include "config.h"
#include <signal.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

static Terminal *g_terminal = NULL;

static void cleanup_handler(int sig) {
    (void)sig;
    if (g_terminal) {
        terminal_destroy(g_terminal);
    }
    exit(0);
}

static void resize_handler(int sig) {
    (void)sig;
    if (g_terminal) {
        // Get new terminal size
        struct winsize ws;
        if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
            int rows = ws.ws_row > 0 ? ws.ws_row : 24;
            int cols = ws.ws_col > 0 ? ws.ws_col : 80;
            terminal_resize(g_terminal, rows, cols);
        }
    }
}

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    fprintf(stderr, "[DEBUG] main: Starting NierTerm\n");

    // Setup signal handlers
    signal(SIGINT, cleanup_handler);
    signal(SIGTERM, cleanup_handler);
    signal(SIGWINCH, resize_handler);  // Resize instead of exit
    fprintf(stderr, "[DEBUG] main: Signal handlers set\n");
    
    // Initialize terminal
    fprintf(stderr, "[DEBUG] main: Creating terminal...\n");
    g_terminal = terminal_create();
    if (!g_terminal) {
        fprintf(stderr, "[ERROR] main: Failed to create terminal\n");
        return 1;
    }
    fprintf(stderr, "[DEBUG] main: Terminal created successfully\n");
    
    // Main event loop
    fprintf(stderr, "[DEBUG] main: Starting terminal run loop\n");
    terminal_run(g_terminal);
    fprintf(stderr, "[DEBUG] main: Terminal run loop exited\n");
    
    // Cleanup
    fprintf(stderr, "[DEBUG] main: Cleaning up terminal\n");
    terminal_destroy(g_terminal);
    fprintf(stderr, "[DEBUG] main: Exiting\n");
    return 0;
}

