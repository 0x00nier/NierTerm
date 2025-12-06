#include "ssh.h"
#include "pty.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <signal.h>

#ifdef LINUX
#include <pty.h>
#elif defined(MACOS)
#include <util.h>
#endif

int ssh_connect(SSHConfig *config, int *master_fd, pid_t *pid) {
    if (!config || !master_fd || !pid) return -1;
    
    PTY *pty = pty_create();
    if (!pty) return -1;
    
    // Create PTY for SSH session
    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
        pty_destroy(pty);
        return -1;
    }
    
    pid_t child_pid = fork();
    if (child_pid < 0) {
        close(master);
        close(slave);
        pty_destroy(pty);
        return -1;
    }
    
    if (child_pid == 0) {
        // Child: run SSH
        close(master);
        dup2(slave, STDIN_FILENO);
        dup2(slave, STDOUT_FILENO);
        dup2(slave, STDERR_FILENO);
        if (slave > 2) close(slave);
        
        // Build SSH command
        char port_str[16];
        snprintf(port_str, sizeof(port_str), "%d", config->port > 0 ? config->port : 22);
        
        // SSH arguments
        char *ssh_args[] = {
            "ssh",
            "-o", "StrictHostKeyChecking=no",
            "-o", "UserKnownHostsFile=/dev/null",
            "-p", port_str,
            NULL, NULL, NULL, NULL
        };
        
        int arg_idx = 6;
        
        // Add key if specified
        if (config->key_path) {
            ssh_args[arg_idx++] = "-i";
            ssh_args[arg_idx++] = config->key_path;
        }
        
        // Add user@host
        char userhost[512];
        if (config->user) {
            snprintf(userhost, sizeof(userhost), "%s@%s", config->user, config->host);
        } else {
            strncpy(userhost, config->host, sizeof(userhost) - 1);
        }
        ssh_args[arg_idx++] = userhost;
        ssh_args[arg_idx] = NULL;
        
        execvp("ssh", ssh_args);
        _exit(1);
    }
    
    // Parent
    close(slave);
    *master_fd = master;
    *pid = child_pid;
    
    // Set non-blocking
    int flags = fcntl(master, F_GETFL);
    fcntl(master, F_SETFL, flags | O_NONBLOCK);
    
    pty_destroy(pty);
    return 0;
}

void ssh_disconnect(int master_fd, pid_t pid) {
    if (master_fd >= 0) {
        close(master_fd);
    }
    if (pid > 0) {
        kill(pid, SIGHUP);
        waitpid(pid, NULL, 0);
    }
}

