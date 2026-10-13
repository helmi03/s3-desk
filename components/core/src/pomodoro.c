#include "pomodoro.h"

static uint32_t length_ms(pomo_phase_t phase)
{
    return (phase == POMO_FOCUS ? POMO_FOCUS_S : POMO_BREAK_S) * 1000u;
}

void pomodoro_init(pomodoro_t *p)
{
    *p = (pomodoro_t){.phase = POMO_FOCUS, .left_ms = length_ms(POMO_FOCUS)};
}

void pomodoro_toggle(pomodoro_t *p, uint32_t now_ms)
{
    if (p->running) {
        pomodoro_tick(p, now_ms);
    }
    p->running = !p->running;
    p->last_ms = now_ms;
}

void pomodoro_reset(pomodoro_t *p)
{
    p->running = false;
    p->left_ms = length_ms(p->phase);
}

void pomodoro_skip(pomodoro_t *p, uint32_t now_ms)
{
    p->phase = p->phase == POMO_FOCUS ? POMO_BREAK : POMO_FOCUS;
    p->left_ms = length_ms(p->phase);
    p->last_ms = now_ms;
}

bool pomodoro_tick(pomodoro_t *p, uint32_t now_ms)
{
    if (!p->running) {
        return false;
    }
    uint32_t elapsed = now_ms - p->last_ms;
    p->last_ms = now_ms;
    if (elapsed < p->left_ms) {
        p->left_ms -= elapsed;
        return false;
    }
    uint32_t over = elapsed - p->left_ms;
    p->phase = p->phase == POMO_FOCUS ? POMO_BREAK : POMO_FOCUS;
    uint32_t len = length_ms(p->phase);
    p->left_ms = over < len ? len - over : len;
    return true;
}

uint32_t pomodoro_seconds_left(const pomodoro_t *p)
{
    return (p->left_ms + 999u) / 1000u;
}

uint16_t pomodoro_progress_permille(const pomodoro_t *p)
{
    uint32_t len = length_ms(p->phase);
    return (uint16_t)((uint64_t)(len - p->left_ms) * 1000u / len);
}
