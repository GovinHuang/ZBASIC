/* 脱机下载包：把工程内所有 BASIC 程序的字节码打包成单个 .zbp 镜像。
 * 控制器（或本机的虚拟控制器）下载该镜像后即可脱离 PC 独立运行。
 */
#ifndef ZB_PACKAGE_H
#define ZB_PACKAGE_H

#include "zb_common.h"
#include "zb_program.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_PACKAGE_MAGIC   "ZBPK"
#define ZB_PACKAGE_VERSION 1u

typedef struct {
    ZbProgram* programs;
    int        count;
    int        cap;
} ZbPackage;

void zb_package_init(ZbPackage* pkg);
void zb_package_free(ZbPackage* pkg);
int  zb_package_add(ZbPackage* pkg, const ZbProgram* prog);
int  zb_package_find(const ZbPackage* pkg, const char* name);
int  zb_package_save(const ZbPackage* pkg, const char* path,
                     char* err, int errcap);
int  zb_package_load(ZbPackage* pkg, const char* path,
                     char* err, int errcap);

#ifdef __cplusplus
}
#endif

#endif /* ZB_PACKAGE_H */
