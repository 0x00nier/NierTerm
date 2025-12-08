#include "pty.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <signal.h>

#ifdef LINUX
#include <pty.h>
#elif defined(MACOS)
#include <util.h>
#endif

PTY *pty_create(void) {
    PTY *pty = calloc(1, sizeof(PTY));
    if (!pty) return NULL;
    
    pty->master_fd = -1;
    pty->slave_fd = -1;
    pty->pid = -1;
    
    return pty;
}

void pty_destroy(PTY *pty) {
    if (!pty) return;
    
    if (pty->master_fd >= 0) {
        close(pty->master_fd);
    }
    if (pty->slave_fd >= 0) {
        close(pty->slave_fd);
    }
    if (pty->pid > 0) {
        kill(pty->pid, SIGHUP);
    }
    
    free(pty);
}

int pty_resize(PTY *pty, int rows, int cols) {
    if (!pty || pty->master_fd < 0) return -1;
    
    struct winsize ws;
    ws.ws_row = rows;
    ws.ws_col = cols;
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    
    if (ioctl(pty->master_fd, TIOCSWINSZ, &ws) < 0) {
        return -1;
    }
    
    return 0;
}

ssize_t pty_read(PTY *pty, void *buf, size_t count) {
    if (!pty || pty->master_fd < 0) return -1;
    return read(pty->master_fd, buf, count);
}

ssize_t pty_write(PTY *pty, const void *buf, size_t count) {
    if (!pty || pty->master_fd < 0) return -1;
    return write(pty->master_fd, buf, count);
}

int pty_spawn_shell(PTY *pty) {
    if (!pty) return -1;
    
    pid_t pid;
    int master_fd, slave_fd;
    
    // Open PTY
    if (openpty(&master_fd, &slave_fd, NULL, NULL, NULL) < 0) {
        return -1;
    }
    
    // Fork
    pid = fork();
    if (pid < 0) {
        close(master_fd);
        close(slave_fd);
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        close(master_fd);
        
        // Make slave the controlling terminal
        setsid();
        if (ioctl(slave_fd, TIOCSCTTY, NULL) < 0) {
            // Ignore error
        }
        
        // Set up terminal attributes for proper readline support
        struct termios tios;
        tcgetattr(slave_fd, &tios);

        // Input flags: Enable ICRNL for Enter key, disable other processing
        tios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR);
        tios.c_iflag |= ICRNL | IXON;

        // Output flags: enable post-processing
        tios.c_oflag |= OPOST | ONLCR;

        // Local flags: Enable canonical mode, echo, and signal handling for readline
        // This allows bash's readline to work properly
        tios.c_lflag |= ICANON | ECHO | ECHOE | ECHOK | ISIG | IEXTEN;

        // Control flags: 8-bit chars
        tios.c_cflag &= ~(CSIZE | PARENB);
        tios.c_cflag |= CS8;

        tcsetattr(slave_fd, TCSANOW, &tios);
        
        // Redirect stdio to slave
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        
        if (slave_fd > 2) close(slave_fd);

        // Spawn bash with embedded bashrc (no ble.sh)
        const char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/bash";

        // Get path to executable directory using /proc/self/exe
        char exe_path[1024] = {0};
        char rcfile[2048] = {0};
        ssize_t exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);

        if (exe_len > 0) {
            exe_path[exe_len] = '\0';
            // Find last slash and truncate to get directory
            char *last_slash = strrchr(exe_path, '/');
            if (last_slash) {
                *last_slash = '\0';
                snprintf(rcfile, sizeof(rcfile), "%s/default_bashrc", exe_path);
            }
        }

        // Fallback paths if /proc/self/exe didn't work
        if (rcfile[0] == '\0') {
            // Try current working directory
            char cwd_buf[1024];
            if (getcwd(cwd_buf, sizeof(cwd_buf))) {
                snprintf(rcfile, sizeof(rcfile), "%s/default_bashrc", cwd_buf);
            } else {
                snprintf(rcfile, sizeof(rcfile), "./default_bashrc");
            }
        }

        // Set TERM for colors
        setenv("TERM", "xterm-256color", 1);

        // Set PS1 with abbreviated path
        setenv("PS1", "\\[\\033[1;32m\\]\\u@\\h\\[\\033[0m\\]:\\[\\033[1;34m\\]\\W\\[\\033[0m\\]\\$ ", 1);

        // Check if rcfile exists before using it
        if (access(rcfile, R_OK) == 0) {
            execl(shell, shell, "--rcfile", rcfile, "-i", NULL);
        }

        // Fallback - use default bashrc with our PS1
        execl(shell, shell, "-i", NULL);
        _exit(1);
    }
    
    // Parent process
    close(slave_fd);
    pty->master_fd = master_fd;
    pty->pid = pid;
    
    // Set non-blocking
    int flags = fcntl(master_fd, F_GETFL);
    fcntl(master_fd, F_SETFL, flags | O_NONBLOCK);
    
    return 0;
}

