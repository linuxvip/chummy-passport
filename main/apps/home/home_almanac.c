#include "apps/home/home_almanac.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "apps/home/home_almanac_data.inc"

static const char *const kStems[] = {
    "甲", "乙", "丙", "丁", "戊", "己", "庚", "辛", "壬", "癸",
};

static const char *const kBranches[] = {
    "子", "丑", "寅", "卯", "辰", "巳",
    "午", "未", "申", "酉", "戌", "亥",
};

static const char *const kSolarTerms[] = {
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

static int days_in_month(int year, int month) {
    static const uint8_t days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month < 1 || month > 12) return 0;
    return days[month - 1] + (month == 2 && leap_year(year));
}

static int day_of_year(int year, int month, int day) {
    int result = day - 1;
    for (int m = 1; m < month; ++m) result += days_in_month(year, m);
    return result;
}

// Howard Hinnant's civil-date conversion, with 1970-01-01 as day zero.
static int64_t days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153U * (month + (month > 2 ? -3 : 9)) + 2U) / 5U + day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

static uint32_t solar_term_minute(int year, int index) {
    const size_t year_index = (size_t)(year - HOME_ALMANAC_FIRST_YEAR);
    const size_t bit = (size_t)index * 20U;
    const size_t word = bit / 32U;
    const unsigned shift = (unsigned)(bit % 32U);
    uint64_t packed = kSolarTermMinutes[year_index][word];
    if (shift > 12U)
        packed |= (uint64_t)kSolarTermMinutes[year_index][word + 1] << 32U;
    return (uint32_t)((packed >> shift) & 0xfffffU);
}

static uint32_t lunar_year_info(int year) {
    return kLunarYearInfo[(size_t)(year - HOME_ALMANAC_FIRST_YEAR)];
}

static int positive_mod(int value, int modulus) {
    int result = value % modulus;
    return result < 0 ? result + modulus : result;
}

static void make_ganzhi(int stem, int branch, char out[7]) {
    snprintf(out, 7, "%s%s", kStems[positive_mod(stem, 10)], kBranches[positive_mod(branch, 12)]);
}

static int calculate_lunar_date(int year, int64_t solar_day, home_almanac_t *out) {
    int lunar_year = year;
    uint32_t info = lunar_year_info(lunar_year);
    int64_t new_year_day = days_from_civil(lunar_year, 1, (info & 0x1ffU) + 1U);
    if (solar_day < new_year_day) {
        --lunar_year;
        info = lunar_year_info(lunar_year);
        new_year_day = days_from_civil(lunar_year, 1, (info & 0x1ffU) + 1U);
    }

    int offset = (int)(solar_day - new_year_day);
    const int leap_month = (int)((info >> 9U) & 0x0fU);
    const uint32_t long_months = info >> 13U;
    int slot = 0;
    int lunar_month = 0;
    bool is_leap = false;

    for (int month = 1; month <= 12; ++month) {
        int days = (long_months & (1U << slot)) ? 30 : 29;
        if (offset < days) {
            lunar_month = month;
            break;
        }
        offset -= days;
        ++slot;

        if (month == leap_month) {
            days = (long_months & (1U << slot)) ? 30 : 29;
            if (offset < days) {
                lunar_month = month;
                is_leap = true;
                break;
            }
            offset -= days;
            ++slot;
        }
    }

    if (lunar_month == 0) {
        out->valid = false;
        return 0;
    }

    out->lunar_month = (uint8_t)lunar_month;
    out->lunar_day = (uint8_t)(offset + 1);
    out->lunar_month_leap = is_leap;
    snprintf(out->lunar_month_day, sizeof(out->lunar_month_day), "%s%s月%s",
             is_leap ? "闰" : "", kLunarMonths[lunar_month], kLunarDays[offset + 1]);
    return lunar_year;
}

bool home_almanac_calculate_ex(int year, int month, int day, int hour, int minute,
                               home_zi_rule_t zi_rule, home_almanac_t *out) {
    if (!out || year < 2024 || year > 2099 || month < 1 || month > 12 ||
        day < 1 || day > days_in_month(year, month) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        (zi_rule != HOME_ZI_EARLY && zi_rule != HOME_ZI_LATE)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    const int doy = day_of_year(year, month, day);
    const int64_t absolute_day = days_from_civil(year, (unsigned)month, (unsigned)day);
    const uint32_t local_minute = (uint32_t)doy * 1440U +
                                  (uint32_t)hour * 60U + (uint32_t)minute;

    // Four-pillar year changes at the exact Li Chun instant.
    const int ganzhi_year = local_minute >= solar_term_minute(year, 2) ? year : year - 1;
    const int year_cycle = positive_mod(ganzhi_year - 4, 60);
    make_ganzhi(year_cycle % 10, year_cycle % 12, out->year);

    // Month pillar changes at the twelve "jie" terms. The latest matching term
    // determines the branch; month stems use the year stem's Five-Tiger rule.
    static const int jie_indexes[] = {0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22};
    int latest_jie = -1;
    for (int i = 11; i >= 0; --i) {
        if (local_minute >= solar_term_minute(year, jie_indexes[i])) {
            latest_jie = i;
            break;
        }
    }
    int month_branch_from_yin;
    if (latest_jie >= 0) {
        // 小寒 is Chou (relative index 11), Li Chun is Yin (0), and so on.
        month_branch_from_yin = positive_mod(latest_jie + 11, 12);
    } else {
        month_branch_from_yin = 10;  // Before Xiao Han: previous year's Zi month.
    }
    const int year_stem = year_cycle % 10;
    make_ganzhi((year_stem * 2 + 2 + month_branch_from_yin) % 10,
                (month_branch_from_yin + 2) % 12, out->month);

    // Civil day pillar; day zero is 1970-01-01, a Xin Si day (cycle index 17).
    const int zi_adjust = zi_rule == HOME_ZI_LATE && hour == 23 ? 1 : 0;
    const int day_cycle = (int)((absolute_day + zi_adjust + 17) % 60);
    make_ganzhi(day_cycle % 10, day_cycle % 12, out->day);

    // Zi hour starts at 23:00. The Five-Rat rule derives its stem from the day stem.
    const int hour_branch = ((hour + 1) / 2) % 12;
    const int day_stem = day_cycle % 10;
    make_ganzhi((day_stem % 5 * 2 + hour_branch) % 10, hour_branch, out->hour);

    calculate_lunar_date(year, absolute_day, out);
    if (!out->valid && out->lunar_month == 0) {
        // calculate_lunar_date cannot reach here for the supported range; keep
        // this guard explicit so a future table edit fails closed.
        return false;
    }

    int term_today = -1;
    for (int i = 0; i < 24; ++i) {
        if ((int)(solar_term_minute(year, i) / 1440U) == doy) {
            term_today = i;
            break;
        }
    }
    if (term_today >= 0)
        snprintf(out->solar_term_today, sizeof(out->solar_term_today), "%s",
                 kSolarTerms[term_today]);

    int next_term = -1;
    int next_term_year = year;
    for (int i = 0; i < 24; ++i) {
        if ((int)(solar_term_minute(year, i) / 1440U) > doy) {
            next_term = i;
            break;
        }
    }
    if (next_term < 0) {
        next_term = 0;
        next_term_year = year + 1;
    }
    const int64_t next_term_day =
        days_from_civil(next_term_year, 1, 1) +
        (int)(solar_term_minute(next_term_year, next_term) / 1440U);
    out->days_to_next_solar_term =
        (uint8_t)(next_term_day - absolute_day);
    snprintf(out->next_solar_term, sizeof(out->next_solar_term), "%s",
             kSolarTerms[next_term]);
    out->valid = true;
    return true;
}

bool home_almanac_calculate(int year, int month, int day, int hour, int minute,
                            home_almanac_t *out) {
    return home_almanac_calculate_ex(year, month, day, hour, minute,
                                     HOME_ZI_EARLY, out);
}
