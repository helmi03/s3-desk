#include "wifi_policy.h"

void wifi_policy_init(wifi_policy_t *p)
{
    *p = (wifi_policy_t){.last_sync = -1, .user_until = -1, .attempt_at = -1};
}

void wifi_policy_synced(wifi_policy_t *p, int64_t now)
{
    p->last_sync = now;
}

bool wifi_policy_want(wifi_policy_t *p, int64_t now, wifi_inputs_t in)
{
    if (in.user) {
        p->user_until = now + WIFI_USER_HOLD_S;
    }
    bool sync_due = p->last_sync < 0 || now - p->last_sync >= WIFI_SYNC_EVERY_S;
    if (!sync_due && !in.fetch_due) {
        p->attempt_at = -1;
    } else if (p->attempt_at < 0 && now >= p->next_try) {
        p->attempt_at = now;
    } else if (p->attempt_at >= 0 && now - p->attempt_at >= WIFI_ATTEMPT_S) {
        p->attempt_at = -1;
        p->next_try = now + WIFI_RETRY_S;
    }
    return now < p->user_until || p->attempt_at >= 0;
}
