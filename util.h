// Copyright (c) 2011 Robert Kooima.  All Rights Reserved.

#ifndef SCMTIFF_UTIL_H
#define SCMTIFF_UTIL_H

//------------------------------------------------------------------------------

#define MIN(A, B) (((A) < (B)) ? (A) : (B))
#define MAX(A, B) (((A) > (B)) ? (A) : (B))

void  normalize(double *);

void  mid2(double *, const double *, const double *);
void  mid4(double *, const double *, const double *,
                    const double *, const double *);

float lerp1(float, float, float);
float lerp2(float, float, float, float, float, float);
/*
void  slerp1(double *, const double *, const double *, double);
void  slerp2(double *, const double *, const double *,
                      const double *, const double *, double, double);
*/
int extcmp(const char *, const char *);

//------------------------------------------------------------------------------

#endif
