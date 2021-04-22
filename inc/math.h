#pragma once

#ifndef __NO_FLONUM
#define M_PI      (3.14159265358979323846)
#define M_E       (2.718281828459045)
#define NAN       (0.0 / 0.0)
#define HUGE_VAL  (1.0 / 0.0)

double sin(double);
double cos(double);
double tan(double);
double atan(double);
double sqrt(double);
double log(double x);
double exp(double x);
double pow(double base, double x);
double fabs(double);
double floor(double);
double ceil(double x);
double fmod(double x, double m);
double frexp(double x, int *p);

int isfinite(double x);
int isnan(double x);
int isinf(double x);

double asin(double);
double acos(double);
double atan2(double, double);
double log2(double);
double log10(double);
#endif
