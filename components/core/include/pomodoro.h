#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    POMO_FOCUS,
    POMO_BREAK,
} pomo_phase_t;

#define POMO_FOCUS_S (25 * 60)
#define POMO_BREAK_S (5 * 60)

/* A focus/break timer driven by a millisecond clock that may wrap. When a
 * phase runs out, the other one starts at once and keeps running. */
typedef struct {
    pomo_phase_t phase;
    bool running;
    uint32_t left_ms;
    uint32_t last_ms; /* clock value at the last tick while running */
} pomodoro_t;

void pomodoro_init(pomodoro_t *p);
void pomodoro_toggle(pomodoro_t *p, uint32_t now_ms);
/* Stops and refills the current phase. */
void pomodoro_reset(pomodoro_t *p);
/* Jumps to the start of the other phase, keeping running or paused. */
void pomodoro_skip(pomodoro_t *p, uint32_t now_ms);
/* Advances a running timer. Returns true when a phase ran out. */
bool pomodoro_tick(pomodoro_t *p, uint32_t now_ms);

uint32_t pomodoro_seconds_left(const pomodoro_t *p);
/* How much of the current phase has passed, 0..1000. */
uint16_t pomodoro_progress_permille(const pomodoro_t *p);
