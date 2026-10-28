/* ZBasic bytecode instruction set.
 *
 * Every instruction is 8 bytes: opcode, flags, operand A, operand B.
 * Operand meaning depends on the opcode:
 *   PUSH_CONST  B = constant pool index
 *   PUSH_STR    B = string pool index
 *   LOAD_G/STORE_G  A = global slot
 *   LOAD_L/STORE_L  A = local slot
 *   LOAD_ARR/STORE_ARR  A = array table index
 *   JMP/JZ/JNZ  B = code index
 *   CALL        A = argument count, B = procedure table index
 *   CALLB       A = builtin id,     B = argument count
 */
#ifndef ZB_OPCODE_H
#define ZB_OPCODE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZB_OP_NOP = 0,
    ZB_OP_PUSH_CONST,
    ZB_OP_PUSH_STR,
    ZB_OP_LOAD_G,
    ZB_OP_STORE_G,
    ZB_OP_LOAD_L,
    ZB_OP_STORE_L,
    ZB_OP_LOAD_ARR,
    ZB_OP_STORE_ARR,
    ZB_OP_POP,
    ZB_OP_DUP,
    ZB_OP_ADD,
    ZB_OP_SUB,
    ZB_OP_MUL,
    ZB_OP_DIV,
    ZB_OP_MOD,
    ZB_OP_POW,
    ZB_OP_NEG,
    ZB_OP_EQ,
    ZB_OP_NE,
    ZB_OP_LT,
    ZB_OP_LE,
    ZB_OP_GT,
    ZB_OP_GE,
    ZB_OP_AND,
    ZB_OP_OR,
    ZB_OP_NOT,
    ZB_OP_XOR,
    ZB_OP_JMP,
    ZB_OP_JZ,
    ZB_OP_JNZ,
    ZB_OP_CALL,
    ZB_OP_RET,
    ZB_OP_RETF,
    ZB_OP_GOSUB,
    ZB_OP_RETSUB,
    ZB_OP_CALLB,
    ZB_OP_YIELD,
    ZB_OP_HALT,
    ZB_OP_COUNT
} ZbOpcode;

typedef struct {
    uint8_t  op;
    uint8_t  flags;
    uint16_t a;
    uint32_t b;
} ZbIns;

const char* zb_opcode_name(int op);
int         zb_opcode_has_operand(int op);

#ifdef __cplusplus
}
#endif

#endif /* ZB_OPCODE_H */
