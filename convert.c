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

#include <stdbool.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>

#include "scm.h"
#include "scmdef.h"
#include "img.h"
#include "err.h"
#include "util.h"
#include "process.h"

//------------------------------------------------------------------------------

#define NTAPS 1024
#define MTAPS (NTAPS * NTAPS)

static int tap[MTAPS];

static void init_tap(int a, int d, int n, int *x)
{
    if (n == 1)
        *x = a;
    else
    {
        init_tap(a,     d * 2, n / 2, x        );
        init_tap(a + d, d * 2, n / 2, x + n / 2);
    }
}

// Determine whether the image intersects with the page at row u column v of the
// w-by-w page array on face f. This function is inefficient and only works
// under limited circumstances.

static bool overlap(img *p, int f, long u, long v, long w)
{
    for (int t = 0; t < MTAPS; ++t)
    {
        int i = tap[t] / NTAPS;
        int j = tap[t] % NTAPS;

        double c[3];

        scm_get_sample_center(f, NTAPS * u + i, NTAPS * v + j, NTAPS * w, c);

        if (img_locate(p, c))
            return true;
    }
    return false;
}

// Given the four corner vectors of a sample, compute the five internal vectors
// of a quincunx filtering of that sample.

static void quincunx(double *q, const double *v)
{
    mid4(q + 12, v +  0, v +  3, v +  6, v +  9);
    mid2(q +  9, q + 12, v +  9);
    mid2(q +  6, q + 12, v +  6);
    mid2(q +  3, q + 12, v +  3);
    mid2(q +  0, q + 12, v +  0);
}

// Compute the corner vectors of the pixel at row i column j of the n-by-n
// page found at row u column v of the w-by-w page array on face f. Sample
// that pixel by projection into image p using a quincunx filtering pattern.

static int multisample(img *p, int  f, int  i, int  j, int n,
                                 long u, long v, long w, float *d)
{
    double c[12];
    double C[15];
    int    N = 0;

    scm_get_sample_corners(f, n * u + i, n * v + j, n * w, c);

    quincunx(C, c);

    for (int l = 0; l < 5; l++)
    {
        float t[4];

        if (img_sample(p, C + l * 3, t))
        {
            switch (p->c)
            {
                case 4: d[3] += t[3];
                case 3: d[2] += t[2];
                case 2: d[1] += t[1];
                case 1: d[0] += t[0];
            }
            N += 1;
        }
    }
    if (N)
    {
        switch (p->c)
        {
            case 4: d[3] /= N;
            case 3: d[2] /= N;
            case 2: d[1] /= N;
            case 1: d[0] /= N;
        }
    }
    return N;
}

// Determine the value of the pixel at row i column j of the page found at row
// u column v of the w-by-w page array on face f. Return the sample hit count.

static int pixel(scm *s, img *p, int  f, int  i, int  j,
                                 long u, long v, long w, float *q)
{
    // Sample the image.

    const int n = scm_get_n(s);
    const int c = scm_get_c(s);

    float *d = q + c * (((size_t) n + 2) * ((size_t) i + 1) + ((size_t) j + 1));

    int N = multisample(p, f, i, j, n, u, v, w, d);

    // Create the alpha channel and swap to BGRA, as necessary.

    if (p->c < c)
    {
        if (p->b == 8 && p->c == 3)
        {
            d[3] = d[0];
            d[0] = d[2];
            d[2] = d[3];
        }
        d[p->c] = N / 5.f;
    }
    return N;
}

// Consider page x of SCM s. Determine whether it contains any of image p.
// If so, sample it or recursively subdivide it as needed.

static long long divide(scm *s, img *p, long long b, int d, long long x,
                                        long u, long v, long w, float *q, float *t)
{
    const int f = (int) scm_page_root(x);
    long long a = b;

    if (overlap(p, f, u, v, w))
    {
        if (d == 0)
        {
            const int o = scm_get_n(s) + 2;
            const int c = scm_get_c(s);
            const int n = scm_get_n(s);

            int N = 0;
            int i;
            int j;

            memset(q, 0, (size_t) (o * o * c) * sizeof (float));

            #pragma omp parallel for private(j) reduction(+:N)
            for     (i = 0; i < n; ++i)
                for (j = 0; j < n; ++j)
                    N += pixel(s, p, f, i, j, u, v, w, q);

            if (p->c < c && N && N < n * n * 5) grow(q, t, c, n);

            if (N) a = scm_append(s, a, x, q);

            report_step();
        }
        else
        {
            long long x0 = scm_page_child(x, 0);
            long long x1 = scm_page_child(x, 1);
            long long x2 = scm_page_child(x, 2);
            long long x3 = scm_page_child(x, 3);

            a = divide(s, p, a, d - 1, x0, u * 2,     v * 2,     w * 2, q, t);
            a = divide(s, p, a, d - 1, x1, u * 2,     v * 2 + 1, w * 2, q, t);
            a = divide(s, p, a, d - 1, x2, u * 2 + 1, v * 2,     w * 2, q, t);
            a = divide(s, p, a, d - 1, x3, u * 2 + 1, v * 2 + 1, w * 2, q, t);
        }
    }
    return a;
}

// Convert image p to SCM s with depth d. Allocate working buffers and perform a
// depth-first traversal of the page tree.

static int process(scm *s, int d, img *p)
{
    float *q;
    float *t;

    report_init(6 << (d * 2));

    if ((q = scm_alloc_buffer(s)))
    {
        if ((t = scm_alloc_buffer(s)))
        {
            long long b = 0;

            b = divide(s, p, b, d, 0, 0, 0, 1, q, t);
            b = divide(s, p, b, d, 1, 0, 0, 1, q, t);
            b = divide(s, p, b, d, 2, 0, 0, 1, q, t);
            b = divide(s, p, b, d, 3, 0, 0, 1, q, t);
            b = divide(s, p, b, d, 4, 0, 0, 1, q, t);
            b = divide(s, p, b, d, 5, 0, 0, 1, q, t);

            free(t);
        }
        free(q);
    }
    return 0;
}

//------------------------------------------------------------------------------

int convert(int argc, char **argv, const char *o,
                                           int n,
                                           int d,
                                           int b,
                                           int g,
                                           int A,
                                 const float  *N,
                                 const double *E,
                                 const double *L,
                                 const double *P)
{
    img  *p = NULL;
    scm  *s = NULL;
    const char *e = NULL;

    char out[256];

    init_tap(0, 1, MTAPS, tap);

    // Iterate over all input file arguments.

    for (int i = 0; i < argc; i++)
    {
        const char *in = argv[i];

        // Generate the output file name.

        if (o) strcpy(out, o);

        else if ((e = strrchr(in, '.')))
        {
            memset (out, 0, 256);
            strncpy(out, in, e - in);
            strcat (out, ".tif");
        }
        else strcpy(out, "out.tif");

        // Load the input file.

        if      (extcmp(in, ".jpg") == 0) p = jpg_load(in);
        else if (extcmp(in, ".png") == 0) p = png_load(in);
        else if (extcmp(in, ".tif") == 0) p = tif_load(in);
        else if (extcmp(in, ".img") == 0) p = pds_load(in);
        else if (extcmp(in, ".lbl") == 0) p = pds_load(in);

        if (p)
        {
            // Allow the channel format overrides.

            if (b == -1) b = p->b;
            if (g == -1) g = p->g;

            // Set the blending parameters.

            if (P[0] || P[1] || P[2])
            {
                p->latc = P[0] * M_PI / 180.0;
                p->lat0 = P[1] * M_PI / 180.0;
                p->lat1 = P[2] * M_PI / 180.0;
            }
            if (L[0] || L[1] || L[2])
            {
                p->lonc = L[0] * M_PI / 180.0;
                p->lon0 = L[1] * M_PI / 180.0;
                p->lon1 = L[2] * M_PI / 180.0;
            }

            // Set the equirectangular subset parameters.

            if (E[0] || E[1] || E[2] || E[3])
            {
                p->westernmost_longitude = E[0] * M_PI / 180.0;
                p->easternmost_longitude = E[1] * M_PI / 180.0;
                p->minimum_latitude = E[2] * M_PI / 180.0;
                p->maximum_latitude = E[3] * M_PI / 180.0;
                p->project = img_default;
            }

            // Set the normalization parameters.

            if (N[0] || N[1])
            {
                p->norm0 = N[0];
                p->norm1 = N[1];
            }
            else if (b == 8)
            {
                if (g) { p->norm0 = 0.0f; p->norm1 =   127.0f; }
                else   { p->norm0 = 0.0f; p->norm1 =   255.0f; }
            }
            else if (b == 16)
            {
                if (g) { p->norm0 = 0.0f; p->norm1 = 32767.0f; }
                else   { p->norm0 = 0.0f; p->norm1 = 65535.0f; }
            }
            else
            {
                p->norm0 = 0.0f;
                p->norm1 = 1.0f;
            }

            // Process the output.

            if ((s = scm_ofile(out, n, p->c + A, b, g)))
            {
                process(s, d, p);
                scm_close(s);
            }
            img_close(p);
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
