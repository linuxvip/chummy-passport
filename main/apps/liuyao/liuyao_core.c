#include "apps/liuyao/liuyao_core.h"

#include "apps/bazi/bazi_almanac.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t code;
    uint8_t palace;
    uint8_t palace_position;
} HexInfo;

typedef struct {
    uint8_t inner[3];
    uint8_t outer[3];
} JiaPair;

typedef struct {
    uint8_t stem;
    uint8_t branch;
} Ganzhi;

static const char *const kHexNames[64] = {
    "坤为地", "地雷复", "地水师", "地泽临", "地山谦", "地火明夷", "地风升", "地天泰",
    "雷地豫", "震为雷", "雷水解", "雷泽归妹", "雷山小过", "雷火丰", "雷风恒", "雷天大壮",
    "水地比", "水雷屯", "坎为水", "水泽节", "水山蹇", "水火既济", "水风井", "水天需",
    "泽地萃", "泽雷随", "泽水困", "兑为泽", "泽山咸", "泽火革", "泽风大过", "泽天夬",
    "山地剥", "山雷颐", "山水蒙", "山泽损", "艮为山", "山火贲", "山风蛊", "山天大畜",
    "火地晋", "火雷噬嗑", "火水未济", "火泽睽", "火山旅", "离为火", "火风鼎", "火天大有",
    "风地观", "风雷益", "风水涣", "风泽中孚", "风山渐", "风火家人", "巽为风", "风天小畜",
    "天地否", "天雷无妄", "天水讼", "天泽履", "天山遁", "天火同人", "天风姤", "乾为天",
};

// Concise modern-language summaries of the traditional hexagram themes.
// These describe the image and its practical emphasis; they deliberately do
// not turn the chart into an automatic good/bad prediction.
static const char *const kHexExplanations[64] = {
    "大地承载万物，宜守正包容，顺势配合；先积累再行动。",
    "一阳来复，事情开始回转，宜循序渐进，及时修正旧路。",
    "众人集结行动，重点在纪律、组织与明确的指挥。",
    "上临下，亲近而有所督导，事情正在靠近，宜以诚意应对。",
    "山藏于地下，强调谦逊、收敛与稳步积累。",
    "光明受伤而入地，处境晦暗时宜自保，收敛锋芒。",
    "木从地中渐长，事情由下而上累积，宜持续用功。",
    "天地相交，上下沟通顺畅，适合推进与合作。",
    "雷动地上，气氛振奋，宜事先准备，不宜放纵乐观。",
    "雷声骤起，变动与警醒并来，先稳住再应对。",
    "雷雨化开僵局，压力开始释放，适合解决遗留问题。",
    "少女随嫁，关系与位置有先后，行动需守分寸。",
    "小事可以适度超越，大事宜谨慎收敛，不宜冒进。",
    "丰盛之时资源集中，宜把握当下，也要防盛极转衰。",
    "雷风相应，贵在长期坚持与关系稳定，不宜频繁变卦。",
    "阳刚壮盛，力量充足，但用力要正，避免恃强。",
    "水行地上，亲近依附，重在选择可信的伙伴与关系。",
    "万事初生，阻力与资源不足，先建立基础再求快。",
    "重险相叠，需要诚实面对困难，以谨慎和耐性过关。",
    "水在泽上，事情要有节度，规则适度则通行。",
    "前有险、后有阻，宜停步整备，借力绕过难关。",
    "事情已经成形，细节决定收尾，宜守成防松。",
    "井水养人，资源固定但可取用，重点在维护与共享。",
    "云聚待雨，条件尚未齐备，合理等待比强求更好。",
    "泽聚地上，人和资源聚合，宜协作并防聚集后的纷争。",
    "雷动泽应，顺势跟随，选择正确的对象与时机。",
    "泽中无水，处境受限，宜守住原则，减少消耗。",
    "泽水相连，以喜悦沟通为主，言语需真诚有度。",
    "山泽相应，彼此感应，关系重在尊重与互相影响。",
    "水火相克而求变，适合改革，也要把握时势和程序。",
    "栋梁承重过度，形势失衡，宜果断处理关键问题。",
    "泽水决口，需要明确决断，但要处理方式克制。",
    "山附于地而逐渐剥落，宜守本防损，等待转机。",
    "山下有雷，饮食与养护之象，重点是资源和长期供养。",
    "山下有水雾，信息不明，宜请教、学习和建立规则。",
    "减损资源以成事，短期取舍要服务长期目标。",
    "两山相止，宜停下来审视，减少欲望与轻率行动。",
    "山下有火，外在修饰与内在实质并重，勿只重表面。",
    "山下有风，旧弊需要整理，宜查明根源后修复。",
    "山藏天势，蓄积力量与资源，宜厚积而后发动。",
    "火行地上，光明上升，事情有进展，适合公开推进。",
    "雷电相交，障碍需果断清除，规则和证据不可缺。",
    "火水未交，事情尚差最后一步，收尾前仍需谨慎。",
    "火上泽下，方向不同，先求小同，勿强行统一。",
    "山上燃火，人在旅途中，宜守礼自持，防止失所。",
    "两火相继，明察与依附并重，保持清醒和持续光明。",
    "木火成鼎，有更新与调和之意，合作需角色清楚。",
    "火在天上，资源与声势充足，宜公正运用所得。",
    "风行地上，宜观察全局与示范影响，不宜仓促决定。",
    "风雷相助，利益与行动同步，适合扩大共识和投入。",
    "风行水上，聚散流动，宜疏通分歧、重启沟通。",
    "风行泽上，诚信贯通内外，承诺与事实要一致。",
    "风林山上，事情逐步推进，按次序积累最为稳妥。",
    "风自火出，内部秩序与责任分工是事情基础。",
    "两风相随，以渗透和沟通成事，宜柔中有原则。",
    "风行天上，小有积蓄，力量未足，宜先聚小成。",
    "天地不交，上下阻隔，先止损与守位，等待沟通恢复。",
    "天雷相动，宜守真实本分，不投机、不强求。",
    "天向上、水向下，分歧明显，宜调解并防官司。",
    "天在上、泽在下，行事如履薄冰，守礼可避险。",
    "天高山远，适时退避比硬碰更有利，保存实力。",
    "天火同明，重在求同协作，公开合作可得助力。",
    "天下有风，意外相遇，机会与风险同在，先辨人再行动。",
    "纯阳自强，主动而有力量，宜持续进取并保持节制。",
};

static const char *const kTrigramNames[8] = {
    "乾", "兑", "离", "震", "巽", "坎", "艮", "坤",
};

static const char *const kPalaceNames[8] = {
    "乾宫", "兑宫", "离宫", "震宫", "巽宫", "坎宫", "艮宫", "坤宫",
};

static const char *const kRelativeNames[LIUYAO_RELATIVE_COUNT] = {
    "父母", "兄弟", "子孙", "妻财", "官鬼",
};

static const char *const kSpiritNames[LIUYAO_SPIRIT_COUNT] = {
    "青龙", "朱雀", "勾陈", "螣蛇", "白虎", "玄武",
};

static const char *const kElementNames[5] = {
    "木", "火", "土", "金", "水",
};

static const char *const kPalaceStateNames[8] = {
    "本宫", "一世", "二世", "三世", "四世", "五世", "游魂", "归魂",
};

static const uint8_t kTrigramBits[8] = {7, 3, 5, 1, 6, 2, 4, 0};
static const uint8_t kPalaceElements[8] = {
    BAZI_ELEMENT_METAL, BAZI_ELEMENT_METAL, BAZI_ELEMENT_FIRE, BAZI_ELEMENT_WOOD,
    BAZI_ELEMENT_WOOD, BAZI_ELEMENT_WATER, BAZI_ELEMENT_EARTH, BAZI_ELEMENT_EARTH,
};
static const uint8_t kPalaceHex[8][8] = {
    {63, 62, 60, 56, 48, 32, 40, 47},
    {27, 26, 24, 28, 20, 4, 12, 11},
    {45, 44, 46, 42, 34, 50, 58, 61},
    {9, 8, 10, 14, 6, 22, 30, 25},
    {54, 55, 53, 49, 57, 41, 33, 38},
    {18, 19, 17, 21, 29, 13, 5, 2},
    {36, 37, 39, 35, 43, 59, 51, 52},
    {0, 1, 3, 7, 15, 31, 23, 16},
};
static const uint8_t kSpiritStartByStem[10] = {
    LIUYAO_SPIRIT_AZURE_DRAGON, LIUYAO_SPIRIT_AZURE_DRAGON,
    LIUYAO_SPIRIT_VERMILION_BIRD, LIUYAO_SPIRIT_VERMILION_BIRD,
    LIUYAO_SPIRIT_HOOKED_CHEN, LIUYAO_SPIRIT_FLYING_SNAKE,
    LIUYAO_SPIRIT_WHITE_TIGER, LIUYAO_SPIRIT_WHITE_TIGER,
    LIUYAO_SPIRIT_BLACK_TORTOISE, LIUYAO_SPIRIT_BLACK_TORTOISE,
};

// Trigram numbers follow 乾1 兑2 离3 震4 巽5 坎6 艮7 坤8.
static const Ganzhi kNajia[8][6] = {
    {{0, 0}, {0, 2}, {0, 4}, {8, 6}, {8, 8}, {8, 10}},
    {{3, 5}, {3, 3}, {3, 1}, {3, 11}, {3, 9}, {3, 7}},
    {{5, 3}, {5, 1}, {5, 11}, {5, 9}, {5, 7}, {5, 5}},
    {{6, 0}, {6, 2}, {6, 4}, {6, 6}, {6, 8}, {6, 10}},
    {{7, 1}, {7, 11}, {7, 9}, {7, 7}, {7, 5}, {7, 3}},
    {{4, 2}, {4, 4}, {4, 6}, {4, 8}, {4, 10}, {4, 0}},
    {{2, 4}, {2, 6}, {2, 8}, {2, 10}, {2, 0}, {2, 2}},
    {{1, 7}, {1, 5}, {1, 3}, {1, 11}, {1, 9}, {1, 7}},
};

static uint8_t mod8_one_based(unsigned value) {
    const uint8_t rem = (uint8_t)(value % 8);
    return rem ? rem : 8;
}

static uint8_t mod6_one_based(unsigned value) {
    const uint8_t rem = (uint8_t)(value % 6);
    return rem ? rem : 6;
}

static bool branch_clashes(uint8_t a, uint8_t b) {
    return ((a + 6) % 12) == (b % 12);
}

// Six-combination pairs: 子丑, 寅亥, 卯戌, 辰酉, 巳申, 午未.
static bool branch_six_combines(uint8_t a, uint8_t b) {
    static const int8_t pairs[12] = {1, 0, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2};
    return a < 12 && b < 12 && pairs[a] == b;
}

static uint8_t relation_for(uint8_t palace_element, uint8_t line_element) {
    if (line_element == palace_element) return LIUYAO_RELATIVE_BROTHER;
    if (line_element == (palace_element + 1) % 5) return LIUYAO_RELATIVE_CHILD;
    if (line_element == (palace_element + 2) % 5) return LIUYAO_RELATIVE_WEALTH;
    if (line_element == (palace_element + 3) % 5) return LIUYAO_RELATIVE_OFFICER;
    return LIUYAO_RELATIVE_PARENT;
}

static uint8_t palace_for_hex(uint8_t code, uint8_t *position) {
    for (uint8_t p = 0; p < 8; ++p) {
        for (uint8_t i = 0; i < 8; ++i) {
            if (kPalaceHex[p][i] == code) {
                if (position) *position = i;
                return p;
            }
        }
    }
    if (position) *position = 0;
    return 0;
}

static uint8_t trigram_number_from_bits(uint8_t bits) {
    for (uint8_t i = 0; i < 8; ++i) {
        if (kTrigramBits[i] == (bits & 7U)) return (uint8_t)(i + 1);
    }
    return LIUYAO_TRIGRAM_KUN;
}

static uint8_t day_cycle(uint8_t stem, uint8_t branch) {
    for (uint8_t i = 0; i < 60; ++i) {
        if (i % 10 == stem && i % 12 == branch) return i;
    }
    return 0;
}

static void set_empty_branches(uint8_t day_stem, uint8_t day_branch, uint8_t out[2]) {
    const uint8_t cycle = day_cycle(day_stem, day_branch);
    const uint8_t xun_start = (uint8_t)(cycle - (cycle % 10));
    out[0] = (uint8_t)((xun_start + 10) % 12);
    out[1] = (uint8_t)((xun_start + 11) % 12);
}

static bool adjust_true_solar(const liuyao_input_t *input,
                              int *year, int *month, int *day,
                              int *hour, int *minute) {
    if (input->time_mode != BAZI_TIME_TRUE_SOLAR) return true;
    if (*year < 1900 || *year > 2100) return false;
    int doy = *day - 1;
    for (int m = 1; m < *month; ++m) doy += liuyao_input_max_day(*year, m);
    const double b = 2.0 * 3.14159265358979323846 * (double)(doy - 81) / 364.0;
    const double eot = 9.87 * sin(2.0 * b) - 7.53 * cos(b) - 1.5 * sin(b);
    const double longitude = (double)input->longitude_e4 / 10000.0;
    const int correction = (int)lround(4.0 * (longitude - 120.0) + eot);
    int64_t absolute = bazi_almanac_days_from_civil(*year, *month, *day);
    int total = *hour * 60 + *minute + correction;
    while (total < 0) {
        total += 1440;
        --absolute;
    }
    while (total >= 1440) {
        total -= 1440;
        ++absolute;
    }
    bazi_almanac_civil_from_days(absolute, year, month, day);
    *hour = total / 60;
    *minute = total % 60;
    return bazi_almanac_valid_date(*year, *month, *day);
}

uint8_t liuyao_input_max_day(int year, int month) {
    return bazi_almanac_solar_month_days(year, month);
}

uint8_t liuyao_minute_step(uint8_t minute, int delta) {
    int value = (int)minute + delta;
    while (value < 0) value += 60;
    while (value >= 60) value -= 60;
    return (uint8_t)value;
}

bool liuyao_input_valid(const liuyao_input_t *input) {
    if (!input) return false;
    if (input->method > LIUYAO_METHOD_NUMBER) return false;
    if (input->time_mode != BAZI_TIME_STANDARD &&
        input->time_mode != BAZI_TIME_TRUE_SOLAR) return false;
    if (input->zi_rule != BAZI_ZI_EARLY && input->zi_rule != BAZI_ZI_LATE) return false;
    if (input->year < 1900 || input->year > 2100 ||
        input->month < 1 || input->month > 12 ||
        input->day < 1 || input->day > liuyao_input_max_day(input->year, input->month) ||
        input->hour > 23 || input->minute > 59) return false;
    if (input->method == LIUYAO_METHOD_NUMBER) {
        if (input->digit_count < 2 || input->digit_count > LIUYAO_MAX_DIGITS)
            return false;
        for (uint8_t i = 0; i < input->digit_count; ++i) {
            if (input->digits[i] < '0' || input->digits[i] > '9') return false;
        }
    }
    return true;
}

static bool fill_number_preview(liuyao_chart_t *chart) {
    const uint8_t count = chart->input.digit_count;
    const uint8_t cut = count % 2 == 0 ? count / 2 : count / 2;
    uint16_t first = 0;
    uint16_t second = 0;
    for (uint8_t i = 0; i < cut; ++i) first += (uint8_t)(chart->input.digits[i] - '0');
    for (uint8_t i = cut; i < count; ++i) second += (uint8_t)(chart->input.digits[i] - '0');
    chart->first_sum = first;
    chart->second_sum = second;
    chart->upper_number = mod8_one_based(first);
    chart->lower_number = mod8_one_based(second);
    chart->hour_number = (uint8_t)(chart->hour_branch + 1);
    chart->moving_line = mod6_one_based((unsigned)chart->upper_number +
                                        chart->lower_number + chart->hour_number);
    return true;
}

static bool fill_time_preview(liuyao_chart_t *chart) {
    chart->year_number = (uint8_t)(chart->year_branch + 1);
    chart->month_number = chart->lunar_month;
    chart->day_number = chart->lunar_day;
    chart->hour_number = (uint8_t)(chart->hour_branch + 1);
    chart->first_sum = (uint16_t)(chart->year_number + chart->month_number +
                                  chart->day_number);
    chart->second_sum = (uint16_t)(chart->first_sum + chart->hour_number);
    chart->upper_number = mod8_one_based(chart->first_sum);
    chart->lower_number = mod8_one_based(chart->second_sum);
    chart->moving_line = mod6_one_based(chart->second_sum);
    return true;
}

static void fill_lines(liuyao_chart_t *chart) {
    for (uint8_t i = 0; i < LIUYAO_LINE_COUNT; ++i) {
        const bool lower = i < 3;
        const uint8_t original_trigram = lower ?
            trigram_number_from_bits((uint8_t)(chart->original_id & 7U)) :
            trigram_number_from_bits((uint8_t)((chart->original_id >> 3) & 7U));
        const uint8_t changed_trigram = lower ?
            trigram_number_from_bits((uint8_t)(chart->changed_id & 7U)) :
            trigram_number_from_bits((uint8_t)((chart->changed_id >> 3) & 7U));
        const Ganzhi original = kNajia[original_trigram - 1][i];
        const Ganzhi changed = kNajia[changed_trigram - 1][i];
        liuyao_line_t *line = &chart->lines[i];
        memset(line, 0, sizeof(*line));
        line->yang = ((chart->original_id >> i) & 1U) != 0;
        line->stem = original.stem;
        line->branch = original.branch;
        line->element = bazi_element_of_branch(line->branch);
        line->relative = relation_for(chart->palace_element, line->element);
        line->spirit = (uint8_t)((kSpiritStartByStem[chart->day_stem] + i) %
                                 LIUYAO_SPIRIT_COUNT);
        line->changed_stem = changed.stem;
        line->changed_branch = changed.branch;
        line->changed_element = bazi_element_of_branch(changed.branch);
        line->changed_relative = relation_for(chart->palace_element,
                                               line->changed_element);
        line->moving = chart->moving_line == i + 1;
        line->empty = line->branch == chart->empty_branch[0] ||
                      line->branch == chart->empty_branch[1];
        line->month_break = branch_clashes(line->branch, chart->month_branch);
        line->day_clash = branch_clashes(line->branch, chart->day_branch);
        line->month_combine = branch_six_combines(line->branch, chart->month_branch);
        line->day_combine = branch_six_combines(line->branch, chart->day_branch);
    }
}

bool liuyao_chart_create(const liuyao_input_t *input, liuyao_chart_t *chart) {
    if (!input || !chart || !liuyao_input_valid(input)) return false;
    memset(chart, 0, sizeof(*chart));
    chart->input = *input;

    int year = input->year;
    int month = input->month;
    int day = input->day;
    int hour = input->hour;
    int minute = input->minute;
    if (!adjust_true_solar(input, &year, &month, &day, &hour, &minute)) return false;

    bazi_almanac_t almanac;
    if (!bazi_almanac_calculate_solar(
            year, month, day, hour, minute,
            input->zi_rule == BAZI_ZI_LATE ? BAZI_ALMANAC_ZI_LATE : BAZI_ALMANAC_ZI_EARLY,
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
    chart->year_stem = almanac.year_stem;
    chart->year_branch = almanac.year_branch;
    chart->month_stem = almanac.month_stem;
    chart->month_branch = almanac.month_branch;
    chart->day_stem = almanac.day_stem;
    chart->day_branch = almanac.day_branch;
    chart->hour_stem = almanac.hour_stem;
    chart->hour_branch = almanac.hour_branch;
    set_empty_branches(chart->day_stem, chart->day_branch, chart->empty_branch);

    if (input->method == LIUYAO_METHOD_NUMBER) {
        fill_number_preview(chart);
    } else {
        fill_time_preview(chart);
    }

    const uint8_t lower_bits = kTrigramBits[chart->lower_number - 1];
    const uint8_t upper_bits = kTrigramBits[chart->upper_number - 1];
    chart->original_id = (uint8_t)(lower_bits | (upper_bits << 3));
    chart->changed_id = chart->original_id;
    const uint8_t moving_bit = (uint8_t)(1U << (chart->moving_line - 1));
    chart->changed_id ^= moving_bit;
    chart->palace = palace_for_hex(chart->original_id, &chart->palace_index);
    chart->palace_element = kPalaceElements[chart->palace];
    chart->shi = (uint8_t[]){6, 1, 2, 3, 4, 5, 4, 3}[chart->palace_index];
    chart->ying = (uint8_t)(((chart->shi - 1 + 3) % 6) + 1);
    fill_lines(chart);

    // Fill missing six-kin lines from the pure palace hex, preserving their
    // positions so the UI can show hidden lines without inventing data.
    uint8_t present = 0;
    for (uint8_t i = 0; i < 6; ++i)
        present |= (uint8_t)(1U << chart->lines[i].relative);
    const uint8_t pure = kPalaceHex[chart->palace][0];
    const uint8_t pure_upper = trigram_number_from_bits((uint8_t)((pure >> 3) & 7U));
    const uint8_t pure_lower = trigram_number_from_bits((uint8_t)(pure & 7U));
    for (uint8_t i = 0; i < 6; ++i) {
        const uint8_t trigram = i < 3 ? pure_lower : pure_upper;
        const Ganzhi hidden = kNajia[trigram - 1][i];
        const uint8_t element = bazi_element_of_branch(hidden.branch);
        const uint8_t relative = relation_for(chart->palace_element, element);
        if (present & (uint8_t)(1U << relative)) continue;
        liuyao_hidden_t *target =
            &chart->lines[i].hidden[chart->lines[i].hidden_count++];
        target->stem = hidden.stem;
        target->branch = hidden.branch;
        target->relative = relative;
    }

    chart->valid = true;
    return true;
}

const char *liuyao_hex_name(uint8_t hexagram_id) {
    return hexagram_id < 64 ? kHexNames[hexagram_id] : "";
}

const char *liuyao_hex_explanation(uint8_t hexagram_id) {
    return hexagram_id < 64 ? kHexExplanations[hexagram_id] : "";
}

const char *liuyao_trigram_name(uint8_t trigram_number) {
    return trigram_number >= 1 && trigram_number <= 8 ?
        kTrigramNames[trigram_number - 1] : "";
}

const char *liuyao_palace_name(uint8_t palace_index) {
    return palace_index < 8 ? kPalaceNames[palace_index] : "";
}

const char *liuyao_relative_text(uint8_t relative) {
    return relative < LIUYAO_RELATIVE_COUNT ? kRelativeNames[relative] : "";
}

const char *liuyao_spirit_text(uint8_t spirit) {
    return spirit < LIUYAO_SPIRIT_COUNT ? kSpiritNames[spirit] : "";
}

const char *liuyao_element_text(uint8_t element) {
    return element < 5 ? kElementNames[element] : "";
}

const char *liuyao_palace_state_text(uint8_t palace_index) {
    return palace_index < 8 ? kPalaceStateNames[palace_index] : "";
}
