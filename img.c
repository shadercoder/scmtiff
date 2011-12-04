// Copyright (c) 2011 Robert Kooima.  All Rights Reverved.

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#include "img.h"
#include "err.h"

//------------------------------------------------------------------------------

static void get8u(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        unsigned char *q = (unsigned char *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 255.0;
            case 3: c[2] = q[2] / 255.0;
            case 2: c[1] = q[1] / 255.0;
            case 1: c[0] = q[0] / 255.0;
        }
    }
    else memset(c, 0, p->c * sizeof (double));
}

static void get8s(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        char *q = (char *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 127.0;
            case 3: c[2] = q[2] / 127.0;
            case 2: c[1] = q[1] / 127.0;
            case 1: c[0] = q[0] / 127.0;
        }
    }
    else memset(c, 0, p->c * sizeof (double));
}

static void get16u(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        unsigned short *q = (unsigned short *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 65535.0;
            case 3: c[2] = q[2] / 65535.0;
            case 2: c[1] = q[1] / 65535.0;
            case 1: c[0] = q[0] / 65535.0;
        }
    }
    else memset(c, 0, p->c * sizeof (double));
}

static void get16s(img *p, int i, int j, double *c)
{
    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        short *q = (short *) p->p + p->c * (p->w * i + j);

        switch (p->c)
        {
            case 4: c[3] = q[3] / 32767.0;
            case 3: c[2] = q[2] / 32767.0;
            case 2: c[1] = q[1] / 32767.0;
            case 1: c[0] = q[0] / 32767.0;
        }
    }
    else memset(c, 0, p->c * sizeof (double));
}

//------------------------------------------------------------------------------

img *img_alloc(int w, int h, int c, int b, int s)
{
    img *p = NULL;

    if ((p = (img *) calloc(1, sizeof (img))))
    {
        if ((p->p = calloc(w * h, c * b)))
        {
            p->w = w;
            p->h = h;
            p->c = c;
            p->b = b;
            p->s = s;

            if      (b ==  8 && s == 0) p->get = get8u;
            else if (b ==  8 && s == 1) p->get = get8s;
            else if (b == 16 && s == 0) p->get = get16u;
            else if (b == 16 && s == 1) p->get = get16s;

            return p;
        }
        else apperr("Failed to allocate image buffer");
    }
    else apperr("Failed to allocate image structure");

    img_close(p);
    return NULL;
}

void img_close(img *p)
{
    if (p)
    {
        free(p->p);
        free(p);
    }
}

void *img_scanline(img *p, int r)
{
    assert(p);
    return (char *) p->p + p->w * p->c * p->b * r / 8;
}

//------------------------------------------------------------------------------

static double lerp(double a, double b, double t)
{
    return b * t + a * (1 - t);
}

void img_linear(img *p, double i, double j, double *c)
{
    const double s = i - floor(i);
    const double t = j - floor(j);

    const int ia = (int) floor(i);
    const int ib = (int)  ceil(i);
    const int ja = (int) floor(j);
    const int jb = (int)  ceil(j);

    double aa[3];
    double ab[3];
    double ba[3];
    double bb[3];

    p->get(p, ia, ja, aa);
    p->get(p, ia, jb, ab);
    p->get(p, ib, ja, ba);
    p->get(p, ib, jb, bb);

    switch (p->c)
    {
        case 4: c[3] = lerp(lerp(aa[3], ab[3], t), lerp(ba[3], bb[3], t), s);
        case 3: c[2] = lerp(lerp(aa[2], ab[2], t), lerp(ba[2], bb[2], t), s);
        case 2: c[1] = lerp(lerp(aa[1], ab[1], t), lerp(ba[1], bb[1], t), s);
        case 1: c[0] = lerp(lerp(aa[0], ab[0], t), lerp(ba[0], bb[0], t), s);
    }
}

//------------------------------------------------------------------------------

void img_sample_spheremap(img *p, const double *v, double *c)
{
    const double lon = atan2(v[0], -v[2]), lat = asin(v[1]);

    const double j = (p->w    ) * 0.5 * (M_PI   + lon) / M_PI;
    const double i = (p->h - 1) * 0.5 * (M_PI_2 - lat) / M_PI_2;

   img_linear(p, i, j, c);

    // c[0] = 0.5 * (M_PI   + lon) / M_PI;
    // c[1] = 0.5 * (M_PI_2 - lat) / M_PI_2;
    // c[2] = 0.0;
}

void img_sample_test(img *p, const double *v, double *c)
{
    switch (p->c)
    {
        case 4: c[3] =                1.0;
        case 3: c[2] = (v[2] + 1.0) / 2.0;
        case 2: c[1] = (v[1] + 1.0) / 2.0;
        case 1: c[0] = (v[0] + 1.0) / 2.0;
    }
}

//------------------------------------------------------------------------------
