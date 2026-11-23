// Cached, once-per-day Home quotation fetched in a bounded background task.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HOME_DAILY_QUOTE_TEXT_MAX 160
#define HOME_DAILY_QUOTE_SOURCE_MAX 32

typedef struct {
    uint32_t local_day;  // yyyymmdd
    char text[HOME_DAILY_QUOTE_TEXT_MAX];
    char source[HOME_DAILY_QUOTE_SOURCE_MAX];
} home_daily_quote_t;

void home_daily_quote_init(void);
bool home_daily_quote_get(home_daily_quote_t *out);

// Starts at most one request when connected and today's cache is missing.
// Failed requests are retried after a short delay without blocking Home's UI.
void home_daily_quote_schedule(uint32_t local_day);
// Manual "next quote" request; bypasses today's cache once.
void home_daily_quote_request_refresh(uint32_t local_day);
void home_daily_quote_cancel_requests(void);
bool home_daily_quote_busy(void);
void home_daily_quote_stop(void);
uint32_t home_daily_quote_version(void);

#ifdef __cplusplus
}
#endif
