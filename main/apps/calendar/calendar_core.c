#include "apps/calendar/calendar_core.h"

#include <stdio.h>
#include <string.h>

#include "apps/bazi/bazi_almanac.h"
#include "apps/bazi/bazi_core.h"
#include "apps/calendar/calendar_terms_data.inc"

static const char *const kTermNames[] = {
    "小寒", "大寒", "立春", "雨水", "惊蛰", "春分",
    "清明", "谷雨", "立夏", "小满", "芒种", "夏至",
    "小暑", "大暑", "立秋", "处暑", "白露", "秋分",
    "寒露", "霜降", "立冬", "小雪", "大雪", "冬至",
};

static const char *const kLunarMonths[] = {
    "", "正", "二", "三", "四", "五", "六",
    "七", "八", "九", "十", "冬", "腊",
};

static const char *const kLunarDays[] = {
    "",
    "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
    "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
    "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十",
};

static bool leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

uint8_t calendar_days_in_month(int year, int month) {
    if (year < CALENDAR_FIRST_YEAR || year > CALENDAR_LAST_YEAR ||
        month < 1 || month > 12) return 0;
    static const uint8_t days[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31,
    };
    return (uint8_t)(days[month - 1] + (month == 2 && leap_year(year)));
}

uint8_t calendar_weekday(int year, int month, int day) {
    const int64_t absolute = bazi_almanac_days_from_civil(year, month, day);
    return (uint8_t)((absolute + 4) % 7 + 7) % 7;
}

static int day_of_year(int year, int month, int day) {
    int result = day - 1;
    for (int m = 1; m < month; ++m) result += calendar_days_in_month(year, m);
    return result;
}

static uint32_t term_minute(int year, int index) {
    const size_t year_index = (size_t)(year - CALENDAR_TERM_FIRST_YEAR);
    const size_t bit = (size_t)index * 20U;
    const size_t word = bit / 32U;
    const unsigned shift = (unsigned)(bit % 32U);
    uint64_t packed = kCalendarTermMinutes[year_index][word];
    if (shift > 12U)
        packed |= (uint64_t)kCalendarTermMinutes[year_index][word + 1] << 32U;
    return (uint32_t)((packed >> shift) & 0xfffffU);
}

static int term_on_day(int year, int day_index) {
    for (int i = 0; i < 24; ++i) {
        if ((int)(term_minute(year, i) / 1440U) == day_index) return i;
    }
    return -1;
}

const char *calendar_solar_term_name(uint8_t index) {
    return index < 24 ? kTermNames[index] : "";
}

bool calendar_day_info(int year, int month, int day, calendar_day_t *out) {
    if (!out || year < CALENDAR_FIRST_YEAR || year > CALENDAR_LAST_YEAR ||
        day < 1 || day > calendar_days_in_month(year, month)) return false;

    bazi_almanac_t almanac;
    if (!bazi_almanac_calculate_solar(year, month, day, 12, 0,
                                      BAZI_ALMANAC_ZI_EARLY, &almanac)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    out->valid = true;
    out->year = year;
    out->month = month;
    out->day = day;
    out->weekday = calendar_weekday(year, month, day);
    out->lunar_month = almanac.lunar_month;
    out->lunar_day = almanac.lunar_day;
    out->lunar_month_leap = almanac.lunar_month_leap;
    snprintf(out->year_ganzhi, sizeof(out->year_ganzhi), "%s%s",
             bazi_stem_text(almanac.year_stem),
             bazi_branch_text(almanac.year_branch));
    snprintf(out->month_ganzhi, sizeof(out->month_ganzhi), "%s%s",
             bazi_stem_text(almanac.month_stem),
             bazi_branch_text(almanac.month_branch));
    snprintf(out->day_ganzhi, sizeof(out->day_ganzhi), "%s%s",
             bazi_stem_text(almanac.day_stem),
             bazi_branch_text(almanac.day_branch));
    snprintf(out->lunar_full, sizeof(out->lunar_full), "农历%s%s月%s",
             out->lunar_month_leap ? "闰" : "",
             kLunarMonths[out->lunar_month],
             kLunarDays[out->lunar_day]);
    if (out->lunar_day == 1) {
        snprintf(out->lunar_cell, sizeof(out->lunar_cell), "%s%s月",
                 out->lunar_month_leap ? "闰" : "",
                 kLunarMonths[out->lunar_month]);
    } else {
        snprintf(out->lunar_cell, sizeof(out->lunar_cell), "%s",
                 kLunarDays[out->lunar_day]);
    }

    const int term = term_on_day(year, day_of_year(year, month, day));
    out->solar_term_index = term < 0 ? UINT8_MAX : (uint8_t)term;
    return true;
}

bool calendar_next_solar_term(int year, int month, int day,
                              char name[10], uint8_t *days_until) {
    if (!name || !days_until || year < CALENDAR_FIRST_YEAR ||
        year > CALENDAR_LAST_YEAR || day < 1 ||
        day > calendar_days_in_month(year, month)) return false;

    const int today = day_of_year(year, month, day);
    int next = -1;
    int next_year = year;
    for (int i = 0; i < 24; ++i) {
        if ((int)(term_minute(year, i) / 1440U) > today) {
            next = i;
            break;
        }
    }
    if (next < 0) {
        next = 0;
        next_year = year + 1;
    }
    const int64_t absolute = bazi_almanac_days_from_civil(year, month, day);
    const int64_t next_absolute =
        bazi_almanac_days_from_civil(next_year, 1, 1) +
        (int)(term_minute(next_year, next) / 1440U);
    snprintf(name, 10, "%s", kTermNames[next]);
    *days_until = (uint8_t)(next_absolute - absolute);
    return true;
}
