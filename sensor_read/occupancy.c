#include "occupancy.h"

/* ─── Dummy tuning constants ─────────────────────────────────────────────
 * These are PLACEHOLDERS. Iterate α/β/γ and the class thresholds on the
 * terminal side (replay mode in terminal/occupancy_dashboard.py) against
 * logged CSV, then copy the final values in here and reflash.
 *
 * Design matches the Python reference:
 *   a_ewma = λ·a + (1−λ)·a_ewma           (same for v, p)
 *   a_norm = clip01((a_ewma − a_base) / (ADC_MAX − a_base))
 *   v_norm = clip01((v_ewma − v_base) / (ADC_MAX − v_base))
 *   p_norm = clip01(p_ewma)               (presence is already 0/1)
 *   CI     = α·p_norm + β·v_norm + γ·a_norm
 * a_base / v_base are learned from the first OCC_CALIB_N raw samples.
 * ──────────────────────────────────────────────────────────────────────── */
#define OCC_LAMBDA   0.20f   /* EWMA weight; higher = less smoothing  */
#define OCC_ALPHA    0.50f   /* presence (radar) weight — dominant    */
#define OCC_BETA     0.30f   /* vibration weight                      */
#define OCC_GAMMA    0.20f   /* acoustic weight                       */
#define OCC_T_LOW    0.15f   /* CI ≥ this → LOW                       */
#define OCC_T_MED    0.40f   /* CI ≥ this → MED                       */
#define OCC_T_HIGH   0.70f   /* CI ≥ this → HIGH                      */
#define OCC_CALIB_N  30u     /* 30 samples × 100 ms = 3 s noise floor */
#define OCC_ADC_MAX  4095.0f /* 12-bit ADC full-scale                 */

static float    a_ewma, v_ewma, p_ewma;
static float    a_base, v_base;
static uint32_t a_sum,  v_sum;
static uint32_t calib_n;
static int      initialised;
static int      calibrating;

void Occupancy_Init(void)
{
    a_ewma = v_ewma = p_ewma = 0.0f;
    a_base = v_base = 0.0f;
    a_sum  = v_sum  = 0u;
    calib_n     = 0u;
    initialised = 0;
    calibrating = 1;
}

static float clamp01(float x)
{
    if (x < 0.0f) return 0.0f;
    if (x > 1.0f) return 1.0f;
    return x;
}

static float norm_above_base(float ewma_val, float base)
{
    float denom = OCC_ADC_MAX - base;
    if (denom <= 0.0f) return 0.0f;
    return clamp01((ewma_val - base) / denom);
}

OccResult Occupancy_Update(uint16_t acoustic, uint16_t vibration, int presence)
{
    OccResult r;
    float     a_norm, v_norm, p_norm, ci;

    /* Seed the EWMAs with the first sample so the filter doesn't ramp
     * from zero — otherwise the first second of output is garbage. */
    if (!initialised)
    {
        a_ewma = (float)acoustic;
        v_ewma = (float)vibration;
        p_ewma = (float)presence;
        initialised = 1;
    }
    else
    {
        a_ewma = OCC_LAMBDA * (float)acoustic  + (1.0f - OCC_LAMBDA) * a_ewma;
        v_ewma = OCC_LAMBDA * (float)vibration + (1.0f - OCC_LAMBDA) * v_ewma;
        p_ewma = OCC_LAMBDA * (float)presence  + (1.0f - OCC_LAMBDA) * p_ewma;
    }

    /* Learn acoustic/vibration noise floors during the calibration window.
     * Caller is expected to keep the space empty during this period. */
    if (calibrating)
    {
        a_sum  += acoustic;
        v_sum  += vibration;
        calib_n++;
        if (calib_n >= OCC_CALIB_N)
        {
            a_base = (float)a_sum / (float)calib_n;
            v_base = (float)v_sum / (float)calib_n;
            calibrating = 0;
        }
    }

    if (calibrating)
    {
        a_norm = 0.0f;
        v_norm = 0.0f;
    }
    else
    {
        a_norm = norm_above_base(a_ewma, a_base);
        v_norm = norm_above_base(v_ewma, v_base);
    }
    p_norm = clamp01(p_ewma);

    ci = OCC_ALPHA * p_norm + OCC_BETA * v_norm + OCC_GAMMA * a_norm;

    r.ci_milli = (int)(ci * 1000.0f + 0.5f);

    if (calibrating)           r.cls = OCC_EMPTY;
    else if (ci < OCC_T_LOW)   r.cls = OCC_EMPTY;
    else if (ci < OCC_T_MED)   r.cls = OCC_LOW;
    else if (ci < OCC_T_HIGH)  r.cls = OCC_MED;
    else                       r.cls = OCC_HIGH;

    r.calibrating = calibrating;
    return r;
}

char Occupancy_ClassChar(OccClass c)
{
    switch (c)
    {
        case OCC_EMPTY: return 'E';
        case OCC_LOW:   return 'L';
        case OCC_MED:   return 'M';
        case OCC_HIGH:  return 'H';
        default:        return '?';
    }
}
