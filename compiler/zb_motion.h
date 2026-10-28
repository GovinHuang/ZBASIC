/* 运动控制模型：16 轴（脉冲 / 模拟量 / EtherCAT 总线）、直线插补、圆弧插补、电子凸轮。 */
#ifndef ZB_MOTION_H
#define ZB_MOTION_H

#include "zb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZB_MAX_AXES        16
#define ZB_MAX_CAM_TABLES  8
#define ZB_CAM_POINTS      64

/* 轴类型，与 ZMC 系列 ATYPE 含义一致 */
#define ZB_ATYPE_PULSE   1
#define ZB_ATYPE_ANALOG  2
#define ZB_ATYPE_ECAT    3

/* 轴状态 */
#define ZB_AXIS_IDLE     0
#define ZB_AXIS_MOVING   1
#define ZB_AXIS_CAM      2

typedef struct {
    double master;
    double slave;
} ZbCamPoint;

typedef struct {
    ZbCamPoint points[ZB_CAM_POINTS];
    int        count;
} ZbCamTable;

typedef struct {
    uint8_t atype;      /* 1=脉冲 2=模拟量 3=EtherCAT */
    double  units;      /* 每单位脉冲数 */
    double  dpos;       /* 指令位置 */
    double  mpos;       /* 反馈位置 */
    double  speed;      /* 单位/秒 */
    double  accel;      /* 单位/秒^2 */
    double  decel;
    uint8_t state;
} ZbAxis;

typedef struct {
    ZbAxis   axes[ZB_MAX_AXES];
    ZbCamTable cams[ZB_MAX_CAM_TABLES];

    /* 直线 / 圆弧插补组 */
    int     active;      /* 0 空闲，1 直线，2 圆弧 */
    int     n;
    int     axis_idx[8];
    double  start[8];
    double  delta[8];
    double  dist;
    double  traveled;
    double  vmax;
    double  vcur;
    double  accel;
    double  decel;

    /* 圆弧参数 */
    int     ax1, ax2;
    double  cx, cy, radius;
    double  theta0, theta_end, theta, sign;
    double  arc_len;
    double  end1, end2;

    /* 电子凸轮 */
    int     cam_active;
    int     cam_table;
    int     cam_master;
    int     cam_slave;
} ZbMotion;

void zb_motion_init(ZbMotion* m);
ZbAxis* zb_motion_axis(ZbMotion* m, int index);

int zb_motion_start_linear(ZbMotion* m, const int* axes, const double* targets,
                           int n, int absolute, double speed,
                           double accel, double decel,
                           char* err, int errcap);
int zb_motion_start_circular(ZbMotion* m, int ax1, int ax2,
                             double end1, double end2,
                             double c1, double c2, int absolute, int dir,
                             double speed, double accel, double decel,
                             char* err, int errcap);
void zb_motion_cancel(ZbMotion* m, int axis);
void zb_motion_tick(ZbMotion* m, double dt_ms);
int  zb_motion_is_idle(const ZbMotion* m);
int  zb_motion_axis_status(const ZbMotion* m, int axis);

int  zb_motion_add_cam_point(ZbMotion* m, int table, double master, double slave,
                             char* err, int errcap);
int  zb_motion_start_cam(ZbMotion* m, int table, int master, int slave,
                         char* err, int errcap);
void zb_motion_stop_cam(ZbMotion* m);
double zb_motion_cam_lookup(const ZbMotion* m, int table, double master);

#ifdef __cplusplus
}
#endif

#endif /* ZB_MOTION_H */
