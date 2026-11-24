#ifndef GAHSHOMAR_CALENDAR_H
#define GAHSHOMAR_CALENDAR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Gahshomar calendar core.
 *
 * The Imperial year numbering used here is the Solar Hijri year plus 1180.
 * Calendar calculations are independent of the operating system and UI.
 */

#define GH_IMPERIAL_YEAR_OFFSET 1180

/* Borkowski break-year algorithm limits: Solar Hijri -61 through 3177. */
#define GH_MIN_IMPERIAL_YEAR (GH_IMPERIAL_YEAR_OFFSET - 61)
#define GH_MAX_IMPERIAL_YEAR (GH_IMPERIAL_YEAR_OFFSET + 3177)

typedef struct GhDate {
    int year;
    int month;
    int day;
} GhDate;

/*
 * Convert a Gregorian civil date to the Imperial Persian calendar.
 *
 * Returns 1 on success and 0 if the Gregorian date is invalid or outside
 * the supported arithmetic-calendar range.
 */
int gh_gregorian_to_imperial(int gregorian_year,
                             int gregorian_month,
                             int gregorian_day,
                             GhDate *result);

/*
 * Read the computer's local civil date and convert it to the Imperial
 * Persian calendar. The system time zone and clock are used; no network
 * service is contacted.
 *
 * Returns 1 on success and 0 if the system date cannot be read or converted.
 */
int gh_today(GhDate *result);

/*
 * Return nonzero when the supplied Imperial Persian year is a leap year.
 * Return 0 when the year is outside GH_MIN_IMPERIAL_YEAR..GH_MAX_IMPERIAL_YEAR.
 */
int gh_is_leap_year(int imperial_year);

/*
 * Return the number of days in an Imperial Persian month (1-12).
 * Returns 0 for an invalid month or a year outside the supported range.
 */
int gh_days_in_month(int imperial_year, int month);

/*
 * Return the Persian name of a month (UTF-8), or NULL for an invalid month.
 */
const char *gh_month_name(int month);

/* Format a signed integer using Persian digits (۰۱۲۳۴۵۶۷۸۹) as UTF-8.
 * Returns 1 on success; returns 0 for a NULL/undersized output buffer.
 */
int gh_format_number(int value, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
