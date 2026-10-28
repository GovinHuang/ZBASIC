#include "zb_lexer.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

static const char* kKeywords[] = {
    "if", "then", "else", "elseif", "endif", "end",
    "for", "to", "step", "next",
    "while", "wend", "repeat", "until",
    "goto", "gosub", "return",
    "sub", "endsub", "function", "endfunction",
    "call", "local", "dim", "and", "or", "not", "mod", "xor",
    "print", "wait", "idle", "run", "stop",
    "exit", "exitsub", "exitfunction"
};

static int zb_is_alpha(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int zb_is_digit(int c)
{
    return c >= '0' && c <= '9';
}

static int zb_is_alnum(int c)
{
    return zb_is_alpha(c) || zb_is_digit(c);
}

int zb_is_keyword(const char* text)
{
    int i;
    int n = (int)(sizeof(kKeywords) / sizeof(kKeywords[0]));
    if (!text) return 0;
    for (i = 0; i < n; ++i) {
        if (zb_stricmp(text, kKeywords[i]) == 0) return 1;
    }
    return 0;
}

int zb_token_is_keyword(const ZbToken* tok, const char* kw)
{
    return tok && tok->type == ZB_TK_KEYWORD && zb_stricmp(tok->text, kw) == 0;
}

int zb_token_is_op(const ZbToken* tok, const char* op)
{
    return tok && tok->type == ZB_TK_OP && strcmp(tok->text, op) == 0;
}

static ZbToken* zb_push(ZbTokenList* list, ZbDiagList* diags)
{
    ZbToken* tok;
    if (list->count >= list->cap) {
        int cap = list->cap ? list->cap * 2 : 256;
        ZbToken* p = (ZbToken*)realloc(list->toks, (size_t)cap * sizeof(ZbToken));
        if (!p) {
            zb_diag_add(diags, 0, 0, "out of memory while tokenizing");
            return NULL;
        }
        list->toks = p;
        list->cap = cap;
    }
    tok = &list->toks[list->count++];
    memset(tok, 0, sizeof(*tok));
    return tok;
}

void zb_token_list_free(ZbTokenList* list)
{
    if (!list) return;
    free(list->toks);
    list->toks = NULL;
    list->count = 0;
    list->cap = 0;
}

int zb_lex(const char* src, ZbTokenList* out, ZbDiagList* diags)
{
    const char* p;
    int line = 1;
    int col = 1;
    int ok = 1;

    memset(out, 0, sizeof(*out));
    if (!src) {
        ZbToken* t = zb_push(out, diags);
        if (t) { t->type = ZB_TK_EOF; }
        return 1;
    }

    p = src;
    while (*p) {
        unsigned char c = (unsigned char)*p;

        if (c == '\r') { ++p; continue; }
        if (c == '\n') {
            ZbToken* t = zb_push(out, diags);
            if (!t) { ok = 0; break; }
            t->type = ZB_TK_EOL;
            t->line = line;
            t->col = col;
            t->pos = (int)(p - src);
            t->len = 1;
            ++p;
            ++line;
            col = 1;
            continue;
        }
        if (c == ' ' || c == '\t') {
            col += (c == '\t') ? 4 : 1;
            ++p;
            continue;
        }
        if (c == '\'') {
            const char* start = p;
            ZbToken* t;
            ++p;
            ++col;
            while (*p && *p != '\n' && *p != '\r') { ++p; ++col; }
            t = zb_push(out, diags);
            if (!t) { ok = 0; break; }
            t->type = ZB_TK_COMMENT;
            t->line = line;
            t->col = col;
            t->pos = (int)(start - src);
            t->len = (int)(p - start);
            zb_strlcpy(t->text, "'", sizeof(t->text));
            continue;
        }
        if (c == '"') {
            const char* start = p;
            char buf[512];
            int n = 0;
            ZbToken* t;
            ++p;
            ++col;
            while (*p) {
                if (*p == '"') {
                    if (p[1] == '"') {
                        if (n < (int)sizeof(buf) - 1) buf[n++] = '"';
                        p += 2;
                        col += 2;
                        continue;
                    }
                    ++p;
                    ++col;
                    break;
                }
                if (*p == '\n' || *p == '\r') break;
                if (n < (int)sizeof(buf) - 1) buf[n++] = *p;
                ++p;
                ++col;
            }
            buf[n] = '\0';
            t = zb_push(out, diags);
            if (!t) { ok = 0; break; }
            t->type = ZB_TK_STRING;
            t->line = line;
            t->col = col;
            t->pos = (int)(start - src);
            t->len = (int)(p - start);
            zb_strlcpy(t->text, buf, sizeof(t->text));
            continue;
        }
        if (zb_is_digit(c) || (c == '.' && zb_is_digit((unsigned char)p[1]))) {
            const char* start = p;
            ZbToken* t;
            char buf[128];
            int n = 0;
            while (zb_is_digit((unsigned char)*p) || *p == '.') {
                if (n < (int)sizeof(buf) - 1) buf[n++] = *p;
                ++p;
                ++col;
            }
            if ((*p == 'e' || *p == 'E') &&
                (zb_is_digit((unsigned char)p[1]) ||
                 ((p[1] == '+' || p[1] == '-') && zb_is_digit((unsigned char)p[2])))) {
                if (n < (int)sizeof(buf) - 1) buf[n++] = *p;
                ++p;
                ++col;
                if (*p == '+' || *p == '-') {
                    if (n < (int)sizeof(buf) - 1) buf[n++] = *p;
                    ++p;
                    ++col;
                }
                while (zb_is_digit((unsigned char)*p)) {
                    if (n < (int)sizeof(buf) - 1) buf[n++] = *p;
                    ++p;
                    ++col;
                }
            }
            buf[n] = '\0';
            t = zb_push(out, diags);
            if (!t) { ok = 0; break; }
            t->type = ZB_TK_NUMBER;
            t->line = line;
            t->col = col;
            t->pos = (int)(start - src);
            t->len = (int)(p - start);
            t->num = strtod(buf, NULL);
            zb_strlcpy(t->text, buf, sizeof(t->text));
            continue;
        }
        if (zb_is_alpha(c)) {
            const char* start = p;
            char buf[64];
            int n = 0;
            int i;
            ZbToken* t;
            while (zb_is_alnum((unsigned char)*p)) {
                if (n < (int)sizeof(buf) - 1) {
                    char ch = *p;
                    if (ch >= 'A' && ch <= 'Z') ch = (char)(ch - 'A' + 'a');
                    buf[n++] = ch;
                }
                ++p;
                ++col;
            }
            if (*p == '$' || *p == '%') { ++p; ++col; }
            buf[n] = '\0';

            if (strcmp(buf, "rem") == 0) {
                const char* cstart = start;
                while (*p && *p != '\n' && *p != '\r') { ++p; ++col; }
                t = zb_push(out, diags);
                if (!t) { ok = 0; break; }
                t->type = ZB_TK_COMMENT;
                t->line = line;
                t->col = col;
                t->pos = (int)(cstart - src);
                t->len = (int)(p - cstart);
                zb_strlcpy(t->text, "rem", sizeof(t->text));
                continue;
            }

            t = zb_push(out, diags);
            if (!t) { ok = 0; break; }
            t->line = line;
            t->col = col;
            t->pos = (int)(start - src);
            t->len = (int)(p - start);
            zb_strlcpy(t->text, buf, sizeof(t->text));
            t->type = ZB_TK_IDENT;
            for (i = 0; i < (int)(sizeof(kKeywords) / sizeof(kKeywords[0])); ++i) {
                if (strcmp(buf, kKeywords[i]) == 0) {
                    t->type = ZB_TK_KEYWORD;
                    break;
                }
            }
            continue;
        }

        /* operators and separators */
        {
            const char* start = p;
            char op[3];
            int oplen = 1;
            ZbTokType type = ZB_TK_OP;
            op[0] = (char)c;
            op[1] = '\0';
            op[2] = '\0';

            if (c == '<' && (p[1] == '=' || p[1] == '>')) {
                op[1] = p[1];
                op[2] = '\0';
                oplen = 2;
            } else if (c == '>' && p[1] == '=') {
                op[1] = '=';
                op[2] = '\0';
                oplen = 2;
            }

            switch (c) {
            case '(': type = ZB_TK_LPAREN; break;
            case ')': type = ZB_TK_RPAREN; break;
            case ',': type = ZB_TK_COMMA; break;
            case ':': type = ZB_TK_COLON; break;
            case ';': type = ZB_TK_SEMICOLON; break;
            case '+': case '-': case '*': case '/': case '\\':
            case '^': case '&': case '|': case '~': case '=':
                type = ZB_TK_OP; break;
            case '<': case '>':
                type = ZB_TK_OP; break;
            case '?':
                type = ZB_TK_KEYWORD;
                zb_strlcpy(op, "print", sizeof(op));
                oplen = 1;
                break;
            default:
                zb_diag_add(diags, line, col, "unexpected character '%c' (0x%02X)", c, c);
                ok = 0;
                ++p;
                ++col;
                continue;
            }

            {
                ZbToken* t = zb_push(out, diags);
                if (!t) { ok = 0; break; }
                t->type = type;
                t->line = line;
                t->col = col;
                t->pos = (int)(start - src);
                t->len = oplen;
                zb_strlcpy(t->text, op, sizeof(t->text));
            }
            p += oplen;
            col += oplen;
        }
    }

    {
        ZbToken* t = zb_push(out, diags);
        if (t) {
            t->type = ZB_TK_EOF;
            t->line = line;
            t->col = col;
            t->pos = (int)(p - src);
        }
    }
    return ok;
}
