#include "zb_package.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void zb_package_init(ZbPackage* pkg)
{
    memset(pkg, 0, sizeof(*pkg));
}

void zb_package_free(ZbPackage* pkg)
{
    int i;
    if (!pkg) return;
    for (i = 0; i < pkg->count; ++i) zb_program_free(&pkg->programs[i]);
    free(pkg->programs);
    memset(pkg, 0, sizeof(*pkg));
}

static int zb_program_copy(ZbProgram* dst, const ZbProgram* src)
{
    uint32_t i;

    zb_program_init(dst, src->name);
    dst->global_count = src->global_count;
    dst->array_count = src->array_count;
    dst->proc_count = src->proc_count;
    dst->entry = src->entry;
    for (i = 0; i < src->array_count; ++i) dst->arrays[i] = src->arrays[i];
    for (i = 0; i < src->proc_count; ++i) dst->procs[i] = src->procs[i];

    for (i = 0; i < src->const_count; ++i) {
        if (zb_program_add_const(dst, src->consts[i]) < 0) goto fail;
    }
    for (i = 0; i < src->str_count; ++i) {
        if (zb_program_add_string(dst, src->strings[i]) < 0) goto fail;
    }
    for (i = 0; i < src->code_count; ++i) {
        ZbIns* ins = &src->code[i];
        if (zb_program_add_ins(dst, ins->op, ins->a, ins->b) < 0) goto fail;
    }
    return 1;

fail:
    zb_program_free(dst);
    return 0;
}

int zb_package_add(ZbPackage* pkg, const ZbProgram* prog)
{
    if (pkg->count >= pkg->cap) {
        int cap = pkg->cap ? pkg->cap * 2 : 8;
        ZbProgram* p = (ZbProgram*)realloc(pkg->programs,
                                           (size_t)cap * sizeof(ZbProgram));
        if (!p) return 0;
        pkg->programs = p;
        pkg->cap = cap;
    }
    if (!zb_program_copy(&pkg->programs[pkg->count], prog)) return 0;
    pkg->count++;
    return 1;
}

static const char* zb_skip_ext(const char* name)
{
    const char* dot = strrchr(name, '.');
    if (dot && (zb_stricmp(dot, ".bas") == 0 || zb_stricmp(dot, ".zbas") == 0))
        return dot;
    return name + strlen(name);
}

int zb_package_find(const ZbPackage* pkg, const char* name)
{
    int i;
    if (!name) return -1;
    for (i = 0; i < pkg->count; ++i) {
        const char* a = pkg->programs[i].name;
        size_t la = (size_t)(zb_skip_ext(a) - a);
        size_t lb = (size_t)(zb_skip_ext(name) - name);
        if (la == lb && zb_strnicmp(a, name, la) == 0) return i;
    }
    return -1;
}

/* ---------- 二进制读写（小端，PC 端通用） ---------- */

static void zb_w8(FILE* f, uint32_t v) { fputc((int)(v & 0xFF), f); }
static void zb_w16(FILE* f, uint32_t v)
{
    fputc((int)(v & 0xFF), f);
    fputc((int)((v >> 8) & 0xFF), f);
}
static void zb_w32(FILE* f, uint32_t v)
{
    zb_w16(f, v & 0xFFFF);
    zb_w16(f, (v >> 16) & 0xFFFF);
}
static void zb_wf64(FILE* f, double d)
{
    uint64_t u;
    int i;
    memcpy(&u, &d, sizeof(u));
    for (i = 0; i < 8; ++i) fputc((int)((u >> (i * 8)) & 0xFF), f);
}

static int zb_r8(FILE* f, uint32_t* v)
{
    int c = fgetc(f);
    if (c == EOF) return 0;
    *v = (uint32_t)(c & 0xFF);
    return 1;
}
static int zb_r16(FILE* f, uint32_t* v)
{
    uint32_t a, b;
    if (!zb_r8(f, &a) || !zb_r8(f, &b)) return 0;
    *v = a | (b << 8);
    return 1;
}
static int zb_r32(FILE* f, uint32_t* v)
{
    uint32_t a, b;
    if (!zb_r16(f, &a) || !zb_r16(f, &b)) return 0;
    *v = a | (b << 16);
    return 1;
}
static int zb_rf64(FILE* f, double* d)
{
    uint64_t u = 0;
    int i;
    for (i = 0; i < 8; ++i) {
        int c = fgetc(f);
        if (c == EOF) return 0;
        u |= ((uint64_t)(c & 0xFF)) << (i * 8);
    }
    memcpy(d, &u, sizeof(*d));
    return 1;
}

static void zb_set_err(char* err, int errcap, const char* text)
{
    if (err && errcap > 0) zb_strlcpy(err, text, (size_t)errcap);
}

int zb_package_save(const ZbPackage* pkg, const char* path,
                    char* err, int errcap)
{
    FILE* f;
    int i;

    f = fopen(path, "wb");
    if (!f) {
        zb_set_err(err, errcap, "cannot open package file for writing");
        return 0;
    }
    fwrite(ZB_PACKAGE_MAGIC, 1, 4, f);
    zb_w32(f, ZB_PACKAGE_VERSION);
    zb_w32(f, (uint32_t)pkg->count);

    for (i = 0; i < pkg->count; ++i) {
        const ZbProgram* p = &pkg->programs[i];
        uint32_t k;
        char name[ZB_MAX_NAME];
        memset(name, 0, sizeof(name));
        zb_strlcpy(name, p->name, sizeof(name));
        fwrite(name, 1, sizeof(name), f);
        zb_w32(f, p->entry);
        zb_w32(f, p->code_count);
        zb_w32(f, p->const_count);
        zb_w32(f, p->str_count);
        zb_w16(f, p->global_count);
        zb_w16(f, p->array_count);
        zb_w16(f, p->proc_count);

        for (k = 0; k < p->code_count; ++k) {
            zb_w8(f, p->code[k].op);
            zb_w8(f, p->code[k].flags);
            zb_w16(f, p->code[k].a);
            zb_w32(f, p->code[k].b);
        }
        for (k = 0; k < p->const_count; ++k) zb_wf64(f, p->consts[k]);
        for (k = 0; k < p->str_count; ++k) {
            size_t len = strlen(p->strings[k]);
            if (len > 60000) len = 60000;
            zb_w16(f, (uint32_t)len);
            fwrite(p->strings[k], 1, len, f);
        }
        for (k = 0; k < p->array_count; ++k) {
            zb_w16(f, p->arrays[k].base);
            zb_w16(f, p->arrays[k].size);
        }
        for (k = 0; k < p->proc_count; ++k) {
            char pname[ZB_MAX_NAME];
            memset(pname, 0, sizeof(pname));
            zb_strlcpy(pname, p->procs[k].name, sizeof(pname));
            fwrite(pname, 1, sizeof(pname), f);
            zb_w32(f, p->procs[k].entry);
            zb_w16(f, p->procs[k].nparams);
            zb_w16(f, p->procs[k].nlocals);
            zb_w8(f, p->procs[k].is_function ? 1 : 0);
        }
    }

    if (ferror(f)) {
        fclose(f);
        zb_set_err(err, errcap, "write error while saving package");
        return 0;
    }
    fclose(f);
    return 1;
}

int zb_package_load(ZbPackage* pkg, const char* path,
                    char* err, int errcap)
{
    FILE* f;
    char magic[4];
    uint32_t version = 0, count = 0, i;

    f = fopen(path, "rb");
    if (!f) {
        zb_set_err(err, errcap, "cannot open package file");
        return 0;
    }
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, ZB_PACKAGE_MAGIC, 4) != 0) {
        fclose(f);
        zb_set_err(err, errcap, "not a ZBasic package (bad magic)");
        return 0;
    }
    if (!zb_r32(f, &version) || version != ZB_PACKAGE_VERSION) {
        fclose(f);
        zb_set_err(err, errcap, "unsupported package version");
        return 0;
    }
    if (!zb_r32(f, &count) || count > 64) {
        fclose(f);
        zb_set_err(err, errcap, "invalid program count in package");
        return 0;
    }

    for (i = 0; i < count; ++i) {
        ZbProgram prog;
        char name[ZB_MAX_NAME];
        uint32_t entry = 0, code_count = 0, const_count = 0, str_count = 0;
        uint32_t global_count = 0, array_count = 0, proc_count = 0, k;

        if (fread(name, 1, sizeof(name), f) != sizeof(name)) goto bad;
        name[sizeof(name) - 1] = '\0';
        if (!zb_r32(f, &entry) || !zb_r32(f, &code_count) ||
            !zb_r32(f, &const_count) || !zb_r32(f, &str_count) ||
            !zb_r16(f, &global_count) || !zb_r16(f, &array_count) ||
            !zb_r16(f, &proc_count))
            goto bad;
        if (code_count > 1000000 || const_count > 1000000 || str_count > 65535 ||
            global_count > ZB_MAX_GLOBALS || array_count > ZB_MAX_ARRAYS ||
            proc_count > ZB_MAX_PROCS)
            goto bad;

        zb_program_init(&prog, name);
        prog.entry = entry;
        prog.global_count = (uint16_t)global_count;
        prog.array_count = (uint16_t)array_count;
        prog.proc_count = (uint16_t)proc_count;

        for (k = 0; k < code_count; ++k) {
            uint32_t op = 0, flags = 0, a = 0, b = 0;
            if (!zb_r8(f, &op) || !zb_r8(f, &flags) ||
                !zb_r16(f, &a) || !zb_r32(f, &b)) {
                zb_program_free(&prog);
                goto bad;
            }
            if (zb_program_add_ins(&prog, (int)op, (int)a, b) < 0) {
                zb_program_free(&prog);
                goto bad;
            }
            prog.code[prog.code_count - 1].flags = (uint8_t)flags;
        }
        for (k = 0; k < const_count; ++k) {
            double d;
            if (!zb_rf64(f, &d) || zb_program_add_const(&prog, d) < 0) {
                zb_program_free(&prog);
                goto bad;
            }
        }
        for (k = 0; k < str_count; ++k) {
            uint32_t len = 0;
            char buf[60001];
            if (!zb_r16(f, &len) || len > 60000) {
                zb_program_free(&prog);
                goto bad;
            }
            if (len && fread(buf, 1, len, f) != len) {
                zb_program_free(&prog);
                goto bad;
            }
            buf[len] = '\0';
            if (zb_program_add_string(&prog, buf) < 0) {
                zb_program_free(&prog);
                goto bad;
            }
        }
        for (k = 0; k < array_count; ++k) {
            uint32_t base = 0, size = 0;
            if (!zb_r16(f, &base) || !zb_r16(f, &size)) {
                zb_program_free(&prog);
                goto bad;
            }
            prog.arrays[k].base = (uint16_t)base;
            prog.arrays[k].size = (uint16_t)size;
        }
        for (k = 0; k < proc_count; ++k) {
            char pname[ZB_MAX_NAME];
            uint32_t pentry = 0, nparams = 0, nlocals = 0, isfn = 0;
            if (fread(pname, 1, sizeof(pname), f) != sizeof(pname)) {
                zb_program_free(&prog);
                goto bad;
            }
            pname[sizeof(pname) - 1] = '\0';
            if (!zb_r32(f, &pentry) || !zb_r16(f, &nparams) ||
                !zb_r16(f, &nlocals) || !zb_r8(f, &isfn)) {
                zb_program_free(&prog);
                goto bad;
            }
            zb_strlcpy(prog.procs[k].name, pname, ZB_MAX_NAME);
            prog.procs[k].entry = pentry;
            prog.procs[k].nparams = (uint16_t)nparams;
            prog.procs[k].nlocals = (uint16_t)nlocals;
            prog.procs[k].is_function = (uint8_t)(isfn ? 1 : 0);
        }

        if (pkg->count >= pkg->cap) {
            int cap = pkg->cap ? pkg->cap * 2 : 8;
            ZbProgram* p = (ZbProgram*)realloc(pkg->programs,
                                               (size_t)cap * sizeof(ZbProgram));
            if (!p) {
                zb_program_free(&prog);
                goto bad;
            }
            pkg->programs = p;
            pkg->cap = cap;
        }
        pkg->programs[pkg->count++] = prog;
    }

    fclose(f);
    return 1;

bad:
    fclose(f);
    zb_set_err(err, errcap, "package file is corrupted or truncated");
    return 0;
}
