#include "zb_program.h"
#include <stdlib.h>
#include <string.h>

void zb_program_init(ZbProgram* prog, const char* name)
{
    if (!prog) return;
    memset(prog, 0, sizeof(*prog));
    zb_strlcpy(prog->name, name ? name : "program", sizeof(prog->name));
}

void zb_program_free(ZbProgram* prog)
{
    uint32_t i;
    if (!prog) return;
    free(prog->code);
    free(prog->consts);
    if (prog->strings) {
        for (i = 0; i < prog->str_count; ++i) free(prog->strings[i]);
        free(prog->strings);
    }
    memset(prog, 0, sizeof(*prog));
}

static int zb_grow(void** ptr, uint32_t* cap, uint32_t need, size_t elem_size)
{
    uint32_t newcap;
    void* p;
    if (need <= *cap) return 1;
    newcap = *cap ? *cap * 2 : 32;
    while (newcap < need) newcap *= 2;
    p = realloc(*ptr, (size_t)newcap * elem_size);
    if (!p) return 0;
    *ptr = p;
    *cap = newcap;
    return 1;
}

int zb_program_add_const(ZbProgram* prog, double value)
{
    if (!zb_grow((void**)&prog->consts, &prog->const_cap,
                 prog->const_count + 1, sizeof(double)))
        return -1;
    prog->consts[prog->const_count] = value;
    return (int)prog->const_count++;
}

int zb_program_add_string(ZbProgram* prog, const char* text)
{
    uint32_t i;
    char* copy;

    for (i = 0; i < prog->str_count; ++i) {
        if (strcmp(prog->strings[i], text) == 0)
            return (int)i;
    }
    if (!zb_grow((void**)&prog->strings, &prog->str_cap,
                 prog->str_count + 1, sizeof(char*)))
        return -1;
    copy = zb_strdup(text);
    if (!copy) return -1;
    prog->strings[prog->str_count] = copy;
    return (int)prog->str_count++;
}

int zb_program_add_ins(ZbProgram* prog, int op, int a, uint32_t b)
{
    ZbIns* ins;
    if (!zb_grow((void**)&prog->code, &prog->code_cap,
                 prog->code_count + 1, sizeof(ZbIns)))
        return -1;
    ins = &prog->code[prog->code_count];
    ins->op = (uint8_t)op;
    ins->flags = 0;
    ins->a = (uint16_t)a;
    ins->b = b;
    return (int)prog->code_count++;
}
