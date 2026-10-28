#include "zb_opcode.h"

static const char* kOpNames[ZB_OP_COUNT] = {
    "NOP",
    "PUSH_CONST",
    "PUSH_STR",
    "LOAD_G",
    "STORE_G",
    "LOAD_L",
    "STORE_L",
    "LOAD_ARR",
    "STORE_ARR",
    "POP",
    "DUP",
    "ADD",
    "SUB",
    "MUL",
    "DIV",
    "MOD",
    "POW",
    "NEG",
    "EQ",
    "NE",
    "LT",
    "LE",
    "GT",
    "GE",
    "AND",
    "OR",
    "NOT",
    "XOR",
    "JMP",
    "JZ",
    "JNZ",
    "CALL",
    "RET",
    "RETF",
    "GOSUB",
    "RETSUB",
    "CALLB",
    "YIELD",
    "HALT"
};

const char* zb_opcode_name(int op)
{
    if (op < 0 || op >= ZB_OP_COUNT) return "???";
    return kOpNames[op];
}

int zb_opcode_has_operand(int op)
{
    switch (op) {
    case ZB_OP_PUSH_CONST:
    case ZB_OP_PUSH_STR:
    case ZB_OP_LOAD_G:
    case ZB_OP_STORE_G:
    case ZB_OP_LOAD_L:
    case ZB_OP_STORE_L:
    case ZB_OP_LOAD_ARR:
    case ZB_OP_STORE_ARR:
    case ZB_OP_JMP:
    case ZB_OP_JZ:
    case ZB_OP_JNZ:
    case ZB_OP_CALL:
    case ZB_OP_GOSUB:
    case ZB_OP_CALLB:
        return 1;
    default:
        return 0;
    }
}
