#include "apps/bazi/bazi_almanac.h"

#include <stddef.h>
#include <string.h>

#include "apps/bazi/bazi_almanac_data.inc"

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

static uint32_t lunar_info(int year) {
    return kLunarYearInfo[(size_t)(year - BAZI_LUNAR_FIRST_YEAR)];
}

static int lunar_leap_month(uint32_t info) {
    return (int)(info & 0x0fU);
}

static bool lunar_month_long(uint32_t info, int slot) {
    return (info >> (4U + slot)) & 1U;
}

static int64_t lunar_new_year_day(int year) {
    const uint32_t info = lunar_info(year);
    return bazi_almanac_days_from_civil(year, 1, 1) + (int64_t)((info >> 17U) & 0x1ffU);
}

static int64_t jie_second(int year, int index) {
    if (year < BAZI_TERM_FIRST_YEAR || year > BAZI_TERM_LAST_YEAR ||
        index < 0 || index >= 12) return -1;
    return bazi_almanac_days_from_civil(year, 1, 1) * 86400 +
           (int64_t)kTermSecondOfYear[(size_t)(year - BAZI_TERM_FIRST_YEAR)][index];
}

static bool convert_lunar_to_solar(int lunar_year, int lunar_month, int lunar_day,
                                   bool leap, int *solar_year, int *solar_month,
                                   int *solar_day) {
    if (lunar_year < 1900 || lunar_year > 2100 ||
        lunar_month < 1 || lunar_month > 12 || lunar_day < 1 || lunar_day > 30) {
        return false;
    }
    const uint32_t info = lunar_info(lunar_year);
    const int leap_month = lunar_leap_month(info);
    if (leap && leap_month != lunar_month) return false;

    int64_t offset = 0;
    int slot = 0;
    bool found = false;
    for (int month = 1; month <= 12; ++month) {
        const int days = lunar_month_long(info, slot) ? 30 : 29;
        if (month == lunar_month && !leap) {
            if (lunar_day > days) return false;
            offset += lunar_day - 1;
            found = true;
            break;
        }
        offset += days;
        ++slot;
        if (month == leap_month) {
            const int leap_days = lunar_month_long(info, slot) ? 30 : 29;
            if (month == lunar_month && leap) {
                if (lunar_day > leap_days) return false;
                offset += lunar_day - 1;
                found = true;
                break;
            }
            offset += leap_days;
            ++slot;
        }
    }
    if (!found) return false;

    bazi_almanac_civil_from_days(lunar_new_year_day(lunar_year) + offset,
                                 solar_year, solar_month, solar_day);
    return true;
}

static void calculate_lunar_date(int year, int64_t solar_day, bazi_almanac_t *out) {
    int lunar_year = year;
    int64_t new_year = lunar_new_year_day(lunar_year);
    if (solar_day < new_year) {
        --lunar_year;
        new_year = lunar_new_year_day(lunar_year);
    }
    const uint32_t info = lunar_info(lunar_year);
    int offset = (int)(solar_day - new_year);
    const int leap_month = lunar_leap_month(info);
    int slot = 0;
    for (int month = 1; month <= 12; ++month) {
        const int days = lunar_month_long(info, slot) ? 30 : 29;
        if (offset < days) {
            out->lunar_year = (int16_t)lunar_year;
            out->lunar_month = (uint8_t)month;
            out->lunar_day = (uint8_t)(offset + 1);
            out->lunar_month_leap = false;
            return;
        }
        offset -= days;
        ++slot;
        if (month == leap_month) {
            const int leap_days = lunar_month_long(info, slot) ? 30 : 29;
            if (offset < leap_days) {
                out->lunar_year = (int16_t)lunar_year;
                out->lunar_month = (uint8_t)month;
                out->lunar_day = (uint8_t)(offset + 1);
                out->lunar_month_leap = true;
                return;
            }
            offset -= leap_days;
            ++slot;
        }
    }
    out->valid = false;
}

bool bazi_almanac_valid_date(int year, int month, int day) {
    return year >= 1900 && year <= 2100 && month >= 1 && month <= 12 &&
           day >= 1 && day <= days_in_month(year, month);
}

uint8_t bazi_almanac_solar_month_days(int year, int month) {
    if (year < 1900 || year > 2100 || month < 1 || month > 12) return 0;
    return (uint8_t)days_in_month(year, month);
}

uint8_t bazi_almanac_lunar_month_days(int year, int month, bool leap) {
    if (year < 1900 || year > 2100 || month < 1 || month > 12) return 0;
    const uint32_t info = lunar_info(year);
    const int leap_month = lunar_leap_month(info);
    int slot = 0;
    for (int current = 1; current <= 12; ++current) {
        const uint8_t days = lunar_month_long(info, slot) ? 30 : 29;
        if (current == month && !leap) return days;
        ++slot;
        if (current == leap_month) {
            const uint8_t leap_days = lunar_month_long(info, slot) ? 30 : 29;
            if (current == month && leap) return leap_days;
            ++slot;
        }
    }
    return 0;
}

int64_t bazi_almanac_days_from_civil(int year, int month, int day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = (unsigned)(year - era * 400);
    const unsigned doy = (153U * (unsigned)(month + (month > 2 ? -3 : 9)) + 2U) / 5U +
                         (unsigned)day - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return (int64_t)era * 146097 + (int64_t)doe - 719468;
}

void bazi_almanac_civil_from_days(int64_t days, int *year, int *month, int *day) {
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const unsigned doe = (unsigned)(days - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int y = (int)yoe + (int)era * 400;
    const unsigned doy = doe - (365U * yoe + yoe / 4U - yoe / 100U);
    const unsigned mp = (5U * doy + 2U) / 153U;
    const unsigned d = doy - (153U * mp + 2U) / 5U + 1U;
    const int m = (int)mp + (mp < 10U ? 3 : -9);
    y += m <= 2;
    *year = y;
    *month = (int)m;
    *day = (int)d;
}

int64_t bazi_almanac_absolute_minute(int year, int month, int day, int hour, int minute) {
    return bazi_almanac_days_from_civil(year, month, day) * 1440 +
           (int64_t)hour * 60 + minute;
}

bool bazi_almanac_jie_absolute_second(int year, uint8_t jie, int64_t *out) {
    const int64_t value = jie_second(year, jie);
    if (!out || value < 0) return false;
    *out = value;
    return true;
}

bool bazi_almanac_calculate_solar(int year, int month, int day, int hour, int minute,
                                  bazi_almanac_zi_rule_t zi_rule,
                                  bazi_almanac_t *out) {
    if (!out || !bazi_almanac_valid_date(year, month, day) ||
        hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        (zi_rule != BAZI_ALMANAC_ZI_EARLY && zi_rule != BAZI_ALMANAC_ZI_LATE)) {
        return false;
    }
    memset(out, 0, sizeof(*out));
    out->year = (int16_t)year;
    out->month = (uint8_t)month;
    out->day = (uint8_t)day;
    out->hour = (uint8_t)hour;
    out->minute = (uint8_t)minute;

    const int64_t absolute_day = bazi_almanac_days_from_civil(year, month, day);

    const int64_t absolute_second = absolute_day * 86400 +
                                    (int64_t)hour * 3600 + (int64_t)minute * 60;
    const int ganzhi_year = absolute_second >= jie_second(year, 1) ? year : year - 1;
    const int year_cycle = positive_mod(ganzhi_year - 4, 60);
    out->year_stem = (uint8_t)(year_cycle % 10);
    out->year_branch = (uint8_t)(year_cycle % 12);

    int latest_jie = -1;
    for (int i = 11; i >= 0; --i) {
        if (absolute_second >= jie_second(year, i)) {
            latest_jie = i;
            break;
        }
    }
    int month_from_yin;
    if (latest_jie >= 0) month_from_yin = positive_mod(latest_jie - 1, 12);
    else month_from_yin = 10;
    const int year_stem = year_cycle % 10;
    out->month_stem = (uint8_t)positive_mod(year_stem * 2 + 2 + month_from_yin, 10);
    out->month_branch = (uint8_t)((month_from_yin + 2) % 12);

    const int zi_adjust = zi_rule == BAZI_ALMANAC_ZI_LATE && hour == 23 ? 1 : 0;
    const int day_cycle = (int)((absolute_day + zi_adjust + 17) % 60);
    out->day_stem = (uint8_t)(day_cycle % 10);
    out->day_branch = (uint8_t)(day_cycle % 12);

    const int hour_branch = ((hour + 1) / 2) % 12;
    out->hour_branch = (uint8_t)hour_branch;
    out->hour_stem = (uint8_t)positive_mod((out->day_stem % 5) * 2 + hour_branch, 10);

    calculate_lunar_date(year, absolute_day, out);
    out->valid = true;
    return true;
}

bool bazi_almanac_calculate_lunar(int year, int month, int day, int hour, int minute,
                                  bool leap, bazi_almanac_zi_rule_t zi_rule,
                                  bazi_almanac_t *out) {
    int solar_year, solar_month, solar_day;
    if (!convert_lunar_to_solar(year, month, day, leap,
                                &solar_year, &solar_month, &solar_day)) {
        return false;
    }
    return bazi_almanac_calculate_solar(solar_year, solar_month, solar_day,
                                        hour, minute, zi_rule, out);
}
