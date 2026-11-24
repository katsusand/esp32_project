#include "touch_calibration_logic.h"

#include <stddef.h>

static const uint16_t TOUCH_CAL_RAW_MAX = 4095;
static const uint16_t TOUCH_CAL_MIN_SPAN = 100;
static const int32_t TOUCH_CAL_MIN_AREA = 10000;

static int32_t touch_cal_abs_i32(int32_t value)
{
    return value < 0 ? -value : value;
}

static float touch_cal_abs_f(float value)
{
    return value < 0.0f ? -value : value;
}

static int32_t touch_cal_triangle_area2(const uint16_t *params, size_t a, size_t b, size_t c)
{
    int32_t ax = params[a * 2];
    int32_t ay = params[a * 2 + 1];
    int32_t bx = params[b * 2];
    int32_t by = params[b * 2 + 1];
    int32_t cx = params[c * 2];
    int32_t cy = params[c * 2 + 1];

    return touch_cal_abs_i32((bx - ax) * (cy - ay) - (by - ay) * (cx - ax));
}

bool touch_cal_params_valid(const uint16_t *params)
{
    if (params == NULL) {
        return false;
    }

    uint16_t min_x = UINT16_MAX;
    uint16_t min_y = UINT16_MAX;
    uint16_t max_x = 0;
    uint16_t max_y = 0;

    for (size_t i = 0; i < TOUCH_CAL_POINT_COUNT; ++i) {
        uint16_t x = params[i * 2];
        uint16_t y = params[i * 2 + 1];
        if (x > TOUCH_CAL_RAW_MAX || y > TOUCH_CAL_RAW_MAX) {
            return false;
        }
        min_x = x < min_x ? x : min_x;
        min_y = y < min_y ? y : min_y;
        max_x = x > max_x ? x : max_x;
        max_y = y > max_y ? y : max_y;
    }

    if ((max_x - min_x) < TOUCH_CAL_MIN_SPAN || (max_y - min_y) < TOUCH_CAL_MIN_SPAN) {
        return false;
    }

    for (size_t a = 0; a < 2; ++a) {
        for (size_t b = a + 1; b < 3; ++b) {
            for (size_t c = b + 1; c < 4; ++c) {
                if (touch_cal_triangle_area2(params, a, b, c) >= TOUCH_CAL_MIN_AREA) {
                    return true;
                }
            }
        }
    }

    return false;
}

void touch_cal_target(int32_t index, int32_t width, int32_t height, int32_t *x, int32_t *y)
{
    if (x != NULL) {
        *x = (index & 2) ? (width - 1) : 0;
    }
    if (y != NULL) {
        *y = (index & 1) ? (height - 1) : 0;
    }
}

/* Same normal-equation solve LovyanGFX uses (in-place Gauss-Jordan inverse). */
bool touch_cal_solve(const uint16_t *params, int32_t width, int32_t height, touch_cal_affine_t *out)
{
    if (params == NULL || out == NULL) {
        return false;
    }

    uint32_t vect[6] = { 0, 0, 0, 0, 0, 0 };
    float mat[3][3] = {
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f },
    };

    for (int32_t i = 0; i < TOUCH_CAL_POINT_COUNT; ++i) {
        int32_t tx = 0;
        int32_t ty = 0;
        touch_cal_target(i, width, height, &tx, &ty);
        int32_t px = params[i * 2];
        int32_t py = params[i * 2 + 1];
        float a = (float)(px * px);
        mat[0][0] += a;
        a = (float)(px * py);
        mat[0][1] += a;
        mat[1][0] += a;
        a = (float)px;
        mat[0][2] += a;
        mat[2][0] += a;
        a = (float)(py * py);
        mat[1][1] += a;
        a = (float)py;
        mat[1][2] += a;
        mat[2][1] += a;
        mat[2][2] += 1.0f;

        vect[0] += (uint32_t)(px * tx);
        vect[1] += (uint32_t)(py * tx);
        vect[2] += (uint32_t)tx;
        vect[3] += (uint32_t)(px * ty);
        vect[4] += (uint32_t)(py * ty);
        vect[5] += (uint32_t)ty;
    }

    for (int k = 0; k < 3; ++k) {
        float t = mat[k][k];
        if (t == 0.0f) {
            return false;
        }

        for (int i = 0; i < 3; ++i) {
            mat[k][i] /= t;
        }

        mat[k][k] = 1.0f / t;
        for (int j = 0; j < 3; ++j) {
            if (j == k) {
                continue;
            }

            float u = mat[j][k];
            for (int i = 0; i < 3; ++i) {
                if (i != k) {
                    mat[j][i] -= mat[k][i] * u;
                } else {
                    mat[j][i] = -u / t;
                }
            }
        }
    }

    float v0 = (float)vect[0];
    float v1 = (float)vect[1];
    float v2 = (float)vect[2];
    out->m[0] = mat[0][0] * v0 + mat[0][1] * v1 + mat[0][2] * v2;
    out->m[1] = mat[1][0] * v0 + mat[1][1] * v1 + mat[1][2] * v2;
    out->m[2] = mat[2][0] * v0 + mat[2][1] * v1 + mat[2][2] * v2;

    float v3 = (float)vect[3];
    float v4 = (float)vect[4];
    float v5 = (float)vect[5];
    out->m[3] = mat[0][0] * v3 + mat[0][1] * v4 + mat[0][2] * v5;
    out->m[4] = mat[1][0] * v3 + mat[1][1] * v4 + mat[1][2] * v5;
    out->m[5] = mat[2][0] * v3 + mat[2][1] * v4 + mat[2][2] * v5;
    return true;
}

void touch_cal_map(const touch_cal_affine_t *affine, int32_t raw_x, int32_t raw_y, int32_t *x, int32_t *y)
{
    if (affine == NULL) {
        return;
    }

    /* Truncation order kept from the original runtime transform. */
    int32_t tx = (int32_t)(affine->m[0] * (float)raw_x + affine->m[1] * (float)raw_y) + (int32_t)affine->m[2];
    int32_t ty = (int32_t)(affine->m[3] * (float)raw_x + affine->m[4] * (float)raw_y) + (int32_t)affine->m[5];
    if (x != NULL) {
        *x = tx;
    }
    if (y != NULL) {
        *y = ty;
    }
}

/*
 * Which raw axis drives one frame axis, and in which direction:
 * +1 / -1 for raw X, +2 / -2 for raw Y, 0 when neither dominates.
 */
static int touch_cal_axis_signature(float from_raw_x, float from_raw_y)
{
    float ax = touch_cal_abs_f(from_raw_x);
    float ay = touch_cal_abs_f(from_raw_y);
    if (ax > ay) {
        return from_raw_x > 0.0f ? 1 : -1;
    }
    if (ay > ax) {
        return from_raw_y > 0.0f ? 2 : -2;
    }
    return 0;
}

bool touch_cal_same_orientation(const touch_cal_affine_t *affine, const touch_cal_affine_t *reference)
{
    if (affine == NULL || reference == NULL) {
        return false;
    }

    int x_sig = touch_cal_axis_signature(affine->m[0], affine->m[1]);
    int y_sig = touch_cal_axis_signature(affine->m[3], affine->m[4]);
    int ref_x_sig = touch_cal_axis_signature(reference->m[0], reference->m[1]);
    int ref_y_sig = touch_cal_axis_signature(reference->m[3], reference->m[4]);

    return x_sig != 0 && y_sig != 0 && x_sig == ref_x_sig && y_sig == ref_y_sig;
}

bool touch_cal_points_near_targets(const uint16_t *params,
                                   const touch_cal_affine_t *affine,
                                   int32_t width,
                                   int32_t height,
                                   int32_t tolerance)
{
    if (params == NULL || affine == NULL) {
        return false;
    }

    for (int32_t i = 0; i < TOUCH_CAL_POINT_COUNT; ++i) {
        int32_t tx = 0;
        int32_t ty = 0;
        int32_t x = 0;
        int32_t y = 0;
        touch_cal_target(i, width, height, &tx, &ty);
        touch_cal_map(affine, params[i * 2], params[i * 2 + 1], &x, &y);
        if (touch_cal_abs_i32(x - tx) > tolerance || touch_cal_abs_i32(y - ty) > tolerance) {
            return false;
        }
    }
    return true;
}

static touch_cal_check_t touch_cal_check_set(const uint16_t *params,
                                             const touch_cal_affine_t *reference,
                                             int32_t width,
                                             int32_t height,
                                             touch_cal_affine_t *out_affine)
{
    if (!touch_cal_params_valid(params) || !touch_cal_solve(params, width, height, out_affine)) {
        return TOUCH_CAL_CHECK_INVALID;
    }
    if (reference != NULL && !touch_cal_same_orientation(out_affine, reference)) {
        return TOUCH_CAL_CHECK_ORIENTATION;
    }
    return TOUCH_CAL_CHECK_OK;
}

touch_cal_check_t touch_cal_check_pair(const uint16_t *first,
                                       const uint16_t *second,
                                       const touch_cal_affine_t *reference,
                                       int32_t width,
                                       int32_t height,
                                       uint16_t *out_params,
                                       touch_cal_affine_t *out_affine)
{
    if (first == NULL || second == NULL || out_params == NULL || out_affine == NULL) {
        return TOUCH_CAL_CHECK_INVALID;
    }

    touch_cal_affine_t first_affine;
    touch_cal_affine_t second_affine;
    touch_cal_check_t result = touch_cal_check_set(first, reference, width, height, &first_affine);
    if (result != TOUCH_CAL_CHECK_OK) {
        return result;
    }
    result = touch_cal_check_set(second, reference, width, height, &second_affine);
    if (result != TOUCH_CAL_CHECK_OK) {
        return result;
    }

    if (!touch_cal_points_near_targets(second, &first_affine, width, height, TOUCH_CAL_AGREE_TOLERANCE_PX) ||
        !touch_cal_points_near_targets(first, &second_affine, width, height, TOUCH_CAL_AGREE_TOLERANCE_PX)) {
        return TOUCH_CAL_CHECK_MISMATCH;
    }

    uint16_t average[TOUCH_CAL_PARAM_COUNT];
    for (int32_t i = 0; i < TOUCH_CAL_PARAM_COUNT; ++i) {
        average[i] = (uint16_t)(((uint32_t)first[i] + (uint32_t)second[i] + 1U) / 2U);
    }
    touch_cal_affine_t average_affine;
    result = touch_cal_check_set(average, reference, width, height, &average_affine);
    if (result != TOUCH_CAL_CHECK_OK) {
        return result;
    }

    for (int32_t i = 0; i < TOUCH_CAL_PARAM_COUNT; ++i) {
        out_params[i] = average[i];
    }
    *out_affine = average_affine;
    return TOUCH_CAL_CHECK_OK;
}

const char *touch_cal_check_name(touch_cal_check_t result)
{
    switch (result) {
    case TOUCH_CAL_CHECK_OK:
        return "ok";
    case TOUCH_CAL_CHECK_INVALID:
        return "invalid";
    case TOUCH_CAL_CHECK_ORIENTATION:
        return "orientation";
    case TOUCH_CAL_CHECK_MISMATCH:
        return "mismatch";
    default:
        return "unknown";
    }
}

bool touch_cal_rect_contains(const touch_cal_rect_t *rect, int32_t x, int32_t y)
{
    if (rect == NULL || rect->w <= 0 || rect->h <= 0) {
        return false;
    }
    return x >= rect->x && x < rect->x + rect->w && y >= rect->y && y < rect->y + rect->h;
}
