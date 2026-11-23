#include "apps/bazi/bazi_core.h"

#include <math.h>
#include <string.h>

#include "apps/bazi/bazi_almanac.h"

static const char *const kStems[] = {
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸",
};
static const char *const kBranches[] = {
    "子", "丑", "寅", "卯", "辰", "巳", "午", "未", "申", "酉", "戌", "亥",
};
static const char *const kGods[] = {
    "比肩", "劫财", "食神", "伤官", "偏财", "正财", "七杀", "正官", "偏印", "正印",
};

static const uint8_t kHiddenStems[12][3] = {
    {9, 0, 0}, {5, 9, 7}, {0, 2, 4}, {1, 0, 0},
    {4, 1, 9}, {2, 6, 4}, {3, 5, 0}, {5, 3, 1},
    {6, 8, 4}, {7, 0, 0}, {4, 7, 3}, {8, 0, 0},
};
static const uint8_t kHiddenCount[12] = {1, 3, 3, 1, 3, 3, 2, 3, 3, 1, 3, 2};

typedef struct {
    const char *name;
    int32_t longitude_e4;
} bazi_city_t;

static const bazi_city_t kCities[] = {
    {"北京", 1164074},
    {"上海", 1214737},
    {"广州", 1132640},
    {"深圳", 1140574},
    {"成都", 1040662},
    {"西安", 1089397},
    {"哈尔滨", 1265347},
    {"乌鲁木齐", 876168},
    {"香港", 1141690},
    {"台北", 1215653},
};

static bool leap_year(int year) {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

static int days_in_month(int year, int month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && leap_year(year));
}

static int positive_mod(int value, int modulus) {
    int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

static uint8_t stem_god(uint8_t day_stem, uint8_t other_stem) {
    const int day_element = day_stem / 2;
    const int other_element = other_stem / 2;
    const bool same_polarity = (day_stem & 1U) == (other_stem & 1U);
    if (other_element == day_element) return same_polarity ? BAZI_GOD_BI_JIAN : BAZI_GOD_JIE_CAI;
    if (other_element == (day_element + 1) % 5)
        return same_polarity ? BAZI_GOD_SHI_SHEN : BAZI_GOD_SHANG_GUAN;
    if (other_element == (day_element + 2) % 5)
        return same_polarity ? BAZI_GOD_PIAN_CAI : BAZI_GOD_ZHENG_CAI;
    if (other_element == (day_element + 3) % 5)
        return same_polarity ? BAZI_GOD_QI_SHA : BAZI_GOD_ZHENG_GUAN;
    return same_polarity ? BAZI_GOD_PIAN_YIN : BAZI_GOD_ZHENG_YIN;
}

static bool adjust_true_solar(const bazi_birth_t *birth,
                              int *year, int *month, int *day,
                              int *hour, int *minute) {
    if (birth->time_mode != BAZI_TIME_TRUE_SOLAR) return true;
    if (*year < 1900 || *year > 2100) return false;
    int doy = *day - 1;
    for (int m = 1; m < *month; ++m) doy += days_in_month(*year, m);
    const double b = 2.0 * 3.14159265358979323846 * (double)(doy - 81) / 364.0;
    const double eot = 9.87 * sin(2.0 * b) - 7.53 * cos(b) - 1.5 * sin(b);
    const double longitude = (double)birth->longitude_e4 / 10000.0;
    const int correction = (int)lround(4.0 * (longitude - 120.0) + eot);
    int64_t absolute = bazi_almanac_days_from_civil(*year, *month, *day);
    int total = *hour * 60 + *minute + correction;
    while (total < 0) { total += 1440; --absolute; }
    while (total >= 1440) { total -= 1440; ++absolute; }
    bazi_almanac_civil_from_days(absolute, year, month, day);
    *hour = total / 60;
    *minute = total % 60;
    return bazi_almanac_valid_date(*year, *month, *day);
}

static int64_t add_days(int64_t days, int amount) {
    return days + amount;
}

static void add_start_date(int solar_year, int solar_month, int solar_day,
                           int solar_hour, int years, int months, int days,
                           int extra_hours,
                           bazi_luck_t *luck) {
    int year = solar_year + years;
    int month0 = (solar_month - 1) + months;
    year += month0 / 12;
    month0 %= 12;
    int month = month0 + 1;
    int day = solar_day;
    int limit = days_in_month(year, month);
    if (day > limit) day = limit;
    int hour = solar_hour + extra_hours;
    if (hour >= 24) {
        days += hour / 24;
        hour %= 24;
    }
    int64_t absolute = add_days(bazi_almanac_days_from_civil(year, month, day), days);
    int final_year, final_month, final_day;
    bazi_almanac_civil_from_days(absolute, &final_year, &final_month, &final_day);
    luck->first_year = (int16_t)final_year;
    luck->first_month = (uint8_t)final_month;
    luck->first_day = (uint8_t)final_day;
    luck->first_hour = (uint8_t)hour;
}

static int ganzhi_cycle(bazi_ganzhi_t ganzhi) {
    for (int cycle = 0; cycle < 60; ++cycle) {
        if (cycle % 10 == ganzhi.stem && cycle % 12 == ganzhi.branch) return cycle;
    }
    return 0;
}

static bool calculate_luck(const bazi_chart_t *chart, bazi_luck_t *luck) {
    memset(luck, 0, sizeof(*luck));
    const int year_stem = chart->pillar[0].stem;
    const bool yang_year = (year_stem & 1U) == 0;
    luck->forward = (chart->birth.sex == BAZI_SEX_MALE && yang_year) ||
                    (chart->birth.sex == BAZI_SEX_FEMALE && !yang_year);

    const int64_t birth_second = bazi_almanac_absolute_minute(
        chart->solar_year, chart->solar_month, chart->solar_day,
        chart->solar_hour, chart->solar_minute) * 60;
    int64_t previous = INT64_MIN;
    int64_t next = INT64_MAX;
    for (int year = chart->solar_year - 1; year <= chart->solar_year + 1; ++year) {
        for (int jie = 0; jie < 12; ++jie) {
            int64_t value;
            if (!bazi_almanac_jie_absolute_second(year, (uint8_t)jie, &value)) continue;
            if (value <= birth_second && value > previous) previous = value;
            if (value > birth_second && value < next) next = value;
        }
    }
    if (previous == INT64_MIN || next == INT64_MAX) return false;
    const int64_t delta = (luck->forward ? next - birth_second : birth_second - previous) / 60;
    if (delta < 0) return false;
    const int years = (int)(delta / 4320);
    int remainder = (int)(delta % 4320);
    const int months = remainder / 360;
    remainder %= 360;
    const int days = remainder / 12;
    remainder %= 12;
    const int extra_hours = remainder * 2;
    add_start_date(chart->solar_year, chart->solar_month, chart->solar_day,
                   chart->solar_hour, years, months, days, extra_hours, luck);
    luck->first_age = (uint8_t)(luck->first_year - chart->solar_year + 1);

    const int month_cycle = ganzhi_cycle(chart->pillar[1]);
    for (int i = 0; i < BAZI_LUCK_COUNT; ++i) {
        const int offset = luck->forward ? i + 1 : -(i + 1);
        const int cycle = positive_mod(month_cycle + offset, 60);
        luck->step[i].start_year = (int16_t)(luck->first_year + i * 10);
        luck->step[i].start_age = (uint8_t)(luck->first_age + i * 10);
        luck->step[i].ganzhi.stem = (uint8_t)(cycle % 10);
        luck->step[i].ganzhi.branch = (uint8_t)(cycle % 12);
    }
    return true;
}

bool bazi_core_valid_birth(const bazi_birth_t *birth) {
    if (!birth || (birth->calendar != BAZI_CALENDAR_SOLAR &&
                   birth->calendar != BAZI_CALENDAR_LUNAR) ||
        (birth->sex != BAZI_SEX_MALE && birth->sex != BAZI_SEX_FEMALE) ||
        birth->hour > 23 || birth->minute > 59 ||
        (birth->zi_rule != BAZI_ZI_EARLY && birth->zi_rule != BAZI_ZI_LATE) ||
        (birth->time_mode != BAZI_TIME_STANDARD &&
         birth->time_mode != BAZI_TIME_TRUE_SOLAR)) return false;
    if (birth->calendar == BAZI_CALENDAR_SOLAR)
        return bazi_almanac_valid_date(birth->year, birth->month, birth->day);
    return birth->year >= 1900 && birth->year <= 2100 &&
           birth->month >= 1 && birth->month <= 12 &&
           birth->day >= 1 && birth->day <= 30;
}

uint8_t bazi_birth_max_day(const bazi_birth_t *birth) {
    if (!birth) return 0;
    if (birth->calendar == BAZI_CALENDAR_SOLAR)
        return bazi_almanac_solar_month_days(birth->year, birth->month);
    if (birth->calendar == BAZI_CALENDAR_LUNAR)
        return bazi_almanac_lunar_month_days(
            birth->year, birth->month, birth->lunar_leap != 0);
    return 0;
}

uint8_t bazi_minute_step(uint8_t minute, int delta) {
    int value = ((int)minute + delta) % 60;
    if (value < 0) value += 60;
    return (uint8_t)value;
}

bool bazi_core_create(const bazi_birth_t *birth, bazi_chart_t *chart) {
    if (!chart || !bazi_core_valid_birth(birth)) return false;
    memset(chart, 0, sizeof(*chart));
    chart->birth = *birth;
    if (chart->birth.city_id >= bazi_city_count()) chart->birth.city_id = 0;
    if (chart->birth.longitude_e4 == 0)
        chart->birth.longitude_e4 = kCities[chart->birth.city_id].longitude_e4;

    int solar_year = birth->year;
    int solar_month = birth->month;
    int solar_day = birth->day;
    if (birth->calendar == BAZI_CALENDAR_LUNAR) {
        bazi_almanac_t converted;
        if (!bazi_almanac_calculate_lunar(
                birth->year, birth->month, birth->day, birth->hour, birth->minute,
                birth->lunar_leap != 0,
                birth->zi_rule == BAZI_ZI_LATE ? BAZI_ALMANAC_ZI_LATE : BAZI_ALMANAC_ZI_EARLY,
                &converted)) return false;
        solar_year = converted.year;
        solar_month = converted.month;
        solar_day = converted.day;
    }
    int hour = birth->hour;
    int minute = birth->minute;
    if (!adjust_true_solar(&chart->birth, &solar_year, &solar_month, &solar_day,
                           &hour, &minute)) return false;

    bazi_almanac_t almanac;
    if (!bazi_almanac_calculate_solar(
            solar_year, solar_month, solar_day, hour, minute,
            birth->zi_rule == BAZI_ZI_LATE ? BAZI_ALMANAC_ZI_LATE : BAZI_ALMANAC_ZI_EARLY,
            &almanac)) return false;

    chart->solar_year = almanac.year;
    chart->solar_month = almanac.month;
    chart->solar_day = almanac.day;
    chart->solar_hour = almanac.hour;
    chart->solar_minute = almanac.minute;
    chart->lunar_year = almanac.lunar_year;
    chart->lunar_month = almanac.lunar_month;
    chart->lunar_day = almanac.lunar_day;
    chart->lunar_month_leap = almanac.lunar_month_leap;
    chart->pillar[0] = (bazi_ganzhi_t){almanac.year_stem, almanac.year_branch};
    chart->pillar[1] = (bazi_ganzhi_t){almanac.month_stem, almanac.month_branch};
    chart->pillar[2] = (bazi_ganzhi_t){almanac.day_stem, almanac.day_branch};
    chart->pillar[3] = (bazi_ganzhi_t){almanac.hour_stem, almanac.hour_branch};
    for (int i = 0; i < 4; ++i) {
        chart->stem_god[i] = i == 2 ? BAZI_GOD_DAY_MASTER :
            stem_god(chart->pillar[2].stem, chart->pillar[i].stem);
        chart->hidden_count[i] = kHiddenCount[chart->pillar[i].branch];
        memcpy(chart->hidden[i], kHiddenStems[chart->pillar[i].branch],
               chart->hidden_count[i]);
    }
    chart->luck.forward = false;
    chart->valid = calculate_luck(chart, &chart->luck);
    return chart->valid;
}

const char *bazi_stem_text(uint8_t stem) {
    return stem < 10 ? kStems[stem] : "";
}

const char *bazi_branch_text(uint8_t branch) {
    return branch < 12 ? kBranches[branch] : "";
}

const char *bazi_god_text(uint8_t god) {
    return god < 10 ? kGods[god] : "日主";
}

uint8_t bazi_city_count(void) {
    return (uint8_t)(sizeof(kCities) / sizeof(kCities[0]));
}

const char *bazi_city_name(uint8_t city_id) {
    return city_id < bazi_city_count() ? kCities[city_id].name : "北京";
}

int32_t bazi_city_longitude_e4(uint8_t city_id) {
    return city_id < bazi_city_count() ? kCities[city_id].longitude_e4 :
           kCities[0].longitude_e4;
}

uint8_t bazi_element_of_stem(uint8_t stem) {
    return stem < 10 ? (uint8_t)(stem / 2) : BAZI_ELEMENT_EARTH;
}

uint8_t bazi_element_of_branch(uint8_t branch) {
    static const uint8_t elements[12] = {
        BAZI_ELEMENT_WATER, BAZI_ELEMENT_EARTH, BAZI_ELEMENT_WOOD, BAZI_ELEMENT_WOOD,
        BAZI_ELEMENT_EARTH, BAZI_ELEMENT_FIRE, BAZI_ELEMENT_FIRE, BAZI_ELEMENT_EARTH,
        BAZI_ELEMENT_METAL, BAZI_ELEMENT_METAL, BAZI_ELEMENT_EARTH, BAZI_ELEMENT_WATER,
    };
    return branch < 12 ? elements[branch] : BAZI_ELEMENT_EARTH;
}

bazi_ganzhi_t bazi_flow_year(int year) {
    const int cycle = positive_mod(year - 4, 60);
    return (bazi_ganzhi_t){(uint8_t)(cycle % 10), (uint8_t)(cycle % 12)};
}

uint8_t bazi_current_luck_index(const bazi_chart_t *chart, int current_year) {
    if (!chart || !chart->valid) return 0;
    for (int i = 0; i < BAZI_LUCK_COUNT; ++i) {
        const int start = chart->luck.step[i].start_year;
        if (current_year >= start && current_year < start + BAZI_YEARS_PER_LUCK) return (uint8_t)i;
    }
    return current_year < chart->luck.step[0].start_year ? 0 : BAZI_LUCK_COUNT - 1;
}

uint8_t bazi_flow_row_for_year(const bazi_chart_t *chart, uint8_t luck_index,
                               int current_year) {
    if (!chart || !chart->valid || luck_index >= BAZI_LUCK_COUNT) return 0;
    int row = current_year - chart->luck.step[luck_index].start_year;
    if (row < 0) row = 0;
    if (row >= BAZI_YEARS_PER_LUCK) row = BAZI_YEARS_PER_LUCK - 1;
    return (uint8_t)row;
}
