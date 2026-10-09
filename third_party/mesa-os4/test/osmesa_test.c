/*
 * osmesa_test.c - smoke test for the AmigaOS 4 software OSMesa build.
 * Renders two overlapping depth-tested triangles into a 320x240 RGBA
 * buffer and checks a few pixels. Prints PASS/FAIL; exit code 0 on pass.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <GL/osmesa.h>
#include <GL/gl.h>

#define W 320
#define H 240

static GLubyte *buf;
static int failures;

static void check(const char *what, int x, int y, int r, int g, int b)
{
    /* OSMesa default: row 0 is the bottom of the image (GL convention) */
    const GLubyte *p = buf + ((long)y * W + x) * 4;
    int ok = abs(p[0] - r) <= 2 && abs(p[1] - g) <= 2 && abs(p[2] - b) <= 2;
    printf("%-28s (%3ld,%3ld) = %3ld %3ld %3ld %3ld  expect %3ld %3ld %3ld  %s\n",
           what, (long)x, (long)y, (long)p[0], (long)p[1], (long)p[2], (long)p[3],
           (long)r, (long)g, (long)b, ok ? "ok" : "MISMATCH");
    if (!ok)
        failures++;
}

int main(void)
{
    OSMesaContext ctx;

    buf = (GLubyte *)malloc(W * H * 4);
    if (!buf) {
        printf("FAIL: out of memory\n");
        return 20;
    }
    memset(buf, 0x55, W * H * 4);

    ctx = OSMesaCreateContextExt(OSMESA_RGBA, 16, 0, 0, NULL);
    if (!ctx) {
        printf("FAIL: OSMesaCreateContextExt returned NULL\n");
        return 20;
    }
    if (!OSMesaMakeCurrent(ctx, buf, GL_UNSIGNED_BYTE, W, H)) {
        printf("FAIL: OSMesaMakeCurrent failed\n");
        return 20;
    }

    printf("GL_VENDOR   = %s\n", (const char *)glGetString(GL_VENDOR));
    printf("GL_RENDERER = %s\n", (const char *)glGetString(GL_RENDERER));
    printf("GL_VERSION  = %s\n", (const char *)glGetString(GL_VERSION));

    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, W, 0.0, H, -10.0, 10.0);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glClearColor(0.0f, 0.0f, 1.0f, 1.0f);   /* blue background */
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    /* Red triangle, near (z = +5 -> eye z -5 -> small depth). Drawn SECOND
     * would be trivial; draw it FIRST so the depth test must reject green. */
    glBegin(GL_TRIANGLES);
    glColor3f(1.0f, 0.0f, 0.0f);
    glVertex3f( 20.0f,  20.0f, 5.0f);
    glVertex3f(200.0f,  20.0f, 5.0f);
    glVertex3f( 20.0f, 200.0f, 5.0f);
    glEnd();

    /* Green triangle, far (z = -5), overlapping the red one. */
    glBegin(GL_TRIANGLES);
    glColor3f(0.0f, 1.0f, 0.0f);
    glVertex3f(100.0f,  40.0f, -5.0f);
    glVertex3f(300.0f,  40.0f, -5.0f);
    glVertex3f(100.0f, 220.0f, -5.0f);
    glEnd();

    glFinish();

    check("background", 5, 5, 0, 0, 255);
    check("background (top right)", 315, 235, 0, 0, 255);
    check("red only", 40, 40, 255, 0, 0);
    check("overlap (red wins depth)", 110, 60, 255, 0, 0);
    check("green only", 250, 60, 0, 255, 0);

    if (glGetError() != GL_NO_ERROR) {
        printf("GL error reported\n");
        failures++;
    }

    OSMesaDestroyContext(ctx);
    free(buf);

    printf("%s\n", failures ? "FAIL" : "PASS");
    return failures ? 10 : 0;
}
