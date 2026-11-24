#ifndef GAHSHOMAR_CALENDAR_H
#define GAHSHOMAR_CALENDAR_H

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
 */
int gh_is_leap_year(int imperial_year);

/*
 * Return the number of days in an Imperial Persian month (1-12).
 * Returns 0 for an invalid month or unsupported year.
 */
int gh_days_in_month(int imperial_year, int month);

/*
 * Return the Persian name of a month (UTF-8), or NULL for an invalid month.
 */
const char *gh_month_name(int month);

#ifdef __cplusplus
}
#endif

#endif
