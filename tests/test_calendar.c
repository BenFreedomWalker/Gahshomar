#include "gahshomar/calendar.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void fail(const char *message)
{
    if (failures < 10)
        fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

static void check_date(int gy, int gm, int gd,
                       int expected_year, int expected_month, int expected_day)
{
    GhDate actual;

    if (!gh_gregorian_to_imperial(gy, gm, gd, &actual)) {
        fprintf(stderr, "FAIL: conversion rejected %04d-%02d-%02d\n",
                gy, gm, gd);
        ++failures;
        return;
    }

    if (actual.year != expected_year ||
        actual.month != expected_month ||
        actual.day != expected_day) {
        fprintf(stderr,
                "FAIL: %04d-%02d-%02d -> %d/%d/%d; expected %d/%d/%d\n",
                gy, gm, gd,
                actual.year, actual.month, actual.day,
                expected_year, expected_month, expected_day);
        ++failures;
    }
}

static int gregorian_month_length(int year, int month)
{
    static const int lengths[12] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };

    if (month == 2 &&
        ((year % 400 == 0) || ((year % 4 == 0) && (year % 100 != 0))))
        return 29;

    return lengths[month - 1];
}

/*
 * Walk every Gregorian day in the supported conversion range. Each result
 * must be a valid Imperial date and must immediately follow the prior date.
 * This catches off-by-one errors at every month and year boundary.
 */
static void check_full_range_continuity(void)
{
    GhDate previous = {0, 0, 0};
    int have_previous = 0;
    int year, month, day;
    unsigned long checked = 0;

    for (year = 1800; year <= 2255; ++year) {
        for (month = 1; month <= 12; ++month) {
            int limit = gregorian_month_length(year, month);

            for (day = 1; day <= limit; ++day) {
                GhDate current;
                int expected_year = previous.year;
                int expected_month = previous.month;
                int expected_day = previous.day;

                if (!gh_gregorian_to_imperial(year, month, day, &current)) {
                    fprintf(stderr,
                            "FAIL: range conversion rejected %04d-%02d-%02d\n",
                            year, month, day);
                    ++failures;
                    return;
                }

                if (current.month < 1 || current.month > 12 ||
                    current.day < 1 ||
                    current.day > gh_days_in_month(current.year, current.month)) {
                    fprintf(stderr,
                            "FAIL: invalid Imperial date for %04d-%02d-%02d: %d/%d/%d\n",
                            year, month, day,
                            current.year, current.month, current.day);
                    ++failures;
                }

                if (have_previous) {
                    int month_length =
                        gh_days_in_month(expected_year, expected_month);

                    if (expected_day < month_length) {
                        ++expected_day;
                    } else {
                        expected_day = 1;
                        if (++expected_month == 13) {
                            expected_month = 1;
                            ++expected_year;
                        }
                    }

                    if (current.year != expected_year ||
                        current.month != expected_month ||
                        current.day != expected_day) {
                        fprintf(stderr,
                                "FAIL: discontinuity at %04d-%02d-%02d; "
                                "got %d/%d/%d, expected %d/%d/%d\n",
                                year, month, day,
                                current.year, current.month, current.day,
                                expected_year, expected_month, expected_day);
                        ++failures;
                        return;
                    }
                }

                previous = current;
                have_previous = 1;
                ++checked;
            }
        }
    }

    if (checked != 166551UL) {
        fprintf(stderr, "FAIL: checked unexpected number of dates: %lu\n",
                checked);
        ++failures;
    }
}

int main(void)
{
    /* Independent Borkowski/jalaali-js reference vectors. */
    check_date(2011, 3, 21, 2570, 1, 1);
    check_date(2012, 3, 20, 2571, 1, 1);
    check_date(2016, 3, 20, 2575, 1, 1);
    check_date(2016, 4, 11, 2575, 1, 23);
    check_date(2021, 7, 21, 2580, 4, 30);

    /* Existing application dates and Nowruz/Esfand boundaries. */
    check_date(2024, 3, 20, 2583, 1, 1);
    check_date(2025, 3, 20, 2583, 12, 30);
    check_date(2025, 3, 21, 2584, 1, 1);
    check_date(2026, 3, 21, 2585, 1, 1);
    check_date(2026, 11, 24, 2585, 9, 3);

    if (!gh_gregorian_to_imperial(1800, 1, 1, &(GhDate){0}) ||
        !gh_gregorian_to_imperial(2255, 12, 31, &(GhDate){0}) ||
        gh_gregorian_to_imperial(1799, 12, 31, &(GhDate){0}) ||
        gh_gregorian_to_imperial(2256, 1, 1, &(GhDate){0})) {
        fail("Gregorian conversion range is not enforced correctly");
    }

    if (gh_days_in_month(2585, 1) != 31 ||
        gh_days_in_month(2585, 6) != 31 ||
        gh_days_in_month(2585, 7) != 30 ||
        gh_days_in_month(2585, 11) != 30 ||
        gh_days_in_month(2583, 12) != 30 ||
        gh_days_in_month(2584, 12) != 29 ||
        gh_days_in_month(2575, 12) != 30 ||
        gh_days_in_month(2574, 12) != 29 ||
        gh_days_in_month(9000, 1) != 0) {
        fail("month lengths are incorrect");
    }

    if (!gh_is_leap_year(2575) || gh_is_leap_year(2574) ||
        !gh_is_leap_year(2583) || gh_is_leap_year(2584)) {
        fail("leap-year calculation is incorrect");
    }

    if (gh_is_leap_year(GH_MIN_IMPERIAL_YEAR - 1) ||
        gh_is_leap_year(GH_MAX_IMPERIAL_YEAR + 1) ||
        gh_is_leap_year(INT_MIN) || gh_is_leap_year(INT_MAX) ||
        gh_days_in_month(INT_MIN, 1) != 0 ||
        gh_days_in_month(INT_MAX, 1) != 0 ||
        gh_days_in_month(GH_MIN_IMPERIAL_YEAR - 1, 1) != 0 ||
        gh_days_in_month(GH_MAX_IMPERIAL_YEAR + 1, 1) != 0) {
        fail("unsupported Imperial years were not rejected safely");
    }

    if (gh_month_name(9) == NULL ||
        strcmp(gh_month_name(9), "آذر") != 0 ||
        gh_month_name(0) != NULL ||
        gh_month_name(13) != NULL) {
        fail("Persian month names or invalid-month handling are incorrect");
    }

    if (gh_gregorian_to_imperial(2026, 2, 30, &(GhDate){0}) != 0 ||
        gh_gregorian_to_imperial(2026, 0, 10, &(GhDate){0}) != 0 ||
        gh_gregorian_to_imperial(2026, 13, 10, &(GhDate){0}) != 0 ||
        gh_gregorian_to_imperial(2026, 1, 0, &(GhDate){0}) != 0 ||
        gh_gregorian_to_imperial(2026, 1, 1, NULL) != 0) {
        fail("invalid Gregorian input was accepted");
    }

    check_full_range_continuity();

    if (failures != 0) {
        fprintf(stderr, "%d calendar test(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    puts("All calendar tests passed (166,551 Gregorian dates checked).");
    return EXIT_SUCCESS;
}
