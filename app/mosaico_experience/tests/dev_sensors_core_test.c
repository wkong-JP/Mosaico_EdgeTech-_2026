// SPDX-License-Identifier: Apache-2.0
// Host test for the pure helpers of the device sensor task (firmware/main/dev_sensors_core.h).
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../../../firmware/main/dev_sensors_core.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)
#define NEAR(a, b) (fabsf((a) - (b)) < 1e-5f)
#define NEAR_DEG(a, b) (fabsf((a) - (b)) < 1e-3f)   /* float sums of 50 steps drift by ~2e-5 deg: irrelevant on screen */

int main(void)
{
    /* mean of the samples between two reads, hold when nothing new arrived */
    dev_mean3_t m;
    memset(&m, 0, sizeof(m));
    float out[3];
    CHECK(dev_mean3_take(&m, out) == 0 && out[0] == 0.0f);                 /* nothing yet: zeros, n = 0 */
    const float s1[3] = {1, 2, 3}, s2[3] = {3, 4, 5}, s3[3] = {-1, 0, 7};
    dev_mean3_add(&m, s1); dev_mean3_add(&m, s2); dev_mean3_add(&m, s3);
    CHECK(dev_mean3_take(&m, out) == 3);
    CHECK(NEAR(out[0], 1.0f) && NEAR(out[1], 2.0f) && NEAR(out[2], 5.0f));
    CHECK(dev_mean3_take(&m, out) == 0);                                   /* held */
    CHECK(NEAR(out[0], 1.0f) && NEAR(out[1], 2.0f) && NEAR(out[2], 5.0f));
    dev_mean3_add(&m, s2);
    CHECK(dev_mean3_take(&m, out) == 1 && NEAR(out[0], 3.0f));            /* sums were reset */

    /* integrated angle */
    dev_angle3_t an;
    dev_angle3_reset(&an);
    const float turn[3] = {0.0f, -30.0f, 90.0f};
    for (int i = 0; i < 50; ++i) dev_angle3_add(&an, turn, 0.020f);        /* 1 s at 20 ms steps */
    CHECK(NEAR_DEG(an.deg[0], 0.0f) && NEAR_DEG(an.deg[1], -30.0f) && NEAR_DEG(an.deg[2], 90.0f));
    dev_angle3_add(&an, turn, 0.5f);                                       /* gap: not integrated */
    dev_angle3_add(&an, turn, 0.0f);
    dev_angle3_add(&an, turn, -0.01f);
    CHECK(NEAR_DEG(an.deg[2], 90.0f));
    const float back[3] = {0.0f, 30.0f, -90.0f};
    for (int i = 0; i < 50; ++i) dev_angle3_add(&an, back, 0.020f);        /* turning back returns to zero */
    CHECK(NEAR_DEG(an.deg[1], 0.0f) && NEAR_DEG(an.deg[2], 0.0f));
    dev_angle3_add(&an, turn, 0.020f);
    dev_angle3_reset(&an);
    CHECK(an.deg[0] == 0.0f && an.deg[1] == 0.0f && an.deg[2] == 0.0f);

    /* mapping */
    static const int8_t I[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static const int8_t RZ90[3][3] = {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}};   /* x -> y */
    static const int8_t FLIPXY[3][3] = {{-1, 0, 0}, {0, -1, 0}, {0, 0, 1}};
    static const int8_t MIRROR[3][3] = {{-1, 0, 0}, {0, 1, 0}, {0, 0, 1}}; /* det -1: not a rotation */
    const float v[3] = {1, 2, 3};
    float r[3];
    dev_map3(I, v, r);
    CHECK(NEAR(r[0], 1) && NEAR(r[1], 2) && NEAR(r[2], 3));
    const float ex[3] = {1, 0, 0};
    dev_map3(RZ90, ex, r);
    CHECK(NEAR(r[0], 0) && NEAR(r[1], 1) && NEAR(r[2], 0));
    dev_map3(FLIPXY, v, r);
    CHECK(NEAR(r[0], -1) && NEAR(r[1], -2) && NEAR(r[2], 3));
    CHECK(dev_map_is_rotation(I) && dev_map_is_rotation(RZ90) && dev_map_is_rotation(FLIPXY));
    CHECK(!dev_map_is_rotation(MIRROR));

    /* exhaustive: of the 48 signed permutation matrices exactly 24 are proper rotations */
    static const int perm[6][3] = {{0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}};
    int proper = 0, total = 0;
    for (int p = 0; p < 6; ++p)
        for (int sg = 0; sg < 8; ++sg) {
            int8_t M[3][3] = {{0}};
            for (int i = 0; i < 3; ++i) M[i][perm[p][i]] = (int8_t)((sg >> i) & 1 ? -1 : 1);
            ++total;
            if (dev_map_is_rotation((const int8_t (*)[3])M)) ++proper;
        }
    CHECK(total == 48 && proper == 24);

    /* things that must be rejected */
    static const int8_t ZERO[3][3] = {{0}};
    static const int8_t TWO[3][3] = {{2, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    static const int8_t DUP[3][3] = {{1, 0, 0}, {1, 0, 0}, {0, 0, 1}};
    static const int8_t DIAG[3][3] = {{1, 1, 0}, {0, 1, 0}, {0, 0, 1}};
    CHECK(!dev_map_is_rotation(ZERO) && !dev_map_is_rotation(TWO) && !dev_map_is_rotation(DUP) && !dev_map_is_rotation(DIAG));

    /* BMM150 helpers: overflow marker in any axis invalidates the sample; scale is raw / LSB-per-uT */
    CHECK(dev_mag_valid(0, 0, 0) && dev_mag_valid(32767, -32767, 1));
    CHECK(!dev_mag_valid(-32768, 0, 0) && !dev_mag_valid(0, -32768, 0) && !dev_mag_valid(0, 0, -32768));
    CHECK(NEAR(dev_mag_to_ut(46, 1.0f), 46.0f) && NEAR(dev_mag_to_ut(-160, 16.0f), -10.0f));   /* scale is a parameter */

    /* Device evidence for the mag0 map (0.5.1 photos, uT, board flat face up, top edge toward magnetic N/E/S/W, held by
     * hand): chip (x,y,z). With UI = diag(1,-1,-1) * chip, N minus S must point along +Y (twice the horizontal field,
     * about 60 uT) and E minus W along -X (a field from the north seen with the top edge toward east lies to the left). */
    static const int8_t MAG0[3][3] = {{1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
    CHECK(dev_map_is_rotation(MAG0));
    const float cN[3] = {-495, 131, 121}, cE[3] = {-496, 161, 121}, cS[3] = {-485, 175, 146}, cW[3] = {-440, 135, 137};
    float uN[3], uE[3], uS[3], uW[3];
    dev_map3(MAG0, cN, uN); dev_map3(MAG0, cE, uE); dev_map3(MAG0, cS, uS); dev_map3(MAG0, cW, uW);
    const float ns[3] = {uN[0] - uS[0], uN[1] - uS[1], uN[2] - uS[2]}, ew[3] = {uE[0] - uW[0], uE[1] - uW[1], uE[2] - uW[2]};
    CHECK(ns[1] > 30.0f && ns[1] < 80.0f && fabsf(ns[0]) < 0.5f * ns[1]);     /* +Y, about 2H */
    CHECK(ew[0] < -30.0f && ew[0] > -80.0f && fabsf(ew[1]) < 0.7f * -ew[0]);  /* -X, about 2H */

    /* sweep check: a perfect compass turned counter-clockwise has head = ref - turned -> no deviation; an offset-like
     * distortion (one bump per turn) and a mirrored compass (head = ref + turned) must show up. */
    {
        dev_sweep_t sw;
        dev_sweep_reset(&sw);
        for (int t = -3; t < 360; t += 2) dev_sweep_add(&sw, (float)t, dev_wrap180(100.0f - (float)t) + 360.0f);
        CHECK(dev_sweep_filled(&sw, 3) == DEV_SWEEP_BINS);
        CHECK(dev_sweep_max_dev(&sw) < 2.5f);
        dev_sweep_reset(&sw);
        for (int t = 0; t < 360; t += 2) {
            const float bump = 25.0f * sinf((float)t * 3.14159265f / 180.0f);
            dev_sweep_add(&sw, (float)t, 100.0f - (float)t + bump);
        }
        CHECK(dev_sweep_max_dev(&sw) > 20.0f && dev_sweep_max_dev(&sw) < 28.0f);
        dev_sweep_reset(&sw);
        for (int t = 0; t < 360; t += 2) dev_sweep_add(&sw, (float)t, 100.0f + (float)t);   /* mirrored: heads the wrong way */
        CHECK(dev_sweep_max_dev(&sw) > 100.0f);
        dev_sweep_reset(&sw);
        dev_sweep_add(&sw, 90.0f, 10.0f);                                  /* not near 0: no reference yet */
        CHECK(!sw.have_ref && dev_sweep_filled(&sw, 1) == 0);
    }

    /* battery SoC calculation and uncalibrated gauge override */
    {
        CHECK(dev_battery_soc_from_mv(4250) == 100);
        CHECK(dev_battery_soc_from_mv(4150) == 100);
        CHECK(dev_battery_soc_from_mv(4002) == 90);
        CHECK(dev_battery_soc_from_mv(3824) == 50);
        CHECK(dev_battery_soc_from_mv(3490) == 0);
        CHECK(dev_battery_soc_from_mv(3200) == 0);

        /* 4195 mV with uncalibrated 6% gauge reading -> must resolve to 100% */
        CHECK(dev_battery_resolve_soc(4195, 6) == 100);
        /* 4150 mV with uncalibrated -1 -> must resolve to 100% */
        CHECK(dev_battery_resolve_soc(4150, -1) == 100);
        /* 3824 mV with plausible calibrated 52% -> must keep 52% */
        CHECK(dev_battery_resolve_soc(3824, 52) == 52);
        /* 3450 mV with contradictory 85% -> must resolve to 0% */
        CHECK(dev_battery_resolve_soc(3450, 85) == 0);
    }

    if (fails) { printf("dev_sensors_core: %d FAILED\n", fails); return 1; }
    printf("dev_sensors_core: all checks passed (48 signed permutations: 24 proper rotations)\n");
    return 0;
}
