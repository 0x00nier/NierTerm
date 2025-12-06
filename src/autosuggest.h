#ifndef AUTOSUGGEST_H
#define AUTOSUGGEST_H

#include "terminal.h"

void autosuggest_update(Terminal *term, const char *line);
void autosuggest_accept(Terminal *term);
void autosuggest_dismiss(Terminal *term);

#endif

