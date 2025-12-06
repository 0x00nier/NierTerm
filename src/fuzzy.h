#ifndef FUZZY_H
#define FUZZY_H

#include "terminal.h"
#include "input.h"

void fuzzy_start(Terminal *term);
void fuzzy_stop(Terminal *term);
void fuzzy_handle_key(Terminal *term, KeyEvent *event);
void fuzzy_update_query(Terminal *term, const char *query);
int fuzzy_score(const char *str, const char *pattern);

#endif

