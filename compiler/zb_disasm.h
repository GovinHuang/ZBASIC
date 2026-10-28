/* 字节码反汇编，用于 IDE 的“查看字节码”与命令行调试。 */
#ifndef ZB_DISASM_H
#define ZB_DISASM_H

#include "zb_common.h"
#include "zb_program.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

void zb_disasm_program(const ZbProgram* prog, FILE* out);

#ifdef __cplusplus
}
#endif

#endif /* ZB_DISASM_H */
