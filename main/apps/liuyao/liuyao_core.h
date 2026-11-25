#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "apps/bazi/bazi_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LIUYAO_LINE_COUNT 6
#define LIUYAO_MAX_DIGITS 12

typedef enum {
    LIUYAO_METHOD_TIME = 0,
    LIUYAO_METHOD_NUMBER = 1,
} liuyao_method_t;

typedef enum {
    LIUYAO_RELATIVE_PARENT = 0,
    LIUYAO_RELATIVE_BROTHER,
    LIUYAO_RELATIVE_CHILD,
    LIUYAO_RELATIVE_WEALTH,
    LIUYAO_RELATIVE_OFFICER,
    LIUYAO_RELATIVE_COUNT,
} liuyao_relative_t;

typedef enum {
    LIUYAO_SPIRIT_AZURE_DRAGON = 0,
    LIUYAO_SPIRIT_VERMILION_BIRD,
    LIUYAO_SPIRIT_HOOKED_CHEN,
    LIUYAO_SPIRIT_FLYING_SNAKE,
    LIUYAO_SPIRIT_WHITE_TIGER,
    LIUYAO_SPIRIT_BLACK_TORTOISE,
    LIUYAO_SPIRIT_COUNT,
} liuyao_spirit_t;

typedef enum {
    LIUYAO_TRIGRAM_QIAN = 1,
    LIUYAO_TRIGRAM_DUI = 2,
    LIUYAO_TRIGRAM_LI = 3,
    LIUYAO_TRIGRAM_ZHEN = 4,
    LIUYAO_TRIGRAM_XUN = 5,
    LIUYAO_TRIGRAM_KAN = 6,
    LIUYAO_TRIGRAM_GEN = 7,
    LIUYAO_TRIGRAM_KUN = 8,
} liuyao_trigram_t;

typedef struct {
    uint8_t stem;
    uint8_t branch;
    uint8_t relative;
} liuyao_hidden_t;

typedef struct {
    uint8_t stem;
    uint8_t branch;
    uint8_t element;
    uint8_t relative;
    uint8_t spirit;
    uint8_t changed_stem;
    uint8_t changed_branch;
    uint8_t changed_element;
    uint8_t changed_relative;
    uint8_t hidden_count;
    liuyao_hidden_t hidden[2];
    bool yang;
    bool moving;
    bool empty;
    bool month_break;
    bool day_clash;
    bool month_combine;
    bool day_combine;
} liuyao_line_t;

typedef struct {
    uint8_t method;
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t time_mode;
    uint8_t zi_rule;
    uint8_t city_id;
    int32_t longitude_e4;
    char digits[LIUYAO_MAX_DIGITS + 1];
    uint8_t digit_count;
} liuyao_input_t;

typedef struct {
    bool valid;
    liuyao_input_t input;
    int16_t solar_year;
    uint8_t solar_month;
    uint8_t solar_day;
    uint8_t solar_hour;
    uint8_t solar_minute;
    int16_t lunar_year;
    uint8_t lunar_month;
    uint8_t lunar_day;
    bool lunar_month_leap;
    uint8_t year_stem;
    uint8_t year_branch;
    uint8_t month_stem;
    uint8_t month_branch;
    uint8_t day_stem;
    uint8_t day_branch;
    uint8_t hour_stem;
    uint8_t hour_branch;
    uint8_t year_number;
    uint8_t month_number;
    uint8_t day_number;
    uint8_t hour_number;
    uint16_t first_sum;
    uint16_t second_sum;
    uint8_t upper_number;
    uint8_t lower_number;
    uint8_t moving_line;
    uint8_t original_id;
    uint8_t changed_id;
    uint8_t palace;
    uint8_t palace_index;
    uint8_t palace_element;
    uint8_t shi;
    uint8_t ying;
    uint8_t empty_branch[2];
    liuyao_line_t lines[LIUYAO_LINE_COUNT];
} liuyao_chart_t;

bool liuyao_chart_create(const liuyao_input_t *input, liuyao_chart_t *chart);
bool liuyao_input_valid(const liuyao_input_t *input);
uint8_t liuyao_input_max_day(int year, int month);
uint8_t liuyao_minute_step(uint8_t minute, int delta);

const char *liuyao_hex_name(uint8_t hexagram_id);
const char *liuyao_hex_explanation(uint8_t hexagram_id);
const char *liuyao_trigram_name(uint8_t trigram_number);
const char *liuyao_palace_name(uint8_t palace_index);
const char *liuyao_relative_text(uint8_t relative);
const char *liuyao_spirit_text(uint8_t spirit);
const char *liuyao_element_text(uint8_t element);
const char *liuyao_palace_state_text(uint8_t palace_index);

#ifdef __cplusplus
}
#endif
