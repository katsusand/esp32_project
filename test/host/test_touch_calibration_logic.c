/*
 * Touch calibration checks: which pairs of 4-point rounds may be saved.
 * Corners tapped mirrored or in the wrong order, a second round that drifted,
 * and the hit test the save dialog uses. Taking the points and drawing the
 * screens live in cyd_input.c / cyd_display.cpp and are checked on a unit.
 */

#include <stdio.h>
#include <string.h>
#include "touch_calibration_logic.h"

#define FRAME_W 320
#define FRAME_H 240

/* The CYD sdkconfig.defaults raw range, in frame corner order. */
static const uint16_t GOOD[TOUCH_CAL_PARAM_COUNT] = {
    300, 400, 300, 3900, 3900, 400, 3900, 3900,
};

static int s_checks;
static int s_failures;

static void check(int ok, const char *what)
{
    ++s_checks;
    if (!ok) {
        ++s_failures;
        printf("FAIL: %s\n", what);
    } else {
        printf("ok: %s\n", what);
    }
}

/* Copies GOOD with its corners reordered: out corner i = GOOD corner order[i]. */
static void reorder(const int order[4], uint16_t *out)
{
    for (int i = 0; i < 4; ++i) {
        out[i * 2] = GOOD[order[i] * 2];
        out[i * 2 + 1] = GOOD[order[i] * 2 + 1];
    }
}

static touch_cal_check_t pair(const uint16_t *a, const uint16_t *b, const touch_cal_affine_t *ref)
{
    uint16_t out[TOUCH_CAL_PARAM_COUNT];
    touch_cal_affine_t affine;
    return touch_cal_check_pair(a, b, ref, FRAME_W, FRAME_H, out, &affine);
}

static void test_solve(touch_cal_affine_t *reference)
{
    check(touch_cal_params_valid(GOOD), "the default raw range is a valid set");
    check(touch_cal_solve(GOOD, FRAME_W, FRAME_H, reference), "the default set solves");
    check(touch_cal_points_near_targets(GOOD, reference, FRAME_W, FRAME_H, 1),
          "the default set maps onto its own corners");

    uint16_t same[TOUCH_CAL_PARAM_COUNT];
    for (int i = 0; i < 4; ++i) {
        same[i * 2] = 2000;
        same[i * 2 + 1] = 2000;
    }
    check(!touch_cal_params_valid(same), "the same spot four times is not a set");
}

static void test_orientation(const touch_cal_affine_t *ref)
{
    static const struct {
        int order[4];
        const char *what;
    } cases[] = {
        { { 2, 3, 0, 1 }, "left and right swapped is rejected" },
        { { 1, 0, 3, 2 }, "top and bottom swapped is rejected" },
        { { 0, 2, 1, 3 }, "two corners taken in the wrong order is rejected" },
        { { 3, 2, 1, 0 }, "upside down is rejected" },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        uint16_t wrong[TOUCH_CAL_PARAM_COUNT];
        reorder(cases[i].order, wrong);
        check(touch_cal_params_valid(wrong) && pair(wrong, wrong, ref) == TOUCH_CAL_CHECK_ORIENTATION,
              cases[i].what);
    }

    uint16_t mirrored[TOUCH_CAL_PARAM_COUNT];
    const int lr[4] = { 2, 3, 0, 1 };
    reorder(lr, mirrored);
    check(pair(mirrored, mirrored, NULL) == TOUCH_CAL_CHECK_OK,
          "with the orientation check off, only the two rounds are compared");
}

static void test_pair(const touch_cal_affine_t *ref)
{
    uint16_t out[TOUCH_CAL_PARAM_COUNT];
    touch_cal_affine_t affine;

    check(touch_cal_check_pair(GOOD, GOOD, ref, FRAME_W, FRAME_H, out, &affine) == TOUCH_CAL_CHECK_OK &&
              memcmp(out, GOOD, sizeof(out)) == 0,
          "two identical good rounds pass and save that set");

    /* About 9 px in X and 7 px in Y: a normal second round. */
    uint16_t close[TOUCH_CAL_PARAM_COUNT];
    memcpy(close, GOOD, sizeof(close));
    close[0] += 100;
    close[3] -= 100;
    close[6] -= 100;
    check(touch_cal_check_pair(GOOD, close, ref, FRAME_W, FRAME_H, out, &affine) == TOUCH_CAL_CHECK_OK &&
              out[0] == 350 && out[3] == 3850 && out[6] == 3850 && out[1] == GOOD[1],
          "a slightly different second round passes and the average is saved");
    check(touch_cal_points_near_targets(GOOD, &affine, FRAME_W, FRAME_H, TOUCH_CAL_AGREE_TOLERANCE_PX),
          "the saved average still maps the corners within tolerance");

    /* About 36 px: the finger landed well off the target. */
    uint16_t far[TOUCH_CAL_PARAM_COUNT];
    memcpy(far, GOOD, sizeof(far));
    far[4] -= 400;
    check(pair(GOOD, far, ref) == TOUCH_CAL_CHECK_MISMATCH, "one corner far off in the second round is rejected");
    check(pair(far, GOOD, ref) == TOUCH_CAL_CHECK_MISMATCH, "and in the first round too");

    uint16_t lopsided[TOUCH_CAL_PARAM_COUNT];
    memcpy(lopsided, GOOD, sizeof(lopsided));
    lopsided[2] = 1500; /* corner 1 pulled toward the middle */
    lopsided[3] = 2500;
    check(pair(lopsided, lopsided, ref) != TOUCH_CAL_CHECK_OK,
          "a set whose own corners do not fit an affine is rejected even when repeated");

    uint16_t same[TOUCH_CAL_PARAM_COUNT];
    for (int i = 0; i < TOUCH_CAL_PARAM_COUNT; ++i) {
        same[i] = 2000;
    }
    check(pair(same, GOOD, ref) == TOUCH_CAL_CHECK_INVALID, "an invalid first round is rejected as invalid");
    check(pair(GOOD, same, ref) == TOUCH_CAL_CHECK_INVALID, "an invalid second round is rejected as invalid");
}

static void test_rect(void)
{
    const touch_cal_rect_t rect = { 20, 150, 120, 40 };
    check(touch_cal_rect_contains(&rect, 20, 150) && touch_cal_rect_contains(&rect, 139, 189),
          "the button's own corners hit");
    check(!touch_cal_rect_contains(&rect, 19, 160) && !touch_cal_rect_contains(&rect, 140, 160) &&
              !touch_cal_rect_contains(&rect, 30, 149) && !touch_cal_rect_contains(&rect, 30, 190),
          "one pixel outside misses");
    const touch_cal_rect_t empty = { 0, 0, 0, 10 };
    check(!touch_cal_rect_contains(&empty, 0, 0) && !touch_cal_rect_contains(NULL, 0, 0),
          "an empty or missing rect never hits");
}

int main(void)
{
    touch_cal_affine_t reference;

    printf("solve\n");
    test_solve(&reference);
    printf("orientation\n");
    test_orientation(&reference);
    printf("two rounds\n");
    test_pair(&reference);
    printf("dialog buttons\n");
    test_rect();
    printf("%d checks, %d failed\n", s_checks, s_failures);
    return s_failures == 0 ? 0 : 1;
}
