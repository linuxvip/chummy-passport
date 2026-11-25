#include "apps/liuyao/liuyao_core.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static liuyao_input_t base_input(liuyao_method_t method) {
    liuyao_input_t input = {
        .method = method,
        .year = 2026,
        .month = 11,
        .day = 24,
        .hour = 9,
        .minute = 42,
        .time_mode = BAZI_TIME_STANDARD,
        .zi_rule = BAZI_ZI_LATE,
        .city_id = 1,
        .longitude_e4 = 1214739,
    };
    return input;
}

static void test_time_method(void) {
    liuyao_input_t input = base_input(LIUYAO_METHOD_TIME);
    liuyao_chart_t chart;
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.valid);
    assert(chart.original_id == 60);
    assert(chart.changed_id == 56);
    assert(strcmp(liuyao_hex_name(chart.original_id), "天山遁") == 0);
    assert(strcmp(liuyao_hex_name(chart.changed_id), "天地否") == 0);
    assert(chart.upper_number == 1);
    assert(chart.lower_number == 7);
    assert(chart.moving_line == 3);
    assert(chart.palace_index == 2);
    assert(chart.shi == 2 && chart.ying == 5);
    assert(chart.day_stem == 8 && chart.day_branch == 2);
    assert(chart.empty_branch[0] == 4 && chart.empty_branch[1] == 5);
    assert(chart.lines[2].moving);
    assert(chart.lines[2].yang);
    assert(chart.lines[2].relative == LIUYAO_RELATIVE_BROTHER);
    assert(chart.lines[2].changed_relative == LIUYAO_RELATIVE_WEALTH);
    assert(chart.lines[3].spirit == LIUYAO_SPIRIT_HOOKED_CHEN);
    assert(chart.lines[4].spirit == LIUYAO_SPIRIT_FLYING_SNAKE);
    assert(chart.lines[5].spirit == LIUYAO_SPIRIT_WHITE_TIGER);
    assert(chart.lines[0].spirit == LIUYAO_SPIRIT_BLACK_TORTOISE);
    assert(chart.lines[1].spirit == LIUYAO_SPIRIT_AZURE_DRAGON);
    assert(chart.lines[2].spirit == LIUYAO_SPIRIT_VERMILION_BIRD);
    assert(chart.lines[2].day_clash);
    assert(chart.lines[0].hidden_count == 1);
    assert(chart.lines[0].hidden[0].relative == LIUYAO_RELATIVE_CHILD);
    assert(chart.lines[1].hidden_count == 1);
    assert(chart.lines[1].hidden[0].relative == LIUYAO_RELATIVE_WEALTH);
}

static void test_number_method(void) {
    liuyao_input_t input = base_input(LIUYAO_METHOD_NUMBER);
    strcpy(input.digits, "27518");
    input.digit_count = 5;
    liuyao_chart_t chart;
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.original_id == 58);
    assert(chart.changed_id == 59);
    assert(strcmp(liuyao_hex_name(chart.original_id), "天水讼") == 0);
    assert(strcmp(liuyao_hex_name(chart.changed_id), "天泽履") == 0);
    assert(chart.first_sum == 9);
    assert(chart.second_sum == 14);
    assert(chart.upper_number == 1);
    assert(chart.lower_number == 6);
    assert(chart.moving_line == 1);
    assert(!chart.lines[0].yang);
    assert(chart.lines[0].moving);

    strcpy(input.digits, "275183");
    input.digit_count = 6;
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.first_sum == 14);
    assert(chart.second_sum == 12);
    assert(chart.upper_number == 6);
    assert(chart.lower_number == 4);

    strcpy(input.digits, "0000");
    input.digit_count = 4;
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.upper_number == 8);
    assert(chart.lower_number == 8);

    input.hour = 11;
    strcpy(input.digits, "111");
    input.digit_count = 3;
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.hour_number == 7);
    assert(chart.upper_number == 1 && chart.lower_number == 2);
    assert(chart.moving_line == 4);
}

static void test_validation_and_boundaries(void) {
    liuyao_input_t input = base_input(LIUYAO_METHOD_NUMBER);
    liuyao_chart_t chart;
    strcpy(input.digits, "7");
    input.digit_count = 1;
    assert(!liuyao_chart_create(&input, &chart));

    strcpy(input.digits, "1234567890123");
    input.digit_count = 13;
    assert(!liuyao_chart_create(&input, &chart));

    strcpy(input.digits, "12A4");
    input.digit_count = 4;
    assert(!liuyao_chart_create(&input, &chart));

    input = base_input(LIUYAO_METHOD_TIME);
    input.year = 2026;
    input.month = 2;
    input.day = 30;
    assert(!liuyao_chart_create(&input, &chart));

    input = base_input(LIUYAO_METHOD_TIME);
    input.time_mode = BAZI_TIME_TRUE_SOLAR;
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.solar_hour == 10 && chart.solar_minute == 1);

    assert(liuyao_minute_step(0, -1) == 59);
    assert(liuyao_minute_step(59, 1) == 0);
    assert(liuyao_input_max_day(2024, 2) == 29);
    assert(liuyao_input_max_day(2023, 2) == 28);

    for (uint8_t i = 0; i < 64; ++i) {
        assert(liuyao_hex_name(i)[0] != '\0');
        assert(liuyao_hex_explanation(i)[0] != '\0');
        for (uint8_t j = i + 1; j < 64; ++j)
            assert(strcmp(liuyao_hex_name(i), liuyao_hex_name(j)) != 0);
    }
    for (uint8_t i = 1; i <= 8; ++i) {
        assert(liuyao_trigram_name(i)[0] != '\0');
    }
    for (uint8_t i = 0; i < 8; ++i) {
        assert(liuyao_palace_name(i)[0] != '\0');
        assert(liuyao_palace_state_text(i)[0] != '\0');
    }
    for (uint8_t i = 0; i < LIUYAO_RELATIVE_COUNT; ++i) {
        assert(liuyao_relative_text(i)[0] != '\0');
    }
    for (uint8_t i = 0; i < LIUYAO_SPIRIT_COUNT; ++i) {
        assert(liuyao_spirit_text(i)[0] != '\0');
    }
}

int main(void) {
    test_time_method();
    test_number_method();
    test_validation_and_boundaries();
    puts("liuyao core: PASS");
    return 0;
}
