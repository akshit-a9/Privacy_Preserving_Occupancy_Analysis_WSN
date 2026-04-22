#ifndef OCCUPANCY_H
#define OCCUPANCY_H

#include <stdint.h>

/* Occupancy classifier: EWMA-smoothed weighted Congestion Index,
 * bucketed into four classes. Mirrors the terminal-side Python
 * reference at terminal/occupancy_dashboard.py — the constants in
 * occupancy.c are placeholders until we calibrate against a log. */

typedef enum {
    OCC_EMPTY = 0,
    OCC_LOW   = 1,
    OCC_MED   = 2,
    OCC_HIGH  = 3
} OccClass;

typedef struct {
    int       ci_milli;    /* CI scaled to 0..1000 (milli-CI) */
    OccClass  cls;
    int       calibrating; /* 1 while learning noise floor, 0 after */
} OccResult;

void      Occupancy_Init(void);
OccResult Occupancy_Update(uint16_t acoustic, uint16_t vibration, int presence);
char      Occupancy_ClassChar(OccClass c); /* 'E' | 'L' | 'M' | 'H' */

#endif
