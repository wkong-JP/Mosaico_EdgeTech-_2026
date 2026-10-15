// SPDX-License-Identifier: Apache-2.0
// Host unit test for magnetic edge classifier and dock state machine.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../firmware/main/dev_magnet_core.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL line %d: %s\n", __LINE__, #c); ++fails; } } while (0)

int main(void)
{
    printf("dev_magnet_core_test starting...\n");

    /* 1. Baseline calibration success under normal earth field */
    {
        dev_mag_engine_t eng;
        dev_mag_calib_config_t cal = DEV_MAG_CALIB_DEFAULT;
        dev_mag_dock_config_t cfg = DEV_MAG_DOCK_CONFIG_DEFAULT();
        dev_mag_engine_init(&eng, &cal, &cfg);

        CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
        CHECK(!eng.classifier.baseline_ready);
        CHECK(eng.classifier.calib_state == DEV_MAG_CALIB_CALIBRATING);

        // Feed 3 clean samples near zero
        for (uint32_t t = 100; t <= 300; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 50, .y = 80, .z = -120},
                .left  = {.valid = true, .x = -40, .y = 60, .z = -110},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }
        CHECK(eng.classifier.baseline_ready);
        CHECK(eng.classifier.calib_state == DEV_MAG_CALIB_READY);
        CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
        CHECK(eng.classifier.baseline_right_y == 80);
    }

    /* 2. Boot while already attached: baseline rejected */
    {
        dev_mag_engine_t eng;
        dev_mag_calib_config_t cal = DEV_MAG_CALIB_DEFAULT;
        dev_mag_dock_config_t cfg = DEV_MAG_DOCK_CONFIG_DEFAULT();
        dev_mag_engine_init(&eng, &cal, &cfg);

        // Feed samples with right_x already saturated
        for (uint32_t t = 100; t <= 900; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 32000, .y = 80, .z = -120},
                .left  = {.valid = true, .x = -40, .y = 60, .z = -110},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }
        // Baseline must NOT be ready; should fail after 3 rejections
        CHECK(!eng.classifier.baseline_ready);
        CHECK(eng.classifier.calib_state == DEV_MAG_CALIB_FAILED);
        CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
    }

    /* 3. RIGHT edge contact and release cycle with debounce */
    {
        dev_mag_engine_t eng;
        dev_mag_engine_init(&eng, NULL, NULL);

        // Establish baseline first
        for (uint32_t t = 100; t <= 300; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 100, .y = 100, .z = 0},
                .left  = {.valid = true, .x = 100, .y = 100, .z = 0},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }
        CHECK(eng.classifier.baseline_ready);
        CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);

        // 1st saturated right sample -> ATTACH_PENDING
        dev_mag_sample_t sat1 = {
            .right = {.valid = true, .x = 31000, .y = 100, .z = 0},
            .left  = {.valid = true, .x = 100, .y = 100, .z = 0},
            .timestamp_ms = 400,
        };
        dev_mag_engine_feed(&eng, &sat1);
        CHECK(eng.sm.state == DEV_MAG_STATE_ATTACH_PENDING);

        // 2nd saturated right sample -> LOGO_ACTIVE (confirmed after 2 frames)
        dev_mag_sample_t sat2 = {
            .right = {.valid = true, .x = 31500, .y = 100, .z = 0},
            .left  = {.valid = true, .x = 100, .y = 100, .z = 0},
            .timestamp_ms = 500,
        };
        dev_mag_engine_feed(&eng, &sat2);
        CHECK(eng.sm.state == DEV_MAG_STATE_LOGO_ACTIVE);
        CHECK(eng.sm.total_contacts == 1);

        // While attached: stays in LOGO_ACTIVE
        dev_mag_sample_t sat3 = {
            .right = {.valid = true, .x = 32000, .y = 100, .z = 0},
            .left  = {.valid = true, .x = 100, .y = 100, .z = 0},
            .timestamp_ms = 600,
        };
        dev_mag_engine_feed(&eng, &sat3);
        CHECK(eng.sm.state == DEV_MAG_STATE_LOGO_ACTIVE);

        // 1st normal sample -> DETACH_PENDING
        dev_mag_sample_t rel1 = {
            .right = {.valid = true, .x = 100, .y = 100, .z = 0},
            .left  = {.valid = true, .x = 100, .y = 100, .z = 0},
            .timestamp_ms = 700,
        };
        dev_mag_engine_feed(&eng, &rel1);
        CHECK(eng.sm.state == DEV_MAG_STATE_DETACH_PENDING);

        // 2nd normal sample -> still DETACH_PENDING (needs 3 frames)
        dev_mag_sample_t rel2 = {
            .right = {.valid = true, .x = 100, .y = 100, .z = 0},
            .left  = {.valid = true, .x = 100, .y = 100, .z = 0},
            .timestamp_ms = 800,
        };
        dev_mag_engine_feed(&eng, &rel2);
        CHECK(eng.sm.state == DEV_MAG_STATE_DETACH_PENDING);

        // 3rd normal sample -> NORMAL
        dev_mag_sample_t rel3 = {
            .right = {.valid = true, .x = 100, .y = 100, .z = 0},
            .left  = {.valid = true, .x = 100, .y = 100, .z = 0},
            .timestamp_ms = 900,
        };
        dev_mag_engine_feed(&eng, &rel3);
        CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
        CHECK(eng.sm.total_releases == 1);
    }

    /* 4. Wrong edge rejection: LEFT edge saturation must NOT enter LOGO_ACTIVE */
    {
        dev_mag_engine_t eng;
        dev_mag_engine_init(&eng, NULL, NULL);

        for (uint32_t t = 100; t <= 300; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 50, .y = 80, .z = 0},
                .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }
        CHECK(eng.classifier.baseline_ready);

        // Left saturated strongly, Right normal
        for (uint32_t t = 400; t <= 1000; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 50, .y = 80, .z = 0},
                .left  = {.valid = true, .x = 32000, .y = 80, .z = 800},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
            CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
        }
        CHECK(eng.sm.total_contacts == 0);
    }

    /* 5. Jitter / brief bounce: 1-sample glitch in NORMAL must return to NORMAL */
    {
        dev_mag_engine_t eng;
        dev_mag_engine_init(&eng, NULL, NULL);

        for (uint32_t t = 100; t <= 300; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 50, .y = 80, .z = 0},
                .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }

        // Glitch sample (1 frame only)
        dev_mag_sample_t glitch = {
            .right = {.valid = true, .x = 32000, .y = 80, .z = 0},
            .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
            .timestamp_ms = 400,
        };
        dev_mag_engine_feed(&eng, &glitch);
        CHECK(eng.sm.state == DEV_MAG_STATE_ATTACH_PENDING);

        // Immediate drop back to normal
        dev_mag_sample_t norm = {
            .right = {.valid = true, .x = 50, .y = 80, .z = 0},
            .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
            .timestamp_ms = 500,
        };
        dev_mag_engine_feed(&eng, &norm);
        CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
        CHECK(eng.sm.total_contacts == 0);
    }

    /* 6. Hardware overflow -32768 handled properly as saturation */
    {
        dev_mag_engine_t eng;
        dev_mag_engine_init(&eng, NULL, NULL);

        for (uint32_t t = 100; t <= 300; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 50, .y = 80, .z = 0},
                .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }

        // Right sensor hardware clipped (-32768)
        for (uint32_t t = 400; t <= 600; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = -32768, .y = 80, .z = 0},
                .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }
        CHECK(eng.sm.state == DEV_MAG_STATE_LOGO_ACTIVE);
        CHECK(eng.sm.total_contacts == 1);
    }

    /* 7. I2C Error (valid = false): no state corruption */
    {
        dev_mag_engine_t eng;
        dev_mag_engine_init(&eng, NULL, NULL);

        for (uint32_t t = 100; t <= 300; t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 50, .y = 80, .z = 0},
                .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }

        // Feeding invalid samples
        dev_mag_sample_t bad = {
            .right = {.valid = false, .x = 0, .y = 0, .z = 0},
            .left  = {.valid = false, .x = 0, .y = 0, .z = 0},
            .timestamp_ms = 400,
        };
        dev_mag_engine_feed(&eng, &bad);
        CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
        CHECK(eng.sm.sample_errors > 0);
    }

    /* 8. 100 continuous Attach / Detach stress test */
    {
        dev_mag_engine_t eng;
        dev_mag_engine_init(&eng, NULL, NULL);

        uint32_t t = 100;
        // Baseline
        for (int i = 0; i < 3; ++i, t += 100) {
            dev_mag_sample_t s = {
                .right = {.valid = true, .x = 50, .y = 80, .z = 0},
                .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                .timestamp_ms = t,
            };
            dev_mag_engine_feed(&eng, &s);
        }

        for (int cycle = 0; cycle < 100; ++cycle) {
            // Attach (2 saturated samples)
            for (int k = 0; k < 2; ++k, t += 100) {
                dev_mag_sample_t s = {
                    .right = {.valid = true, .x = 31000, .y = 80, .z = 0},
                    .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                    .timestamp_ms = t,
                };
                dev_mag_engine_feed(&eng, &s);
            }
            CHECK(eng.sm.state == DEV_MAG_STATE_LOGO_ACTIVE);

            // Detach (3 normal samples)
            for (int k = 0; k < 3; ++k, t += 100) {
                dev_mag_sample_t s = {
                    .right = {.valid = true, .x = 50, .y = 80, .z = 0},
                    .left  = {.valid = true, .x = 50, .y = 80, .z = 0},
                    .timestamp_ms = t,
                };
                dev_mag_engine_feed(&eng, &s);
            }
            CHECK(eng.sm.state == DEV_MAG_STATE_NORMAL);
        }
        CHECK(eng.sm.total_contacts == 100);
        CHECK(eng.sm.total_releases == 100);
    }

    if (fails == 0) {
        printf("ALL DEV_MAGNET_CORE TESTS PASSED (0 failures)\n");
        return 0;
    } else {
        printf("DEV_MAGNET_CORE TESTS FAILED (%d failures)\n", fails);
        return 1;
    }
}
