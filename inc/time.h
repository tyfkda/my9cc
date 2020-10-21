#pragma once

#include "stddef.h"  // size_t

typedef long time_t;
typedef long clock_t;

struct tm {
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
};

#define CLOCKS_PER_SEC  (1000)

time_t time(time_t *timer);
clock_t clock(void);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
size_t strftime(char *s, size_t max, const char *format, const struct tm *tm);
time_t mktime(struct tm *tp);

#ifndef __NO_FLONUM
double difftime(time_t time2, time_t time1);
#endif
