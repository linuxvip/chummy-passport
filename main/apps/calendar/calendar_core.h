#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CALENDAR_FIRST_YEAR 1900
#define CALENDAR_LAST_YEAR 2100

typedef struct {
    bool valid;
    int year;
    int month;
    int day;
    uint8_t weekday;             // 0=Sunday ... 6=Saturday
    uint8_t lunar_month;
    uint8_t lunar_day;
    bool lunar_month_leap;
    char year_ganzhi[7];
    char month_ganzhi[7];
    char day_ganzhi[7];
    char lunar_full[24];         // "农历十月十五"
    char lunar_cell[12];         // "十月" on day 1, otherwise "十五"
    uint8_t solar_term_index;    // 0=小寒 ... 23=冬至, UINT8_MAX on ordinary days
} calendar_day_t;

uint8_t calendar_days_in_month(int year, int month);
uint8_t calendar_weekday(int year, int month, int day);
bool calendar_day_info(int year, int month, int day, calendar_day_t *out);
bool calendar_next_solar_term(int year, int month, int day,
                              char name[10], uint8_t *days_until);
const char *calendar_solar_term_name(uint8_t index);

#ifdef __cplusplus
}
#endif
