#ifndef TOUCH_CALIBRATION_LOGIC_H
#define TOUCH_CALIBRATION_LOGIC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Touch calibration math and the checks that decide whether a calibration may
 * be saved. Pure C with no SDK calls, so test/host can run it.
 *
 * English contract:
 *  - A set is 4 raw points, uint16_t[8] = {x0,y0,x1,y1,x2,y2,x3,y3}, in the
 *    order of the touch calibration frame corners (0,0), (0,h-1), (w-1,0),
 *    (w-1,h-1). This is also the NVS blob layout; do not reorder it.
 *  - An affine maps raw to the touch calibration frame (before the runtime
 *    rotation/flip): x = m[0]*rx + m[1]*ry + m[2], y = m[3]*rx + m[4]*ry + m[5].
 *  - Nothing here touches the running mapping. The caller decides when a
 *    checked result becomes the runtime affine.
 */

#define TOUCH_CAL_POINT_COUNT 4
#define TOUCH_CAL_PARAM_COUNT (TOUCH_CAL_POINT_COUNT * 2)

/*
 * How far, in pixels, a point of one set may land from its target when it is
 * mapped through the other set's affine. Larger lets a careless second round
 * through; smaller makes a careful user repeat. Tune on hardware.
 */
#define TOUCH_CAL_AGREE_TOLERANCE_PX 16

typedef struct {
    float m[6];
} touch_cal_affine_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t w;
    int32_t h;
} touch_cal_rect_t;

typedef enum {
    TOUCH_CAL_CHECK_OK = 0,
    /* A set failed the range / spread / area check, or its affine is singular. */
    TOUCH_CAL_CHECK_INVALID,
    /* A set maps raw axes onto the screen differently from the reference:
       corners tapped mirrored, upside down, or in the wrong order. */
    TOUCH_CAL_CHECK_ORIENTATION,
    /* Both sets are valid on their own but disagree with each other. */
    TOUCH_CAL_CHECK_MISMATCH,
} touch_cal_check_t;

/* Raw range, spread and triangle area. The same rule as loading from NVS. */
bool touch_cal_params_valid(const uint16_t *params);

/* Least-squares affine from the 4 points to the frame corners. False when singular. */
bool touch_cal_solve(const uint16_t *params, int32_t width, int32_t height, touch_cal_affine_t *out);

void touch_cal_map(const touch_cal_affine_t *affine, int32_t raw_x, int32_t raw_y, int32_t *x, int32_t *y);

/* Frame corner `index` (0..3) of a width x height frame. */
void touch_cal_target(int32_t index, int32_t width, int32_t height, int32_t *x, int32_t *y);

/*
 * True when both affines drive frame X from the same raw axis with the same
 * sign, and frame Y likewise.
 */
bool touch_cal_same_orientation(const touch_cal_affine_t *affine, const touch_cal_affine_t *reference);

/* True when every point of `params` mapped through `affine` is within `tolerance` px of its target. */
bool touch_cal_points_near_targets(const uint16_t *params,
                                   const touch_cal_affine_t *affine,
                                   int32_t width,
                                   int32_t height,
                                   int32_t tolerance);

/*
 * Checks two rounds of 4 points and, on TOUCH_CAL_CHECK_OK, writes their
 * average to `out_params` and its affine to `out_affine`.
 *
 * `reference` is the affine that defines the expected orientation (the
 * Kconfig default mapping); NULL skips the orientation check. Each set must
 * be valid and correctly oriented, and each set's points must land within
 * TOUCH_CAL_AGREE_TOLERANCE_PX of their targets through the other set's
 * affine.
 */
touch_cal_check_t touch_cal_check_pair(const uint16_t *first,
                                       const uint16_t *second,
                                       const touch_cal_affine_t *reference,
                                       int32_t width,
                                       int32_t height,
                                       uint16_t *out_params,
                                       touch_cal_affine_t *out_affine);

const char *touch_cal_check_name(touch_cal_check_t result);

bool touch_cal_rect_contains(const touch_cal_rect_t *rect, int32_t x, int32_t y);

#ifdef __cplusplus
}
#endif

#endif
