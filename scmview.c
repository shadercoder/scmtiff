// Copyright (c) 2011 Robert Kooima.  All Rights Reverved.

// SCMVIEW is a light-weight OpenGL-based viewer for SCMTIFF format image files.
// It enables panning, zooming, and false-color rendering of individual pages,
// as well as side-by-side comparison of multiple SCMTIFF data files. SCMVIEW is
// a debugging tool, and does NOT produce seamless spherical renderings.

#include <stdio.h>
#include <float.h>
#include <stdlib.h>
#include <string.h>

#include <OpenGL/gl.h>
#include <GLUT/glut.h>

#include "scm.h"
#include "err.h"

//------------------------------------------------------------------------------
// SCM TIFF file and page state

#define MAX 4

struct file
{
    scm   *s;               // SCM file
    int    paged;           // Depth of this SCM
    off_t *pageo;           // Offset mapping of this SCM
    GLuint texture;         // On-screen texture cache
};

struct file *filev;
static int   filec;
static int   pagei;

//------------------------------------------------------------------------------
// Viewing and interaction state

static GLfloat pos_x = -0.5f;
static GLfloat pos_y = -0.5f;
static GLfloat scale =  1.0f;

static int     drag_modifier;
static int     drag_button;
static int     drag_x;
static int     drag_y;
static GLfloat drag_pos_x;
static GLfloat drag_pos_y;
static GLfloat drag_scale;

static GLuint  prog_color;
static GLuint  cmap_color;

//------------------------------------------------------------------------------

static double  *dbuf;
static GLfloat *fbuf;

// Initialize the float buffer to a checkerboard pattern. This provides an
// unambiguous representation for missing pages, where a black page might be
// a page that's actually black.

static void data_null(int n, int c)
{
    int l = 0;

    for         (int i = 0; i < n; ++i)
        for     (int j = 0; j < n; ++j)
            for (int k = 0; k < c; ++k, ++l)
                fbuf[l] = ((((i - 1) >> 3) & 1) ==
                           (((j - 1) >> 3) & 1)) ? 0.4f : 0.6f;
}

// Copy the contents of the double buffer to the float buffer.

static void data_copy(int n)
{
    for (int k = 0; k < n; ++k)
        fbuf[k] = (GLfloat) dbuf[k];
}

// Load page x from all files. Upload the data to the on-screen texture cache.

static int data_load(int x)
{
    const GLenum e[] = { 0, GL_LUMINANCE,
                            GL_LUMINANCE_ALPHA,
                            GL_RGB,
                            GL_RGBA };
    const GLenum f[] = { 0, GL_LUMINANCE32F_ARB,
                            GL_LUMINANCE_ALPHA32F_ARB,
                            GL_RGB32F_ARB,
                            GL_RGBA32F_ARB };

    for (int i = 0; i < filec; ++i)
    {
        const int m = scm_get_page_count(filev[i].paged);
        const int n = scm_get_n(filev[i].s) + 2;
        const int c = scm_get_c(filev[i].s);

        const off_t o = filev[i].pageo[x];

        if (0 <= x && x < m && o && scm_read_page(filev[i].s, o, dbuf))
            data_copy(n * n * c);
        else
            data_null(n, c);

        glBindTexture(GL_TEXTURE_2D, filev[i].texture);
        glTexImage2D (GL_TEXTURE_2D, 0, f[c], n, n, 0, e[c], GL_FLOAT, fbuf);
    }
    return 0;
}

// Iterate the list of files given on the command line and initialize a data
// file structure for each. Note the size and channel extreme, and allocate
// working buffers to accommodate the largest file.

static int data_init(int argc, char **argv)
{
    static const GLenum T = GL_TEXTURE_2D;

    int N = 0;
    int C = 0;
    filec = 0;

    if ((filev = (struct file *) calloc(argc, sizeof (struct file))))

        for (int argi = 1; argi < argc; ++argi)
            if ((filev[filec].s = scm_ifile(argv[argi])))
            {
                const int n = scm_get_n  (filev[filec].s) + 2;
                const int c = scm_get_c  (filev[filec].s);
                const int d = scm_mapping(filev[filec].s, &filev[filec].pageo);

                if (N < n) N = n;
                if (C < c) C = c;

                glGenTextures  (1, &filev[filec].texture);
                glBindTexture  (T,  filev[filec].texture);
                glTexParameteri(T, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(T, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

                filev[filec++].paged = d;
            }

    if (N && C)
    {
        if ((dbuf = (double *) malloc(N * N * C * sizeof (double))))
        {
            if ((fbuf = (GLfloat *) malloc(N * N * C * sizeof (GLfloat))))
            {
                return 1;
            }
        }
    }
    return 0;
}

//------------------------------------------------------------------------------

static const char *vert_color =
    "void main()\n"
    "{\n"
    "    gl_TexCoord[0] = gl_MultiTexCoord0;\n"
    "    gl_Position = ftransform();\n"
    "}\n";

static const char *frag_color =
    "uniform sampler2D image;"
    "uniform sampler1D color;"
    "uniform float a;"
    "uniform float z;"

    "void main()\n"
    "{\n"
    "    vec4 i = texture2D(image, gl_TexCoord[0].xy);\n"
    "    vec4 c = texture1D(color, (i.r - a) / (z - a));\n"
    "    gl_FragColor = vec4(c.rgb, i.a);\n"
    "}\n";

// Compile and link the given shader source files to a program object.  Scan
// the loaded files for minimum and maximum sample values and apply these to
// the program uniform.

static GLuint prog_init(const char *vertsrc, const char *fragsrc)
{
    GLuint prog = glCreateProgram();
    GLuint vert = glCreateShader(GL_VERTEX_SHADER);
    GLuint frag = glCreateShader(GL_FRAGMENT_SHADER);

    glShaderSource(vert, 1, (const GLchar **) &vertsrc, NULL);
    glCompileShader(vert);
    glAttachShader(prog, vert);

    glShaderSource(frag, 1, (const GLchar **) &fragsrc, NULL);
    glCompileShader(frag);
    glAttachShader(prog, frag);

    glLinkProgram(prog);
    glUseProgram(prog);

    double a =  DBL_MAX;
    double z = -DBL_MAX;

    for (int i = 0; i < filec; ++i)
    {
        double min[scm_get_c(filev[i].s)];
        double max[scm_get_c(filev[i].s)];

        scm_get_min(filev[i].s, min);
        scm_get_max(filev[i].s, max);

        if (a > min[0]) a = min[0];
        if (z < max[0]) z = max[0];
    }

    glUniform1f(glGetUniformLocation(prog, "a"),     a);
    glUniform1f(glGetUniformLocation(prog, "z"),     z);

    return prog;
}

// Initialize a 1D texture object with a rainbow color color map.

static GLuint cmap_init(GLuint prog)
{
    static const GLubyte c[8][4] = {
        { 0x00, 0x00, 0x00, 0xFF },
        { 0xFF, 0x00, 0xFF, 0xFF },
        { 0x00, 0x00, 0xFF, 0xFF },
        { 0x00, 0xFF, 0xFF, 0xFF },
        { 0x00, 0xFF, 0x00, 0xFF },
        { 0xFF, 0xFF, 0x00, 0xFF },
        { 0xFF, 0x00, 0x00, 0xFF },
        { 0xFF, 0xFF, 0xFF, 0xFF },
    };

    GLuint texture;

    glActiveTexture(GL_TEXTURE1);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_1D, texture);
    glTexImage1D(GL_TEXTURE_1D, 0, GL_RGBA, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, c);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_1D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glActiveTexture(GL_TEXTURE0);

    glUniform1i(glGetUniformLocation(prog, "color"), 1);

    return texture;
}

// Set the current page. Load the appropriate data from each file and set the
// window title accordingly.

static void page(int x)
{
    char str[256];

    data_load(x);

    pagei = x;

    sprintf(str, "Page %d\n", x);

    glutSetWindowTitle(str);
    glutPostRedisplay();
}

//------------------------------------------------------------------------------

// Application startup.

static int start(int argc, char **argv)
{
    if (data_init(argc, argv))
    {
        glDisable(GL_LIGHTING);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        prog_color = prog_init(vert_color, frag_color);
        cmap_color = cmap_init(prog_color);

        glUseProgram(0);

        page(0);

        return 1;
    }
    return 0;
}

// GLUT keyboard callback.

static void keyboard(unsigned char key, int x, int y)
{
    if (key == 27)
        exit(EXIT_SUCCESS);

    else if (key == 13)
    {
        pos_x = -0.5f;
        pos_y = -0.5f;
        scale =  1.0f;
        glutPostRedisplay();
    }

    else if (key == '0') page(0);
    else if (key == '1') page(scm_get_page_count(0));
    else if (key == '2') page(scm_get_page_count(1));
    else if (key == '3') page(scm_get_page_count(2));
    else if (key == '4') page(scm_get_page_count(3));
    else if (key == '5') page(scm_get_page_count(4));
    else if (key == '6') page(scm_get_page_count(5));
    else if (key == '7') page(scm_get_page_count(6));
    else if (key == '8') page(scm_get_page_count(7));
    else if (key == '9') page(scm_get_page_count(8));
}

// GLUT special keyboard callback.

static void special(int key, int x, int y)
{
    if      (key == GLUT_KEY_PAGE_UP)   page(pagei + 1);
    else if (key == GLUT_KEY_PAGE_DOWN) page(pagei - 1);
    else if (key == GLUT_KEY_F1)
    {
        glUseProgram(0);
        glutPostRedisplay();
    }
    else if (key == GLUT_KEY_F2)
    {
        glUseProgram(prog_color);
        glutPostRedisplay();
    }
}

// GLUT display callback.

static void display(void)
{
    int w = glutGet(GLUT_WINDOW_WIDTH);
    int h = glutGet(GLUT_WINDOW_HEIGHT);

    glViewport(0, 0, w, h);

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, 1, 0, 1, 0, 1);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0.5f, 0.5f, 0.0f);
    glScalef(1.0f / scale, 1.0f / scale, 1.0f);
    glTranslatef(pos_x, pos_y, 0);

    for (int i = 0; i < filec; ++i)
    {
        glViewport(i * w / filec, 0, w / filec, h);
        glBindTexture(GL_TEXTURE_2D, filev[i].texture);

        glBegin(GL_QUADS);
        {
            glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
            glTexCoord2f(1.f, 1.f);
            glVertex2f(0.f, 0.f);
            glTexCoord2f(0.f, 1.f);
            glVertex2f(1.f, 0.f);
            glTexCoord2f(0.f, 0.f);
            glVertex2f(1.f, 1.f);
            glTexCoord2f(1.f, 0.f);
            glVertex2f(0.f, 1.f);
        }
        glEnd();
    }
    glutSwapBuffers();
}

// GLUT active motion callback.

static void motion(int x, int y)
{
    if (drag_button == GLUT_LEFT_BUTTON)
    {
        const int w = glutGet(GLUT_WINDOW_WIDTH) / filec;
        const int h = glutGet(GLUT_WINDOW_HEIGHT);

        if (drag_modifier == GLUT_ACTIVE_ALT)
        {
            scale = drag_scale - (GLfloat) (y - drag_y) / (GLfloat) h;
        }
        else
        {
            pos_x = drag_pos_x + (GLfloat) (x - drag_x) / (GLfloat) w;
            pos_y = drag_pos_y - (GLfloat) (y - drag_y) / (GLfloat) h;
        }
        glutPostRedisplay();
    }
}

// GLUT mouse button click callback.

static void mouse(int button, int state, int x, int y)
{
    drag_modifier = glutGetModifiers();
    drag_button   = button;
    drag_x        = x;
    drag_y        = y;
    drag_pos_x    = pos_x;
    drag_pos_y    = pos_y;
    drag_scale    = scale;
    glutPostRedisplay();
}

//------------------------------------------------------------------------------

int main(int argc, char **argv)
{
    setexe(argv[0]);

    glutInitDisplayMode(GLUT_DEPTH | GLUT_DOUBLE);
    glutInitWindowSize((argc - 1) * 512, 512);
    glutInit(&argc, argv);

    glutCreateWindow(argv[0]);

    glutKeyboardFunc(keyboard);
    glutSpecialFunc(special);
    glutDisplayFunc(display);
    glutMotionFunc(motion);
    glutMouseFunc(mouse);

    if (start(argc, argv))
        glutMainLoop();

    return 0;
}
