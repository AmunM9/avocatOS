/*
 * Sun and Moon for the Órbita face.
 * Sunrise/sunset: the "Almanac for Computers" algorithm (US Naval
 * Observatory, 1990), accurate to a couple of minutes between the polar
 * circles. Moon: the mean synodic month from a known new moon; the real
 * phase can drift up to ~14 h from it, which is invisible on a watch.
 */
#include <math.h>
#include "avo_core.h"

#define PI 3.14159265358979323846
#define DEG (PI / 180.0)
#define ZENITH 90.833                   /* sun centre + refraction + radius */
#define NEW_MOON_EPOCH 947182440LL      /* 2000-01-06 18:14 UTC            */

static double norm360(double x)
{
    x = fmod(x, 360.0);
    return x < 0 ? x + 360.0 : x;
}

static int day_of_year(int y, int m, int d)
{
    static const int cum[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return cum[m - 1] + d + (leap && m > 2 ? 1 : 0);
}

/* Local time (hours) of sunrise (rising) or sunset; NAN when it never happens. */
static double sun_event(double lat, double lon, int n, bool rising, double offset_h, int *polar)
{
    double lng_hour = lon / 15.0;
    double t = n + ((rising ? 6.0 : 18.0) - lng_hour) / 24.0;
    double m = 0.9856 * t - 3.289;
    double l = norm360(m + 1.916 * sin(m * DEG) + 0.020 * sin(2 * m * DEG) + 282.634);
    double ra = norm360(atan(0.91764 * tan(l * DEG)) / DEG);
    ra += floor(l / 90.0) * 90.0 - floor(ra / 90.0) * 90.0; /* same quadrant as L */
    ra /= 15.0;
    double sin_dec = 0.39782 * sin(l * DEG);
    double cos_dec = cos(asin(sin_dec));
    double cos_h = (cos(ZENITH * DEG) - sin_dec * sin(lat * DEG)) / (cos_dec * cos(lat * DEG));
    if (cos_h > 1) {
        *polar = -1; /* never rises */
        return NAN;
    }
    if (cos_h < -1) {
        *polar = 1; /* never sets */
        return NAN;
    }
    double h = (rising ? 360.0 - acos(cos_h) / DEG : acos(cos_h) / DEG) / 15.0;
    double local_t = h + ra - 0.06571 * t - 6.622;
    return fmod(fmod(local_t - lng_hour + offset_h, 24.0) + 24.0, 24.0);
}

bool avo_sun_times(double lat, double lon, int year, int month, int day, int16_t utc_offset_min, avo_sun_t *out)
{
    int n = day_of_year(year, month, day), polar = 0;
    double off = utc_offset_min / 60.0;
    double rise = sun_event(lat, lon, n, true, off, &polar);
    double set = sun_event(lat, lon, n, false, off, &polar);
    *out = (avo_sun_t){ .polar_day = polar > 0, .polar_night = polar < 0 };
    if (isnan(rise) || isnan(set)) {
        return false;
    }
    out->rise = (int16_t)lround(rise * 60.0);
    out->set = (int16_t)lround(set * 60.0);
    return true;
}

double avo_sun_path_angle(const avo_sun_t *s, int minute)
{
    if (s->polar_day) return 180.0 + 180.0 * minute / 1440.0;
    if (s->polar_night) return 180.0 * minute / 1440.0;
    int day_len = s->set - s->rise;
    if (minute >= s->rise && minute <= s->set && day_len > 0) {
        return 180.0 + 180.0 * (minute - s->rise) / day_len;
    }
    int night_len = 1440 - day_len;
    int since_set = (minute - s->set + 1440) % 1440;
    return 180.0 * since_set / night_len;
}

double avo_moon_age(int64_t utc_seconds)
{
    double days = (double)(utc_seconds - NEW_MOON_EPOCH) / 86400.0;
    double age = fmod(days, AVO_MOON_SYNODIC);
    return age < 0 ? age + AVO_MOON_SYNODIC : age;
}

double avo_moon_illumination(double age)
{
    return (1.0 - cos(2 * PI * age / AVO_MOON_SYNODIC)) / 2.0;
}

bool avo_moon_waxing(double age)
{
    return age < AVO_MOON_SYNODIC / 2;
}

const char *avo_moon_phase_name(double age)
{
    static const char *const NAMES[8] = {
        "Luna nueva", "Creciente", "Cuarto creciente", "Gibosa creciente",
        "Luna llena", "Gibosa menguante", "Cuarto menguante", "Menguante",
    };
    int i = (int)floor(age / AVO_MOON_SYNODIC * 8.0 + 0.5) % 8; /* centred eighths */
    return NAMES[i];
}
