#include "autocomplete.h"
#include "terminal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fnmatch.h>

static void find_completions(Terminal *term, const char *prefix, const char *path);

void autocomplete_update(Terminal *term, const char *line, int cursor_pos) {
    if (!term || !line) return;
    
    // Clear previous suggestions
    for (int i = 0; i < term->suggestion_count; i++) {
        free(term->suggestions[i].text);
    }
    term->suggestion_count = 0;
    
    // Find the word at cursor
    int start = cursor_pos;
    while (start > 0 && line[start - 1] != ' ' && line[start - 1] != '\t') {
        start--;
    }
    
    int len = cursor_pos - start;
    if (len == 0) return;
    
    char *prefix = malloc(len + 1);
    strncpy(prefix, line + start, len);
    prefix[len] = '\0';
    
    // Try to complete as command first
    const char *path = getenv("PATH");
    if (path) {
        char *path_copy = strdup(path);
        char *dir = strtok(path_copy, ":");
        while (dir && term->suggestion_count < MAX_SUGGESTIONS) {
            find_completions(term, prefix, dir);
            dir = strtok(NULL, ":");
        }
        free(path_copy);
    }
    
    // Try current directory
    find_completions(term, prefix, ".");
    
    free(prefix);
    term->autocomplete_index = 0;
    term->autocomplete_active = (term->suggestion_count > 0);
}

void autocomplete_next(Terminal *term) {
    if (!term || !term->autocomplete_active || term->suggestion_count == 0) return;
    
    term->autocomplete_index = (term->autocomplete_index + 1) % term->suggestion_count;
}

void autocomplete_prev(Terminal *term) {
    if (!term || !term->autocomplete_active || term->suggestion_count == 0) return;
    
    term->autocomplete_index = (term->autocomplete_index - 1 + term->suggestion_count) % term->suggestion_count;
}

void autocomplete_accept(Terminal *term) {
    if (!term || !term->autocomplete_active || term->suggestion_count == 0) return;
    
    if (term->autocomplete_index >= 0 && term->autocomplete_index < term->suggestion_count) {
        // Replace current word with suggestion
        // This would be handled by the input system
        (void)term->suggestions[term->autocomplete_index]; // Suppress unused warning
    }
    
    term->autocomplete_active = false;
}

static void find_completions(Terminal *term, const char *prefix, const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return;
    
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && term->suggestion_count < MAX_SUGGESTIONS) {
        if (strncmp(prefix, entry->d_name, strlen(prefix)) == 0) {
            
            // Check if executable
            char full_path[1024];
            snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
            struct stat st;
            if (stat(full_path, &st) == 0 && (st.st_mode & S_IXUSR)) {
                term->suggestions[term->suggestion_count].text = strdup(entry->d_name);
                term->suggestions[term->suggestion_count].length = strlen(entry->d_name);
                term->suggestion_count++;
            }
        }
    }
    
    closedir(dir);
}

