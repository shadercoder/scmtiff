// SCMTIFF Copyright (C) 2012-2015 Robert Kooima
//
// This program is free software: you can redistribute it and/or modify it
// under the terms of the GNU General Public License as published by the Free
// Software Foundation, either version 3 of the License, or (at your option)
// any later version.
//
// This program is distributed in the hope that it will be useful, but WITH-
// OUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
// FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
// more details.

#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <math.h>

#include "config.h"
#include "img.h"
#include "err.h"
#include "util.h"

//------------------------------------------------------------------------------

// Allocate, initialize, and return an image structure representing a pixel
// buffer with width w, height h, channel count c, bits-per-channel count b,
// and signedness g.

img *img_alloc(int w, int h, int c, int b, int g)
{
    size_t n = (size_t) w * (size_t) h * (size_t) c * (size_t) b / 8;
    img   *p = NULL;

    if ((p = (img *) calloc(1, sizeof (img))))
    {
        if ((p->p = malloc(n)))
        {
            p->project = img_default;
            p->n = n;
            p->w = w;
            p->h = h;
            p->c = c;
            p->b = b;
            p->g = g;

            p->minimum_latitude      = -0.5 * M_PI;
            p->maximum_latitude      =  0.5 * M_PI;
            p->westernmost_longitude =  0.0 * M_PI;
            p->easternmost_longitude =  2.0 * M_PI;

            p->norm0          = 0.f;
            p->norm1          = 1.f;
            p->scaling_factor = 1.f;
            p->offset         = 0.f;

            return p;
        }
        else apperr("Failed to allocate image buffer");
    }
    else apperr("Failed to allocate image structure");

    img_close(p);
    return NULL;
}

// Close the image file and release any mapped or allocated buffers.

void img_close(img *p)
{
    if (p)
    {
#ifndef _WIN32
        if (p->q)
            munmap(p->q, p->n);
        else
            free(p->p);

        if (p->d)
            close(p->d);
#else
        if (p->hFM)
            UnmapViewOfFile(p->p);
        else
            free(p->p);

        if (p->hF)
            CloseHandle(p->hF);
#endif
        free(p);
    }
}

// Calculate and return a pointer to scanline r of the given image. This is
// useful during image I/O.

void *img_scanline(img *p, int r)
{
    assert(p);
    return (char *) p->p + ((size_t) p->w *
                            (size_t) p->c *
                            (size_t) p->b *
                            (size_t) r / 8);
}

//------------------------------------------------------------------------------

static int16_t getint16(const img *p, const int16_t *s)
{
    int16_t d;

    const uint8_t *src = (const uint8_t *)  s;
          uint8_t *dst = (      uint8_t *) &d;

    if (p->o)
    {
        dst[0] = src[1];
        dst[1] = src[0];
    }
    else
    {
        dst[0] = src[0];
        dst[1] = src[1];
    }
    return d;
}

static uint16_t getuint16(const img *p, const uint16_t *s)
{
    uint16_t d;

    const uint8_t *src = (const uint8_t *)  s;
          uint8_t *dst = (      uint8_t *) &d;

    if (p->o)
    {
        dst[0] = src[1];
        dst[1] = src[0];
    }
    else
    {
        dst[0] = src[0];
        dst[1] = src[1];
    }
    return d;
}

static float getfloat(const img *p, const float *s)
{
    float d;

    const uint8_t *src = (const uint8_t *)  s;
          uint8_t *dst = (      uint8_t *) &d;

    if (p->o)
    {
        dst[0] = src[3];
        dst[1] = src[2];
        dst[2] = src[1];
        dst[3] = src[0];
    }
    else
    {
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
    }
    return d;
}

static int normu8(const img *p, uint8_t u, float *f)
{
    *f = ((float) u * p->scaling_factor + p->offset - p->norm0)
                                        / (p->norm1 - p->norm0);
    return 1;
}

static int norms8(const img *p, int8_t s, float *f)
{
    *f = ((float) s * p->scaling_factor + p->offset - p->norm0)
                                        / (p->norm1 - p->norm0);
    return 1;
}

static int normu16(const img *p, uint16_t u, float *f)
{
    *f = ((float) u * p->scaling_factor + p->offset - p->norm0)
                                        / (p->norm1 - p->norm0);
    return 1;
}

static int norms16(const img *p, int16_t s, float *f)
{
    int   d = 1;
    float k = 0;

    if      (s == -32768) d =      0;  // Null
    else if (s == -32767) k = -32768;  // Representation  saturation low
    else if (s == -32766) k = -32768;  // Instrumentation saturation low
    else if (s == -32764) k =  32767;  // Representation  saturation high
    else if (s == -32765) k =  32767;  // Instrumentation saturation high
    else                  k =      s;  // Good

    *f = (k * p->scaling_factor + p->offset - p->norm0)
                                / (p->norm1 - p->norm0);
    return d;
}

static int normf(const img *p, float e, float *f)
{
    const uint32_t *w = (const uint32_t *) &e;

    int   d = 1;
    float k = 0;

    if      (*w == 0xFF7FFFFB) d =   0;  // Null
    else if (*w == 0xFF7FFFFC) k = 0.f;  // Representation  saturation low
    else if (*w == 0xFF7FFFFD) k = 0.f;  // Instrumentation saturation low
    else if (*w == 0xFF7FFFFE) k = 1.f;  // Representation  saturation high
    else if (*w == 0xFF7FFFFF) k = 1.f;  // Instrumentation saturation high
    else if (isnormal(e))      k =   e;  // Good
    else                       d =   0;  // Punt

    *f = (k * p->scaling_factor + p->offset - p->norm0)
                                / (p->norm1 - p->norm0);
    return d;
}

static int getchan(const img *p, int i, int j, int k, float *f)
{
    const size_t s = ((size_t) p->w * i + j) * ((size_t) p->c) + k;

    if (p->b == 32)
    {
        return normf(p, getfloat(p, (const float *) p->p + s), f);
    }
    else if (p->b == 16)
    {
        if (p->g)
            return norms16(p,  getint16(p, (const int16_t  *) p->p + s), f);
        else
            return normu16(p, getuint16(p, (const uint16_t *) p->p + s), f);
    }
    else if (p->b ==  8)
    {
        if (p->g)
            return norms8(p, ((const  int8_t *) p->p)[s], f);
        else
            return normu8(p, ((const uint8_t *) p->p)[s], f);
    }
    return 0;
}

//------------------------------------------------------------------------------

int img_pixel(img *p, int i, int j, float *c)
{
    int d = 0;

    if (0 <= i && i < p->h && 0 <= j && j < p->w)
    {
        switch (p->c)
        {
            case 4: d |= getchan(p, i, j, 3, c + 3);
            case 3: d |= getchan(p, i, j, 2, c + 2);
            case 2: d |= getchan(p, i, j, 1, c + 1);
            case 1: d |= getchan(p, i, j, 0, c + 0);
        }
    }
    return d;
}

// Perform a linearly-filtered sampling of the image p. The filter position
// is smoothly-varying in the range [0, w), [0, h).

static int img_linear(img *p, const double *v, float *c)
{
    double s = v[0] - 0.5;
    double t = v[1] - 0.5;

    const int ia = (int) floor(s);
    const int ib = (int)  ceil(s);
    const int ja = (int) floor(t);
    const int jb = (int)  ceil(t);

    float aa[4], ab[4];
    float ba[4], bb[4];

    int daa = img_pixel(p, ia, ja, aa);
    int dab = img_pixel(p, ia, jb, ab);
    int dba = img_pixel(p, ib, ja, ba);
    int dbb = img_pixel(p, ib, jb, bb);

    if (daa && dab && dba && dbb)
    {
        const float u = (float) (s - floor(s));
        const float v = (float) (t - floor(t));

        switch (p->c)
        {
            case 4: c[3] = lerp2(aa[3], ab[3], ba[3], bb[3], u, v);
            case 3: c[2] = lerp2(aa[2], ab[2], ba[2], bb[2], u, v);
            case 2: c[1] = lerp2(aa[1], ab[1], ba[1], bb[1], u, v);
            case 1: c[0] = lerp2(aa[0], ab[0], ba[0], bb[0], u, v);
        }
    }
    else if (daa)
    {
        switch (p->c)
        {
            case 4: c[3] = aa[3];
            case 3: c[2] = aa[2];
            case 2: c[1] = aa[1];
            case 1: c[0] = aa[0];
        }
    }
    else if (dab)
    {
        switch (p->c)
        {
            case 4: c[3] = ab[3];
            case 3: c[2] = ab[2];
            case 2: c[1] = ab[1];
            case 1: c[0] = ab[0];
        }
    }
    else if (dba)
    {
        switch (p->c)
        {
            case 4: c[3] = ba[3];
            case 3: c[2] = ba[2];
            case 2: c[1] = ba[1];
            case 1: c[0] = ba[0];
        }
    }
    else if (dbb)
    {
        switch (p->c)
        {
            case 4: c[3] = bb[3];
            case 3: c[2] = bb[2];
            case 2: c[1] = bb[1];
            case 1: c[0] = bb[0];
        }
    }
    return (daa || dab || dba || dbb) ? 1 : 0;
}

//------------------------------------------------------------------------------

static double todeg(double r)
{
    return r * 180.0 / M_PI;
}

static inline double tolon(double a)
{
    double b = fmod(a, 2.0 * M_PI);
    return b < 0 ? b + 2.0 * M_PI : b;
}

//------------------------------------------------------------------------------

int img_equirectangular(img *p, const double *v, double lon, double lat, double *t)
{
    double x = p->a_axis_radius * (lon - p->center_longitude) * cos(p->center_latitude);
    double y = p->a_axis_radius * (lat);

    t[0] =   p->line_projection_offset - y / p->map_scale;
    t[1] = p->sample_projection_offset + x / p->map_scale;

    return 1;
}

int img_orthographic(img *p, const double *v, double lon, double lat, double *t)
{
    if (p->westernmost_longitude <= lon && lon <= p->easternmost_longitude &&
             p->minimum_latitude <= lat && lat <=      p->maximum_latitude)
    {
        double x = p->a_axis_radius * cos(lat - p->center_latitude) * sin(lon - p->center_longitude);
        double y = p->a_axis_radius * sin(lat - p->center_latitude);

        t[0] =   p->line_projection_offset - y / p->map_scale;
        t[1] = p->sample_projection_offset + x / p->map_scale;

        return 1;
    }
    return 0;
}

int img_polar_stereographic(img *p, const double *v, double lon, double lat, double *t)
{
    double x;
    double y;

    if (p->center_latitude > 0)
    {
        x =  2 * p->a_axis_radius * tan((M_PI / 4.0) - lat / 2) * sin(lon - p->center_longitude);
        y = -2 * p->a_axis_radius * tan((M_PI / 4.0) - lat / 2) * cos(lon - p->center_longitude);
    }
    else
    {
        x =  2 * p->a_axis_radius * tan((M_PI / 4.0) + lat / 2) * sin(lon - p->center_longitude);
        y =  2 * p->a_axis_radius * tan((M_PI / 4.0) + lat / 2) * cos(lon - p->center_longitude);
    }

#if 0
    t[0] =   p->line_projection_offset - y / p->map_scale;
    t[1] = p->sample_projection_offset + x / p->map_scale;
#else
    t[0] = (p->h / 2.0 - 0.5) - y / p->map_scale - 1; // FIRST_PIXEL
    t[1] = (p->h / 2.0 - 0.5) + x / p->map_scale - 1; // FIRST_PIXEL
#endif
    return 1;
}

int img_simple_cylindrical(img *p, const double *v, double lon, double lat, double *t)
{
#if 0
    lon = tolon(lon - M_PI);
#endif

    t[0] =   p->line_projection_offset - p->map_resolution * (todeg(lat) - todeg( p->center_latitude)) - 1; // FIRST_PIXEL
    t[1] = p->sample_projection_offset + p->map_resolution * (todeg(lon) - todeg(p->center_longitude)) - 1; // FIRST_PIXEL

    return 1;
}

int img_default(img *p, const double *v, double lon, double lat, double *t)
{
    t[0] = p->h *      (lat      - p->minimum_latitude) / (     p->maximum_latitude -      p->minimum_latitude);
    t[1] = p->w * tolon(lon - p->westernmost_longitude) / (p->easternmost_longitude - p->westernmost_longitude);

    return 1;
}

// Panoramas are spheres viewed from the inside while planets are spheres
// viewed from the outside. This difference reverses the handedness of the
// coordinate system. The default projection is "inside" and is applied to
// panorama images, which do not contain projection specifications. All other
// projections are "outside" and are applied to planets, which are usually
// aquired in PDS format, which does contain a projection specification. This is
// the means by which the handedness of the coordinate system is inferred. Be
// advised that this will fail in obscure cases.

//------------------------------------------------------------------------------

static double blend(double a, double b, double k)
{
    if (a < b)
    {
        if (k < a) return 1.f;
        if (b < k) return 0.f;

        double t = 1.f - (k - a) / (b - a);

        return 3 * t * t - 2 * t * t * t;
    }
    else
    {
        if (k > a) return 1.f;
        if (b > k) return 0.f;

        double t = 1.f - (a - k) / (a - b);

        return 3 * t * t - 2 * t * t * t;
    }
}

static double angle(double a, double b)
{
    double d;

    if (a > b)
    {
        if ((d = a - b) < M_PI)
            return d;
        else
            return 2 * M_PI - d;
    }
    else
    {
        if ((d = b - a) < M_PI)
            return d;
        else
            return 2 * M_PI - d;
    }
}

int img_sample(img *p, const double *v, float *c)
{
    const double lon = tolon(atan2(v[0], v[2])), lat = asin(v[1]);

    float klat = 1.f;
    float klon = 1.f;

    if (p->latc || p->lat0 || p->lat1)
        klat = (float) blend(p->lat0, p->lat1, angle(lat, p->latc));
    if (p->lonc || p->lon0 || p->lon1)
        klon = (float) blend(p->lon0, p->lon1, angle(lon, p->lonc));

    float k;
    int   h = 0;

    if ((k = klat * klon))
    {
        double t[2];

        if (p->project(p, v, lon, lat, t))
        {
            if ((h = img_linear(p, t, c)))
            {
                switch (p->c)
                {
                    case 4: c[3] *= k;
                    case 3: c[2] *= k;
                    case 2: c[1] *= k;
                    case 1: c[0] *= k;
                }
            }
        }
    }
    return h;
}

int img_locate(img *p, const double *v)
{
    const double lon = tolon(atan2(v[0], v[2])), lat = asin(v[1]);

    double t[2];

    if (p->project(p, v, lon, lat, t))
    {
        if (0 <= t[0] && t[0] < p->h && 0 <= t[1] && t[1] < p->w)
        {
            return 1;
        }
        return 0;
    }
    return 0;
}

//------------------------------------------------------------------------------
