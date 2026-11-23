#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BAZI_ALMANAC_ZI_EARLY = 0,
    BAZI_ALMANAC_ZI_LATE = 1,
} bazi_almanac_zi_rule_t;

typedef struct {
    bool valid;
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t year_stem;
    uint8_t year_branch;
    uint8_t month_stem;
    uint8_t month_branch;
    uint8_t day_stem;
    uint8_t day_branch;
    uint8_t hour_stem;
    uint8_t hour_branch;
    int16_t lunar_year;
    uint8_t lunar_month;
    uint8_t lunar_day;
    bool lunar_month_leap;
} bazi_almanac_t;

// Public Bazi range. The private data tables extend one year in each direction
// so near-year Jie boundaries and dayun calculations fail closed at the edge.
bool bazi_almanac_calculate_solar(int year, int month, int day, int hour, int minute,
                                  bazi_almanac_zi_rule_t zi_rule,
                                  bazi_almanac_t *out);

// Lunar month is 1..12; leap selects the duplicate month after lunar_month.
bool bazi_almanac_calculate_lunar(int year, int month, int day, int hour, int minute,
                                  bool leap, bazi_almanac_zi_rule_t zi_rule,
                                  bazi_almanac_t *out);

// Absolute minutes are counted from 1970-01-01 00:00 China standard time.
int64_t bazi_almanac_days_from_civil(int year, int month, int day);
void bazi_almanac_civil_from_days(int64_t days, int *year, int *month, int *day);
int64_t bazi_almanac_absolute_minute(int year, int month, int day, int hour, int minute);

// Jie index 0=小寒, 1=立春, ... 11=大雪. Absolute seconds from 1970-01-01.
bool bazi_almanac_jie_absolute_second(int year, uint8_t jie, int64_t *out);
bool bazi_almanac_valid_date(int year, int month, int day);
uint8_t bazi_almanac_solar_month_days(int year, int month);
uint8_t bazi_almanac_lunar_month_days(int year, int month, bool leap);

#ifdef __cplusplus
}
#endif
