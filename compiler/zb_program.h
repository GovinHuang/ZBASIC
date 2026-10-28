/* A compiled ZBasic program image (one BASIC file = one program / task). */
#ifndef ZB_PROGRAM_H
#define ZB_PROGRAM_H

#include "zb_common.h"
#include "zb_opcode.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_MAX_GLOBALS     4096
#define ZB_MAX_ARRAYS      64
#define ZB_MAX_PROCS       64
#define ZB_MAX_PARAMS      16
#define ZB_MAX_NAME        32
#define ZB_MAX_LOCALS      512

typedef struct {
    char     name[ZB_MAX_NAME];
    uint32_t entry;      /* code index */
    uint16_t nparams;
    uint16_t nlocals;
    uint8_t  is_function;
} ZbProc;

typedef struct {
    uint16_t base;       /* first global slot */
    uint16_t size;       /* element count */
} ZbArray;

typedef struct {
    char     name[ZB_MAX_NAME];
    ZbIns*   code;
    uint32_t code_count;
    uint32_t code_cap;
    double*  consts;
    uint32_t const_count;
    uint32_t const_cap;
    char**   strings;
    uint32_t str_count;
    uint32_t str_cap;
    uint16_t global_count;
    ZbArray  arrays[ZB_MAX_ARRAYS];
    uint16_t array_count;
    ZbProc   procs[ZB_MAX_PROCS];
    uint16_t proc_count;
    uint32_t entry;
} ZbProgram;

void zb_program_init(ZbProgram* prog, const char* name);
void zb_program_free(ZbProgram* prog);
int  zb_program_add_const(ZbProgram* prog, double value);
int  zb_program_add_string(ZbProgram* prog, const char* text);
int  zb_program_add_ins(ZbProgram* prog, int op, int a, uint32_t b);

#ifdef __cplusplus
}
#endif

#endif /* ZB_PROGRAM_H */
