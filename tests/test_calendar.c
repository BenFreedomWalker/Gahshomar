#include "gahshomar/calendar.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

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

int main(void)
{
    check_date(2026, 11, 24, 2585, 9, 3);
    check_date(2026, 3, 21, 2585, 1, 1);
    check_date(2025, 3, 20, 2583, 12, 30);
    check_date(2025, 3, 21, 2584, 1, 1);
    check_date(2024, 3, 20, 2583, 1, 1);

    if (gh_days_in_month(2585, 1) != 31 ||
        gh_days_in_month(2585, 6) != 31 ||
        gh_days_in_month(2585, 7) != 30 ||
        gh_days_in_month(2585, 11) != 30 ||
        gh_days_in_month(2583, 12) != 30 ||
        gh_days_in_month(2584, 12) != 29 ||
        gh_days_in_month(9000, 1) != 0) {
        fprintf(stderr, "FAIL: month lengths are incorrect\n");
        ++failures;
    }

    if (!gh_is_leap_year(2583) || gh_is_leap_year(2584)) {
        fprintf(stderr, "FAIL: leap-year calculation is incorrect\\n");
        ++failures;
    }

    if (gh_month_name(9) == NULL ||
        strcmp(gh_month_name(9), "آذر") != 0) {
        fprintf(stderr, "FAIL: Persian month name is incorrect\n");
        ++failures;
    }

    if (gh_gregorian_to_imperial(2026, 2, 30, &(GhDate){0}) != 0) {
        fprintf(stderr, "FAIL: invalid Gregorian date was accepted\n");
        ++failures;
    }

    if (failures != 0) {
        fprintf(stderr, "%d calendar test(s) failed\n", failures);
        return EXIT_FAILURE;
    }

    puts("All calendar tests passed.");
    return EXIT_SUCCESS;
}
