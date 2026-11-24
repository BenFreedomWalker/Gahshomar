#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "gahshomar/calendar.h"

#include <stddef.h>
#include <time.h>

/*
 * The conversion follows the break-year Jalali calendar calculation.
 * The arithmetic core is deliberately isolated from platform and rendering
 * code so it can be tested and reused independently.
 */

static const int g_jalali_breaks[] = {
    -61, 9, 38, 199, 426, 686, 756, 818, 1111, 1181,
    1210, 1635, 2060, 2097, 2192, 2262, 2324, 2394,
    2456, 3178
};

static const char *g_month_names[12] = {
    "فروردین",
    "اردیبهشت",
    "خرداد",
    "تیر",
    "مرداد",
    "شهریور",
    "مهر",
    "آبان",
    "آذر",
    "دی",
    "بهمن",
    "اسفند"
};

typedef struct GhJalaliYearInfo {
    int gregorian_year;
    int march_day;
    int leap_code;
} GhJalaliYearInfo;

/* Gregorian dates in the supported range always use positive JDN values. */
static int gh_gregorian_jdn(int year, int month, int day)
{
    int a = (14 - month) / 12;
    int y = year + 4800 - a;
    int m = month + 12 * a - 3;

    return day + (153 * m + 2) / 5 + 365 * y + y / 4
         - y / 100 + y / 400 - 32045;
}

/*
 * Calculate the Gregorian March day on which a Jalali year begins.
 * leap_code == 0 identifies a leap year.
 */
static int gh_jalali_year_info(int jalali_year, GhJalaliYearInfo *info)
{
    const int break_count =
        (int)(sizeof(g_jalali_breaks) / sizeof(g_jalali_breaks[0]));
    int gregorian_year;
    int leap_j = -14;
    int jump = 0;
    int previous_break = g_jalali_breaks[0];
    int i;
    int n;
    int leap_g;
    int march;
    int leap;

    if (info == NULL ||
        jalali_year < g_jalali_breaks[0] ||
        jalali_year >= g_jalali_breaks[break_count - 1]) {
        return 0;
    }

    gregorian_year = jalali_year + 621;

    for (i = 1; i < break_count; ++i) {
        int current_break = g_jalali_breaks[i];

        jump = current_break - previous_break;
        if (jalali_year < current_break) {
            break;
        }

        leap_j += (jump / 33) * 8 + ((jump % 33) / 4);
        previous_break = current_break;
    }

    n = jalali_year - previous_break;
    leap_j += (n / 33) * 8 + (((n % 33) + 3) / 4);

    if ((jump % 33) == 4 && (jump - n) == 4) {
        ++leap_j;
    }

    leap_g = gregorian_year / 4
           - (((gregorian_year / 100) + 1) * 3) / 4
           - 150;

    march = 20 + leap_j - leap_g;

    if ((jump - n) < 6) {
        n = n - jump + ((jump + 4) / 33) * 33;
    }

    leap = (((n + 1) % 33) - 1) % 4;
    if (leap < 0) {
        leap += 4;
    }

    info->gregorian_year = gregorian_year;
    info->march_day = march;
    info->leap_code = leap;

    return 1;
}

static int gh_jalali_new_year_jdn(int jalali_year, int *jdn)
{
    GhJalaliYearInfo info;

    if (jdn == NULL || !gh_jalali_year_info(jalali_year, &info)) {
        return 0;
    }

    *jdn = gh_gregorian_jdn(info.gregorian_year, 3, info.march_day);
    return 1;
}

static int gh_valid_gregorian_date(int year, int month, int day)
{
    static const int month_lengths[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    int limit;

    if (month < 1 || month > 12 || day < 1) {
        return 0;
    }

    limit = month_lengths[month - 1];

    if (month == 2 &&
        ((year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0)))) {
        limit = 29;
    }

    return day <= limit;
}

static int gh_jalali_to_month_day(int day_of_year, int *month, int *day)
{
    if (month == NULL || day == NULL || day_of_year < 0) {
        return 0;
    }

    if (day_of_year <= 185) {
        *month = 1 + day_of_year / 31;
        *day = 1 + day_of_year % 31;
    } else {
        int remaining = day_of_year - 186;

        *month = 7 + remaining / 30;
        *day = 1 + remaining % 30;
    }

    return *month >= 1 && *month <= 12;
}

int gh_gregorian_to_imperial(int gregorian_year,
                             int gregorian_month,
                             int gregorian_day,
                             GhDate *result)
{
    int jalali_year;
    int new_year_jdn;
    int current_jdn;
    int day_of_year;
    int month;
    int day;

    if (result == NULL || gregorian_year < 1800 || gregorian_year > 2255 ||
        !gh_valid_gregorian_date(gregorian_year,
                                 gregorian_month,
                                 gregorian_day)) {
        return 0;
    }

    jalali_year = gregorian_year - 621;
    current_jdn = gh_gregorian_jdn(gregorian_year,
                                   gregorian_month,
                                   gregorian_day);

    if (!gh_jalali_new_year_jdn(jalali_year, &new_year_jdn)) {
        return 0;
    }

    if (current_jdn < new_year_jdn) {
        --jalali_year;

        if (!gh_jalali_new_year_jdn(jalali_year, &new_year_jdn)) {
            return 0;
        }
    }

    day_of_year = current_jdn - new_year_jdn;

    if (!gh_jalali_to_month_day(day_of_year, &month, &day)) {
        return 0;
    }

    result->year = jalali_year + GH_IMPERIAL_YEAR_OFFSET;
    result->month = month;
    result->day = day;

    return 1;
}

int gh_today(GhDate *result)
{
    time_t now;
    struct tm local_date;

    if (result == NULL) {
        return 0;
    }

    now = time(NULL);
    if (now == (time_t)-1) {
        return 0;
    }

#if defined(_WIN32)
    if (localtime_s(&local_date, &now) != 0) {
        return 0;
    }
#else
    if (localtime_r(&now, &local_date) == NULL) {
        return 0;
    }
#endif

    return gh_gregorian_to_imperial(local_date.tm_year + 1900,
                                    local_date.tm_mon + 1,
                                    local_date.tm_mday,
                                    result);
}


/* Keep public year arithmetic inside the algorithm's documented bounds. */
static int gh_imperial_year_supported(int imperial_year)
{
    return imperial_year >= GH_MIN_IMPERIAL_YEAR &&
           imperial_year <= GH_MAX_IMPERIAL_YEAR;
}

int gh_is_leap_year(int imperial_year)
{
    GhJalaliYearInfo info;

    if (!gh_imperial_year_supported(imperial_year) ||
        !gh_jalali_year_info(imperial_year - GH_IMPERIAL_YEAR_OFFSET, &info)) {
        return 0;
    }

    return info.leap_code == 0;
}

int gh_days_in_month(int imperial_year, int month)
{
    GhJalaliYearInfo info;

    if (month < 1 || month > 12 ||
        !gh_imperial_year_supported(imperial_year) ||
        !gh_jalali_year_info(imperial_year - GH_IMPERIAL_YEAR_OFFSET, &info)) {
        return 0;
    }

    if (month <= 6) {
        return 31;
    }

    if (month <= 11) {
        return 30;
    }

    return info.leap_code == 0 ? 30 : 29;
}

const char *gh_month_name(int month)
{
    if (month < 1 || month > 12) {
        return NULL;
    }

    return g_month_names[month - 1];
}
