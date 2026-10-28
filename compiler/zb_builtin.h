/* Built-in functions / statements of the ZBasic dialect. */
#ifndef ZB_BUILTIN_H
#define ZB_BUILTIN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ZB_B_NONE = 0,
    ZB_B_PRINT = 1,
    ZB_B_MOVE,
    ZB_B_MOVEABS,
    ZB_B_MOVELINEAR,
    ZB_B_MOVEABSLINEAR,
    ZB_B_MOVECIRC,
    ZB_B_MOVEABSCIRC,
    ZB_B_SPEED,
    ZB_B_ACCEL,
    ZB_B_DECEL,
    ZB_B_UNITS,
    ZB_B_ATYPE,
    ZB_B_DPOS,
    ZB_B_MPOS,
    ZB_B_AXISSTATUS,
    ZB_B_BASE,
    ZB_B_WAITIDLE,
    ZB_B_DELAY,
    ZB_B_CANCEL,
    ZB_B_RAPIDSTOP,
    ZB_B_IN,
    ZB_B_OP,
    ZB_B_RUN,
    ZB_B_STOPTASK,
    ZB_B_TASKSTATE,
    ZB_B_ECAM,
    ZB_B_ECAMSTART,
    ZB_B_ECAMSTOP,
    ZB_B_SIN,
    ZB_B_COS,
    ZB_B_TAN,
    ZB_B_ATN,
    ZB_B_SQRT,
    ZB_B_ABS,
    ZB_B_INT,
    ZB_B_FIX,
    ZB_B_RND,
    ZB_B_SGN,
    ZB_B_MIN,
    ZB_B_MAX,
    ZB_B_TICKS,
    ZB_B_COUNT
} ZbBuiltinId;

typedef struct {
    int         id;
    const char* name;
    int         min_args;
    int         max_args;   /* -1 = variadic */
    int         returns_value;
} ZbBuiltin;

const ZbBuiltin* zb_builtin_lookup(const char* name);
const ZbBuiltin* zb_builtin_by_id(int id);
const ZbBuiltin* zb_builtin_table(int* count);

#ifdef __cplusplus
}
#endif

#endif /* ZB_BUILTIN_H */
