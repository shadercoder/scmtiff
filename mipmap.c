// Copyright (c) 2011 Robert Kooima.  All Rights Reverved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scm.h"
#include "err.h"
#include "util.h"

//------------------------------------------------------------------------------

// Determine and return the depth of SCM s. Allocate and initialize a mapping
// from index to offset for each passible page of s. A zero offset indicates
// that the corresponding page is not present in s.

static int catalog(scm *s, off_t **map)
{
    off_t *ov;
    int   *xv;
    int    c;
    int    i;

    if ((c = scm_catalog(s, &xv, &ov)))
    {
        const int d = scm_get_page_level(xv[c - 1]);
        const int m = scm_get_page_count(d);

        if ((*map = calloc(m, sizeof (off_t))))
        {
            for (i = 0; i < c; ++i)
                (*map)[xv[i]] = ov[i];
        }
        else apperr("Failed to allocate offset map");

        free(ov);
        free(xv);
        return d;
    }
    return 0;
}

static void copy(double *p, int ki, int kj, int c, int n, double *q)
{
    for     (int qi = 0; qi < n; ++qi)
        for (int qj = 0; qj < n; ++qj)
        {
            const int pi = qi / 2 + ki * n / 2;
            const int pj = qj / 2 + kj * n / 2;

            double *qq = q + ((qi + 1) * (n + 2) + (qj + 1)) * c;
            double *pp = p + ((pi + 1) * (n + 2) + (pj + 1)) * c;

            switch (c)
            {
                case 4: pp[3] += qq[3] / 4.0;
                case 3: pp[2] += qq[2] / 4.0;
                case 2: pp[1] += qq[1] / 4.0;
                case 1: pp[0] += qq[0] / 4.0;
            }
        }
}

// scan SCM s seeking any page that is not present, but which has at least one
// child present. Fill such pages using down-sampled child data and append them
// to SCM t.

static int sample(scm *s, scm *t)
{
    int    d, r = EXIT_FAILURE;
    off_t *m, b = 0;

    if ((d = catalog(s, &m)))
    {
        const int M = scm_get_page_count(d);
        const int N = scm_get_n(s) + 2;
        const int n = scm_get_n(s);
        const int c = scm_get_c(s);

        double *p;
        double *q;

        if ((p = (double *) malloc(N * N * c * sizeof (double))) &&
            (q = (double *) malloc(N * N * c * sizeof (double))))
        {
            for (int x = 0; x < M; ++x)
            {
                int i = scm_get_page_child(x, 0);
                int j = scm_get_page_child(x, 1);
                int k = scm_get_page_child(x, 2);
                int l = scm_get_page_child(x, 3);

                if (m[x] == 0 && (m[i] || m[j] || m[k] || m[l]))
                {
                    memset(p, 0, N * N * c * sizeof (double));

                    if (m[i] && scm_read_page(s, m[i], q)) copy(p, 0, 0, c, n, q);
                    if (m[j] && scm_read_page(s, m[j], q)) copy(p, 0, 1, c, n, q);
                    if (m[k] && scm_read_page(s, m[k], q)) copy(p, 1, 0, c, n, q);
                    if (m[l] && scm_read_page(s, m[l], q)) copy(p, 1, 1, c, n, q);

                    b = scm_append(t, b, 0, 0, x, p);
                    r = EXIT_SUCCESS;
                }
            }
            free(q);
            free(p);
        }
    }
    return r;
}

// Append the contents of SCM s to SCM t.

static void append(scm *s, scm *t)
{
}

// Mipmap SCM s to the fullest extent possible. To do so,  When finished,
// append the contents of SCM s to SCM t. Return failure to indicate that no
// processing was performed, implying that mipmapping of SCM s is complete.

static int process(scm *s, scm *t)
{
    if (sample(s, t) == EXIT_SUCCESS)
    {
        append(s, t);
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

//------------------------------------------------------------------------------

int mipmap(int argc, char **argv)
{
    const char *out = "out.tif";
    const char *in  = "in.tif";

    scm *s = NULL;
    scm *t = NULL;

    int r = EXIT_FAILURE;

    for (int i = 1; i < argc; ++i)
        if      (strcmp(argv[i],   "-o") == 0) out = argv[++i];
        else if (extcmp(argv[i], ".tif") == 0) in  = argv[  i];

    if ((s = scm_ifile(in)))
    {
        if ((t = scm_ofile(out, scm_get_n(s), scm_get_c(s),
                                scm_get_b(s), scm_get_s(s),
                                scm_get_copyright(s))))
        {
            r = process(s, t);
            scm_close(t);
        }
        scm_close(s);
    }
    return r;
}

//------------------------------------------------------------------------------