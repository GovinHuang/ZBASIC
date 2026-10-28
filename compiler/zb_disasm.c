#include "zb_disasm.h"
#include "zb_opcode.h"
#include "zb_builtin.h"
#include <stdio.h>

static const char* zb_reg_type(const ZbProgram* prog, uint16_t slot)
{
    uint16_t i;
    for (i = 0; i < prog->array_count; ++i) {
        uint16_t base = prog->arrays[i].base;
        uint16_t size = prog->arrays[i].size;
        if (slot >= base && slot < base + size) return "arr";
    }
    return "g";
}

void zb_disasm_program(const ZbProgram* prog, FILE* out)
{
    uint32_t i;

    fprintf(out, "=== %s ===\n", prog->name);
    fprintf(out, "entry=%u  globals=%u  arrays=%u  procs=%u\n\n",
            prog->entry, prog->global_count, prog->array_count, prog->proc_count);

    for (i = 0; i < prog->proc_count; ++i) {
        const ZbProc* p = &prog->procs[i];
        fprintf(out, "PROC %s (entry=%u, params=%u, locals=%u, %s)\n",
                p->name, p->entry, p->nparams, p->nlocals,
                p->is_function ? "function" : "sub");
    }
    if (prog->proc_count) fprintf(out, "\n");

    fprintf(out, "-- Constants (%u) --\n", prog->const_count);
    for (i = 0; i < prog->const_count; ++i)
        fprintf(out, "  [%u] = %g\n", i, prog->consts[i]);
    fprintf(out, "\n");

    fprintf(out, "-- Strings (%u) --\n", prog->str_count);
    for (i = 0; i < prog->str_count; ++i)
        fprintf(out, "  [%u] = \"%s\"\n", i, prog->strings[i]);
    fprintf(out, "\n");

    fprintf(out, "-- Arrays (%u) --\n", prog->array_count);
    for (i = 0; i < prog->array_count; ++i)
        fprintf(out, "  [%u] base=%u size=%u\n",
                i, prog->arrays[i].base, prog->arrays[i].size);
    fprintf(out, "\n");

    fprintf(out, "-- Code (%u instructions) --\n", prog->code_count);
    for (i = 0; i < prog->code_count; ++i) {
        const ZbIns* ins = &prog->code[i];
        const char* name = zb_opcode_name((int)ins->op);
        char mark = ' ';

        if (i == prog->entry) mark = '>';
        else {
            uint32_t j;
            for (j = 0; j < prog->proc_count; ++j) {
                if (i == prog->procs[j].entry) { mark = '#'; break; }
            }
        }
        fprintf(out, "%04X %c ", i, mark);

        switch (ins->op) {
        case ZB_OP_PUSH_CONST:
            if (ins->b < prog->const_count)
                fprintf(out, "PUSH_CONST [%u] %g", ins->b, prog->consts[ins->b]);
            else
                fprintf(out, "PUSH_CONST [%u] ???", ins->b);
            break;
        case ZB_OP_PUSH_STR:
            if (ins->b < prog->str_count)
                fprintf(out, "PUSH_STR [%u] \"%s\"", ins->b, prog->strings[ins->b]);
            else
                fprintf(out, "PUSH_STR [%u] ???", ins->b);
            break;
        case ZB_OP_LOAD_G:
            fprintf(out, "LOAD_G  %s[%u]", zb_reg_type(prog, ins->a), ins->a);
            break;
        case ZB_OP_STORE_G:
            fprintf(out, "STORE_G %s[%u]", zb_reg_type(prog, ins->a), ins->a);
            break;
        case ZB_OP_LOAD_L:
            fprintf(out, "LOAD_L  [%u]", ins->a);
            break;
        case ZB_OP_STORE_L:
            fprintf(out, "STORE_L [%u]", ins->a);
            break;
        case ZB_OP_LOAD_ARR:
            fprintf(out, "LOAD_ARR [%u]", ins->a);
            break;
        case ZB_OP_STORE_ARR:
            fprintf(out, "STORE_ARR [%u]", ins->a);
            break;
        case ZB_OP_JMP:
        case ZB_OP_JZ:
        case ZB_OP_JNZ:
        case ZB_OP_GOSUB:
            fprintf(out, "%-8s ->%04X", name, ins->b);
            break;
        case ZB_OP_CALL:
            if (ins->b < prog->proc_count)
                fprintf(out, "CALL    %s (%u args)",
                        prog->procs[ins->b].name, ins->a);
            else
                fprintf(out, "CALL    [proc %u] (%u args)", ins->b, ins->a);
            break;
        case ZB_OP_CALLB: {
            const ZbBuiltin* b = zb_builtin_by_id((int)ins->a);
            fprintf(out, "CALLB   %s (%u args)",
                    b ? b->name : zb_opcode_name((int)ins->op), ins->b);
            break;
        }
        default:
            if (zb_opcode_has_operand((int)ins->op))
                fprintf(out, "%-8s a=%u b=%u", name, ins->a, ins->b);
            else
                fprintf(out, "%s", name);
            break;
        }
        fprintf(out, "\n");
    }
}
