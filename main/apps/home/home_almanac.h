// Offline four-pillar, lunar-date and solar-term calculation for Home.
//
// The calculation is deterministic and does not use the network. Solar terms
// use China standard time (UTC+8), matching the Chinese calendar convention.
// The day pillar changes at local midnight; the Zi hour starts at 23:00.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool valid;
    char year[7];              // 年柱, e.g. "丙午"
    char month[7];             // 月柱, e.g. "己亥"
    char day[7];               // 日柱, e.g. "庚子"
    char hour[7];              // 时柱, e.g. "辛巳"
    char lunar_month_day[20];  // "十月十四"; leap month includes "闰"
    char solar_term_today[10]; // non-empty only when the term starts today
    char next_solar_term[10];  // next upcoming term
    uint8_t lunar_month;       // 1..12
    uint8_t lunar_day;         // 1..30
    uint8_t days_to_next_solar_term;
    bool lunar_month_leap;
} home_almanac_t;

typedef enum {
    HOME_ZI_EARLY = 0,  // 23:00-23:59 keeps the current civil day pillar.
    HOME_ZI_LATE = 1,   // 23:00-23:59 uses the next day pillar.
} home_zi_rule_t;

// Supports 2024-01-01 through 2099-12-31. Month is 1..12, hour is 0..23.
bool home_almanac_calculate(int year, int month, int day, int hour, int minute,
                            home_almanac_t *out);
bool home_almanac_calculate_ex(int year, int month, int day, int hour, int minute,
                               home_zi_rule_t zi_rule, home_almanac_t *out);

#ifdef __cplusplus
}
#endif
