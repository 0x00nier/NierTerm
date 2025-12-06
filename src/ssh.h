#ifndef SSH_H
#define SSH_H

#include "terminal.h"

typedef struct {
    char *host;
    char *user;
    int port;
    char *key_path;
} SSHConfig;

int ssh_connect(SSHConfig *config, int *master_fd, pid_t *pid);
void ssh_disconnect(int master_fd, pid_t pid);

#endif

