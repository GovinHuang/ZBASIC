/* ZBasic tokenizer.
 * Shared by the bytecode compiler and the IDE syntax highlighter.
 */
#ifndef ZB_LEXER_H
#define ZB_LEXER_H

#include "zb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZB_TK_EOF = 0,
    ZB_TK_EOL,
    ZB_TK_NUMBER,
    ZB_TK_STRING,
    ZB_TK_IDENT,
    ZB_TK_KEYWORD,
    ZB_TK_OP,
    ZB_TK_COLON,
    ZB_TK_COMMA,
    ZB_TK_LPAREN,
    ZB_TK_RPAREN,
    ZB_TK_SEMICOLON,
    ZB_TK_COMMENT
} ZbTokType;

typedef struct {
    ZbTokType type;
    int       line;
    int       col;
    int       pos;      /* byte offset in source */
    int       len;      /* byte length in source */
    double    num;
    char      text[512];/* normalized token text (decoded string / lowercase ident) */
} ZbToken;

typedef struct {
    ZbToken* toks;
    int      count;
    int      cap;
} ZbTokenList;

int  zb_lex(const char* source, ZbTokenList* out, ZbDiagList* diags);
void zb_token_list_free(ZbTokenList* list);

int  zb_is_keyword(const char* text);
int  zb_token_is_keyword(const ZbToken* tok, const char* kw);
int  zb_token_is_op(const ZbToken* tok, const char* op);

#ifdef __cplusplus
}
#endif

#endif /* ZB_LEXER_H */
