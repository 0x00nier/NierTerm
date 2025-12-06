#include "fuzzy.h"
#include "terminal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>

static void fuzzy_search_history(Terminal *term);

void fuzzy_start(Terminal *term) {
    if (!term) return;
    
    term->fuzzy_finder_active = true;
    free(term->fuzzy_query);
    term->fuzzy_query = strdup("");
    term->fuzzy_selection = 0;
    
    fuzzy_search_history(term);
}

void fuzzy_stop(Terminal *term) {
    if (!term) return;
    
    term->fuzzy_finder_active = false;
    free(term->fuzzy_query);
    term->fuzzy_query = NULL;
    term->fuzzy_selection = 0;
}

void fuzzy_handle_key(Terminal *term, KeyEvent *event) {
    if (!term || !event) return;
    
    if (event->key == 27) { // ESC
        fuzzy_stop(term);
        return;
    }
    
    if (event->key == '\n' || event->key == '\r') {
        // Accept selection
        if (term->fuzzy_selection >= 0 && term->fuzzy_selection < term->suggestion_count) {
            Suggestion *sug = &term->suggestions[term->fuzzy_selection];
            // Write to terminal
            write(term->master_fd, sug->text, sug->length);
            write(term->master_fd, "\n", 1);
        }
        fuzzy_stop(term);
        return;
    }
    
    if (event->special_key == 1) { // Up arrow
        if (term->fuzzy_selection > 0) {
            term->fuzzy_selection--;
        }
        return;
    }
    
    if (event->special_key == 2) { // Down arrow
        if (term->fuzzy_selection < term->suggestion_count - 1) {
            term->fuzzy_selection++;
        }
        return;
    }
    
    if (event->key == '\b' || event->key == 127) {
        // Backspace
        if (term->fuzzy_query && strlen(term->fuzzy_query) > 0) {
            int len = strlen(term->fuzzy_query);
            term->fuzzy_query[len - 1] = '\0';
            fuzzy_search_history(term);
        }
        return;
    }
    
    if (event->unicode > 0 && event->unicode < 128 && isprint(event->unicode)) {
        // Add to query
        int len = term->fuzzy_query ? strlen(term->fuzzy_query) : 0;
        term->fuzzy_query = realloc(term->fuzzy_query, len + 2);
        term->fuzzy_query[len] = event->unicode;
        term->fuzzy_query[len + 1] = '\0';
        fuzzy_search_history(term);
    }
}

void fuzzy_update_query(Terminal *term, const char *query) {
    if (!term) return;
    
    free(term->fuzzy_query);
    term->fuzzy_query = strdup(query ? query : "");
    fuzzy_search_history(term);
}

int fuzzy_score(const char *str, const char *pattern) {
    if (!str || !pattern) return 0;
    
    int score = 0;
    int str_len = strlen(str);
    int pat_len = strlen(pattern);
    
    if (pat_len == 0) return 1000;
    
    // Exact match
    if (strcmp(str, pattern) == 0) return 10000;
    
    // Prefix match
    if (strncmp(str, pattern, pat_len) == 0) return 5000;
    
    // Case-insensitive prefix
    for (int i = 0; i < pat_len && i < str_len; i++) {
        if (tolower(str[i]) != tolower(pattern[i])) break;
        if (i == pat_len - 1) return 3000;
    }
    
    // Fuzzy match (pattern characters appear in order)
    int str_idx = 0;
    int pat_idx = 0;
    int consecutive = 0;
    int max_consecutive = 0;
    
    while (str_idx < str_len && pat_idx < pat_len) {
        if (tolower(str[str_idx]) == tolower(pattern[pat_idx])) {
            score += 10;
            if (str_idx > 0 && tolower(str[str_idx - 1]) == tolower(pattern[pat_idx - 1])) {
                consecutive++;
                max_consecutive = (consecutive > max_consecutive) ? consecutive : max_consecutive;
            } else {
                consecutive = 1;
            }
            pat_idx++;
        }
        str_idx++;
    }
    
    if (pat_idx == pat_len) {
        score += max_consecutive * 50;
        score += (str_len - pat_len) * -1; // Penalize longer strings
        return score;
    }
    
    return 0;
}

static void fuzzy_search_history(Terminal *term) {
    if (!term) return;
    
    // Clear suggestions
    for (int i = 0; i < term->suggestion_count; i++) {
        free(term->suggestions[i].text);
    }
    term->suggestion_count = 0;
    
    if (!term->fuzzy_query || strlen(term->fuzzy_query) == 0) {
        // Show all history
        for (int i = term->history_count - 1; i >= 0 && term->suggestion_count < MAX_SUGGESTIONS; i--) {
            term->suggestions[term->suggestion_count].text = strdup(term->history[i].command);
            term->suggestions[term->suggestion_count].length = term->history[i].length;
            term->suggestions[term->suggestion_count].score = 1000;
            term->suggestion_count++;
        }
        return;
    }
    
    // Score and sort history entries
    for (int i = term->history_count - 1; i >= 0; i--) {
        int score = fuzzy_score(term->history[i].command, term->fuzzy_query);
        if (score > 0) {
            // Insert sorted by score
            int insert_pos = term->suggestion_count;
            while (insert_pos > 0 && term->suggestions[insert_pos - 1].score < score) {
                insert_pos--;
            }
            
            if (term->suggestion_count < MAX_SUGGESTIONS) {
                // Shift
                for (int j = term->suggestion_count; j > insert_pos; j--) {
                    term->suggestions[j] = term->suggestions[j - 1];
                }
                
                term->suggestions[insert_pos].text = strdup(term->history[i].command);
                term->suggestions[insert_pos].length = term->history[i].length;
                term->suggestions[insert_pos].score = score;
                term->suggestion_count++;
            }
        }
    }
    
    if (term->fuzzy_selection >= term->suggestion_count) {
        term->fuzzy_selection = term->suggestion_count - 1;
    }
    if (term->fuzzy_selection < 0 && term->suggestion_count > 0) {
        term->fuzzy_selection = 0;
    }
}

