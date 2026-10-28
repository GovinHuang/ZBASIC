/* zbcc —— 命令行字节码编译器 / 虚拟控制器
 *
 * 用法:
 *   zbcc [-d] [-r] [-t task] [-T ms] [-o out.zbp] file.bas [file2.bas ...]
 *
 *   -d        输出反汇编
 *   -r        在虚拟控制器上运行（10ms 周期）
 *   -t name   指定运行的起始程序名（默认第一个）
 *   -T ms     最长仿真时间，默认 60000ms
 *   -o file   保存脱机下载包 (.zbp)
 */
#include "zb_compiler.h"
#include "zb_package.h"
#include "zb_vm.h"
#include "zb_disasm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* zb_read_file(const char* path)
{
    FILE* f = fopen(path, "rb");
    long size;
    char* buf;

    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    buf = (char*)malloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    if (size > 0 && fread(buf, 1, (size_t)size, f) != (size_t)size) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[size] = '\0';
    fclose(f);
    return buf;
}

static void zb_program_name(const char* path, char* out, int cap)
{
    const char* base = path;
    const char* p;
    const char* dot;
    for (p = path; *p; ++p) {
        if (*p == '/' || *p == '\\') base = p + 1;
    }
    dot = strrchr(base, '.');
    if (dot && zb_stricmp(dot, ".bas") == 0) {
        int n = (int)(dot - base);
        if (n >= cap) n = cap - 1;
        memcpy(out, base, (size_t)n);
        out[n] = '\0';
    } else {
        zb_strlcpy(out, base, (size_t)cap);
    }
}

static void zb_print_cb(void* user, const char* text)
{
    (void)user;
    printf("%s\n", text);
}

static void zb_usage(void)
{
    printf("ZBasic compiler %d.%d\n", ZB_VERSION_MAJOR, ZB_VERSION_MINOR);
    printf("usage: zbcc [-d] [-r] [-t task] [-T ms] [-o out.zbp] file.bas ...\n");
}

int main(int argc, char** argv)
{
    const char* out_path = NULL;
    const char* run_task = NULL;
    int disasm = 0;
    int run = 0;
    int max_ms = 60000;
    int first = 1;
    int i;
    int failures = 0;
    ZbPackage pkg;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-d") == 0) { disasm = 1; first = i + 1; }
        else if (strcmp(argv[i], "-r") == 0) { run = 1; first = i + 1; }
        else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) { out_path = argv[++i]; first = i + 1; }
        else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) { run_task = argv[++i]; first = i + 1; }
        else if (strcmp(argv[i], "-T") == 0 && i + 1 < argc) { max_ms = atoi(argv[++i]); first = i + 1; }
        else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            zb_usage();
            return 0;
        } else {
            break;
        }
    }
    if (first >= argc) {
        zb_usage();
        return 2;
    }

    zb_package_init(&pkg);

    for (i = first; i < argc; ++i) {
        char name[ZB_MAX_NAME];
        char* source;
        ZbProgram prog;
        ZbDiagList diags;
        int j;

        zb_program_name(argv[i], name, sizeof(name));
        source = zb_read_file(argv[i]);
        if (!source) {
            fprintf(stderr, "%s: cannot read file\n", argv[i]);
            failures++;
            continue;
        }
        zb_diag_init(&diags);
        if (!zb_compile(name, source, &prog, &diags)) {
            for (j = 0; j < diags.count; ++j)
                fprintf(stderr, "%s(%d,%d): error: %s\n", argv[i],
                        diags.items[j].line, diags.items[j].col,
                        diags.items[j].message);
            failures++;
        } else {
            if (disasm) zb_disasm_program(&prog, stdout);
            if (!zb_package_add(&pkg, &prog)) {
                fprintf(stderr, "%s: out of memory building package\n", argv[i]);
                failures++;
            }
            zb_program_free(&prog);
        }
        zb_diag_free(&diags);
        free(source);
    }

    if (failures) {
        zb_package_free(&pkg);
        return 1;
    }

    if (out_path) {
        char err[128];
        if (!zb_package_save(&pkg, out_path, err, sizeof(err))) {
            fprintf(stderr, "save %s failed: %s\n", out_path, err);
            zb_package_free(&pkg);
            return 1;
        }
        printf("package saved: %s (%d program%s)\n", out_path, pkg.count,
               pkg.count == 1 ? "" : "s");
    }

    if (run) {
        ZbVm vm;
        uint32_t sim = 0;
        int task_id;

        zb_vm_init(&vm);
        zb_vm_set_package(&vm, &pkg);
        zb_vm_set_print(&vm, zb_print_cb, NULL);
        task_id = zb_vm_start(&vm, run_task ? run_task : pkg.programs[0].name);
        if (task_id < 0) {
            fprintf(stderr, "run: program not found\n");
            zb_vm_free(&vm);
            zb_package_free(&pkg);
            return 1;
        }
        while (zb_vm_is_running(&vm) && (int)sim < max_ms) {
            zb_vm_tick(&vm, 10.0);
            sim += 10;
        }
        printf("\n-- simulation stopped at %u ms --\n", sim);
        for (i = 0; i < 4; ++i) {
            printf("axis%d: dpos=%.3f mpos=%.3f status=%d\n", i,
                   vm.motion.axes[i].dpos, vm.motion.axes[i].mpos,
                   zb_motion_axis_status(&vm.motion, i));
        }
        for (i = 0; i < ZB_MAX_TASKS; ++i) {
            if (vm.tasks[i].state == ZB_TASK_EMPTY) continue;
            printf("task%d: %s %s%s%s\n", i,
                   zb_vm_task_state_name(vm.tasks[i].state),
                   vm.package->programs[vm.tasks[i].program].name,
                   vm.tasks[i].error[0] ? " - " : "",
                   vm.tasks[i].error);
        }
        zb_vm_free(&vm);
    }

    zb_package_free(&pkg);
    return 0;
}
