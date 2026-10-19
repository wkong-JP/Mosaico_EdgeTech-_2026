// SPDX-License-Identifier: Apache-2.0
// Pure helpers of the device sensor task (no ESP-IDF): axis mapping and averaging. Host-tested.
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

/* Running mean of 3-vectors between two reads by the UI loop. The gyroscope is integrated by the app once per
 * UI step, so it must be the MEAN rate over the time since the previous step, not a single sample. */
typedef struct {
    float sum[3];
    uint32_t n;
    float held[3];   /* last mean, returned when no new sample arrived */
} dev_mean3_t;

static inline void dev_mean3_add(dev_mean3_t *m, const float v[3])
{
    for (int i = 0; i < 3; ++i) m->sum[i] += v[i];
    ++m->n;
}

/* Returns the number of samples the mean is made of (0 = nothing new, the previous mean is returned). */
static inline uint32_t dev_mean3_take(dev_mean3_t *m, float out[3])
{
    const uint32_t n = m->n;
    if (n) {
        for (int i = 0; i < 3; ++i) { m->held[i] = m->sum[i] / (float)n; m->sum[i] = 0.0f; }
        m->n = 0;
    }
    for (int i = 0; i < 3; ++i) out[i] = m->held[i];
    return n;
}

/* Net rotation (degrees) about each axis since the last reset: the integral of the gyroscope rate. Used by the
 * on-screen gyroscope direction check: turn the board 90 degrees counter-clockwise (seen from the glass side) and
 * the Z angle must read about +90 if the sign convention is right-handed. A gap longer than 100 ms is not integrated
 * (the rate during a gap is unknown, and a guess would look like data). */
typedef struct { float deg[3]; } dev_angle3_t;

static inline void dev_angle3_reset(dev_angle3_t *a) { for (int i = 0; i < 3; ++i) a->deg[i] = 0.0f; }

static inline void dev_angle3_add(dev_angle3_t *a, const float rate_dps[3], float dt_s)
{
    if (!(dt_s > 0.0f) || dt_s > 0.1f) return;
    for (int i = 0; i < 3; ++i) a->deg[i] += rate_dps[i] * dt_s;
}

/* chip frame -> UI frame: out = M * in. M must be a proper rotation (signed permutation, det +1): both frames
 * are right-handed, so the same matrix is right for the accelerometer and the gyroscope. */
static inline void dev_map3(const int8_t M[3][3], const float in[3], float out[3])
{
    for (int i = 0; i < 3; ++i) out[i] = (float)M[i][0] * in[0] + (float)M[i][1] * in[1] + (float)M[i][2] * in[2];
}

static inline bool dev_map_is_rotation(const int8_t M[3][3])
{
    for (int i = 0; i < 3; ++i) {
        int row = 0, col = 0;
        for (int j = 0; j < 3; ++j) {
            const int a = M[i][j], b = M[j][i];
            if (a < -1 || a > 1 || b < -1 || b > 1) return false;
            row += a * a;
            col += b * b;
        }
        if (row != 1 || col != 1) return false;   /* exactly one +-1 per row and per column */
    }
    const int det = M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1]) -
                    M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0]) +
                    M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
    return det == 1;
}

/* BMM150 through the vendored Bosch fixed-point API: int16 per axis, already in microtesla (1 LSB = 1 uT; the driver's
 * compensation ends with "/ 16"), -32768 marks an overflowed / not-yet-converted sample. The scale stays a build option
 * (EXP_MAG_LSB_PER_UT) so the log view can be compared with the raw numbers. */
#define DEV_MAG_OVERFLOW (-32768)

static inline bool dev_mag_valid(int x, int y, int z)
{
    return x != DEV_MAG_OVERFLOW && y != DEV_MAG_OVERFLOW && z != DEV_MAG_OVERFLOW;
}

static inline float dev_mag_to_ut(int raw, float lsb_per_ut) { return (float)raw / lsb_per_ut; }

/* Rotation-consistency check of the compass, independent of any reference north. While the board lies flat and is turned
 * once around, the gyroscope angle (counter-clockwise positive) and the magnetometer heading (clockwise positive) must
 * satisfy  head + turned = const.  The deviation from that constant, averaged per 30 degrees of turn, shows how far the
 * field the sensor sees is from a clean circle: a bump of one period per turn is a leftover offset, two periods a gain
 * or axis mismatch, a slope the wrong handedness. The first sample taken near turned = 0 defines the constant. */
#define DEV_SWEEP_BINS 12
typedef struct {
    bool have_ref;
    float ref;
    float sum[DEV_SWEEP_BINS];
    uint16_t n[DEV_SWEEP_BINS];
} dev_sweep_t;

static inline float dev_wrap180(float d)
{
    while (d > 180.0f) d -= 360.0f;
    while (d < -180.0f) d += 360.0f;
    return d;
}

static inline void dev_sweep_reset(dev_sweep_t *s)
{
    s->have_ref = false;
    s->ref = 0.0f;
    for (int i = 0; i < DEV_SWEEP_BINS; ++i) { s->sum[i] = 0.0f; s->n[i] = 0; }
}

static inline void dev_sweep_add(dev_sweep_t *s, float turned_ccw_deg, float head_cw_deg)
{
    if (!s->have_ref) {
        if (fabsf(dev_wrap180(turned_ccw_deg)) > 10.0f) return;      /* start of a sweep: wait until turned is near 0 */
        s->ref = head_cw_deg + turned_ccw_deg;
        s->have_ref = true;
    }
    const float e = dev_wrap180(head_cw_deg + turned_ccw_deg - s->ref);
    float t = fmodf(turned_ccw_deg, 360.0f);
    if (t < 0.0f) t += 360.0f;
    int b = (int)(t / (360.0f / DEV_SWEEP_BINS));
    if (b >= DEV_SWEEP_BINS) b = DEV_SWEEP_BINS - 1;
    if (s->n[b] < 65535) { s->sum[b] += e; ++s->n[b]; }
}

static inline int dev_sweep_filled(const dev_sweep_t *s, int min_n)
{
    int k = 0;
    for (int i = 0; i < DEV_SWEEP_BINS; ++i) if (s->n[i] >= min_n) ++k;
    return k;
}

/* Mean deviation of bin i in degrees; valid only when n[i] > 0. */
static inline float dev_sweep_mean(const dev_sweep_t *s, int i) { return s->n[i] ? s->sum[i] / (float)s->n[i] : 0.0f; }

static inline float dev_sweep_max_dev(const dev_sweep_t *s)
{
    float m = 0.0f;
    for (int i = 0; i < DEV_SWEEP_BINS; ++i)
        if (s->n[i] >= 3 && fabsf(dev_sweep_mean(s, i)) > m) m = fabsf(dev_sweep_mean(s, i));
    return m;
}

/* Battery state of charge estimation based on measured cell voltage (mV).
 * Characterized for the ESP-MOSAICO 80 mAh Li-Po cell.
 * When the BQ27220 CEDV fuel gauge is in default / unlearned state (or returns an implausible
 * SoC contradictory to the open-circuit voltage), this piecewise linear voltage curve provides
 * a reliable, monotonic, and physically accurate percentage (0..100%). */
static inline int dev_battery_soc_from_mv(int mv)
{
    static const struct { int mv; int soc; } CURVE[] = {
        {4150, 100},
        {4002, 90},
        {3969, 80},
        {3938, 70},
        {3880, 60},
        {3824, 50},
        {3794, 40},
        {3753, 30},
        {3677, 20},
        {3574, 10},
        {3490, 0},
    };
    enum { N = (int)(sizeof(CURVE) / sizeof(CURVE[0])) };
    if (mv >= CURVE[0].mv) return 100;
    if (mv <= CURVE[N - 1].mv) return 0;
    for (int i = 0; i < N - 1; ++i) {
        if (mv >= CURVE[i + 1].mv) {
            const int dv = CURVE[i].mv - CURVE[i + 1].mv;
            const int dsoc = CURVE[i].soc - CURVE[i + 1].soc;
            return CURVE[i + 1].soc + (mv - CURVE[i + 1].mv) * dsoc / dv;
        }
    }
    return 0;
}

static inline int dev_battery_resolve_soc(int mv, int raw_soc)
{
    const int v_soc = dev_battery_soc_from_mv(mv);
    if (raw_soc < 0 || raw_soc > 100) return v_soc;
    if ((mv >= 3900 && raw_soc <= 20) || (mv >= 4100 && raw_soc <= 50) ||
        (mv <= 3650 && raw_soc >= 60)) {
        return v_soc;
    }
    return raw_soc;
}

