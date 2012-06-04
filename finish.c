// Copyright (c) 2011 Robert Kooima.  All Rights Reserved.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "scm.h"
#include "err.h"
#include "util.h"
#include "process.h"

//------------------------------------------------------------------------------

static char *load_txt(const char *name)
{
    // Load the named file into a newly-allocated buffer.

    FILE *fp = 0;
    void  *p = 0;
    size_t n = 0;

    if ((fp = fopen(name, "rb")))
    {
        if (fseek(fp, 0, SEEK_END) == 0)
        {
            if ((n = (size_t) ftell(fp)))
            {
                if (fseek(fp, 0, SEEK_SET) == 0)
                {
                    if ((p = calloc(n + 1, 1)))
                    {
                        if (fread(p, 1, n, fp) == n)
                        {
                            // The top of the mountain.
                        }
                        else apperr("Failure to read %s", name);
                    }
                    else apperr("Failure to allocate %s", name);
                }
                else apperr("Failed to seek %s", name);
            }
            else apperr("Failed to tell %s", name);
        }
        else apperr("Failed to seek %s", name);
    }
    else apperr("Failed to open %s", name);

    fclose(fp);
    return p;
}

//------------------------------------------------------------------------------

int finish(int argc, char **argv, const char *t, int l)
{
    for (int i = 0; i < argc; i++)
    {
        char *txt = NULL;
        scm  *s   = NULL;

        if (t)
            txt = load_txt(t);

        if (txt == NULL)
            txt = "Copyright (c) 2012 Robert Kooima";

        if ((s = scm_ifile(argv[i])))
        {
            scm_finish(s, txt, l);
            scm_close(s);
        }
    }
    return 0;
}

//------------------------------------------------------------------------------
