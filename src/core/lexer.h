#ifndef FSH_CORE_LEXER_H
#define FSH_CORE_LEXER_H

#include "core/token.h"

TokenList lexer_tokenize(const char *input);
void lexer_free(TokenList *list);

#endif
