#include "zb_builtin.h"
#include "zb_common.h"

static const ZbBuiltin kBuiltins[] = {
    { ZB_B_PRINT,         "print",         0, -1, 0 },
    { ZB_B_MOVE,          "move",          1, 8,  0 },
    { ZB_B_MOVEABS,       "moveabs",       1, 8,  0 },
    { ZB_B_MOVELINEAR,    "movelinear",    2, 8,  0 },
    { ZB_B_MOVEABSLINEAR, "moveabslinear", 2, 8,  0 },
    { ZB_B_MOVECIRC,      "movecirc",      5, 5,  0 },
    { ZB_B_MOVEABSCIRC,   "moveabscirc",   5, 5,  0 },
    { ZB_B_SPEED,         "speed",         1, 2,  1 },
    { ZB_B_ACCEL,         "accel",         1, 2,  1 },
    { ZB_B_DECEL,         "decel",         1, 2,  1 },
    { ZB_B_UNITS,         "units",         2, 2,  0 },
    { ZB_B_ATYPE,         "atype",         2, 2,  0 },
    { ZB_B_DPOS,          "dpos",          1, 2,  1 },
    { ZB_B_MPOS,          "mpos",          1, 1,  1 },
    { ZB_B_AXISSTATUS,    "axisstatus",    1, 1,  1 },
    { ZB_B_BASE,          "base",          1, 1,  1 },
    { ZB_B_WAITIDLE,      "waitidle",      0, 0,  0 },
    { ZB_B_DELAY,         "delay",         1, 1,  0 },
    { ZB_B_CANCEL,        "cancel",        0, 1,  0 },
    { ZB_B_RAPIDSTOP,     "rapidstop",     0, 0,  0 },
    { ZB_B_IN,            "in",            1, 1,  1 },
    { ZB_B_OP,            "op",            1, 2,  1 },
    { ZB_B_RUN,           "run",           1, 1,  1 },
    { ZB_B_STOPTASK,      "stoptask",      0, 1,  0 },
    { ZB_B_TASKSTATE,     "taskstate",     1, 1,  1 },
    { ZB_B_ECAM,          "ecam",          3, 3,  0 },
    { ZB_B_ECAMSTART,     "ecamstart",     3, 3,  0 },
    { ZB_B_ECAMSTOP,      "ecamstop",      0, 0,  0 },
    { ZB_B_SIN,           "sin",           1, 1,  1 },
    { ZB_B_COS,           "cos",           1, 1,  1 },
    { ZB_B_TAN,           "tan",           1, 1,  1 },
    { ZB_B_ATN,           "atn",           1, 1,  1 },
    { ZB_B_SQRT,          "sqrt",          1, 1,  1 },
    { ZB_B_ABS,           "abs",           1, 1,  1 },
    { ZB_B_INT,           "int",           1, 1,  1 },
    { ZB_B_FIX,           "fix",           1, 1,  1 },
    { ZB_B_RND,           "rnd",           0, 1,  1 },
    { ZB_B_SGN,           "sgn",           1, 1,  1 },
    { ZB_B_MIN,           "min",           2, 2,  1 },
    { ZB_B_MAX,           "max",           2, 2,  1 },
    { ZB_B_TICKS,         "ticks",         0, 0,  1 }
};

const ZbBuiltin* zb_builtin_table(int* count)
{
    if (count) *count = (int)(sizeof(kBuiltins) / sizeof(kBuiltins[0]));
    return kBuiltins;
}

const ZbBuiltin* zb_builtin_lookup(const char* name)
{
    int i;
    int n = (int)(sizeof(kBuiltins) / sizeof(kBuiltins[0]));
    for (i = 0; i < n; ++i) {
        if (zb_stricmp(kBuiltins[i].name, name) == 0)
            return &kBuiltins[i];
    }
    return 0;
}

const ZbBuiltin* zb_builtin_by_id(int id)
{
    int i;
    int n = (int)(sizeof(kBuiltins) / sizeof(kBuiltins[0]));
    for (i = 0; i < n; ++i) {
        if (kBuiltins[i].id == id)
            return &kBuiltins[i];
    }
    return 0;
}
