#include "apps/calendar/calendar_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_day(int year, int month, int day, uint8_t weekday,
                       const char *lunar, const char *cell,
                       const char *ganzhi) {
    calendar_day_t info;
    assert(calendar_day_info(year, month, day, &info));
    assert(info.valid);
    assert(info.weekday == weekday);
    assert(!strcmp(info.lunar_full, lunar));
    assert(!strcmp(info.lunar_cell, cell));
    assert(!strcmp(info.day_ganzhi, ganzhi));
}

int main(void) {
    assert(calendar_days_in_month(2024, 2) == 29);
    assert(calendar_days_in_month(2026, 2) == 28);
    assert(calendar_days_in_month(2100, 2) == 28);

    expect_day(2026, 11, 1, 0, "农历九月廿三", "廿三", "己卯");
    expect_day(2026, 11, 7, 6, "农历九月廿九", "廿九", "乙酉");
    expect_day(2026, 11, 22, 0, "农历十月十四", "十四", "庚子");
    expect_day(2026, 11, 23, 1, "农历十月十五", "十五", "辛丑");
    expect_day(2026, 11, 30, 1, "农历十月廿二", "廿二", "戊申");
    expect_day(2025, 7, 25, 5, "农历闰六月初一", "闰六月", "乙未");

    calendar_day_t info;
    assert(calendar_day_info(2026, 11, 7, &info));
    assert(info.solar_term_index == 20);
    assert(!strcmp(calendar_solar_term_name(info.solar_term_index), "立冬"));
    assert(calendar_day_info(2026, 11, 22, &info));
    assert(info.solar_term_index == 21);
    assert(!strcmp(calendar_solar_term_name(info.solar_term_index), "小雪"));
    assert(calendar_day_info(2026, 11, 23, &info));
    assert(info.solar_term_index == UINT8_MAX);

    char term[10];
    uint8_t days = 0;
    assert(calendar_next_solar_term(2026, 11, 21, term, &days));
    assert(!strcmp(term, "小雪") && days == 1);
    assert(calendar_next_solar_term(2026, 11, 22, term, &days));
    assert(!strcmp(term, "大雪") && days == 15);
    assert(calendar_next_solar_term(2026, 11, 23, term, &days));
    assert(!strcmp(term, "大雪") && days == 14);

    assert(!calendar_day_info(1899, 12, 31, &info));
    assert(!calendar_day_info(2026, 2, 29, &info));
    assert(!calendar_day_info(2101, 1, 1, &info));
    puts("calendar core: PASS");
    return 0;
}
