#include "apps/bazi/bazi_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static bazi_birth_t birth(int year, int month, int day, int hour, int minute,
                          uint8_t calendar, uint8_t sex) {
    bazi_birth_t value = {
        .version = 1,
        .calendar = calendar,
        .sex = sex,
        .zi_rule = BAZI_ZI_EARLY,
        .time_mode = BAZI_TIME_STANDARD,
        .lunar_leap = 0,
        .city_id = 0,
        .year = (int16_t)year,
        .month = (uint8_t)month,
        .day = (uint8_t)day,
        .hour = (uint8_t)hour,
        .minute = (uint8_t)minute,
        .longitude_e4 = 1164074,
    };
    return value;
}

static void expect_pillars(const bazi_chart_t *chart,
                           const char *year, const char *month,
                           const char *day, const char *hour) {
    char text[7];
    const char *expected[] = {year, month, day, hour};
    for (int i = 0; i < 4; ++i) {
        snprintf(text, sizeof(text), "%s%s",
                 bazi_stem_text(chart->pillar[i].stem),
                 bazi_branch_text(chart->pillar[i].branch));
        if (strcmp(text, expected[i])) {
            fprintf(stderr, "pillar %d got %s expected %s\n", i, text, expected[i]);
        }
        assert(!strcmp(text, expected[i]));
    }
}

int main(void) {
    bazi_chart_t chart;
    assert(bazi_minute_step(59, 1) == 0);
    assert(bazi_minute_step(0, -1) == 59);
    assert(bazi_minute_step(41, 0) == 41);
    assert(bazi_minute_step(41, 1) == 42);
    bazi_birth_t sample = birth(1990, 5, 17, 8, 30, BAZI_CALENDAR_SOLAR,
                                BAZI_SEX_MALE);
    assert(bazi_birth_max_day(&sample) == 31);
    sample.month = 2;
    sample.year = 2024;
    assert(bazi_birth_max_day(&sample) == 29);
    sample.year = 2023;
    assert(bazi_birth_max_day(&sample) == 28);
    sample = birth(1990, 5, 17, 8, 30, BAZI_CALENDAR_SOLAR, BAZI_SEX_MALE);
    assert(bazi_core_create(&sample, &chart));
    expect_pillars(&chart, "庚午", "辛巳", "壬午", "甲辰");
    assert(chart.stem_god[0] == BAZI_GOD_PIAN_YIN);
    assert(chart.stem_god[1] == BAZI_GOD_ZHENG_YIN);
    assert(chart.stem_god[2] == BAZI_GOD_DAY_MASTER);
    assert(chart.stem_god[3] == BAZI_GOD_SHI_SHEN);
    assert(chart.hidden_count[0] == 2);
    assert(chart.hidden_count[1] == 3);
    assert(chart.hidden_count[2] == 2);
    assert(chart.hidden_count[3] == 3);
    assert(chart.lunar_month == 4 && chart.lunar_day == 23);
    assert(chart.luck.forward);
    assert(chart.luck.first_year == 1997);
    assert(chart.luck.first_month == 1);
    assert(chart.luck.first_day == 7);
    assert(chart.luck.first_hour == 16);
    assert(chart.luck.first_age == 8);
    assert(chart.luck.step[0].ganzhi.stem == 8);   // 壬
    assert(chart.luck.step[0].ganzhi.branch == 6); // 午
    assert(chart.luck.step[1].ganzhi.stem == 9);   // 癸
    assert(chart.luck.step[1].ganzhi.branch == 7); // 未
    assert(chart.luck.step[2].ganzhi.stem == 0);   // 甲
    assert(chart.luck.step[2].ganzhi.branch == 8); // 申
    assert(bazi_current_luck_index(&chart, 2026) == 2);
    assert(bazi_flow_row_for_year(&chart, 2, 2026) == 9);
    bazi_ganzhi_t flow = bazi_flow_year(2026);
    assert(flow.stem == 2 && flow.branch == 6);     // 丙午

    bazi_birth_t lunar = sample;
    lunar.calendar = BAZI_CALENDAR_LUNAR;
    lunar.month = 4;
    lunar.day = 23;
    assert(bazi_core_create(&lunar, &chart));
    expect_pillars(&chart, "庚午", "辛巳", "壬午", "甲辰");
    assert(chart.solar_year == 1990 && chart.solar_month == 5 && chart.solar_day == 17);

    bazi_birth_t modern = birth(2026, 11, 22, 9, 41, BAZI_CALENDAR_SOLAR,
                                BAZI_SEX_MALE);
    assert(bazi_core_create(&modern, &chart));
    expect_pillars(&chart, "丙午", "己亥", "庚子", "辛巳");
    modern.time_mode = BAZI_TIME_TRUE_SOLAR;
    assert(bazi_core_create(&modern, &chart));
    assert(chart.solar_hour == 9 && chart.solar_minute == 40);

    bazi_birth_t millennium = birth(2000, 1, 1, 0, 15, BAZI_CALENDAR_SOLAR,
                                    BAZI_SEX_MALE);
    assert(bazi_core_create(&millennium, &chart));
    expect_pillars(&chart, "己卯", "丙子", "戊午", "壬子");

    bazi_birth_t lichun = birth(2024, 2, 4, 16, 27, BAZI_CALENDAR_SOLAR,
                                BAZI_SEX_MALE);
    assert(bazi_core_create(&lichun, &chart));
    expect_pillars(&chart, "癸卯", "乙丑", "戊戌", "庚申");
    lichun.minute = 28;
    assert(bazi_core_create(&lichun, &chart));
    expect_pillars(&chart, "甲辰", "丙寅", "戊戌", "庚申");

    bazi_birth_t reverse = sample;
    reverse.sex = BAZI_SEX_FEMALE;
    assert(bazi_core_create(&reverse, &chart));
    assert(!chart.luck.forward);
    assert(chart.luck.step[0].ganzhi.stem == 6);   // 庚
    assert(chart.luck.step[0].ganzhi.branch == 4); // 辰

    bazi_birth_t boundary = birth(1988, 2, 4, 23, 30, BAZI_CALENDAR_SOLAR,
                                  BAZI_SEX_MALE);
    boundary.zi_rule = BAZI_ZI_EARLY;
    assert(bazi_core_create(&boundary, &chart));
    expect_pillars(&chart, "戊辰", "甲寅", "己丑", "甲子");
    boundary.zi_rule = BAZI_ZI_LATE;
    assert(bazi_core_create(&boundary, &chart));
    expect_pillars(&chart, "戊辰", "甲寅", "庚寅", "丙子");

    bazi_birth_t leap = birth(2025, 6, 1, 0, 0, BAZI_CALENDAR_LUNAR,
                              BAZI_SEX_MALE);
    leap.lunar_leap = 1;
    assert(bazi_core_create(&leap, &chart));
    assert(chart.solar_year == 2025 && chart.solar_month == 7 && chart.solar_day == 25);

    bazi_birth_t invalid = birth(2026, 2, 30, 0, 0, BAZI_CALENDAR_SOLAR,
                                 BAZI_SEX_MALE);
    assert(!bazi_core_create(&invalid, &chart));
    invalid = birth(1899, 12, 31, 0, 0, BAZI_CALENDAR_SOLAR, BAZI_SEX_MALE);
    assert(!bazi_core_create(&invalid, &chart));

    puts("bazi core: PASS");
    return 0;
}
