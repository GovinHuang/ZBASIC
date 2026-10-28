/* ZBasic 字节码编译器：把 BASIC 源码直接编译为栈式字节码。
 *
 * 支持的语法子集：
 *   IF/THEN/ELSE/ELSEIF/ENDIF（单行与块式）
 *   FOR/TO/STEP/NEXT、WHILE/WEND、REPEAT/UNTIL
 *   GOTO/GOSUB/RETURN、SUB/ENDSUB、FUNCTION/ENDFUNCTION、CALL、LOCAL
 *   DIM 一维数组、PRINT、WAIT UNTIL/IDLE、END/STOP
 *   运动指令与系统函数（见 zb_builtin.h）
 */
#ifndef ZB_COMPILER_H
#define ZB_COMPILER_H

#include "zb_common.h"
#include "zb_program.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 编译一个 BASIC 源文件（一个源文件即一个可独立运行的任务程序）。
 * 成功返回 1；失败返回 0 并向 diags 追加诊断信息，此时 out 内容已被释放。 */
int zb_compile(const char* program_name, const char* source,
               ZbProgram* out, ZbDiagList* diags);

#ifdef __cplusplus
}
#endif

#endif /* ZB_COMPILER_H */
