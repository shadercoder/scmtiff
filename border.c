// Copyright (c) 2011 Robert Kooima.  All Rights Reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scm.h"
#include "scmdef.h"

#include "util.h"
#include "process.h"

//------------------------------------------------------------------------------

typedef int (*tofun)(int, int, int);

static int topi(int i, int j, int n) { return         i; }
static int toni(int i, int j, int n) { return n - 1 - i; }
static int topj(int i, int j, int n) { return         j; }
static int tonj(int i, int j, int n) { return n - 1 - j; }

static const tofun translate_i[6][6] = {
    { topi, NULL, tonj, topj, topi, topi },
    { NULL, topi, topj, tonj, topi, topi },
    { topj, tonj, topi, NULL, topi, toni },
    { tonj, topj, NULL, topi, topi, toni },
    { topi, topi, topi, topi, topi, NULL },
    { topi, topi, toni, toni, NULL, topi },
};

static const  tofun translate_j[6][6] = {
    { topj, NULL, topi, toni, topj, topj },
    { NULL, topj, toni, topi, topj, topj },
    { toni, topi, topj, NULL, topj, tonj },
    { topi, toni, NULL, topj, topj, tonj },
    { topj, topj, topj, topj, topj, NULL },
    { topj, topj, tonj, tonj, NULL, topj },
};

static float *pixel(float *p, int n, int c, int i, int j)
{
    return p + c * (i * n + j);
}

static void cpy(float *p, const float *q, int c)
{
    switch (c)
    {
        case 4: p[3] = q[3];
        case 3: p[2] = q[2];
        case 2: p[1] = q[1];
        case 1: p[0] = q[0];
    }
}

static void copyn(float *p, long long x, float *q, long long y, int n, int c)
{
    for (int j = 0; j < n; ++j)
        cpy(pixel(p, n, c, 0, j),
            pixel(q, n, c, translate_i[x][y](n - 2, j, n),
                           translate_j[x][y](n - 2, j, n)), c);
}

static void copys(float *p, long long x, float *q, long long y, int n, int c)
{
    for (int j = 0; j < n; ++j)
        cpy(pixel(p, n, c, n - 1, j),
            pixel(q, n, c, translate_i[x][y](1, j, n),
                           translate_j[x][y](1, j, n)), c);
}

static void copyw(float *p, long long x, float *q, long long y, int n, int c)
{
    for (int i = 0; i < n; ++i)
        cpy(pixel(p, n, c, i, 0),
            pixel(q, n, c, translate_i[x][y](i, n - 2, n),
                           translate_j[x][y](i, n - 2, n)), c);
}

static void copye(float *p, long long x, float *q, long long y, int n, int c)
{
    for (int i = 0; i < n; ++i)
        cpy(pixel(p, n, c, i, n - 1),
            pixel(q, n, c, translate_i[x][y](i, 1, n),
                           translate_j[x][y](i, 1, n)), c);
}

static void copynw(float *p, long long x, float *q, long long y, int n, int c)
{
    if (translate_i[x][y])
        cpy(pixel(p, n, c, 0, 0),
            pixel(q, n, c, translate_i[x][y](n - 2, n - 2, n),
                           translate_j[x][y](n - 2, n - 2, n)), c);
}

static void copyne(float *p, long long x, float *q, long long y, int n, int c)
{
    if (translate_i[x][y])
        cpy(pixel(p, n, c, 0, n - 1),
            pixel(q, n, c, translate_i[x][y](n - 2, 1, n),
                           translate_j[x][y](n - 2, 1, n)), c);
}

static void copysw(float *p, long long x, float *q, long long y, int n, int c)
{
    if (translate_i[x][y])
        cpy(pixel(p, n, c, n - 1, 0),
            pixel(q, n, c, translate_i[x][y](1, n - 2, n),
                           translate_j[x][y](1, n - 2, n)), c);
}

static void copyse(float *p, long long x, float *q, long long y, int n, int c)
{
    if (translate_i[x][y])
        cpy(pixel(p, n, c, n - 1, n - 1),
            pixel(q, n, c, translate_i[x][y](1, 1, n),
                           translate_j[x][y](1, 1, n)), c);
}

static void process(scm *s, scm *t)
{
    const int o = scm_get_n(s) + 2;
    const int c = scm_get_c(s);

    long long b = 0;
    float    *p;
    float    *q;

    if (scm_scan_catalog(s))
    {
        if ((p = scm_alloc_buffer(s)) && (q = scm_alloc_buffer(t)))
        {
            for (long long i = 0; i < scm_get_l(s); ++i)
            {
                if (scm_read_page(s, scm_get_offset(s, i), p))
                {
                    // Determine the page indices of all neighboring pages.

                    long long x   = scm_get_index(s, i);

                    long long xn  = scm_page_north(x);
                    long long xs  = scm_page_south(x);
                    long long xw  = scm_page_west (x);
                    long long xe  = scm_page_east (x);

                    // Determine their roots.

                    long long f   = scm_page_root(x);
                    long long fn  = scm_page_root(xn);
                    long long fs  = scm_page_root(xs);
                    long long fw  = scm_page_root(xw);
                    long long fe  = scm_page_root(xe);

                    // Use roots to guide the determination of diagonals.

                    long long xnw = (fn == f) ? scm_page_west (xn)
                                              : scm_page_north(xw);
                    long long xne = (fn == f) ? scm_page_east (xn)
                                              : scm_page_north(xe);
                    long long xsw = (fs == f) ? scm_page_west (xs)
                                              : scm_page_south(xw);
                    long long xse = (fs == f) ? scm_page_east (xs)
                                              : scm_page_south(xe);

                    // Seek the page catalog locations of all neighbors.

                    long long on  = scm_find_offset(s, xn);
                    long long os  = scm_find_offset(s, xs);
                    long long ow  = scm_find_offset(s, xw);
                    long long oe  = scm_find_offset(s, xe);
                    long long onw = scm_find_offset(s, xnw);
                    long long one = scm_find_offset(s, xne);
                    long long osw = scm_find_offset(s, xsw);
                    long long ose = scm_find_offset(s, xse);

                    // Copy the borders of all adjacent pages into this one.

                    if (on > 0 && scm_read_page(s, on, q))
                        copyn(p, f, q, fn, o, c);
                    if (os > 0 && scm_read_page(s, os, q))
                        copys(p, f, q, fs, o, c);
                    if (ow > 0 && scm_read_page(s, ow, q))
                        copyw(p, f, q, fw, o, c);
                    if (oe > 0 && scm_read_page(s, oe, q))
                        copye(p, f, q, fe, o, c);

                    // Copy the corners of all diagonal pages into this one.

                    long long fnw = scm_page_root(xnw);
                    long long fne = scm_page_root(xne);
                    long long fsw = scm_page_root(xsw);
                    long long fse = scm_page_root(xse);

                    if (onw > 0 && scm_read_page(s, onw, q))
                        copynw(p, f, q, fnw, o, c);
                    if (one > 0 && scm_read_page(s, one, q))
                        copyne(p, f, q, fne, o, c);
                    if (osw > 0 && scm_read_page(s, osw, q))
                        copysw(p, f, q, fsw, o, c);
                    if (ose > 0 && scm_read_page(s, ose, q))
                        copyse(p, f, q, fse, o, c);

                    // Write the resulting page to the output.

                    b = scm_append(t, b, x, p);
                }
            }
            free(q);
            free(p);
        }
    }
}

//------------------------------------------------------------------------------

int border(int argc, char **argv, const char *o)
{
    if (argc > 0)
    {
        const char *out = o ? o : "out.tif";

        scm *s = NULL;
        scm *t = NULL;

        if ((s = scm_ifile(argv[0])))
        {
            int   n = scm_get_n(s);
            int   c = scm_get_c(s);
            int   b = scm_get_b(s);
            int   g = scm_get_g(s);
            char *T = scm_get_description(s);

            if ((t = scm_ofile(out, n, c, b, g, T)))
            {
                process(s, t);
                scm_close(t);
            }
            scm_close(s);
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
