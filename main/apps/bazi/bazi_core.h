#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BAZI_LUCK_COUNT 10
#define BAZI_YEARS_PER_LUCK 10

typedef enum {
    BAZI_CALENDAR_SOLAR = 0,
    BAZI_CALENDAR_LUNAR = 1,
} bazi_calendar_t;

typedef enum {
    BAZI_SEX_UNSET = 0,
    BAZI_SEX_MALE = 1,
    BAZI_SEX_FEMALE = 2,
} bazi_sex_t;

typedef enum {
    BAZI_ZI_EARLY = 0,
    BAZI_ZI_LATE = 1,
} bazi_zi_rule_t;

typedef enum {
    BAZI_TIME_STANDARD = 0,
    BAZI_TIME_TRUE_SOLAR = 1,
} bazi_time_mode_t;

typedef enum {
    BAZI_ELEMENT_WOOD = 0,
    BAZI_ELEMENT_FIRE,
    BAZI_ELEMENT_EARTH,
    BAZI_ELEMENT_METAL,
    BAZI_ELEMENT_WATER,
} bazi_element_t;

typedef enum {
    BAZI_GOD_BI_JIAN = 0,
    BAZI_GOD_JIE_CAI,
    BAZI_GOD_SHI_SHEN,
    BAZI_GOD_SHANG_GUAN,
    BAZI_GOD_PIAN_CAI,
    BAZI_GOD_ZHENG_CAI,
    BAZI_GOD_QI_SHA,
    BAZI_GOD_ZHENG_GUAN,
    BAZI_GOD_PIAN_YIN,
    BAZI_GOD_ZHENG_YIN,
    BAZI_GOD_DAY_MASTER = 255,
} bazi_god_t;

typedef struct {
    uint8_t version;
    uint8_t calendar;
    uint8_t sex;
    uint8_t zi_rule;
    uint8_t time_mode;
    uint8_t lunar_leap;
    uint8_t city_id;
    uint8_t reserved;
    int16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    int32_t longitude_e4;
} bazi_birth_t;

typedef struct {
    uint8_t stem;
    uint8_t branch;
} bazi_ganzhi_t;

typedef struct {
    int16_t start_year;
    uint8_t start_age;
    bazi_ganzhi_t ganzhi;
} bazi_luck_step_t;

typedef struct {
    bool forward;
    int16_t first_year;
    uint8_t first_month;
    uint8_t first_day;
    uint8_t first_hour;
    uint8_t first_age;
    bazi_luck_step_t step[BAZI_LUCK_COUNT];
} bazi_luck_t;

typedef struct {
    bool valid;
    bazi_birth_t birth;
    int16_t solar_year;
    uint8_t solar_month;
    uint8_t solar_day;
    uint8_t solar_hour;
    uint8_t solar_minute;
    int16_t lunar_year;
    uint8_t lunar_month;
    uint8_t lunar_day;
    bool lunar_month_leap;
    bazi_ganzhi_t pillar[4];
    uint8_t stem_god[4];
    uint8_t hidden_count[4];
    uint8_t hidden[4][3];
    bazi_luck_t luck;
} bazi_chart_t;

bool bazi_core_create(const bazi_birth_t *birth, bazi_chart_t *chart);
bool bazi_core_valid_birth(const bazi_birth_t *birth);
uint8_t bazi_birth_max_day(const bazi_birth_t *birth);
uint8_t bazi_minute_step(uint8_t minute, int delta);

const char *bazi_stem_text(uint8_t stem);
const char *bazi_branch_text(uint8_t branch);
const char *bazi_god_text(uint8_t god);
const char *bazi_city_name(uint8_t city_id);
int32_t bazi_city_longitude_e4(uint8_t city_id);
uint8_t bazi_city_count(void);
uint8_t bazi_element_of_stem(uint8_t stem);
uint8_t bazi_element_of_branch(uint8_t branch);

bazi_ganzhi_t bazi_flow_year(int year);
uint8_t bazi_current_luck_index(const bazi_chart_t *chart, int current_year);
uint8_t bazi_flow_row_for_year(const bazi_chart_t *chart, uint8_t luck_index,
                               int current_year);

#ifdef __cplusplus
}
#endif
