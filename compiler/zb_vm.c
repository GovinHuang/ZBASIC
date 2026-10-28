#include "zb_vm.h"
#include "zb_builtin.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <math.h>

static ZbValue zb_num(double v)
{
    ZbValue r;
    r.type = ZB_VAL_NUMBER;
    r.num = v;
    r.str = NULL;
    return r;
}

static ZbValue zb_strv(const char* s)
{
    ZbValue r;
    r.type = ZB_VAL_STRING;
    r.num = 0.0;
    r.str = s ? s : "";
    return r;
}

static int zb_truthy(const ZbValue* v)
{
    if (v->type == ZB_VAL_STRING) return v->str && v->str[0] != '\0';
    return v->num != 0.0;
}

static void zb_print_line(ZbVm* vm, const char* text)
{
    if (vm->print) vm->print(vm->user, text);
}

static void zb_task_error(ZbVm* vm, ZbTask* t, const char* fmt, ...)
{
    char msg[256];
    char line[320];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    zb_strlcpy(t->error, msg, sizeof(t->error));
    t->state = ZB_TASK_ERROR;
    t->wait_kind = 0;
    snprintf(line, sizeof(line), "[task %d] error: %s", t->id, msg);
    zb_print_line(vm, line);
}

static int zb_push(ZbVm* vm, ZbTask* t, ZbValue v)
{
    if (t->sp >= ZB_STACK_SIZE) {
        zb_task_error(vm, t, "expression stack overflow");
        return 0;
    }
    t->stack[t->sp++] = v;
    return 1;
}

static int zb_pop(ZbVm* vm, ZbTask* t, ZbValue* out)
{
    if (t->sp <= 0) {
        zb_task_error(vm, t, "expression stack underflow");
        return 0;
    }
    *out = t->stack[--t->sp];
    return 1;
}

static int zb_arg_num(ZbVm* vm, ZbTask* t, ZbValue* args, int argc, int i,
                      double* out, const char* fname)
{
    if (i >= argc) {
        zb_task_error(vm, t, "%s: missing argument", fname);
        return 0;
    }
    if (args[i].type != ZB_VAL_NUMBER) {
        zb_task_error(vm, t, "%s: numeric argument expected", fname);
        return 0;
    }
    *out = args[i].num;
    return 1;
}

static int zb_arg_axis(ZbVm* vm, ZbTask* t, ZbValue* args, int argc, int i,
                       int* out, const char* fname)
{
    double d;
    int axis;
    if (!zb_arg_num(vm, t, args, argc, i, &d, fname)) return 0;
    axis = (int)(d < 0 ? d - 0.5 : d + 0.5);
    if (axis < 0 || axis >= ZB_MAX_AXES) {
        zb_task_error(vm, t, "%s: axis %d out of range (0..%d)",
                      fname, axis, ZB_MAX_AXES - 1);
        return 0;
    }
    *out = axis;
    return 1;
}

static void zb_format_value(const ZbValue* v, char* buf, size_t cap)
{
    if (v->type == ZB_VAL_STRING) {
        zb_strlcpy(buf, v->str ? v->str : "", cap);
    } else {
        snprintf(buf, cap, "%g", v->num);
    }
}

static int zb_vm_spawn(ZbVm* vm, int program_index);

/* ---------- 内建函数 ---------- */

static void zb_call_builtin(ZbVm* vm, ZbTask* t, int id, int argc, ZbValue* args)
{
    ZbMotion* m = &vm->motion;
    char err[128];

    switch (id) {
    case ZB_B_PRINT: {
        char line[2048];
        int i;
        size_t used = 0;
        line[0] = '\0';
        for (i = 0; i < argc; ++i) {
            char tmp[512];
            zb_format_value(&args[i], tmp, sizeof(tmp));
            if (i > 0) {
                if (used + 1 < sizeof(line)) { line[used++] = ' '; line[used] = '\0'; }
            }
            zb_strlcat(line, tmp, sizeof(line));
            used = strlen(line);
        }
        zb_print_line(vm, line);
        return;
    }

    case ZB_B_MOVE:
    case ZB_B_MOVEABS:
    case ZB_B_MOVELINEAR:
    case ZB_B_MOVEABSLINEAR: {
        int axes[8];
        double targets[8];
        int i, n = argc;
        int absolute = (id == ZB_B_MOVEABS || id == ZB_B_MOVEABSLINEAR);
        if (n < 1 || n > 8) {
            zb_task_error(vm, t, "MOVE supports 1..8 axes");
            return;
        }
        for (i = 0; i < n; ++i) {
            if (!zb_arg_num(vm, t, args, argc, i, &targets[i], "MOVE")) return;
            axes[i] = vm->base_axis + i;
            if (axes[i] >= ZB_MAX_AXES) {
                zb_task_error(vm, t, "MOVE: axis %d out of range", axes[i]);
                return;
            }
        }
        {
            ZbAxis* base = &m->axes[vm->base_axis];
            if (!zb_motion_start_linear(m, axes, targets, n, absolute,
                                        base->speed, base->accel, base->decel,
                                        err, sizeof(err))) {
                zb_task_error(vm, t, "MOVE: %s", err);
            }
        }
        return;
    }

    case ZB_B_MOVECIRC:
    case ZB_B_MOVEABSCIRC: {
        double e1, e2, c1, c2, dir;
        int ax1 = vm->base_axis;
        int ax2 = vm->base_axis + 1;
        int absolute = (id == ZB_B_MOVEABSCIRC);
        if (!zb_arg_num(vm, t, args, argc, 0, &e1, "MOVECIRC")) return;
        if (!zb_arg_num(vm, t, args, argc, 1, &e2, "MOVECIRC")) return;
        if (!zb_arg_num(vm, t, args, argc, 2, &c1, "MOVECIRC")) return;
        if (!zb_arg_num(vm, t, args, argc, 3, &c2, "MOVECIRC")) return;
        if (!zb_arg_num(vm, t, args, argc, 4, &dir, "MOVECIRC")) return;
        if (ax2 >= ZB_MAX_AXES) {
            zb_task_error(vm, t, "MOVECIRC: base axis must be <= %d", ZB_MAX_AXES - 2);
            return;
        }
        {
            ZbAxis* base = &m->axes[ax1];
            if (!zb_motion_start_circular(m, ax1, ax2, e1, e2, c1, c2,
                                          absolute, (int)dir,
                                          base->speed, base->accel, base->decel,
                                          err, sizeof(err))) {
                zb_task_error(vm, t, "MOVECIRC: %s", err);
            }
        }
        return;
    }

    case ZB_B_SPEED:
    case ZB_B_ACCEL:
    case ZB_B_DECEL: {
        int axis;
        double value, old;
        if (argc == 1) {
            axis = vm->base_axis;
            if (!zb_arg_num(vm, t, args, argc, 0, &value, "SPEED")) return;
        } else {
            if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "SPEED")) return;
            if (!zb_arg_num(vm, t, args, argc, 1, &value, "SPEED")) return;
        }
        if (id == ZB_B_SPEED) {
            old = m->axes[axis].speed;
            m->axes[axis].speed = value;
        } else if (id == ZB_B_ACCEL) {
            old = m->axes[axis].accel;
            m->axes[axis].accel = value;
        } else {
            old = m->axes[axis].decel;
            m->axes[axis].decel = value;
        }
        zb_push(vm, t, zb_num(old));
        return;
    }

    case ZB_B_UNITS: {
        int axis;
        double units;
        if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "UNITS")) return;
        if (!zb_arg_num(vm, t, args, argc, 1, &units, "UNITS")) return;
        m->axes[axis].units = units > 0.0 ? units : 1.0;
        return;
    }

    case ZB_B_ATYPE: {
        int axis;
        double type;
        if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "ATYPE")) return;
        if (!zb_arg_num(vm, t, args, argc, 1, &type, "ATYPE")) return;
        m->axes[axis].atype = (uint8_t)type;
        return;
    }

    case ZB_B_DPOS: {
        int axis;
        if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "DPOS")) return;
        if (argc >= 2) {
            double value;
            if (!zb_arg_num(vm, t, args, argc, 1, &value, "DPOS")) return;
            m->axes[axis].dpos = value;
            m->axes[axis].mpos = value;
            zb_push(vm, t, zb_num(value));
        } else {
            zb_push(vm, t, zb_num(m->axes[axis].dpos));
        }
        return;
    }

    case ZB_B_MPOS: {
        int axis;
        if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "MPOS")) return;
        zb_push(vm, t, zb_num(m->axes[axis].mpos));
        return;
    }

    case ZB_B_AXISSTATUS: {
        int axis;
        if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "AXISSTATUS")) return;
        zb_push(vm, t, zb_num((double)zb_motion_axis_status(m, axis)));
        return;
    }

    case ZB_B_BASE: {
        int axis, old;
        if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "BASE")) return;
        old = vm->base_axis;
        vm->base_axis = axis;
        zb_push(vm, t, zb_num((double)old));
        return;
    }

    case ZB_B_WAITIDLE:
        t->wait_kind = 2;
        return;

    case ZB_B_DELAY: {
        double ms;
        if (!zb_arg_num(vm, t, args, argc, 0, &ms, "DELAY")) return;
        if (ms < 0.0) ms = 0.0;
        t->wait_kind = 1;
        t->wake_tick = vm->tick + (uint32_t)(ms + 0.5);
        return;
    }

    case ZB_B_CANCEL: {
        if (argc == 0) {
            zb_motion_cancel(m, -1);
        } else {
            int axis;
            if (!zb_arg_axis(vm, t, args, argc, 0, &axis, "CANCEL")) return;
            zb_motion_cancel(m, axis);
        }
        return;
    }

    case ZB_B_RAPIDSTOP:
        zb_motion_cancel(m, -1);
        return;

    case ZB_B_IN: {
        int port;
        double d;
        if (!zb_arg_num(vm, t, args, argc, 0, &d, "IN")) return;
        port = (int)d;
        if (port < 0 || port >= 64) {
            zb_task_error(vm, t, "IN: port %d out of range", port);
            return;
        }
        zb_push(vm, t, zb_num((double)vm->inputs[port]));
        return;
    }

    case ZB_B_OP: {
        int port;
        double d;
        if (!zb_arg_num(vm, t, args, argc, 0, &d, "OP")) return;
        port = (int)d;
        if (port < 0 || port >= 64) {
            zb_task_error(vm, t, "OP: port %d out of range", port);
            return;
        }
        if (argc >= 2) {
            double value;
            if (!zb_arg_num(vm, t, args, argc, 1, &value, "OP")) return;
            vm->outputs[port] = (uint8_t)(value != 0.0 ? 1 : 0);
            zb_push(vm, t, zb_num(value));
        } else {
            zb_push(vm, t, zb_num((double)vm->outputs[port]));
        }
        return;
    }

    case ZB_B_RUN: {
        int pi = -1, id;
        if (argc < 1) {
            zb_task_error(vm, t, "RUN: program name expected");
            return;
        }
        if (!vm->package) {
            zb_task_error(vm, t, "RUN: no program image loaded");
            return;
        }
        if (args[0].type == ZB_VAL_STRING) {
            pi = zb_package_find(vm->package, args[0].str);
        } else {
            int idx = (int)args[0].num;
            if (idx >= 0 && idx < vm->package->count) pi = idx;
        }
        if (pi < 0) {
            zb_task_error(vm, t, "RUN: program not found");
            return;
        }
        id = zb_vm_spawn(vm, pi);
        if (id < 0) {
            zb_task_error(vm, t, "RUN: no free task slot");
            return;
        }
        zb_push(vm, t, zb_num((double)id));
        return;
    }

    case ZB_B_STOPTASK: {
        int id;
        if (argc == 0) {
            id = t->id;
        } else {
            double d;
            if (!zb_arg_num(vm, t, args, argc, 0, &d, "STOPTASK")) return;
            id = (int)d;
        }
        if (id < 0 || id >= ZB_MAX_TASKS) {
            zb_task_error(vm, t, "STOPTASK: invalid task id %d", id);
            return;
        }
        if (id == t->id) {
            t->state = ZB_TASK_DONE;
            t->wait_kind = 0;
        } else if (vm->tasks[id].state != ZB_TASK_EMPTY) {
            vm->tasks[id].state = ZB_TASK_DONE;
            vm->tasks[id].wait_kind = 0;
        }
        return;
    }

    case ZB_B_TASKSTATE: {
        double d;
        int id;
        if (!zb_arg_num(vm, t, args, argc, 0, &d, "TASKSTATE")) return;
        id = (int)d;
        if (id < 0 || id >= ZB_MAX_TASKS) {
            zb_push(vm, t, zb_num(-1.0));
        } else {
            zb_push(vm, t, zb_num((double)vm->tasks[id].state));
        }
        return;
    }

    case ZB_B_ECAM: {
        double table_d, master, slave;
        int table;
        if (!zb_arg_num(vm, t, args, argc, 0, &table_d, "ECAM")) return;
        if (!zb_arg_num(vm, t, args, argc, 1, &master, "ECAM")) return;
        if (!zb_arg_num(vm, t, args, argc, 2, &slave, "ECAM")) return;
        table = (int)table_d;
        if (!zb_motion_add_cam_point(m, table, master, slave, err, sizeof(err))) {
            zb_task_error(vm, t, "ECAM: %s", err);
        }
        return;
    }

    case ZB_B_ECAMSTART: {
        int master, slave, table;
        if (!zb_arg_axis(vm, t, args, argc, 0, &master, "ECAMSTART")) return;
        if (!zb_arg_axis(vm, t, args, argc, 1, &slave, "ECAMSTART")) return;
        {
            double d;
            if (!zb_arg_num(vm, t, args, argc, 2, &d, "ECAMSTART")) return;
            table = (int)d;
        }
        if (!zb_motion_start_cam(m, table, master, slave, err, sizeof(err)))
            zb_task_error(vm, t, "ECAMSTART: %s", err);
        return;
    }

    case ZB_B_ECAMSTOP:
        zb_motion_stop_cam(m);
        return;

    case ZB_B_SIN:
    case ZB_B_COS:
    case ZB_B_TAN:
    case ZB_B_ATN:
    case ZB_B_SQRT:
    case ZB_B_ABS:
    case ZB_B_INT:
    case ZB_B_FIX:
    case ZB_B_SGN: {
        double x, r = 0.0;
        if (!zb_arg_num(vm, t, args, argc, 0, &x, "math")) return;
        switch (id) {
        case ZB_B_SIN: r = sin(x); break;
        case ZB_B_COS: r = cos(x); break;
        case ZB_B_TAN: r = tan(x); break;
        case ZB_B_ATN: r = atan(x); break;
        case ZB_B_SQRT:
            if (x < 0.0) {
                zb_task_error(vm, t, "SQRT: negative argument");
                return;
            }
            r = sqrt(x);
            break;
        case ZB_B_ABS: r = fabs(x); break;
        case ZB_B_INT: r = floor(x); break;
        case ZB_B_FIX: r = (x >= 0.0) ? floor(x) : ceil(x); break;
        case ZB_B_SGN: r = (x > 0.0) ? 1.0 : (x < 0.0 ? -1.0 : 0.0); break;
        }
        zb_push(vm, t, zb_num(r));
        return;
    }

    case ZB_B_RND: {
        double r;
        if (argc >= 1) {
            double n;
            if (!zb_arg_num(vm, t, args, argc, 0, &n, "RND")) return;
            if (n <= 0.0) { zb_push(vm, t, zb_num(0.0)); return; }
            r = (double)(rand() % (int)n);
        } else {
            r = (double)rand() / (double)RAND_MAX;
        }
        zb_push(vm, t, zb_num(r));
        return;
    }

    case ZB_B_MIN:
    case ZB_B_MAX: {
        double a, b;
        if (!zb_arg_num(vm, t, args, argc, 0, &a, "MIN/MAX")) return;
        if (!zb_arg_num(vm, t, args, argc, 1, &b, "MIN/MAX")) return;
        zb_push(vm, t, zb_num(id == ZB_B_MIN ? (a < b ? a : b) : (a > b ? a : b)));
        return;
    }

    case ZB_B_TICKS:
        zb_push(vm, t, zb_num((double)vm->tick));
        return;

    default:
        zb_task_error(vm, t, "unsupported built-in function id %d", id);
        return;
    }
}

/* ---------- 指令执行 ---------- */

static void zb_do_call(ZbVm* vm, ZbTask* t, ZbProgram* prog, uint32_t proc_index,
                       int argc)
{
    ZbProc* p;
    ZbFrame* f;
    int i;

    if (proc_index >= prog->proc_count) {
        zb_task_error(vm, t, "invalid procedure index");
        return;
    }
    if (t->fp >= ZB_MAX_FRAMES) {
        zb_task_error(vm, t, "call stack overflow");
        return;
    }
    if (argc > t->sp) {
        zb_task_error(vm, t, "argument stack underflow");
        return;
    }
    p = &prog->procs[proc_index];
    if (t->local_top + p->nlocals > ZB_MAX_LOCALS) {
        zb_task_error(vm, t, "local variable stack overflow");
        return;
    }
    f = &t->frames[t->fp++];
    f->ret_pc = t->pc;
    f->local_base = (uint16_t)t->local_top;
    f->proc = (uint16_t)proc_index;

    for (i = 0; i < p->nlocals; ++i)
        t->locals[t->local_top + i] = zb_num(0.0);
    for (i = 0; i < argc && i < p->nparams; ++i)
        t->locals[t->local_top + i] = t->stack[t->sp - argc + i];
    t->sp -= argc;
    t->local_top += p->nlocals;
    t->pc = p->entry;
}

static void zb_do_ret(ZbVm* vm, ZbTask* t, int with_value)
{
    ZbValue value;
    ZbFrame f;
    if (t->fp <= 0) {
        zb_task_error(vm, t, "RETURN without CALL");
        return;
    }
    if (with_value) {
        if (!zb_pop(vm, t, &value)) return;
    } else {
        value = zb_num(0.0);
    }
    f = t->frames[--t->fp];
    t->local_top = f.local_base;
    t->pc = f.ret_pc;
    if (with_value) zb_push(vm, t, value);
}

static void zb_compare(ZbVm* vm, ZbTask* t, int op, const ZbValue* a, const ZbValue* b)
{
    int result = 0;
    if (a->type == ZB_VAL_STRING || b->type == ZB_VAL_STRING) {
        if (op != ZB_OP_EQ && op != ZB_OP_NE) {
            zb_task_error(vm, t, "only = and <> can compare strings");
            return;
        }
        if (a->type != b->type) {
            result = (op == ZB_OP_NE) ? 1 : 0;
        } else {
            int c = strcmp(a->str ? a->str : "", b->str ? b->str : "");
            if (op == ZB_OP_EQ) result = (c == 0);
            else result = (c != 0);
        }
    } else {
        double x = a->num, y = b->num;
        switch (op) {
        case ZB_OP_EQ: result = (x == y); break;
        case ZB_OP_NE: result = (x != y); break;
        case ZB_OP_LT: result = (x < y); break;
        case ZB_OP_LE: result = (x <= y); break;
        case ZB_OP_GT: result = (x > y); break;
        case ZB_OP_GE: result = (x >= y); break;
        }
    }
    zb_push(vm, t, zb_num(result ? 1.0 : 0.0));
}

static void zb_exec(ZbVm* vm, ZbTask* t)
{
    ZbProgram* prog;
    uint32_t budget = ZB_TASK_SLICE;

    t->state = ZB_TASK_RUNNING;
    prog = &vm->package->programs[t->program];

    while (budget-- > 0 && t->state == ZB_TASK_RUNNING) {
        ZbIns ins;
        if (t->pc >= prog->code_count) {
            zb_task_error(vm, t, "program counter out of range");
            break;
        }
        ins = prog->code[t->pc++];

        switch (ins.op) {
        case ZB_OP_NOP:
            break;

        case ZB_OP_PUSH_CONST:
            if (ins.b >= prog->const_count) {
                zb_task_error(vm, t, "invalid constant index");
                break;
            }
            zb_push(vm, t, zb_num(prog->consts[ins.b]));
            break;

        case ZB_OP_PUSH_STR:
            if (ins.b >= prog->str_count) {
                zb_task_error(vm, t, "invalid string index");
                break;
            }
            zb_push(vm, t, zb_strv(prog->strings[ins.b]));
            break;

        case ZB_OP_LOAD_G:
            if (ins.a >= prog->global_count) {
                zb_task_error(vm, t, "invalid global variable index");
                break;
            }
            zb_push(vm, t, t->globals[ins.a]);
            break;

        case ZB_OP_STORE_G: {
            ZbValue v;
            if (!zb_pop(vm, t, &v)) break;
            if (ins.a >= prog->global_count) {
                zb_task_error(vm, t, "invalid global variable index");
                break;
            }
            t->globals[ins.a] = v;
            break;
        }

        case ZB_OP_LOAD_L:
            if (ins.a >= ZB_MAX_LOCALS) {
                zb_task_error(vm, t, "invalid local variable index");
                break;
            }
            zb_push(vm, t, t->locals[ins.a]);
            break;

        case ZB_OP_STORE_L: {
            ZbValue v;
            if (!zb_pop(vm, t, &v)) break;
            if (ins.a >= ZB_MAX_LOCALS) {
                zb_task_error(vm, t, "invalid local variable index");
                break;
            }
            t->locals[ins.a] = v;
            break;
        }

        case ZB_OP_LOAD_ARR: {
            ZbValue idx;
            double d;
            int i;
            if (ins.a >= prog->array_count) {
                zb_task_error(vm, t, "invalid array index");
                break;
            }
            if (!zb_pop(vm, t, &idx)) break;
            if (idx.type != ZB_VAL_NUMBER) {
                zb_task_error(vm, t, "array index must be numeric");
                break;
            }
            d = idx.num;
            i = (int)(d < 0 ? d - 0.5 : d + 0.5);
            if (i < 0 || i >= prog->arrays[ins.a].size) {
                zb_task_error(vm, t, "array index %d out of range (0..%d)",
                              i, prog->arrays[ins.a].size - 1);
                break;
            }
            zb_push(vm, t, t->globals[prog->arrays[ins.a].base + i]);
            break;
        }

        case ZB_OP_STORE_ARR: {
            ZbValue idx, val;
            double d;
            int i;
            if (!zb_pop(vm, t, &val)) break;
            if (!zb_pop(vm, t, &idx)) break;
            if (ins.a >= prog->array_count) {
                zb_task_error(vm, t, "invalid array index");
                break;
            }
            if (idx.type != ZB_VAL_NUMBER) {
                zb_task_error(vm, t, "array index must be numeric");
                break;
            }
            d = idx.num;
            i = (int)(d < 0 ? d - 0.5 : d + 0.5);
            if (i < 0 || i >= prog->arrays[ins.a].size) {
                zb_task_error(vm, t, "array index %d out of range (0..%d)",
                              i, prog->arrays[ins.a].size - 1);
                break;
            }
            t->globals[prog->arrays[ins.a].base + i] = val;
            break;
        }

        case ZB_OP_POP: {
            ZbValue v;
            zb_pop(vm, t, &v);
            break;
        }

        case ZB_OP_DUP: {
            ZbValue v;
            if (!zb_pop(vm, t, &v)) break;
            if (!zb_push(vm, t, v)) break;
            zb_push(vm, t, v);
            break;
        }

        case ZB_OP_ADD: case ZB_OP_SUB: case ZB_OP_MUL:
        case ZB_OP_DIV: case ZB_OP_MOD: case ZB_OP_POW: {
            ZbValue a, b;
            double x, y, r = 0.0;
            if (!zb_pop(vm, t, &b) || !zb_pop(vm, t, &a)) break;
            if (a.type != ZB_VAL_NUMBER || b.type != ZB_VAL_NUMBER) {
                zb_task_error(vm, t, "numeric operands required");
                break;
            }
            x = a.num;
            y = b.num;
            switch (ins.op) {
            case ZB_OP_ADD: r = x + y; break;
            case ZB_OP_SUB: r = x - y; break;
            case ZB_OP_MUL: r = x * y; break;
            case ZB_OP_DIV:
                if (y == 0.0) { zb_task_error(vm, t, "division by zero"); break; }
                r = x / y;
                break;
            case ZB_OP_MOD:
                if (y == 0.0) { zb_task_error(vm, t, "MOD by zero"); break; }
                r = fmod(x, y);
                break;
            case ZB_OP_POW: r = pow(x, y); break;
            }
            if (t->state != ZB_TASK_RUNNING) break;
            zb_push(vm, t, zb_num(r));
            break;
        }

        case ZB_OP_NEG: {
            ZbValue a;
            if (!zb_pop(vm, t, &a)) break;
            if (a.type != ZB_VAL_NUMBER) {
                zb_task_error(vm, t, "numeric operand required");
                break;
            }
            zb_push(vm, t, zb_num(-a.num));
            break;
        }

        case ZB_OP_EQ: case ZB_OP_NE: case ZB_OP_LT:
        case ZB_OP_LE: case ZB_OP_GT: case ZB_OP_GE: {
            ZbValue a, b;
            if (!zb_pop(vm, t, &b) || !zb_pop(vm, t, &a)) break;
            zb_compare(vm, t, ins.op, &a, &b);
            break;
        }

        case ZB_OP_AND: case ZB_OP_OR: case ZB_OP_XOR: {
            ZbValue a, b;
            int x, y, r;
            if (!zb_pop(vm, t, &b) || !zb_pop(vm, t, &a)) break;
            x = zb_truthy(&a);
            y = zb_truthy(&b);
            if (ins.op == ZB_OP_AND) r = x && y;
            else if (ins.op == ZB_OP_OR) r = x || y;
            else r = (x != y);
            zb_push(vm, t, zb_num(r ? 1.0 : 0.0));
            break;
        }

        case ZB_OP_NOT: {
            ZbValue a;
            if (!zb_pop(vm, t, &a)) break;
            zb_push(vm, t, zb_num(zb_truthy(&a) ? 0.0 : 1.0));
            break;
        }

        case ZB_OP_JMP:
            if (ins.b >= prog->code_count) {
                zb_task_error(vm, t, "jump target out of range");
                break;
            }
            t->pc = ins.b;
            break;

        case ZB_OP_JZ: case ZB_OP_JNZ: {
            ZbValue a;
            int truth;
            if (!zb_pop(vm, t, &a)) break;
            truth = zb_truthy(&a);
            if ((ins.op == ZB_OP_JZ && !truth) || (ins.op == ZB_OP_JNZ && truth)) {
                if (ins.b >= prog->code_count) {
                    zb_task_error(vm, t, "jump target out of range");
                    break;
                }
                t->pc = ins.b;
            }
            break;
        }

        case ZB_OP_CALL:
            zb_do_call(vm, t, prog, ins.b, (int)ins.a);
            break;

        case ZB_OP_RET:
            zb_do_ret(vm, t, 0);
            break;

        case ZB_OP_RETF:
            zb_do_ret(vm, t, 1);
            break;

        case ZB_OP_GOSUB: {
            ZbFrame* f;
            if (t->fp >= ZB_MAX_FRAMES) {
                zb_task_error(vm, t, "call stack overflow");
                break;
            }
            if (ins.b >= prog->code_count) {
                zb_task_error(vm, t, "GOSUB target out of range");
                break;
            }
            f = &t->frames[t->fp++];
            f->ret_pc = t->pc;
            f->local_base = (uint16_t)t->local_top;
            f->proc = 0xFFFF;
            t->pc = ins.b;
            break;
        }

        case ZB_OP_RETSUB: {
            ZbFrame f;
            if (t->fp <= 0) {
                zb_task_error(vm, t, "RETURN without GOSUB");
                break;
            }
            f = t->frames[--t->fp];
            t->local_top = f.local_base;
            t->pc = f.ret_pc;
            break;
        }

        case ZB_OP_CALLB: {
            ZbValue args[16];
            int argc = (int)ins.b;
            int i;
            if (argc < 0 || argc > 16 || argc > t->sp) {
                zb_task_error(vm, t, "invalid built-in argument count");
                break;
            }
            for (i = 0; i < argc; ++i)
                args[i] = t->stack[t->sp - argc + i];
            t->sp -= argc;
            zb_call_builtin(vm, t, (int)ins.a, argc, args);
            break;
        }

        case ZB_OP_YIELD:
            t->wait_kind = 3;
            break;

        case ZB_OP_HALT:
            t->state = ZB_TASK_DONE;
            break;

        default:
            zb_task_error(vm, t, "invalid opcode %d", (int)ins.op);
            break;
        }

        if (t->state != ZB_TASK_RUNNING) break;
        if (t->wait_kind) break;
    }

    if (t->state == ZB_TASK_RUNNING)
        t->state = t->wait_kind ? ZB_TASK_WAITING : ZB_TASK_READY;
}

/* ---------- 任务管理 ---------- */

static int zb_vm_spawn(ZbVm* vm, int program_index)
{
    int i;
    ZbTask* t = NULL;
    ZbProgram* prog;
    ZbValue* old_globals;

    if (!vm->package) return -1;
    if (program_index < 0 || program_index >= vm->package->count) return -1;

    for (i = 0; i < ZB_MAX_TASKS; ++i) {
        if (vm->tasks[i].state == ZB_TASK_EMPTY ||
            vm->tasks[i].state == ZB_TASK_DONE ||
            vm->tasks[i].state == ZB_TASK_ERROR) {
            t = &vm->tasks[i];
            break;
        }
    }
    if (!t) return -1;

    prog = &vm->package->programs[program_index];
    old_globals = t->globals;
    memset(t, 0, sizeof(*t));
    if (old_globals) free(old_globals);
    t->id = i;
    t->program = program_index;
    t->pc = prog->entry;
    t->state = ZB_TASK_READY;
    t->globals = (ZbValue*)calloc(prog->global_count ? prog->global_count : 1,
                                  sizeof(ZbValue));
    if (!t->globals) {
        t->state = ZB_TASK_EMPTY;
        return -1;
    }
    return i;
}

void zb_vm_init(ZbVm* vm)
{
    memset(vm, 0, sizeof(*vm));
    zb_motion_init(&vm->motion);
    vm->base_axis = 0;
}

void zb_vm_free(ZbVm* vm)
{
    int i;
    if (!vm) return;
    for (i = 0; i < ZB_MAX_TASKS; ++i) {
        free(vm->tasks[i].globals);
        vm->tasks[i].globals = NULL;
    }
    vm->package = NULL;
    vm->running = 0;
}

void zb_vm_reset(ZbVm* vm)
{
    int i;
    for (i = 0; i < ZB_MAX_TASKS; ++i) {
        free(vm->tasks[i].globals);
        vm->tasks[i].globals = NULL;
    }
    memset(vm->tasks, 0, sizeof(vm->tasks));
    zb_motion_init(&vm->motion);
    vm->tick = 0;
    vm->base_axis = 0;
    vm->running = 0;
    memset(vm->inputs, 0, sizeof(vm->inputs));
    memset(vm->outputs, 0, sizeof(vm->outputs));
}

void zb_vm_set_package(ZbVm* vm, ZbPackage* pkg)
{
    vm->package = pkg;
}

void zb_vm_set_print(ZbVm* vm, ZbPrintFn fn, void* user)
{
    vm->print = fn;
    vm->user = user;
}

int zb_vm_start(ZbVm* vm, const char* program_name)
{
    int pi;
    if (!vm->package) return -1;
    zb_vm_stop(vm);
    pi = zb_package_find(vm->package, program_name);
    if (pi < 0) return -1;
    return zb_vm_spawn(vm, pi);
}

void zb_vm_stop(ZbVm* vm)
{
    int i;
    for (i = 0; i < ZB_MAX_TASKS; ++i) {
        ZbTask* t = &vm->tasks[i];
        if (t->state != ZB_TASK_EMPTY) {
            t->state = ZB_TASK_DONE;
            t->wait_kind = 0;
        }
    }
    zb_motion_cancel(&vm->motion, -1);
    vm->running = 0;
}

void zb_vm_tick(ZbVm* vm, double dt_ms)
{
    int i;
    if (!vm || !vm->package || dt_ms <= 0.0) return;

    zb_motion_tick(&vm->motion, dt_ms);
    vm->tick += (uint32_t)(dt_ms + 0.5);

    for (i = 0; i < ZB_MAX_TASKS; ++i) {
        ZbTask* t = &vm->tasks[i];
        if (t->state == ZB_TASK_WAITING) {
            if (t->wait_kind == 1 && vm->tick >= t->wake_tick) {
                t->wait_kind = 0;
                t->state = ZB_TASK_READY;
            } else if (t->wait_kind == 2 && zb_motion_is_idle(&vm->motion)) {
                t->wait_kind = 0;
                t->state = ZB_TASK_READY;
            } else if (t->wait_kind == 3) {
                t->wait_kind = 0;
                t->state = ZB_TASK_READY;
            }
        }
        if (t->state == ZB_TASK_READY) zb_exec(vm, t);
    }
    vm->running = zb_vm_active_tasks(vm) > 0;
}

int zb_vm_is_running(const ZbVm* vm)
{
    return vm ? vm->running : 0;
}

int zb_vm_active_tasks(const ZbVm* vm)
{
    int i, count = 0;
    if (!vm) return 0;
    for (i = 0; i < ZB_MAX_TASKS; ++i) {
        int s = vm->tasks[i].state;
        if (s == ZB_TASK_READY || s == ZB_TASK_RUNNING || s == ZB_TASK_WAITING)
            count++;
    }
    return count;
}

const char* zb_vm_task_state_name(int state)
{
    switch (state) {
    case ZB_TASK_EMPTY:   return "empty";
    case ZB_TASK_READY:   return "ready";
    case ZB_TASK_RUNNING: return "running";
    case ZB_TASK_WAITING: return "waiting";
    case ZB_TASK_DONE:    return "done";
    case ZB_TASK_ERROR:   return "error";
    default:              return "?";
    }
}
