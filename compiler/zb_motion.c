#include "zb_motion.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void zb_set_error(char* err, int errcap, const char* text)
{
    if (err && errcap > 0) zb_strlcpy(err, text, (size_t)errcap);
}

void zb_motion_init(ZbMotion* m)
{
    int i;
    memset(m, 0, sizeof(*m));
    for (i = 0; i < ZB_MAX_AXES; ++i) {
        m->axes[i].atype = ZB_ATYPE_PULSE;
        m->axes[i].units = 1.0;
        m->axes[i].speed = 100.0;
        m->axes[i].accel = 1000.0;
        m->axes[i].decel = 1000.0;
        m->axes[i].state = ZB_AXIS_IDLE;
    }
}

ZbAxis* zb_motion_axis(ZbMotion* m, int index)
{
    if (index < 0 || index >= ZB_MAX_AXES) return NULL;
    return &m->axes[index];
}

/* 梯形速度规划：加速到 vmax，末端按剩余距离减速。 */
static double zb_profile_speed(const ZbMotion* m, double remaining, double dt_s)
{
    double v = m->vcur + m->accel * dt_s;
    double vdec;
    if (v > m->vmax) v = m->vmax;
    if (m->decel > 0.0) {
        vdec = sqrt(2.0 * m->decel * (remaining > 0.0 ? remaining : 0.0));
        if (v > vdec) v = vdec;
    }
    if (v < 0.0) v = 0.0;
    return v;
}

int zb_motion_start_linear(ZbMotion* m, const int* axes, const double* targets,
                           int n, int absolute, double speed,
                           double accel, double decel,
                           char* err, int errcap)
{
    int i, j;
    double sum = 0.0;

    if (n < 1 || n > 8) {
        zb_set_error(err, errcap, "linear interpolation supports 1..8 axes");
        return 0;
    }
    for (i = 0; i < n; ++i) {
        if (axes[i] < 0 || axes[i] >= ZB_MAX_AXES) {
            zb_set_error(err, errcap, "axis index out of range");
            return 0;
        }
        for (j = 0; j < i; ++j) {
            if (axes[j] == axes[i]) {
                zb_set_error(err, errcap, "duplicate axis in interpolation");
                return 0;
            }
        }
    }
    if (m->active) {
        zb_set_error(err, errcap, "motion in progress");
        return 0;
    }

    m->n = n;
    for (i = 0; i < n; ++i) {
        ZbAxis* a = &m->axes[axes[i]];
        m->axis_idx[i] = axes[i];
        m->start[i] = a->dpos;
        m->delta[i] = (absolute ? targets[i] : a->dpos + targets[i]) - a->dpos;
        sum += m->delta[i] * m->delta[i];
    }
    m->dist = sqrt(sum);
    if (m->dist < 1e-9) {
        for (i = 0; i < n; ++i) {
            ZbAxis* a = &m->axes[axes[i]];
            a->dpos = m->start[i] + m->delta[i];
            a->mpos = a->dpos;
        }
        return 1;
    }

    m->vmax = speed > 0.0 ? speed : m->axes[axes[0]].speed;
    m->accel = accel > 0.0 ? accel : m->axes[axes[0]].accel;
    m->decel = decel > 0.0 ? decel : m->axes[axes[0]].decel;
    m->vcur = 0.0;
    m->traveled = 0.0;
    m->active = 1;
    for (i = 0; i < n; ++i) m->axes[axes[i]].state = ZB_AXIS_MOVING;
    return 1;
}

int zb_motion_start_circular(ZbMotion* m, int ax1, int ax2,
                             double end1, double end2,
                             double c1, double c2, int absolute, int dir,
                             double speed, double accel, double decel,
                             char* err, int errcap)
{
    double s1, s2, e1, e2, cx, cy, r0, r1, a0, a1, dth;

    if (ax1 < 0 || ax1 >= ZB_MAX_AXES || ax2 < 0 || ax2 >= ZB_MAX_AXES ||
        ax1 == ax2) {
        zb_set_error(err, errcap, "invalid circular interpolation axes");
        return 0;
    }
    if (m->active) {
        zb_set_error(err, errcap, "motion in progress");
        return 0;
    }

    s1 = m->axes[ax1].dpos;
    s2 = m->axes[ax2].dpos;
    cx = absolute ? c1 : s1 + c1;
    cy = absolute ? c2 : s2 + c2;
    e1 = absolute ? end1 : s1 + end1;
    e2 = absolute ? end2 : s2 + end2;

    r0 = hypot(s1 - cx, s2 - cy);
    r1 = hypot(e1 - cx, e2 - cy);
    if (r0 < 1e-9) {
        zb_set_error(err, errcap, "arc start point equals the centre");
        return 0;
    }
    if (fabs(r1 - r0) > (r0 * 1e-3 + 1e-6)) {
        zb_set_error(err, errcap, "arc end point is not on the circle");
        return 0;
    }

    a0 = atan2(s2 - cy, s1 - cx);
    a1 = atan2(e2 - cy, e1 - cx);
    dth = a1 - a0;
    if (dir >= 0) {
        while (dth <= 0.0) dth += 2.0 * M_PI;
    } else {
        while (dth >= 0.0) dth -= 2.0 * M_PI;
    }

    m->active = 2;
    m->ax1 = ax1;
    m->ax2 = ax2;
    m->cx = cx;
    m->cy = cy;
    m->radius = r0;
    m->theta0 = a0;
    m->theta_end = a0 + dth;
    m->theta = a0;
    m->sign = (dth >= 0.0) ? 1.0 : -1.0;
    m->arc_len = fabs(dth) * r0;
    m->dist = m->arc_len;
    m->end1 = e1;
    m->end2 = e2;
    m->vmax = speed > 0.0 ? speed : m->axes[ax1].speed;
    m->accel = accel > 0.0 ? accel : m->axes[ax1].accel;
    m->decel = decel > 0.0 ? decel : m->axes[ax1].decel;
    m->vcur = 0.0;
    m->traveled = 0.0;
    m->axes[ax1].state = ZB_AXIS_MOVING;
    m->axes[ax2].state = ZB_AXIS_MOVING;
    return 1;
}

void zb_motion_cancel(ZbMotion* m, int axis)
{
    int i;
    if (axis < 0) {
        m->active = 0;
        m->cam_active = 0;
        for (i = 0; i < ZB_MAX_AXES; ++i) m->axes[i].state = ZB_AXIS_IDLE;
        return;
    }
    if (axis >= ZB_MAX_AXES) return;
    m->axes[axis].state = ZB_AXIS_IDLE;
    if (m->cam_active && (axis == m->cam_master || axis == m->cam_slave))
        m->cam_active = 0;
    if (m->active == 1) {
        for (i = 0; i < m->n; ++i) {
            if (m->axis_idx[i] == axis) { m->active = 0; break; }
        }
    } else if (m->active == 2) {
        if (axis == m->ax1 || axis == m->ax2) m->active = 0;
    }
}

void zb_motion_tick(ZbMotion* m, double dt_ms)
{
    double dt_s = dt_ms / 1000.0;
    int i;

    if (dt_s <= 0.0) return;

    if (m->active == 1) {
        double remaining = m->dist - m->traveled;
        double v = zb_profile_speed(m, remaining, dt_s);
        double step = v * dt_s;
        double frac;

        if (step > remaining) step = remaining;
        m->traveled += step;
        m->vcur = (dt_s > 0.0) ? step / dt_s : 0.0;
        if (m->traveled >= m->dist - 1e-9) {
            for (i = 0; i < m->n; ++i) {
                ZbAxis* a = &m->axes[m->axis_idx[i]];
                a->dpos = m->start[i] + m->delta[i];
                a->state = ZB_AXIS_IDLE;
            }
            m->active = 0;
            m->vcur = 0.0;
        } else {
            frac = m->traveled / m->dist;
            for (i = 0; i < m->n; ++i) {
                ZbAxis* a = &m->axes[m->axis_idx[i]];
                a->dpos = m->start[i] + m->delta[i] * frac;
            }
        }
    } else if (m->active == 2) {
        double remaining = m->dist - m->traveled;
        double v = zb_profile_speed(m, remaining, dt_s);
        double step = v * dt_s;

        if (step > remaining) step = remaining;
        m->traveled += step;
        m->vcur = (dt_s > 0.0) ? step / dt_s : 0.0;
        if (m->traveled >= m->dist - 1e-9) {
            m->axes[m->ax1].dpos = m->end1;
            m->axes[m->ax2].dpos = m->end2;
            m->axes[m->ax1].state = ZB_AXIS_IDLE;
            m->axes[m->ax2].state = ZB_AXIS_IDLE;
            m->active = 0;
            m->vcur = 0.0;
        } else {
            m->theta = m->theta0 + m->sign * (m->traveled / m->radius);
            m->axes[m->ax1].dpos = m->cx + m->radius * cos(m->theta);
            m->axes[m->ax2].dpos = m->cy + m->radius * sin(m->theta);
        }
    }

    if (m->cam_active) {
        double master = m->axes[m->cam_master].dpos;
        double slave = zb_motion_cam_lookup(m, m->cam_table, master);
        m->axes[m->cam_slave].dpos = slave;
        m->axes[m->cam_slave].state = ZB_AXIS_CAM;
    }

    /* 反馈位置一阶跟随，模拟伺服滞后 */
    for (i = 0; i < ZB_MAX_AXES; ++i) {
        ZbAxis* a = &m->axes[i];
        if (a->state == ZB_AXIS_IDLE) {
            a->mpos = a->dpos;
        } else {
            a->mpos += (a->dpos - a->mpos) * 0.35;
            if (fabs(a->dpos - a->mpos) < 1e-6) a->mpos = a->dpos;
        }
    }
}

int zb_motion_is_idle(const ZbMotion* m)
{
    return m->active == 0;
}

int zb_motion_axis_status(const ZbMotion* m, int axis)
{
    if (axis < 0 || axis >= ZB_MAX_AXES) return 0;
    return m->axes[axis].state;
}

int zb_motion_add_cam_point(ZbMotion* m, int table, double master, double slave,
                            char* err, int errcap)
{
    ZbCamTable* cam;
    int i, pos;

    if (table < 0 || table >= ZB_MAX_CAM_TABLES) {
        zb_set_error(err, errcap, "cam table index out of range");
        return 0;
    }
    cam = &m->cams[table];
    if (cam->count >= ZB_CAM_POINTS) {
        zb_set_error(err, errcap, "cam table is full");
        return 0;
    }
    pos = cam->count;
    while (pos > 0 && cam->points[pos - 1].master > master) {
        cam->points[pos] = cam->points[pos - 1];
        pos--;
    }
    cam->points[pos].master = master;
    cam->points[pos].slave = slave;
    cam->count++;
    (void)i;
    return 1;
}

int zb_motion_start_cam(ZbMotion* m, int table, int master, int slave,
                        char* err, int errcap)
{
    if (table < 0 || table >= ZB_MAX_CAM_TABLES || m->cams[table].count < 2) {
        zb_set_error(err, errcap, "cam table needs at least 2 points");
        return 0;
    }
    if (master < 0 || master >= ZB_MAX_AXES || slave < 0 || slave >= ZB_MAX_AXES ||
        master == slave) {
        zb_set_error(err, errcap, "invalid cam master/slave axis");
        return 0;
    }
    m->cam_active = 1;
    m->cam_table = table;
    m->cam_master = master;
    m->cam_slave = slave;
    m->axes[slave].state = ZB_AXIS_CAM;
    return 1;
}

void zb_motion_stop_cam(ZbMotion* m)
{
    if (m->cam_active) {
        m->axes[m->cam_slave].state = ZB_AXIS_IDLE;
    }
    m->cam_active = 0;
}

double zb_motion_cam_lookup(const ZbMotion* m, int table, double master)
{
    const ZbCamTable* cam;
    int i;

    if (table < 0 || table >= ZB_MAX_CAM_TABLES) return master;
    cam = &m->cams[table];
    if (cam->count == 0) return master;
    if (cam->count == 1) return cam->points[0].slave;
    if (master <= cam->points[0].master) return cam->points[0].slave;
    if (master >= cam->points[cam->count - 1].master)
        return cam->points[cam->count - 1].slave;

    for (i = 1; i < cam->count; ++i) {
        if (master <= cam->points[i].master) {
            double m0 = cam->points[i - 1].master;
            double m1 = cam->points[i].master;
            double s0 = cam->points[i - 1].slave;
            double s1 = cam->points[i].slave;
            double t = (m1 > m0) ? (master - m0) / (m1 - m0) : 0.0;
            return s0 + (s1 - s0) * t;
        }
    }
    return cam->points[cam->count - 1].slave;
}
