#include "autosuggest.h"
#include "terminal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fnmatch.h>

static int calculate_similarity(const char *line, const char *history);

void autosuggest_update(Terminal *term, const char *line) {
    if (!term || !line || strlen(line) == 0) {
        term->suggestion_count = 0;
        return;
    }
    
    // Clear previous suggestions
    for (int i = 0; i < term->suggestion_count; i++) {
        free(term->suggestions[i].text);
    }
    term->suggestion_count = 0;
    
    // Find best matching history entry
    int best_score = 0;
    char *best_match = NULL;
    
    for (int i = term->history_count - 1; i >= 0; i--) {
        if (strncmp(term->history[i].command, line, strlen(line)) == 0) {
            int score = calculate_similarity(line, term->history[i].command);
            if (score > best_score) {
                best_score = score;
                free(best_match);
                best_match = strdup(term->history[i].command);
            }
        }
    }
    
    if (best_match && strlen(best_match) > strlen(line)) {
        // Add suggestion
        term->suggestions[0].text = strdup(best_match + strlen(line));
        term->suggestions[0].length = strlen(best_match + strlen(line));
        term->suggestions[0].score = best_score;
        term->suggestion_count = 1;
    }
    
    free(best_match);
}

void autosuggest_accept(Terminal *term) {
    if (!term || term->suggestion_count == 0) return;
    
    Suggestion *sug = &term->suggestions[0];
    if (sug->text) {
        write(term->master_fd, sug->text, sug->length);
    }
    
    autosuggest_dismiss(term);
}

void autosuggest_dismiss(Terminal *term) {
    if (!term) return;
    
    for (int i = 0; i < term->suggestion_count; i++) {
        free(term->suggestions[i].text);
    }
    term->suggestion_count = 0;
}

static int calculate_similarity(const char *line, const char *history) {
    if (!line || !history) return 0;
    
    int line_len = strlen(line);
    int hist_len = strlen(history);
    
    if (hist_len < line_len) return 0;
    
    // Check prefix match
    if (strncmp(line, history, line_len) != 0) return 0;
    
    // Score based on recency and length match
    int score = 1000;
    
    // Prefer shorter completions
    score -= (hist_len - line_len) * 10;
    
    return score;
}

