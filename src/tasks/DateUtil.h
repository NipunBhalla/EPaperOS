#pragma once

#include <string>
#include <cstdio>
#include <ctime>

// Small ISO "YYYY-MM-DD" date helpers shared by the add wizard and the calendar
// filter. Header-only so the host test env can pull it in without extra build
// wiring.
namespace dateutil
{
  inline int days_in_month(int y, int mo)
  {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12)
    {
      return 31;
    }
    if (mo == 2)
    {
      bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
      return leap ? 29 : 28;
    }
    return d[mo - 1];
  }

  inline bool parse(const std::string &iso, int &y, int &mo, int &d)
  {
    return std::sscanf(iso.c_str(), "%d-%d-%d", &y, &mo, &d) == 3;
  }

  inline std::string format(int y, int mo, int d)
  {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, mo, d);
    return std::string(buf);
  }

  // `iso` shifted by `days` (may be negative). Returns `iso` unchanged if it
  // does not parse.
  inline std::string plus(const std::string &iso, int days)
  {
    int y, mo, d;
    if (!parse(iso, y, mo, d))
    {
      return iso;
    }
    struct tm t = {};
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = 12; // noon avoids DST/midnight edge cases
    time_t base = mktime(&t);
    if (base == (time_t)-1)
    {
      return iso;
    }
    base += (time_t)days * 86400;
    struct tm *o = localtime(&base);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", o);
    return std::string(buf);
  }
}
