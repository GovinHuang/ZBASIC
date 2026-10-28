/* ZBasic 虚拟控制器：字节码解释器 + 多任务调度 + 运动轴仿真。
 * IDE 的“下载 / 运行 / 停止”直接驱动该虚拟机；真实控制器可用同一字节码镜像。
 */
#ifndef ZB_VM_H
#define ZB_VM_H

#include "zb_common.h"
#include "zb_program.h"
#include "zb_package.h"
#include "zb_motion.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_MAX_TASKS     8
#define ZB_STACK_SIZE    512
#define ZB_MAX_FRAMES    64
#define ZB_TASK_SLICE    4000

#define ZB_TASK_EMPTY    0
#define ZB_TASK_READY    1
#define ZB_TASK_RUNNING  2
#define ZB_TASK_WAITING  3
#define ZB_TASK_DONE     4
#define ZB_TASK_ERROR    5

#define ZB_VAL_NUMBER    0
#define ZB_VAL_STRING    1

typedef struct {
    uint8_t     type;
    double      num;
    const char* str;
} ZbValue;

typedef struct {
    uint32_t ret_pc;
    uint16_t local_base;
    uint16_t proc;      /* 0xFFFF 表示 GOSUB 帧 */
} ZbFrame;

typedef struct {
    uint8_t  state;
    int      program;
    uint32_t pc;
    ZbValue  stack[ZB_STACK_SIZE];
    int      sp;
    ZbFrame  frames[ZB_MAX_FRAMES];
    int      fp;
    ZbValue  locals[ZB_MAX_LOCALS];
    int      local_top;
    ZbValue* globals;
    int      wait_kind;     /* 0=无 1=延时 2=等待轴空闲 3=YIELD */
    uint32_t wake_tick;
    char     error[128];
    int      id;
} ZbTask;

typedef void (*ZbPrintFn)(void* user, const char* text);

typedef struct {
    ZbPackage* package;
    ZbTask     tasks[ZB_MAX_TASKS];
    ZbMotion   motion;
    uint32_t   tick;
    int        base_axis;
    uint8_t    inputs[64];
    uint8_t    outputs[64];
    ZbPrintFn  print;
    void*      user;
    int        running;
} ZbVm;

void zb_vm_init(ZbVm* vm);
void zb_vm_free(ZbVm* vm);
void zb_vm_reset(ZbVm* vm);
void zb_vm_set_package(ZbVm* vm, ZbPackage* pkg);
void zb_vm_set_print(ZbVm* vm, ZbPrintFn fn, void* user);

int  zb_vm_start(ZbVm* vm, const char* program_name);
void zb_vm_stop(ZbVm* vm);
void zb_vm_tick(ZbVm* vm, double dt_ms);
int  zb_vm_is_running(const ZbVm* vm);
int  zb_vm_active_tasks(const ZbVm* vm);
const char* zb_vm_task_state_name(int state);

#ifdef __cplusplus
}
#endif

#endif /* ZB_VM_H */
