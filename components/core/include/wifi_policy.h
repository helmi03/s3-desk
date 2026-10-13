#pragma once
#include <stdbool.h>
#include <stdint.h>

#define WIFI_SYNC_EVERY_S (6 * 3600)
#define WIFI_ATTEMPT_S 120
#define WIFI_RETRY_S (30 * 60)
#define WIFI_USER_HOLD_S (10 * 60)

/* When the station radio is on. Background work (clock sync, data fetch) gets
 * one bounded attempt at a time; the user keeps it on, plus a hold after. All
 * times are monotonic seconds. */
typedef struct {
    int64_t last_sync;  /* -1 until the first sync */
    int64_t user_until;
    int64_t attempt_at; /* start of the running background attempt, -1 when none */
    int64_t next_try;   /* no background attempt before this */
} wifi_policy_t;

typedef struct {
    bool user;      /* Tetapan is open or the web page is in use */
    bool fetch_due; /* the prayer table needs downloading */
} wifi_inputs_t;

void wifi_policy_init(wifi_policy_t *p);
void wifi_policy_synced(wifi_policy_t *p, int64_t now);
bool wifi_policy_want(wifi_policy_t *p, int64_t now, wifi_inputs_t in);
