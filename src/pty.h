#ifndef PTY_H
#define PTY_H

#include <sys/types.h>

typedef struct {
    int master_fd;
    int slave_fd;
    pid_t pid;
} PTY;

PTY *pty_create(void);
void pty_destroy(PTY *pty);
int pty_resize(PTY *pty, int rows, int cols);
ssize_t pty_read(PTY *pty, void *buf, size_t count);
ssize_t pty_write(PTY *pty, const void *buf, size_t count);
int pty_spawn_shell(PTY *pty);

#endif

