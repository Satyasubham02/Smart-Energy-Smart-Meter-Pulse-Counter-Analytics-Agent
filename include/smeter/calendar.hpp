#pragma once
// Local-calendar helpers shared by the usage tracker, load model and agent.
#include <array>
#include <cmath>
#include <ctime>
#include <string>

namespace sm {

using Hours = std::array<double, 24>;  // kWh per local hour of one day

struct LocalTime {
    long day = 0;          // local civil day number (days since 1970-01-01, local date)
    int year = 1970, mon = 0 /*0-11*/, mday = 1, hour = 0, min = 0;
    double hour_f = 0;     // hour of day as a fraction, e.g. 17.5 = 5:30 PM
};

inline LocalTime localTime(double wall_s) {
    std::time_t t = static_cast<std::time_t>(std::floor(wall_s));
    std::tm tm{};
    localtime_r(&t, &tm);
    LocalTime L;
    L.day = static_cast<long>(std::floor((wall_s + tm.tm_gmtoff) / 86400.0));
    L.year = tm.tm_year + 1900; L.mon = tm.tm_mon; L.mday = tm.tm_mday;
    L.hour = tm.tm_hour; L.min = tm.tm_min;
    L.hour_f = tm.tm_hour + tm.tm_min / 60.0 + tm.tm_sec / 3600.0;
    return L;
}

inline int daysInMonth(int year, int mon0) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return (mon0 == 1 && leap) ? 29 : d[mon0];
}

/// Month identifier (year*12+month) of a local civil day number.
inline long monthKeyOfDay(long day) {
    std::time_t t = static_cast<std::time_t>(day) * 86400;
    std::tm tm{};
    gmtime_r(&t, &tm);  // a "day number" is already a civil date, so gmtime gives its y/m/d
    return (tm.tm_year + 1900) * 12L + tm.tm_mon;
}

inline std::string hourLabel(int h) {
    h = ((h % 24) + 24) % 24;
    int h12 = (h % 12 == 0) ? 12 : h % 12;
    return std::to_string(h12) + (h < 12 ? " AM" : " PM");
}

}  // namespace sm
