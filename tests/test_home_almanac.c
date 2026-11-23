#include "apps/home/home_almanac.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect(int year, int month, int day, int hour, int minute,
                   const char *year_gz, const char *month_gz,
                   const char *day_gz, const char *hour_gz,
                   const char *lunar_day, const char *lunar_year,
                   const char *solar_term) {
    home_almanac_t result;
    assert(home_almanac_calculate(year, month, day, hour, minute, &result));
    assert(result.valid);
    if (strcmp(result.year, year_gz) || strcmp(result.month, month_gz) ||
        strcmp(result.day, day_gz) || strcmp(result.hour, hour_gz) ||
        strcmp(result.lunar_month_day, lunar_day) ||
        strcmp(result.lunar_year, lunar_year) ||
        strcmp(result.solar_term, solar_term)) {
        fprintf(stderr,
                "%04d-%02d-%02d %02d:%02d\n"
                "  pillars=%s %s %s %s lunar=%s %s term=%s\n"
                "  expect=%s %s %s %s lunar=%s %s term=%s\n",
                year, month, day, hour, minute,
                result.year, result.month, result.day, result.hour,
                result.lunar_month_day, result.lunar_year, result.solar_term,
                year_gz, month_gz, day_gz, hour_gz,
                lunar_day, lunar_year, solar_term);
        assert(0);
    }
}

int main(void) {
    // User-approved Home reference date.
    expect(2026, 11, 22, 9, 41, "丙午", "己亥", "庚子", "辛巳",
           "十月十四", "丙午马年", "小雪");

    // Exact Li Chun boundary: year and month pillars switch between 03:00 and
    // 05:00, while the lunar date (and therefore lunar zodiac) remains 2025.
    expect(2026, 2, 4, 3, 0, "乙巳", "己丑", "己酉", "丙寅",
           "腊月十七", "乙巳蛇年", "立春");
    expect(2026, 2, 4, 5, 0, "丙午", "庚寅", "己酉", "丁卯",
           "腊月十七", "乙巳蛇年", "立春");

    // Lunar New Year and a leap lunar month.
    expect(2024, 2, 10, 12, 0, "甲辰", "丙寅", "甲辰", "庚午",
           "正月初一", "甲辰龙年", "立春");
    expect(2025, 7, 25, 0, 0, "乙巳", "癸未", "乙未", "丙子",
           "闰六月初一", "乙巳蛇年", "大暑");

    // Late Zi hour uses the civil day pillar by design; the hour branch is Zi.
    expect(2099, 12, 31, 23, 0, "己未", "丙子", "壬寅", "庚子",
           "冬月二十", "己未羊年", "冬至");

    home_almanac_t invalid;
    assert(home_almanac_calculate_ex(2026, 11, 22, 23, 39, HOME_ZI_LATE, &invalid));
    assert(!strcmp(invalid.day, "辛丑"));
    assert(!strcmp(invalid.hour, "戊子"));
    assert(home_almanac_calculate_ex(2026, 11, 22, 23, 39, HOME_ZI_EARLY, &invalid));
    assert(!strcmp(invalid.day, "庚子"));
    assert(!strcmp(invalid.hour, "丙子"));
    assert(!home_almanac_calculate(2023, 12, 31, 12, 0, &invalid));
    assert(!home_almanac_calculate(2026, 2, 29, 12, 0, &invalid));
    assert(!home_almanac_calculate(2026, 11, 22, 24, 0, &invalid));
    assert(!home_almanac_calculate(2100, 1, 1, 12, 0, &invalid));

    puts("home almanac: PASS");
    return 0;
}
