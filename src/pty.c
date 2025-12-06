#include "pty.h"
#include <stdlib.h>
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
        
        // Set up terminal attributes
        struct termios tios;
        tcgetattr(slave_fd, &tios);
        tios.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
        tios.c_oflag &= ~OPOST;
        tios.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
        tios.c_cflag &= ~(CSIZE | PARENB);
        tios.c_cflag |= CS8;
        tios.c_cc[VMIN] = 1;
        tios.c_cc[VTIME] = 0;
        tcsetattr(slave_fd, TCSANOW, &tios);
        
        // Redirect stdio to slave
        dup2(slave_fd, STDIN_FILENO);
        dup2(slave_fd, STDOUT_FILENO);
        dup2(slave_fd, STDERR_FILENO);
        
        if (slave_fd > 2) close(slave_fd);
        
        // Spawn bash
        const char *shell = getenv("SHELL");
        if (!shell) shell = "/bin/bash";
        
        execl(shell, shell, NULL);
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

