/* ZBasic motion controller - common definitions
 * Pure C99, shared by the PC compiler, the virtual controller and the MFC IDE.
 */
#ifndef ZB_COMMON_H
#define ZB_COMMON_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_VERSION_MAJOR 1
#define ZB_VERSION_MINOR 0

/* A single compiler diagnostic. */
typedef struct {
    int  line;
    int  col;
    char message[256];
} ZbDiag;

typedef struct {
    ZbDiag* items;
    int     count;
    int     cap;
} ZbDiagList;

void zb_diag_init(ZbDiagList* list);
void zb_diag_free(ZbDiagList* list);
void zb_diag_add(ZbDiagList* list, int line, int col, const char* fmt, ...);
void zb_diag_addf(ZbDiagList* list, int line, int col, const char* fmt, ...);

/* Small portable string helpers (MSVC does not provide strdup / strcasecmp). */
char* zb_strdup(const char* s);
int   zb_stricmp(const char* a, const char* b);
int   zb_strnicmp(const char* a, const char* b, size_t n);
void  zb_strlcpy(char* dst, const char* src, size_t cap);
void  zb_strlcat(char* dst, const char* src, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* ZB_COMMON_H */
