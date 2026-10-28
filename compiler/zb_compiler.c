#include "zb_compiler.h"
#include "zb_lexer.h"
#include "zb_builtin.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>

#define ZB_MAX_SYMS       1024
#define ZB_MAX_LABELS     128
#define ZB_MAX_FIXUPS     2048
#define ZB_MAX_IFS        32
#define ZB_MAX_LOOPS      32
#define ZB_MAX_EXITS      64
#define ZB_MAX_LOCAL_SYMS 256

/* ---------- 内部数据结构 ---------- */

typedef struct {
    char     name[ZB_MAX_NAME];
    uint16_t slot;
    uint8_t  is_array;
    uint8_t  is_local;
    uint16_t arr_index;
    uint16_t arr_size;
} ZbSym;

typedef struct {
    char     name[ZB_MAX_NAME];
    uint32_t pc;
    uint8_t  defined;
} ZbLabel;

typedef struct {
    int      kind;          /* 0=FOR 1=WHILE 2=REPEAT */
    uint32_t start_pc;
    uint32_t end_patch1;
    uint32_t end_patch2;
    uint32_t exit_patches[8];
    int      exit_count;
    int      if_base;
    uint8_t  var_is_local;
    uint16_t var_slot;
    uint8_t  lim_is_local;
    uint16_t lim_slot;
    uint8_t  step_is_local;
    uint16_t step_slot;
} ZbLoop;

typedef struct {
    uint32_t jz_patch;
    uint32_t end_jumps[32];
    int      end_count;
} ZbIfBlock;

typedef struct {
    int     proc_index;      /* -1 = 主程序 */
    int     nparams;
    int     is_function;
    ZbSym   locals[ZB_MAX_LOCAL_SYMS];
    int     local_count;
    ZbLabel labels[ZB_MAX_LABELS];
    int     label_count;
    ZbLoop  loops[ZB_MAX_LOOPS];
    int     loop_count;
    ZbIfBlock ifs[ZB_MAX_IFS];
    int     if_count;
    uint32_t exits[ZB_MAX_EXITS];
    int     exit_count;
    int     temp_seq;
} ZbProcCtx;

typedef struct {
    char name[ZB_MAX_NAME];
    int  header_start;
    int  body_start;
    int  body_end;
    int  range_end;
    int  nparams;
    char params[ZB_MAX_PARAMS][ZB_MAX_NAME];
    int  is_function;
    int  proc_index;
    int  line;
} ZbPreProc;

typedef struct {
    char     name[ZB_MAX_NAME];
    uint32_t patch_pc;
    int      proc;
    int      line;
    int      col;
    uint8_t  resolved;
} ZbFixup;

typedef struct {
    ZbProgram*  prog;
    ZbDiagList* diags;
    ZbToken*    toks;
    int         count;
    int         pos;
    int         end;          /* 语句解析上界（不含） */
    int         failed;
    jmp_buf     jb;

    ZbSym       globals[ZB_MAX_SYMS];
    int         global_sym_count;

    ZbProcCtx   main_ctx;
    ZbProcCtx*  procs;
    ZbProcCtx*  ctx;

    ZbFixup     fixups[ZB_MAX_FIXUPS];
    int         fixup_count;

    ZbPreProc   pre[ZB_MAX_PROCS];
    int         pre_count;
} ZbCompiler;

/* ---------- 基础工具 ---------- */

static void zb_fail(ZbCompiler* c, int line, int col, const char* fmt, ...)
{
    char msg[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    zb_diag_add(c->diags, line, col, "%s", msg);
    c->failed = 1;
    longjmp(c->jb, 1);
}

static uint32_t zb_emit(ZbCompiler* c, int op, int a, uint32_t b)
{
    int idx = zb_program_add_ins(c->prog, op, a, b);
    if (idx < 0) {
        ZbToken* t = &c->toks[c->pos];
        zb_fail(c, t->line, t->col, "program image is too large");
    }
    return (uint32_t)idx;
}

static uint32_t zb_push_number(ZbCompiler* c, double value)
{
    int idx = zb_program_add_const(c->prog, value);
    if (idx < 0) {
        ZbToken* t = &c->toks[c->pos];
        zb_fail(c, t->line, t->col, "too many numeric constants");
    }
    return zb_emit(c, ZB_OP_PUSH_CONST, 0, (uint32_t)idx);
}

static ZbToken* zb_cur(ZbCompiler* c) { return &c->toks[c->pos]; }

static ZbToken* zb_peek(ZbCompiler* c, int n)
{
    int i = c->pos + n;
    if (i >= c->count) i = c->count - 1;
    if (i < 0) i = 0;
    return &c->toks[i];
}

static void zb_adv(ZbCompiler* c)
{
    if (c->pos < c->count - 1) c->pos++;
}

static int zb_at_end(ZbCompiler* c) { return c->pos >= c->end; }

static int zb_is_stmt_end(ZbCompiler* c)
{
    ZbTokType t = zb_cur(c)->type;
    if (t == ZB_TK_EOL || t == ZB_TK_EOF || t == ZB_TK_COLON) return 1;
    return zb_at_end(c);
}

static int zb_accept_type(ZbCompiler* c, ZbTokType type)
{
    if (zb_cur(c)->type == type) { zb_adv(c); return 1; }
    return 0;
}

static void zb_expect_type(ZbCompiler* c, ZbTokType type, const char* what)
{
    ZbToken* t = zb_cur(c);
    if (t->type != type)
        zb_fail(c, t->line, t->col, "%s expected but found '%s'", what, t->text);
    zb_adv(c);
}

static int zb_accept_kw(ZbCompiler* c, const char* kw)
{
    if (zb_token_is_keyword(zb_cur(c), kw)) { zb_adv(c); return 1; }
    return 0;
}

static void zb_expect_kw(ZbCompiler* c, const char* kw)
{
    ZbToken* t = zb_cur(c);
    if (!zb_token_is_keyword(t, kw))
        zb_fail(c, t->line, t->col, "'%s' expected but found '%s'", kw, t->text);
    zb_adv(c);
}

static int zb_accept_op(ZbCompiler* c, const char* op)
{
    if (zb_token_is_op(zb_cur(c), op)) { zb_adv(c); return 1; }
    return 0;
}

static void zb_expect_op(ZbCompiler* c, const char* op)
{
    ZbToken* t = zb_cur(c);
    if (!zb_token_is_op(t, op))
        zb_fail(c, t->line, t->col, "'%s' expected but found '%s'", op, t->text);
    zb_adv(c);
}

static void zb_expect_ident(ZbCompiler* c, char* out, int out_size)
{
    ZbToken* t = zb_cur(c);
    if (t->type != ZB_TK_IDENT)
        zb_fail(c, t->line, t->col, "identifier expected but found '%s'", t->text);
    zb_strlcpy(out, t->text, (size_t)out_size);
    zb_adv(c);
}

/* ---------- 符号表 ---------- */

static ZbSym* zb_find_local(ZbProcCtx* ctx, const char* name)
{
    int i;
    if (!ctx) return NULL;
    for (i = 0; i < ctx->local_count; ++i) {
        if (zb_stricmp(ctx->locals[i].name, name) == 0) return &ctx->locals[i];
    }
    return NULL;
}

static ZbSym* zb_find_global(ZbCompiler* c, const char* name)
{
    int i;
    for (i = 0; i < c->global_sym_count; ++i) {
        if (zb_stricmp(c->globals[i].name, name) == 0) return &c->globals[i];
    }
    return NULL;
}

static ZbSym* zb_find_sym(ZbCompiler* c, const char* name)
{
    ZbSym* s = NULL;
    if (c->ctx && c->ctx->proc_index >= 0) s = zb_find_local(c->ctx, name);
    if (!s) s = zb_find_global(c, name);
    return s;
}

static ZbSym* zb_add_global_scalar(ZbCompiler* c, const char* name, int line, int col)
{
    ZbSym* sym;
    if (c->global_sym_count >= ZB_MAX_SYMS)
        zb_fail(c, line, col, "too many global variables");
    if (c->prog->global_count >= ZB_MAX_GLOBALS)
        zb_fail(c, line, col, "global variable space exhausted");
    sym = &c->globals[c->global_sym_count++];
    memset(sym, 0, sizeof(*sym));
    zb_strlcpy(sym->name, name, sizeof(sym->name));
    sym->slot = c->prog->global_count++;
    sym->is_array = 0;
    sym->is_local = 0;
    return sym;
}

static ZbSym* zb_add_global_array(ZbCompiler* c, const char* name, int size,
                                  int line, int col)
{
    ZbSym* sym;
    int base;
    if (c->global_sym_count >= ZB_MAX_SYMS)
        zb_fail(c, line, col, "too many global variables");
    if (c->prog->array_count >= ZB_MAX_ARRAYS)
        zb_fail(c, line, col, "too many arrays (max %d)", ZB_MAX_ARRAYS);
    if (size < 1 || size > 65535)
        zb_fail(c, line, col, "invalid array size %d", size);
    if (c->prog->global_count + size > ZB_MAX_GLOBALS)
        zb_fail(c, line, col, "global variable space exhausted");
    base = (int)c->prog->global_count;
    c->prog->global_count = (uint16_t)(c->prog->global_count + size);

    sym = &c->globals[c->global_sym_count++];
    memset(sym, 0, sizeof(*sym));
    zb_strlcpy(sym->name, name, sizeof(sym->name));
    sym->slot = (uint16_t)base;
    sym->is_array = 1;
    sym->is_local = 0;
    sym->arr_size = (uint16_t)size;
    sym->arr_index = c->prog->array_count;
    c->prog->arrays[c->prog->array_count].base = (uint16_t)base;
    c->prog->arrays[c->prog->array_count].size = (uint16_t)size;
    c->prog->array_count++;
    return sym;
}

static ZbSym* zb_add_local(ZbCompiler* c, const char* name, int line, int col)
{
    ZbProcCtx* ctx = c->ctx;
    ZbSym* sym;
    if (!ctx || ctx->proc_index < 0)
        zb_fail(c, line, col, "LOCAL is only valid inside SUB/FUNCTION");
    if (ctx->local_count >= ZB_MAX_LOCAL_SYMS)
        zb_fail(c, line, col, "too many local variables");
    if (ctx->local_count >= ZB_MAX_LOCALS)
        zb_fail(c, line, col, "local variable slots exhausted");
    sym = &ctx->locals[ctx->local_count++];
    memset(sym, 0, sizeof(*sym));
    zb_strlcpy(sym->name, name, sizeof(sym->name));
    sym->slot = (uint16_t)(ctx->local_count - 1);
    sym->is_local = 1;
    return sym;
}

static uint16_t zb_alloc_temp(ZbCompiler* c, int* is_local, int line, int col)
{
    char name[ZB_MAX_NAME];
    ZbSym* sym;
    snprintf(name, sizeof(name), "__t%d", c->ctx->temp_seq++);
    if (c->ctx->proc_index >= 0) {
        sym = zb_add_local(c, name, line, col);
        *is_local = 1;
    } else {
        sym = zb_add_global_scalar(c, name, line, col);
        *is_local = 0;
    }
    return sym->slot;
}

static void zb_emit_load_sym(ZbCompiler* c, int is_local, uint16_t slot)
{
    zb_emit(c, is_local ? ZB_OP_LOAD_L : ZB_OP_LOAD_G, slot, 0);
}

static void zb_emit_store_sym(ZbCompiler* c, int is_local, uint16_t slot)
{
    zb_emit(c, is_local ? ZB_OP_STORE_L : ZB_OP_STORE_G, slot, 0);
}

/* ---------- 标签与跳转回填 ---------- */

static ZbLabel* zb_find_label(ZbProcCtx* ctx, const char* name)
{
    int i;
    for (i = 0; i < ctx->label_count; ++i) {
        if (zb_stricmp(ctx->labels[i].name, name) == 0) return &ctx->labels[i];
    }
    return NULL;
}

static void zb_add_fixup(ZbCompiler* c, const char* name, uint32_t patch_pc,
                         int line, int col)
{
    ZbFixup* f;
    if (c->fixup_count >= ZB_MAX_FIXUPS)
        zb_fail(c, line, col, "too many jumps");
    f = &c->fixups[c->fixup_count++];
    memset(f, 0, sizeof(*f));
    zb_strlcpy(f->name, name, sizeof(f->name));
    f->patch_pc = patch_pc;
    f->proc = c->ctx->proc_index;
    f->line = line;
    f->col = col;
}

static void zb_define_label(ZbCompiler* c)
{
    ZbToken* t = zb_cur(c);
    char name[ZB_MAX_NAME];
    ZbLabel* label;
    ZbProcCtx* ctx = c->ctx;
    int i;

    zb_strlcpy(name, t->text, sizeof(name));
    zb_adv(c);                        /* 名字 */
    zb_expect_type(c, ZB_TK_COLON, "':'");

    label = zb_find_label(ctx, name);
    if (label && label->defined)
        zb_fail(c, t->line, t->col, "duplicate label '%s'", name);
    if (!label) {
        if (ctx->label_count >= ZB_MAX_LABELS)
            zb_fail(c, t->line, t->col, "too many labels");
        label = &ctx->labels[ctx->label_count++];
        memset(label, 0, sizeof(*label));
        zb_strlcpy(label->name, name, sizeof(label->name));
    }
    label->pc = c->prog->code_count;
    label->defined = 1;

    for (i = 0; i < c->fixup_count; ++i) {
        ZbFixup* f = &c->fixups[i];
        if (!f->resolved && f->proc == ctx->proc_index &&
            zb_stricmp(f->name, name) == 0) {
            c->prog->code[f->patch_pc].b = label->pc;
            f->resolved = 1;
        }
    }
}

static void zb_resolve_fixups(ZbCompiler* c, ZbProcCtx* ctx)
{
    int i;
    for (i = 0; i < c->fixup_count; ++i) {
        ZbFixup* f = &c->fixups[i];
        ZbLabel* label;
        if (f->resolved || f->proc != ctx->proc_index) continue;
        label = zb_find_label(ctx, f->name);
        if (!label || !label->defined)
            zb_fail(c, f->line, f->col, "undefined label '%s'", f->name);
        c->prog->code[f->patch_pc].b = label->pc;
        f->resolved = 1;
    }
}

/* ---------- 过程查找 ---------- */

static int zb_find_proc(ZbCompiler* c, const char* name)
{
    int i;
    for (i = 0; i < c->pre_count; ++i) {
        if (zb_stricmp(c->pre[i].name, name) == 0) return i;
    }
    return -1;
}

/* ---------- 表达式 ---------- */

static void zb_parse_or(ZbCompiler* c);
static void zb_compile_expr(ZbCompiler* c);

static int zb_parse_args(ZbCompiler* c, int line, int col)
{
    int argc = 0;
    zb_expect_type(c, ZB_TK_LPAREN, "'('");
    if (zb_accept_type(c, ZB_TK_RPAREN)) return 0;
    for (;;) {
        zb_compile_expr(c);
        argc++;
        if (argc > ZB_MAX_PARAMS)
            zb_fail(c, line, col, "too many arguments (max %d)", ZB_MAX_PARAMS);
        if (zb_accept_type(c, ZB_TK_COMMA)) continue;
        break;
    }
    zb_expect_type(c, ZB_TK_RPAREN, "')'");
    return argc;
}

static int zb_call_returns_value(ZbCompiler* c, const char* name)
{
    const ZbBuiltin* b = zb_builtin_lookup(name);
    int pi;
    if (b) return b->returns_value;
    pi = zb_find_proc(c, name);
    if (pi >= 0) return c->pre[pi].is_function;
    return 0;
}

static void zb_emit_call(ZbCompiler* c, const char* name, int argc,
                         int want_value, int line, int col)
{
    const ZbBuiltin* b = zb_builtin_lookup(name);
    int pi = zb_find_proc(c, name);

    if (b) {
        if (argc < b->min_args || (b->max_args >= 0 && argc > b->max_args))
            zb_fail(c, line, col,
                    "%s() expects %d..%d arguments, got %d",
                    b->name, b->min_args,
                    b->max_args < 0 ? 16 : b->max_args, argc);
        if (want_value && !b->returns_value)
            zb_fail(c, line, col, "%s() does not return a value", name);
        zb_emit(c, ZB_OP_CALLB, b->id, (uint32_t)argc);
        return;
    }
    if (pi >= 0) {
        ZbPreProc* p = &c->pre[pi];
        if (argc != p->nparams)
            zb_fail(c, line, col, "%s expects %d arguments, got %d",
                    p->name, p->nparams, argc);
        if (want_value && !p->is_function)
            zb_fail(c, line, col, "%s is a SUB and does not return a value", name);
        zb_emit(c, ZB_OP_CALL, argc, (uint32_t)p->proc_index);
        return;
    }
    zb_fail(c, line, col, "unknown function or SUB '%s'", name);
}

static void zb_parse_ident_expr(ZbCompiler* c)
{
    ZbToken* t = zb_cur(c);
    char name[ZB_MAX_NAME];
    ZbSym* sym;

    zb_strlcpy(name, t->text, sizeof(name));
    zb_adv(c);

    if (zb_cur(c)->type == ZB_TK_LPAREN) {
        sym = zb_find_sym(c, name);
        if (sym && sym->is_array) {
            zb_expect_type(c, ZB_TK_LPAREN, "'('");
            zb_compile_expr(c);
            zb_expect_type(c, ZB_TK_RPAREN, "')'");
            zb_emit(c, ZB_OP_LOAD_ARR, sym->arr_index, 0);
            return;
        }
        {
            int argc = zb_parse_args(c, t->line, t->col);
            zb_emit_call(c, name, argc, 1, t->line, t->col);
        }
        return;
    }

    if (zb_stricmp(name, "true") == 0) { zb_push_number(c, 1.0); return; }
    if (zb_stricmp(name, "false") == 0) { zb_push_number(c, 0.0); return; }
    if (zb_stricmp(name, "pi") == 0) { zb_push_number(c, 3.14159265358979323846); return; }

    sym = zb_find_sym(c, name);
    if (!sym) {
        const ZbBuiltin* b = zb_builtin_lookup(name);
        if (b && b->min_args == 0 && b->returns_value) {
            zb_emit_call(c, name, 0, 1, t->line, t->col);
            return;
        }
        sym = zb_add_global_scalar(c, name, t->line, t->col);
    }
    if (sym->is_array)
        zb_fail(c, t->line, t->col, "array '%s' requires an index", name);
    zb_emit_load_sym(c, sym->is_local, sym->slot);
}

static void zb_parse_primary(ZbCompiler* c)
{
    ZbToken* t = zb_cur(c);
    switch (t->type) {
    case ZB_TK_NUMBER:
        zb_adv(c);
        zb_push_number(c, t->num);
        return;
    case ZB_TK_STRING: {
        int idx = zb_program_add_string(c->prog, t->text);
        if (idx < 0) zb_fail(c, t->line, t->col, "too many string constants");
        zb_adv(c);
        zb_emit(c, ZB_OP_PUSH_STR, 0, (uint32_t)idx);
        return;
    }
    case ZB_TK_LPAREN:
        zb_adv(c);
        zb_compile_expr(c);
        zb_expect_type(c, ZB_TK_RPAREN, "')'");
        return;
    case ZB_TK_IDENT:
        zb_parse_ident_expr(c);
        return;
    default:
        zb_fail(c, t->line, t->col, "expression expected but found '%s'", t->text);
    }
}

static void zb_parse_pow(ZbCompiler* c)
{
    zb_parse_primary(c);
    if (zb_accept_op(c, "^")) {
        zb_parse_pow(c);
        zb_emit(c, ZB_OP_POW, 0, 0);
    }
}

static void zb_parse_unary(ZbCompiler* c)
{
    if (zb_token_is_op(zb_cur(c), "-")) {
        zb_adv(c);
        zb_parse_unary(c);
        zb_emit(c, ZB_OP_NEG, 0, 0);
        return;
    }
    if (zb_token_is_op(zb_cur(c), "+")) {
        zb_adv(c);
        zb_parse_unary(c);
        return;
    }
    zb_parse_pow(c);
}

static void zb_parse_mul(ZbCompiler* c)
{
    zb_parse_unary(c);
    for (;;) {
        ZbToken* t = zb_cur(c);
        if (zb_token_is_op(t, "*")) { zb_adv(c); zb_parse_unary(c); zb_emit(c, ZB_OP_MUL, 0, 0); }
        else if (zb_token_is_op(t, "/")) { zb_adv(c); zb_parse_unary(c); zb_emit(c, ZB_OP_DIV, 0, 0); }
        else if (zb_token_is_op(t, "\\")) { zb_adv(c); zb_parse_unary(c); zb_emit(c, ZB_OP_DIV, 0, 0); }
        else if (zb_token_is_keyword(t, "mod")) { zb_adv(c); zb_parse_unary(c); zb_emit(c, ZB_OP_MOD, 0, 0); }
        else if (zb_token_is_op(t, "&"))
            zb_fail(c, t->line, t->col, "string concatenation '&' is not supported; use PRINT with commas");
        else break;
    }
}

static void zb_parse_add(ZbCompiler* c)
{
    zb_parse_mul(c);
    for (;;) {
        ZbToken* t = zb_cur(c);
        if (zb_token_is_op(t, "+")) { zb_adv(c); zb_parse_mul(c); zb_emit(c, ZB_OP_ADD, 0, 0); }
        else if (zb_token_is_op(t, "-")) { zb_adv(c); zb_parse_mul(c); zb_emit(c, ZB_OP_SUB, 0, 0); }
        else break;
    }
}

static void zb_parse_compare(ZbCompiler* c)
{
    zb_parse_add(c);
    for (;;) {
        ZbToken* t = zb_cur(c);
        int op = -1;
        if (zb_token_is_op(t, "=")) op = ZB_OP_EQ;
        else if (zb_token_is_op(t, "<>")) op = ZB_OP_NE;
        else if (zb_token_is_op(t, "<")) op = ZB_OP_LT;
        else if (zb_token_is_op(t, "<=")) op = ZB_OP_LE;
        else if (zb_token_is_op(t, ">")) op = ZB_OP_GT;
        else if (zb_token_is_op(t, ">=")) op = ZB_OP_GE;
        if (op < 0) break;
        zb_adv(c);
        zb_parse_add(c);
        zb_emit(c, op, 0, 0);
    }
}

static void zb_parse_not(ZbCompiler* c)
{
    if (zb_token_is_keyword(zb_cur(c), "not")) {
        zb_adv(c);
        zb_parse_not(c);
        zb_emit(c, ZB_OP_NOT, 0, 0);
        return;
    }
    zb_parse_compare(c);
}

static void zb_parse_and(ZbCompiler* c)
{
    zb_parse_not(c);
    while (zb_token_is_keyword(zb_cur(c), "and")) {
        uint32_t jz;
        uint32_t jmp;
        zb_adv(c);
        zb_emit(c, ZB_OP_DUP, 0, 0);
        jz = zb_emit(c, ZB_OP_JZ, 0, 0);
        zb_emit(c, ZB_OP_POP, 0, 0);
        zb_parse_not(c);
        jmp = zb_emit(c, ZB_OP_JMP, 0, 0);
        c->prog->code[jz].b = c->prog->code_count;
        zb_emit(c, ZB_OP_POP, 0, 0);
        zb_push_number(c, 0.0);
        c->prog->code[jmp].b = c->prog->code_count;
    }
}

static void zb_parse_or(ZbCompiler* c)
{
    zb_parse_and(c);
    while (zb_token_is_keyword(zb_cur(c), "or")) {
        uint32_t jnz;
        uint32_t jmp;
        zb_adv(c);
        zb_emit(c, ZB_OP_DUP, 0, 0);
        jnz = zb_emit(c, ZB_OP_JNZ, 0, 0);
        zb_emit(c, ZB_OP_POP, 0, 0);
        zb_parse_and(c);
        jmp = zb_emit(c, ZB_OP_JMP, 0, 0);
        c->prog->code[jnz].b = c->prog->code_count;
        zb_emit(c, ZB_OP_POP, 0, 0);
        zb_push_number(c, 1.0);
        c->prog->code[jmp].b = c->prog->code_count;
    }
}

static void zb_compile_expr(ZbCompiler* c)
{
    zb_parse_or(c);
}

/* ---------- 语句 ---------- */

static void zb_parse_statement(ZbCompiler* c);

static void zb_parse_inline_statements(ZbCompiler* c)
{
    while (!c->failed && !zb_at_end(c)) {
        ZbTokType t = zb_cur(c)->type;
        if (t == ZB_TK_EOL || t == ZB_TK_EOF) break;
        if (zb_token_is_keyword(zb_cur(c), "else")) break;
        if (t == ZB_TK_COLON) { zb_adv(c); continue; }
        zb_parse_statement(c);
        if (c->failed) return;
        t = zb_cur(c)->type;
        if (t != ZB_TK_EOL && t != ZB_TK_EOF && t != ZB_TK_COLON &&
            !zb_token_is_keyword(zb_cur(c), "else"))
            zb_fail(c, zb_cur(c)->line, zb_cur(c)->col,
                    "unexpected '%s' after statement", zb_cur(c)->text);
    }
}

static void zb_parse_if(ZbCompiler* c)
{
    uint32_t jz;
    zb_adv(c);                         /* IF */
    zb_compile_expr(c);
    if (!zb_accept_kw(c, "then"))
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "'THEN' expected");
    if (zb_is_stmt_end(c)) {
        ZbIfBlock* blk;
        jz = zb_emit(c, ZB_OP_JZ, 0, 0);
        if (c->ctx->if_count >= ZB_MAX_IFS)
            zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "IF nesting too deep");
        blk = &c->ctx->ifs[c->ctx->if_count++];
        blk->jz_patch = jz;
        blk->end_count = 0;
        return;
    }
    /* 单行 IF ... THEN ... [ELSE ...] */
    jz = zb_emit(c, ZB_OP_JZ, 0, 0);
    zb_parse_inline_statements(c);
    if (zb_token_is_keyword(zb_cur(c), "else")) {
        uint32_t jmp;
        zb_adv(c);
        jmp = zb_emit(c, ZB_OP_JMP, 0, 0);
        c->prog->code[jz].b = c->prog->code_count;
        zb_parse_inline_statements(c);
        c->prog->code[jmp].b = c->prog->code_count;
    } else {
        c->prog->code[jz].b = c->prog->code_count;
    }
}

static void zb_do_endif(ZbCompiler* c)
{
    int i;
    ZbIfBlock* blk;
    if (c->ctx->if_count <= 0)
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "ENDIF without IF");
    blk = &c->ctx->ifs[c->ctx->if_count - 1];
    if (blk->jz_patch)
        c->prog->code[blk->jz_patch].b = c->prog->code_count;
    for (i = 0; i < blk->end_count; ++i)
        c->prog->code[blk->end_jumps[i]].b = c->prog->code_count;
    c->ctx->if_count--;
}

static void zb_parse_elseif(ZbCompiler* c)
{
    ZbIfBlock* blk;
    uint32_t jmp;
    if (c->ctx->if_count <= 0)
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "ELSEIF without IF");
    blk = &c->ctx->ifs[c->ctx->if_count - 1];
    jmp = zb_emit(c, ZB_OP_JMP, 0, 0);
    if (blk->end_count >= 32)
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "too many ELSEIF branches");
    blk->end_jumps[blk->end_count++] = jmp;
    if (blk->jz_patch)
        c->prog->code[blk->jz_patch].b = c->prog->code_count;
    zb_adv(c);                          /* ELSEIF */
    zb_compile_expr(c);
    if (!zb_accept_kw(c, "then"))
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "'THEN' expected");
    blk->jz_patch = zb_emit(c, ZB_OP_JZ, 0, 0);
}

static void zb_parse_else(ZbCompiler* c)
{
    ZbIfBlock* blk;
    uint32_t jmp;
    if (c->ctx->if_count <= 0)
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "ELSE without IF");
    blk = &c->ctx->ifs[c->ctx->if_count - 1];
    jmp = zb_emit(c, ZB_OP_JMP, 0, 0);
    if (blk->end_count >= 32)
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "too many ELSEIF branches");
    blk->end_jumps[blk->end_count++] = jmp;
    if (blk->jz_patch)
        c->prog->code[blk->jz_patch].b = c->prog->code_count;
    blk->jz_patch = 0;
    zb_adv(c);                          /* ELSE */
}

static void zb_parse_for(ZbCompiler* c)
{
    char name[ZB_MAX_NAME];
    ZbToken* t;
    ZbSym* var;
    ZbLoop* loop;
    uint32_t start_pc, jz_neg, jz1, jz2, jmp_body;
    uint8_t var_is_local;
    uint16_t var_slot;
    int lim_is_local_i, step_is_local_i;
    uint8_t lim_is_local, step_is_local;
    uint16_t lim_slot, step_slot;
    int line, col;

    zb_adv(c);                          /* FOR */
    t = zb_cur(c);
    zb_expect_ident(c, name, sizeof(name));
    line = t->line;
    col = t->col;

    var = zb_find_sym(c, name);
    if (!var) var = zb_add_global_scalar(c, name, line, col);
    if (var->is_array)
        zb_fail(c, line, col, "FOR counter '%s' must be a scalar", name);
    var_is_local = var->is_local;
    var_slot = var->slot;

    zb_expect_op(c, "=");
    zb_compile_expr(c);
    zb_emit_store_sym(c, var_is_local, var_slot);

    zb_expect_kw(c, "to");
    zb_compile_expr(c);
    lim_slot = zb_alloc_temp(c, &lim_is_local_i, line, col);
    lim_is_local = (uint8_t)(lim_is_local_i ? 1 : 0);
    zb_emit_store_sym(c, lim_is_local, lim_slot);

    if (zb_accept_kw(c, "step")) {
        zb_compile_expr(c);
    } else {
        zb_push_number(c, 1.0);
    }
    step_slot = zb_alloc_temp(c, &step_is_local_i, line, col);
    step_is_local = (uint8_t)(step_is_local_i ? 1 : 0);
    zb_emit_store_sym(c, step_is_local, step_slot);

    start_pc = c->prog->code_count;

    /* 按步长符号选择循环条件 */
    zb_emit_load_sym(c, step_is_local, step_slot);
    zb_push_number(c, 0.0);
    zb_emit(c, ZB_OP_GE, 0, 0);
    jz_neg = zb_emit(c, ZB_OP_JZ, 0, 0);

    zb_emit_load_sym(c, var_is_local, var_slot);
    zb_emit_load_sym(c, lim_is_local, lim_slot);
    zb_emit(c, ZB_OP_LE, 0, 0);
    jz1 = zb_emit(c, ZB_OP_JZ, 0, 0);

    jmp_body = zb_emit(c, ZB_OP_JMP, 0, 0);

    c->prog->code[jz_neg].b = c->prog->code_count;
    zb_emit_load_sym(c, var_is_local, var_slot);
    zb_emit_load_sym(c, lim_is_local, lim_slot);
    zb_emit(c, ZB_OP_GE, 0, 0);
    jz2 = zb_emit(c, ZB_OP_JZ, 0, 0);

    c->prog->code[jmp_body].b = c->prog->code_count;

    if (c->ctx->loop_count >= ZB_MAX_LOOPS)
        zb_fail(c, line, col, "loop nesting too deep");
    loop = &c->ctx->loops[c->ctx->loop_count++];
    memset(loop, 0, sizeof(*loop));
    loop->kind = 0;
    loop->start_pc = start_pc;
    loop->end_patch1 = jz1;
    loop->end_patch2 = jz2;
    loop->if_base = c->ctx->if_count;
    loop->var_is_local = var_is_local;
    loop->var_slot = var_slot;
    loop->lim_is_local = lim_is_local;
    loop->lim_slot = lim_slot;
    loop->step_is_local = step_is_local;
    loop->step_slot = step_slot;
}

static ZbLoop* zb_current_loop(ZbCompiler* c, int kind, int line, int col,
                               const char* what)
{
    ZbLoop* loop;
    if (c->ctx->loop_count <= 0)
        zb_fail(c, line, col, "%s without matching loop", what);
    loop = &c->ctx->loops[c->ctx->loop_count - 1];
    if (loop->kind != kind)
        zb_fail(c, line, col, "%s does not match the innermost loop", what);
    return loop;
}

static void zb_parse_next(ZbCompiler* c)
{
    ZbLoop* loop;
    uint32_t end_pc;
    int i;
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;

    loop = zb_current_loop(c, 0, line, col, "NEXT");
    zb_adv(c);                          /* NEXT */
    if (zb_cur(c)->type == ZB_TK_IDENT) zb_adv(c);

    if (c->ctx->if_count != loop->if_base)
        zb_fail(c, line, col, "IF block must be closed before NEXT");

    zb_emit_load_sym(c, loop->var_is_local, loop->var_slot);
    zb_emit_load_sym(c, loop->step_is_local, loop->step_slot);
    zb_emit(c, ZB_OP_ADD, 0, 0);
    zb_emit_store_sym(c, loop->var_is_local, loop->var_slot);
    zb_emit(c, ZB_OP_JMP, 0, loop->start_pc);

    end_pc = c->prog->code_count;
    c->prog->code[loop->end_patch1].b = end_pc;
    c->prog->code[loop->end_patch2].b = end_pc;
    for (i = 0; i < loop->exit_count; ++i)
        c->prog->code[loop->exit_patches[i]].b = end_pc;
    c->ctx->loop_count--;
}

static void zb_parse_while(ZbCompiler* c)
{
    ZbLoop* loop;
    uint32_t jz;
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;

    zb_adv(c);                          /* WHILE */
    if (c->ctx->loop_count >= ZB_MAX_LOOPS)
        zb_fail(c, line, col, "loop nesting too deep");
    loop = &c->ctx->loops[c->ctx->loop_count++];
    memset(loop, 0, sizeof(*loop));
    loop->kind = 1;
    loop->start_pc = c->prog->code_count;
    loop->if_base = c->ctx->if_count;
    zb_compile_expr(c);
    jz = zb_emit(c, ZB_OP_JZ, 0, 0);
    loop->end_patch1 = jz;
}

static void zb_parse_wend(ZbCompiler* c)
{
    ZbLoop* loop;
    int i;
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;
    uint32_t end_pc;

    loop = zb_current_loop(c, 1, line, col, "WEND");
    if (c->ctx->if_count != loop->if_base)
        zb_fail(c, line, col, "IF block must be closed before WEND");
    zb_adv(c);
    zb_emit(c, ZB_OP_JMP, 0, loop->start_pc);
    end_pc = c->prog->code_count;
    c->prog->code[loop->end_patch1].b = end_pc;
    for (i = 0; i < loop->exit_count; ++i)
        c->prog->code[loop->exit_patches[i]].b = end_pc;
    c->ctx->loop_count--;
}

static void zb_parse_repeat(ZbCompiler* c)
{
    ZbLoop* loop;
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;
    zb_adv(c);                          /* REPEAT */
    if (c->ctx->loop_count >= ZB_MAX_LOOPS)
        zb_fail(c, line, col, "loop nesting too deep");
    loop = &c->ctx->loops[c->ctx->loop_count++];
    memset(loop, 0, sizeof(*loop));
    loop->kind = 2;
    loop->start_pc = c->prog->code_count;
    loop->if_base = c->ctx->if_count;
}

static void zb_parse_until(ZbCompiler* c)
{
    ZbLoop* loop;
    int i;
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;
    uint32_t end_pc;

    loop = zb_current_loop(c, 2, line, col, "UNTIL");
    if (c->ctx->if_count != loop->if_base)
        zb_fail(c, line, col, "IF block must be closed before UNTIL");
    zb_adv(c);
    zb_compile_expr(c);
    zb_emit(c, ZB_OP_JZ, 0, loop->start_pc);
    end_pc = c->prog->code_count;
    for (i = 0; i < loop->exit_count; ++i)
        c->prog->code[loop->exit_patches[i]].b = end_pc;
    c->ctx->loop_count--;
}

static void zb_parse_exit_loop(ZbCompiler* c)
{
    ZbLoop* loop;
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;
    uint32_t jmp;

    zb_adv(c);                          /* EXIT */
    if (!zb_accept_kw(c, "for"))
        zb_fail(c, line, col, "EXIT FOR expected");
    if (c->ctx->loop_count <= 0)
        zb_fail(c, line, col, "EXIT FOR outside a loop");
    loop = &c->ctx->loops[c->ctx->loop_count - 1];
    if (loop->kind != 0)
        zb_fail(c, line, col, "EXIT FOR requires a FOR loop");
    jmp = zb_emit(c, ZB_OP_JMP, 0, 0);
    if (loop->exit_count >= 8)
        zb_fail(c, line, col, "too many EXIT FOR statements");
    loop->exit_patches[loop->exit_count++] = jmp;
}

static void zb_parse_goto(ZbCompiler* c)
{
    char name[ZB_MAX_NAME];
    ZbToken* t = zb_cur(c);
    uint32_t pc;
    zb_adv(c);
    zb_expect_ident(c, name, sizeof(name));
    pc = zb_emit(c, ZB_OP_JMP, 0, 0);
    zb_add_fixup(c, name, pc, t->line, t->col);
}

static void zb_parse_gosub(ZbCompiler* c)
{
    char name[ZB_MAX_NAME];
    ZbToken* t = zb_cur(c);
    uint32_t pc;
    zb_adv(c);
    zb_expect_ident(c, name, sizeof(name));
    pc = zb_emit(c, ZB_OP_GOSUB, 0, 0);
    zb_add_fixup(c, name, pc, t->line, t->col);
}

static void zb_parse_return(ZbCompiler* c)
{
    zb_adv(c);                          /* RETURN */
    if (c->ctx->proc_index >= 0) {
        if (c->ctx->is_function) {
            if (!zb_is_stmt_end(c)) zb_compile_expr(c);
            else zb_push_number(c, 0.0);
            zb_emit(c, ZB_OP_RETF, 0, 0);
        } else {
            if (!zb_is_stmt_end(c))
                zb_fail(c, zb_cur(c)->line, zb_cur(c)->col,
                        "a SUB cannot return a value");
            zb_emit(c, ZB_OP_RET, 0, 0);
        }
    } else {
        if (!zb_is_stmt_end(c))
            zb_fail(c, zb_cur(c)->line, zb_cur(c)->col,
                    "RETURN without value only valid in a SUB/GOSUB");
        zb_emit(c, ZB_OP_RETSUB, 0, 0);
    }
}

static void zb_parse_exit_proc(ZbCompiler* c)
{
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;
    uint32_t pc;
    zb_adv(c);                          /* EXITSUB / EXITFUNCTION */
    if (c->ctx->proc_index < 0)
        zb_fail(c, line, col, "EXITSUB outside a SUB/FUNCTION");
    pc = zb_emit(c, ZB_OP_JMP, 0, 0);
    if (c->ctx->exit_count >= ZB_MAX_EXITS)
        zb_fail(c, line, col, "too many EXITSUB statements");
    c->ctx->exits[c->ctx->exit_count++] = pc;
}

static void zb_parse_print(ZbCompiler* c)
{
    int argc = 0;
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;
    zb_adv(c);                          /* PRINT */
    while (!zb_at_end(c)) {
        ZbTokType t = zb_cur(c)->type;
        if (t == ZB_TK_EOL || t == ZB_TK_EOF || t == ZB_TK_COLON) break;
        if (t == ZB_TK_COMMA || t == ZB_TK_SEMICOLON) { zb_adv(c); continue; }
        zb_compile_expr(c);
        argc++;
        if (argc > 16)
            zb_fail(c, line, col, "PRINT supports at most 16 values");
    }
    zb_emit(c, ZB_OP_CALLB, ZB_B_PRINT, (uint32_t)argc);
}

static void zb_parse_dim(ZbCompiler* c)
{
    char name[ZB_MAX_NAME];
    ZbToken* t = zb_cur(c);
    ZbToken* n;
    int size = 0;
    zb_adv(c);                          /* DIM */
    zb_expect_ident(c, name, sizeof(name));
    if (zb_find_global(c, name))
        zb_fail(c, t->line, t->col, "variable '%s' already defined", name);
    if (zb_cur(c)->type == ZB_TK_LPAREN) {
        zb_adv(c);
        n = zb_cur(c);
        if (n->type != ZB_TK_NUMBER || n->num != (double)(int)n->num || n->num < 1.0)
            zb_fail(c, n->line, n->col, "array size must be a positive integer constant");
        size = (int)n->num;
        zb_adv(c);
        if (zb_cur(c)->type == ZB_TK_COMMA)
            zb_fail(c, n->line, n->col, "only one-dimensional arrays are supported");
        zb_expect_type(c, ZB_TK_RPAREN, "')'");
    } else {
        zb_fail(c, t->line, t->col, "DIM requires an array size, e.g. DIM a(10)");
    }
    zb_add_global_array(c, name, size, t->line, t->col);
}

static void zb_parse_local(ZbCompiler* c)
{
    int line = zb_cur(c)->line;
    int col = zb_cur(c)->col;
    zb_adv(c);                          /* LOCAL */
    if (c->ctx->proc_index < 0)
        zb_fail(c, line, col, "LOCAL is only valid inside SUB/FUNCTION");
    for (;;) {
        char name[ZB_MAX_NAME];
        zb_expect_ident(c, name, sizeof(name));
        if (zb_find_local(c->ctx, name))
            zb_fail(c, line, col, "local variable '%s' already declared", name);
        zb_add_local(c, name, line, col);
        if (zb_accept_type(c, ZB_TK_COMMA)) continue;
        break;
    }
}

static void zb_parse_call_statement(ZbCompiler* c)
{
    char name[ZB_MAX_NAME];
    ZbToken* t;
    int argc;
    int line, col;

    zb_adv(c);                          /* CALL */
    t = zb_cur(c);
    zb_expect_ident(c, name, sizeof(name));
    line = t->line;
    col = t->col;
    argc = zb_parse_args(c, line, col);
    zb_emit_call(c, name, argc, 0, line, col);
    if (zb_call_returns_value(c, name))
        zb_emit(c, ZB_OP_POP, 0, 0);
}

static int zb_assign_follows_parens(ZbCompiler* c)
{
    int i = c->pos;
    int depth = 0;
    while (i < c->count) {
        ZbToken* t = &c->toks[i];
        if (t->type == ZB_TK_LPAREN) depth++;
        else if (t->type == ZB_TK_RPAREN) {
            depth--;
            if (depth == 0) { i++; break; }
        } else if (t->type == ZB_TK_EOL || t->type == ZB_TK_EOF) {
            return 0;
        }
        i++;
    }
    if (i < c->count && c->toks[i].type == ZB_TK_OP &&
        strcmp(c->toks[i].text, "=") == 0)
        return 1;
    return 0;
}

static void zb_parse_ident_statement(ZbCompiler* c)
{
    ZbToken* t = zb_cur(c);
    char name[ZB_MAX_NAME];
    ZbSym* sym;

    zb_strlcpy(name, t->text, sizeof(name));

    /* SPEED / ACCEL / DECEL 作为系统变量赋值 */
    if ((zb_stricmp(name, "speed") == 0 ||
         zb_stricmp(name, "accel") == 0 ||
         zb_stricmp(name, "decel") == 0) &&
        zb_token_is_op(zb_peek(c, 1), "=")) {
        const ZbBuiltin* b = zb_builtin_lookup(name);
        zb_adv(c);
        zb_adv(c);
        zb_compile_expr(c);
        zb_emit(c, ZB_OP_CALLB, b->id, 1);
        return;
    }

    sym = zb_find_sym(c, name);

    if (zb_peek(c, 1)->type == ZB_TK_LPAREN) {
        /* 数组元素赋值 */
        if (sym && sym->is_array) {
            zb_adv(c);
            zb_expect_type(c, ZB_TK_LPAREN, "'('");
            zb_compile_expr(c);
            zb_expect_type(c, ZB_TK_RPAREN, "')'");
            zb_expect_op(c, "=");
            zb_compile_expr(c);
            zb_emit(c, ZB_OP_STORE_ARR, sym->arr_index, 0);
            return;
        }
        /* DPOS(axis) = value */
        if (zb_stricmp(name, "dpos") == 0 && zb_assign_follows_parens(c)) {
            zb_adv(c);
            zb_expect_type(c, ZB_TK_LPAREN, "'('");
            zb_compile_expr(c);
            zb_expect_type(c, ZB_TK_RPAREN, "')'");
            zb_expect_op(c, "=");
            zb_compile_expr(c);
            zb_emit(c, ZB_OP_CALLB, ZB_B_DPOS, 2);
            zb_emit(c, ZB_OP_POP, 0, 0);
            return;
        }
        /* 普通函数 / SUB 调用 */
        {
            int argc;
            zb_adv(c);
            argc = zb_parse_args(c, t->line, t->col);
            if (zb_token_is_op(zb_cur(c), "="))
                zb_fail(c, zb_cur(c)->line, zb_cur(c)->col,
                        "cannot assign to a function call");
            zb_emit_call(c, name, argc, 0, t->line, t->col);
            if (zb_call_returns_value(c, name))
                zb_emit(c, ZB_OP_POP, 0, 0);
        }
        return;
    }

    /* 普通赋值 */
    if (zb_token_is_op(zb_peek(c, 1), "=")) {
        uint8_t is_local;
        uint16_t slot;
        zb_adv(c);
        zb_adv(c);
        zb_compile_expr(c);
        if (!sym) sym = zb_add_global_scalar(c, name, t->line, t->col);
        if (sym->is_array)
            zb_fail(c, t->line, t->col, "array '%s' requires an index", name);
        is_local = sym->is_local;
        slot = sym->slot;
        zb_emit_store_sym(c, is_local, slot);
        return;
    }

    /* 无参内建语句（例如 WAITIDLE） */
    {
        const ZbBuiltin* b = zb_builtin_lookup(name);
        if (b && b->min_args == 0) {
            zb_adv(c);
            zb_emit(c, ZB_OP_CALLB, b->id, 0);
            if (b->returns_value) zb_emit(c, ZB_OP_POP, 0, 0);
            return;
        }
    }

    zb_fail(c, t->line, t->col,
            "expected '=' or '(' after '%s'", name);
}

static void zb_parse_wait(ZbCompiler* c)
{
    zb_adv(c);                          /* WAIT */
    if (zb_accept_kw(c, "idle")) {
        zb_emit(c, ZB_OP_CALLB, ZB_B_WAITIDLE, 0);
        return;
    }
    if (!zb_accept_kw(c, "until"))
        zb_fail(c, zb_cur(c)->line, zb_cur(c)->col, "'UNTIL' or 'IDLE' expected");
    {
        uint32_t loop_pc = c->prog->code_count;
        uint32_t jnz;
        zb_compile_expr(c);
        jnz = zb_emit(c, ZB_OP_JNZ, 0, 0);
        zb_emit(c, ZB_OP_YIELD, 0, 0);
        zb_emit(c, ZB_OP_JMP, 0, loop_pc);
        c->prog->code[jnz].b = c->prog->code_count;
    }
}

static void zb_parse_statement(ZbCompiler* c)
{
    ZbToken* t = zb_cur(c);

    if (t->type == ZB_TK_IDENT) {
        if (zb_peek(c, 1)->type == ZB_TK_COLON) {
            zb_define_label(c);
            return;
        }
        zb_parse_ident_statement(c);
        return;
    }

    if (t->type != ZB_TK_KEYWORD)
        zb_fail(c, t->line, t->col, "statement expected but found '%s'", t->text);

    if (zb_stricmp(t->text, "if") == 0) { zb_parse_if(c); return; }
    if (zb_stricmp(t->text, "for") == 0) { zb_parse_for(c); return; }
    if (zb_stricmp(t->text, "next") == 0) { zb_parse_next(c); return; }
    if (zb_stricmp(t->text, "while") == 0) { zb_parse_while(c); return; }
    if (zb_stricmp(t->text, "wend") == 0) { zb_parse_wend(c); return; }
    if (zb_stricmp(t->text, "repeat") == 0) { zb_parse_repeat(c); return; }
    if (zb_stricmp(t->text, "until") == 0) { zb_parse_until(c); return; }
    if (zb_stricmp(t->text, "goto") == 0) { zb_parse_goto(c); return; }
    if (zb_stricmp(t->text, "gosub") == 0) { zb_parse_gosub(c); return; }
    if (zb_stricmp(t->text, "return") == 0) { zb_parse_return(c); return; }
    if (zb_stricmp(t->text, "call") == 0) { zb_parse_call_statement(c); return; }
    if (zb_stricmp(t->text, "local") == 0) { zb_parse_local(c); return; }
    if (zb_stricmp(t->text, "dim") == 0) { zb_parse_dim(c); return; }
    if (zb_stricmp(t->text, "print") == 0) { zb_parse_print(c); return; }
    if (zb_stricmp(t->text, "wait") == 0) { zb_parse_wait(c); return; }
    if (zb_stricmp(t->text, "run") == 0) {
        int line = t->line, col = t->col;
        zb_adv(c);
        zb_compile_expr(c);
        zb_emit(c, ZB_OP_CALLB, ZB_B_RUN, 1);
        zb_emit(c, ZB_OP_POP, 0, 0);
        (void)line; (void)col;
        return;
    }
    if (zb_stricmp(t->text, "exit") == 0) { zb_parse_exit_loop(c); return; }
    if (zb_stricmp(t->text, "exitsub") == 0 ||
        zb_stricmp(t->text, "exitfunction") == 0) {
        zb_parse_exit_proc(c);
        return;
    }
    if (zb_stricmp(t->text, "elseif") == 0) { zb_parse_elseif(c); return; }
    if (zb_stricmp(t->text, "else") == 0) { zb_parse_else(c); return; }
    if (zb_stricmp(t->text, "endif") == 0) { zb_adv(c); zb_do_endif(c); return; }
    if (zb_stricmp(t->text, "end") == 0) {
        if (zb_token_is_keyword(zb_peek(c, 1), "if")) {
            zb_adv(c);
            zb_adv(c);
            zb_do_endif(c);
            return;
        }
        zb_adv(c);
        zb_emit(c, ZB_OP_HALT, 0, 0);
        return;
    }
    if (zb_stricmp(t->text, "stop") == 0) {
        zb_adv(c);
        zb_emit(c, ZB_OP_HALT, 0, 0);
        return;
    }
    if (zb_stricmp(t->text, "then") == 0 ||
        zb_stricmp(t->text, "to") == 0 ||
        zb_stricmp(t->text, "step") == 0)
        zb_fail(c, t->line, t->col, "unexpected '%s'", t->text);

    zb_fail(c, t->line, t->col, "unsupported statement '%s'", t->text);
}

/* ---------- 语句块编译 ---------- */

static void zb_compile_block(ZbCompiler* c)
{
    while (!c->failed && !zb_at_end(c)) {
        ZbToken* t = zb_cur(c);
        if (t->type == ZB_TK_EOL || t->type == ZB_TK_COLON) { zb_adv(c); continue; }
        zb_parse_statement(c);
        if (c->failed) return;
        if (zb_at_end(c)) break;
        t = zb_cur(c);
        if (t->type != ZB_TK_EOL && t->type != ZB_TK_COLON && t->type != ZB_TK_EOF)
            zb_fail(c, t->line, t->col,
                    "unexpected '%s' after statement", t->text);
    }
}

/* ---------- 预扫描 SUB / FUNCTION ---------- */

static void zb_prescan(ZbCompiler* c)
{
    int i = 0;
    while (i < c->count) {
        ZbToken* t = &c->toks[i];
        if (t->type == ZB_TK_KEYWORD &&
            (zb_stricmp(t->text, "sub") == 0 ||
             zb_stricmp(t->text, "function") == 0)) {
            ZbPreProc* p;
            int is_fn = (zb_stricmp(t->text, "function") == 0);
            int header = i;
            int j;

            if (c->pre_count >= ZB_MAX_PROCS)
                zb_fail(c, t->line, t->col, "too many SUB/FUNCTION definitions");
            p = &c->pre[c->pre_count];
            memset(p, 0, sizeof(*p));
            p->header_start = header;
            p->is_function = is_fn;
            p->line = t->line;
            p->proc_index = c->pre_count;
            c->pre_count++;

            i++;
            if (i >= c->count || c->toks[i].type != ZB_TK_IDENT)
                zb_fail(c, t->line, t->col, "SUB/FUNCTION name expected");
            zb_strlcpy(p->name, c->toks[i].text, sizeof(p->name));
            if (zb_builtin_lookup(p->name))
                zb_fail(c, c->toks[i].line, c->toks[i].col,
                        "'%s' conflicts with a built-in function", p->name);
            for (j = 0; j < c->pre_count - 1; ++j) {
                if (zb_stricmp(c->pre[j].name, p->name) == 0)
                    zb_fail(c, c->toks[i].line, c->toks[i].col,
                            "duplicate SUB/FUNCTION '%s'", p->name);
            }
            i++;

            if (i < c->count && c->toks[i].type == ZB_TK_LPAREN) {
                i++;
                if (i < c->count && c->toks[i].type == ZB_TK_RPAREN) {
                    i++;
                } else {
                    for (;;) {
                        int k;
                        if (i >= c->count || c->toks[i].type != ZB_TK_IDENT)
                            zb_fail(c, p->line, 0, "parameter name expected");
                        if (p->nparams >= ZB_MAX_PARAMS)
                            zb_fail(c, c->toks[i].line, c->toks[i].col,
                                    "too many parameters");
                        for (k = 0; k < p->nparams; ++k) {
                            if (zb_stricmp(p->params[k], c->toks[i].text) == 0)
                                zb_fail(c, c->toks[i].line, c->toks[i].col,
                                        "duplicate parameter '%s'", c->toks[i].text);
                        }
                        zb_strlcpy(p->params[p->nparams++], c->toks[i].text,
                                   ZB_MAX_NAME);
                        i++;
                        if (i < c->count && c->toks[i].type == ZB_TK_COMMA) { i++; continue; }
                        break;
                    }
                    if (i >= c->count || c->toks[i].type != ZB_TK_RPAREN)
                        zb_fail(c, p->line, 0, "')' expected in parameter list");
                    i++;
                }
            }

            while (i < c->count && c->toks[i].type == ZB_TK_EOL) i++;
            p->body_start = i;

            while (i < c->count) {
                ZbToken* b = &c->toks[i];
                if (b->type == ZB_TK_KEYWORD &&
                    (zb_stricmp(b->text, "endsub") == 0 ||
                     zb_stricmp(b->text, "endfunction") == 0))
                    break;
                if (b->type == ZB_TK_KEYWORD && zb_stricmp(b->text, "end") == 0 &&
                    i + 1 < c->count &&
                    (zb_token_is_keyword(&c->toks[i + 1], "sub") ||
                     zb_token_is_keyword(&c->toks[i + 1], "function")))
                    break;
                i++;
            }
            if (i >= c->count)
                zb_fail(c, p->line, 0, "missing ENDSUB/ENDFUNCTION for '%s'", p->name);
            p->body_end = i;
            p->range_end = i;
            i++;
        } else {
            i++;
        }
    }
}

static int zb_in_proc_range(ZbCompiler* c, int idx)
{
    int i;
    for (i = 0; i < c->pre_count; ++i) {
        if (idx >= c->pre[i].header_start && idx <= c->pre[i].range_end)
            return 1;
    }
    return 0;
}

/* ---------- 过程编译 ---------- */

static void zb_compile_proc(ZbCompiler* c, int idx)
{
    ZbPreProc* p = &c->pre[idx];
    ZbProcCtx* ctx = &c->procs[idx];
    int i;
    uint32_t ret_pc;

    memset(ctx, 0, sizeof(*ctx));
    ctx->proc_index = idx;
    ctx->nparams = p->nparams;
    ctx->is_function = p->is_function;
    ctx->temp_seq = 0;

    for (i = 0; i < p->nparams; ++i) {
        ZbSym* sym = zb_add_local(c, p->params[i], p->line, 0);
        sym->slot = (uint16_t)i;
        (void)sym;
    }

    c->ctx = ctx;
    c->pos = p->body_start;
    c->end = p->body_end;

    c->prog->procs[idx].entry = c->prog->code_count;
    c->prog->procs[idx].nparams = (uint16_t)p->nparams;
    c->prog->procs[idx].is_function = (uint8_t)(p->is_function ? 1 : 0);
    zb_strlcpy(c->prog->procs[idx].name, p->name, ZB_MAX_NAME);

    while (!c->failed && c->pos < c->end) {
        ZbToken* t = zb_cur(c);
        if (t->type == ZB_TK_EOL || t->type == ZB_TK_COLON) { zb_adv(c); continue; }
        if (t->type == ZB_TK_KEYWORD &&
            (zb_stricmp(t->text, "endsub") == 0 ||
             zb_stricmp(t->text, "endfunction") == 0)) {
            zb_adv(c);
            break;
        }
        if (t->type == ZB_TK_KEYWORD && zb_stricmp(t->text, "end") == 0 &&
            c->pos + 1 < c->count &&
            (zb_token_is_keyword(&c->toks[c->pos + 1], "sub") ||
             zb_token_is_keyword(&c->toks[c->pos + 1], "function"))) {
            zb_adv(c);
            zb_adv(c);
            break;
        }
        zb_parse_statement(c);
        if (c->failed) return;
        if (c->pos >= c->end) break;
        t = zb_cur(c);
        if (t->type != ZB_TK_EOL && t->type != ZB_TK_COLON &&
            t->type != ZB_TK_EOF)
            zb_fail(c, t->line, t->col, "unexpected '%s' after statement", t->text);
    }

    if (ctx->if_count != 0)
        zb_fail(c, p->line, 0, "IF without ENDIF in '%s'", p->name);
    if (ctx->loop_count != 0)
        zb_fail(c, p->line, 0, "loop without NEXT/WEND/UNTIL in '%s'", p->name);

    ret_pc = c->prog->code_count;
    for (i = 0; i < ctx->exit_count; ++i)
        c->prog->code[ctx->exits[i]].b = ret_pc;

    if (p->is_function) {
        zb_push_number(c, 0.0);
        zb_emit(c, ZB_OP_RETF, 0, 0);
    } else {
        zb_emit(c, ZB_OP_RET, 0, 0);
    }

    c->prog->procs[idx].nlocals = (uint16_t)ctx->local_count;

    zb_resolve_fixups(c, ctx);
    c->ctx = &c->main_ctx;
}

/* ---------- 对外接口 ---------- */

int zb_compile(const char* program_name, const char* source,
               ZbProgram* out, ZbDiagList* diags)
{
    ZbTokenList raw;
    ZbToken* all = NULL;
    int all_count = 0;
    ZbToken* main_toks = NULL;
    int main_count = 0;
    ZbCompiler* c = NULL;
    int lex_ok;
    volatile int ok = 0;
    int i;

    zb_program_init(out, program_name);
    zb_lex(source, &raw, diags);
    lex_ok = (diags->count == 0);

    /* 过滤注释，保留其余 token */
    all = (ZbToken*)malloc((size_t)(raw.count + 1) * sizeof(ZbToken));
    main_toks = (ZbToken*)malloc((size_t)(raw.count + 1) * sizeof(ZbToken));
    if (!all || !main_toks) {
        free(all);
        free(main_toks);
        zb_token_list_free(&raw);
        zb_program_free(out);
        return 0;
    }
    for (i = 0; i < raw.count; ++i) {
        if (raw.toks[i].type == ZB_TK_COMMENT) continue;
        all[all_count++] = raw.toks[i];
    }

    if (!lex_ok) goto cleanup;

    c = (ZbCompiler*)calloc(1, sizeof(ZbCompiler));
    if (!c) goto cleanup;
    c->procs = (ZbProcCtx*)calloc(ZB_MAX_PROCS, sizeof(ZbProcCtx));
    if (!c->procs) goto cleanup;
    c->prog = out;
    c->diags = diags;
    c->toks = all;
    c->count = all_count;
    c->ctx = &c->main_ctx;
    c->main_ctx.proc_index = -1;

    if (setjmp(c->jb) != 0) goto cleanup;

    zb_prescan(c);

    /* 主程序 token 流（剔除 SUB/FUNCTION 定义区间） */
    main_toks = (ZbToken*)malloc((size_t)(all_count + 1) * sizeof(ZbToken));
    if (!main_toks) zb_fail(c, 0, 0, "out of memory");
    for (i = 0; i < all_count; ++i) {
        if (all[i].type == ZB_TK_EOF) continue;
        if (zb_in_proc_range(c, i)) continue;
        main_toks[main_count++] = all[i];
    }
    main_toks[main_count++] = all[all_count - 1]; /* EOF */

    c->toks = main_toks;
    c->count = main_count;
    c->pos = 0;
    c->end = main_count - 1;
    c->ctx = &c->main_ctx;

    zb_compile_block(c);
    zb_emit(c, ZB_OP_HALT, 0, 0);
    out->entry = 0;
    zb_resolve_fixups(c, &c->main_ctx);

    for (i = 0; i < c->pre_count; ++i) {
        c->toks = all;
        c->count = all_count;
        zb_compile_proc(c, i);
    }

    ok = 1;

cleanup:
    free(main_toks);
    free(all);
    zb_token_list_free(&raw);
    if (c) {
        free(c->procs);
        free(c);
    }
    if (!ok) {
        zb_program_free(out);
        zb_program_init(out, program_name);
    }
    return ok;
}
