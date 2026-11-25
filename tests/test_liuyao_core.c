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

static void test_manual_method(void) {
    liuyao_input_t input = base_input(LIUYAO_METHOD_MANUAL);
    const uint8_t sample[6] = {8, 8, 9, 8, 7, 6};
    memcpy(input.line_values, sample, sizeof(sample));
    liuyao_chart_t chart;
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.original_id == 20);
    assert(chart.changed_id == 48);
    assert(strcmp(liuyao_hex_name(chart.original_id), "水山蹇") == 0);
    assert(strcmp(liuyao_hex_name(chart.changed_id), "风地观") == 0);
    assert(chart.moving_mask == 0x24);
    assert(chart.moving_line == 3);
    assert(!chart.lines[0].moving && !chart.lines[1].moving);
    assert(chart.lines[2].moving && chart.lines[5].moving);
    assert(chart.lines[2].yang && chart.lines[5].yang == false);
    assert(!chart.lines[0].yang && !chart.lines[3].yang);
    assert(chart.lines[4].yang);

    const uint8_t still[6] = {7, 8, 7, 8, 7, 8};
    memcpy(input.line_values, still, sizeof(still));
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.original_id == chart.changed_id);
    assert(chart.moving_mask == 0);
    assert(chart.moving_line == 0);
    for (uint8_t i = 0; i < 6; ++i) assert(!chart.lines[i].moving);

    const uint8_t old[6] = {6, 6, 6, 6, 6, 6};
    memcpy(input.line_values, old, sizeof(old));
    assert(liuyao_chart_create(&input, &chart));
    assert(chart.original_id == 0);
    assert(chart.changed_id == 63);
    assert(chart.moving_mask == 0x3F);
    for (uint8_t i = 0; i < 6; ++i) assert(chart.lines[i].moving && !chart.lines[i].yang);
}

static void test_validation_and_boundaries(void) {
    liuyao_input_t manual = base_input(LIUYAO_METHOD_MANUAL);
    liuyao_chart_t chart;
    const uint8_t good[6] = {6, 7, 8, 9, 7, 8};
    memcpy(manual.line_values, good, sizeof(good));
    assert(liuyao_chart_create(&manual, &chart));
    uint8_t bad[6] = {5, 7, 8, 9, 7, 8};
    memcpy(manual.line_values, bad, sizeof(bad));
    assert(!liuyao_chart_create(&manual, &chart));
    bad[0] = 10;
    memcpy(manual.line_values, bad, sizeof(bad));
    assert(!liuyao_chart_create(&manual, &chart));
    manual.method = 2;
    memcpy(manual.line_values, good, sizeof(good));
    assert(!liuyao_chart_create(&manual, &chart));

    liuyao_input_t input = base_input(LIUYAO_METHOD_TIME);
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
    test_manual_method();
    test_validation_and_boundaries();
    puts("liuyao core: PASS");
    return 0;
}
