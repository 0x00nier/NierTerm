#ifndef AUTOCOMPLETE_H
#define AUTOCOMPLETE_H

#include "terminal.h"

void autocomplete_update(Terminal *term, const char *line, int cursor_pos);
void autocomplete_next(Terminal *term);
void autocomplete_prev(Terminal *term);
void autocomplete_accept(Terminal *term);

#endif

