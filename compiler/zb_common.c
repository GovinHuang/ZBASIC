#include "zb_common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

void zb_diag_init(ZbDiagList* list)
{
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void zb_diag_free(ZbDiagList* list)
{
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->cap = 0;
}

void zb_diag_add(ZbDiagList* list, int line, int col, const char* fmt, ...)
{
    va_list ap;
    ZbDiag* slot;

    if (!list) return;
    if (list->count >= list->cap) {
        int cap = list->cap ? list->cap * 2 : 16;
        ZbDiag* p = (ZbDiag*)realloc(list->items, (size_t)cap * sizeof(ZbDiag));
        if (!p) return;
        list->items = p;
        list->cap = cap;
    }
    slot = &list->items[list->count++];
    slot->line = line;
    slot->col = col;
    va_start(ap, fmt);
    vsnprintf(slot->message, sizeof(slot->message), fmt, ap);
    va_end(ap);
}

void zb_diag_addf(ZbDiagList* list, int line, int col, const char* fmt, ...)
{
    va_list ap;
    ZbDiag* slot;

    if (!list) return;
    if (list->count >= list->cap) {
        int cap = list->cap ? list->cap * 2 : 16;
        ZbDiag* p = (ZbDiag*)realloc(list->items, (size_t)cap * sizeof(ZbDiag));
        if (!p) return;
        list->items = p;
        list->cap = cap;
    }
    slot = &list->items[list->count++];
    slot->line = line;
    slot->col = col;
    va_start(ap, fmt);
    vsnprintf(slot->message, sizeof(slot->message), fmt, ap);
    va_end(ap);
}

char* zb_strdup(const char* s)
{
    size_t n;
    char* p;
    if (!s) return NULL;
    n = strlen(s) + 1;
    p = (char*)malloc(n);
    if (p) memcpy(p, s, n);
    return p;
}

int zb_stricmp(const char* a, const char* b)
{
    unsigned char ca, cb;
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    while (*a && *b) {
        ca = (unsigned char)*a;
        cb = (unsigned char)*b;
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
        ++a;
        ++b;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int zb_strnicmp(const char* a, const char* b, size_t n)
{
    unsigned char ca, cb;
    size_t i;
    if (!a) return b ? -1 : 0;
    if (!b) return 1;
    for (i = 0; i < n; ++i) {
        ca = (unsigned char)a[i];
        cb = (unsigned char)b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (unsigned char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (unsigned char)(cb - 'A' + 'a');
        if (ca != cb) return (int)ca - (int)cb;
        if (!ca) return 0;
    }
    return 0;
}

void zb_strlcpy(char* dst, const char* src, size_t cap)
{
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    while (--cap > 0 && *src) *dst++ = *src++;
    *dst = '\0';
}

void zb_strlcat(char* dst, const char* src, size_t cap)
{
    size_t len;
    if (!dst || !src || cap == 0) return;
    len = strlen(dst);
    if (len >= cap - 1) return;
    zb_strlcpy(dst + len, src, cap - len);
}
