/*
 * Zhaoxu OS - OpenGL 1.1 subset regression.
 *
 * Renders into an off-screen 0xAARRGGBB buffer and inspects the result, so it
 * exercises the matrix pipeline, clipping, rasterization, depth test, culling
 * and glReadPixels without needing a window server.
 */
#include "lib/opengl/GL/gl.h"
#include "userlib.h"

#define TEST_W 160
#define TEST_H 120

static unsigned int g_frame[TEST_W * TEST_H];
static unsigned char g_readback[TEST_W * TEST_H * 4];

/* Return the packed ARGB pixel at a test-framebuffer coordinate. */
static unsigned int pixel_at(int x, int y)
{
    return g_frame[y * TEST_W + x];
}

/* Compare a pixel against an RGB triple; alpha is always 0xFF. */
static int expect_rgb(int x, int y, unsigned int red, unsigned int green,
                      unsigned int blue)
{
    unsigned int color = pixel_at(x, y);
    unsigned int r = (color >> 16) & 0xFF;
    unsigned int g = (color >> 8) & 0xFF;
    unsigned int b = color & 0xFF;
    return r == red && g == green && b == blue;
}

/* Compare a pixel with a per-channel tolerance; interpolation can land one
 * or two counts away from the ideal value at a pixel centre. */
static int expect_rgb_near(int x, int y, int red, int green, int blue,
                           int tolerance)
{
    unsigned int color = pixel_at(x, y);
    int r = (int)((color >> 16) & 0xFF);
    int g = (int)((color >> 8) & 0xFF);
    int b = (int)(color & 0xFF);
    int dr = r > red ? r - red : red - r;
    int dg = g > green ? g - green : green - g;
    int db = b > blue ? b - blue : blue - b;
    return dr <= tolerance && dg <= tolerance && db <= tolerance;
}

/* Reset colour and depth, leaving the depth test disabled. */
static void reset_frame(void)
{
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(-1, 1, -1, 1, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

/* Draw a full-ish red quad and check that clear + rasterization work. */
static int test_quad(void)
{
    reset_frame();
    glColor3f(1, 0, 0);
    glBegin(GL_QUADS);
    glVertex2f(-0.8f, -0.8f);
    glVertex2f(0.8f, -0.8f);
    glVertex2f(0.8f, 0.8f);
    glVertex2f(-0.8f, 0.8f);
    glEnd();
    if (!expect_rgb(TEST_W / 2, TEST_H / 2, 255, 0, 0))
        return 0;
    if (!expect_rgb(2, 2, 0, 0, 0))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* Draw a Gouraud triangle with three corner colours and check the centroid. */
static int test_gouraud(void)
{
    reset_frame();
    glBegin(GL_TRIANGLES);
    glColor3f(1, 0, 0);
    glVertex2f(-0.8f, -0.8f);
    glColor3f(0, 1, 0);
    glVertex2f(0.8f, -0.8f);
    glColor3f(0, 0, 1);
    glVertex2f(0, 0.8f);
    glEnd();
    if (!expect_rgb_near(TEST_W / 2, 76, 85, 85, 85, 4))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* Verify GL_LESS keeps the nearer (smaller NDC z) fragment in both orders.
 * With glOrtho(-1, 1, ..., -1, 1) the near plane is at model z = +1. */
static int test_depth(void)
{
    reset_frame();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    glColor3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex3f(-0.7f, -0.7f, 0.5f);
    glVertex3f(0.7f, -0.7f, 0.5f);
    glVertex3f(0.7f, 0.7f, 0.5f);
    glVertex3f(-0.7f, 0.7f, 0.5f);
    glEnd();
    glColor3f(0, 1, 0);
    glBegin(GL_QUADS);
    glVertex3f(-0.7f, -0.7f, -0.5f);
    glVertex3f(0.7f, -0.7f, -0.5f);
    glVertex3f(0.7f, 0.7f, -0.5f);
    glVertex3f(-0.7f, 0.7f, -0.5f);
    glEnd();
    if (!expect_rgb(TEST_W / 2, TEST_H / 2, 0, 0, 255))
        return 0;

    reset_frame();
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glColor3f(0, 1, 0);
    glBegin(GL_QUADS);
    glVertex3f(-0.7f, -0.7f, -0.5f);
    glVertex3f(0.7f, -0.7f, -0.5f);
    glVertex3f(0.7f, 0.7f, -0.5f);
    glVertex3f(-0.7f, 0.7f, -0.5f);
    glEnd();
    glColor3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex3f(-0.7f, -0.7f, 0.5f);
    glVertex3f(0.7f, -0.7f, 0.5f);
    glVertex3f(0.7f, 0.7f, 0.5f);
    glVertex3f(-0.7f, 0.7f, 0.5f);
    glEnd();
    if (!expect_rgb(TEST_W / 2, TEST_H / 2, 0, 0, 255))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* CCW is front by default: a CW triangle must disappear when culling. */
static int test_cull(void)
{
    reset_frame();
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    glColor3f(1, 0, 0);
    glBegin(GL_TRIANGLES);
    glVertex2f(-0.8f, -0.8f);
    glVertex2f(0.8f, -0.8f);
    glVertex2f(0, 0.8f);
    glEnd();
    if (!expect_rgb(80, 80, 255, 0, 0))
        return 0;

    /* Clockwise, therefore a back face and culled. */
    glColor3f(0, 0, 1);
    glBegin(GL_TRIANGLES);
    glVertex2f(0.2f, 0.2f);
    glVertex2f(0.2f, 0.8f);
    glVertex2f(0.8f, 0.8f);
    glEnd();
    if (!expect_rgb(130, 25, 0, 0, 0))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* glTranslatef must move geometry, and glPopMatrix must undo it. */
static int test_matrices(void)
{
    reset_frame();
    glPushMatrix();
    glTranslatef(0.5f, 0, 0);
    glColor3f(1, 0, 0);
    glBegin(GL_QUADS);
    glVertex2f(-0.2f, -0.2f);
    glVertex2f(0.2f, -0.2f);
    glVertex2f(0.2f, 0.2f);
    glVertex2f(-0.2f, 0.2f);
    glEnd();
    glPopMatrix();
    if (!expect_rgb(TEST_W * 3 / 4, TEST_H / 2, 255, 0, 0))
        return 0;
    if (!expect_rgb(TEST_W / 2, TEST_H / 2, 0, 0, 0))
        return 0;

    glColor3f(0, 1, 0);
    glBegin(GL_QUADS);
    glVertex2f(-0.2f, -0.2f);
    glVertex2f(0.2f, -0.2f);
    glVertex2f(0.2f, 0.2f);
    glVertex2f(-0.2f, 0.2f);
    glEnd();
    if (!expect_rgb(TEST_W / 2, TEST_H / 2, 0, 255, 0))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* A quad at z=0 stays visible through a z=-3 perspective camera. */
static int test_perspective(void)
{
    reset_frame();
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1, 1, -1, 1, 1, 10);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glTranslatef(0, 0, -3);
    glColor3f(0, 1, 1);
    glBegin(GL_QUADS);
    glVertex3f(-0.5f, -0.5f, 0);
    glVertex3f(0.5f, -0.5f, 0);
    glVertex3f(0.5f, 0.5f, 0);
    glVertex3f(-0.5f, 0.5f, 0);
    glEnd();
    if (!expect_rgb(TEST_W / 2, TEST_H / 2, 0, 255, 255))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* Upload a 2x2 RGBA texture and sample all four texels through a quad. */
static int test_texture(void)
{
    static const unsigned char image[16] = {
        255, 0, 0, 255,     0, 255, 0, 255,
        0, 0, 255, 255,     255, 255, 255, 255};
    reset_frame();
    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (!texture)
        return 0;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, image);
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(-1, -1);
    glTexCoord2f(1, 0);
    glVertex2f(1, -1);
    glTexCoord2f(1, 1);
    glVertex2f(1, 1);
    glTexCoord2f(0, 1);
    glVertex2f(-1, 1);
    glEnd();
    glDisable(GL_TEXTURE_2D);
    glDeleteTextures(1, &texture);

    if (!expect_rgb(40, 30, 0, 0, 255))
        return 0;
    if (!expect_rgb(120, 90, 0, 255, 0))
        return 0;
    if (!expect_rgb(40, 90, 255, 0, 0))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* A directional white light on a red diffuse material lights the quad red. */
static int test_lighting(void)
{
    reset_frame();
    GLfloat zero[4] = {0, 0, 0, 1};
    GLfloat red[4] = {1, 0, 0, 1};
    GLfloat position[4] = {0, 0, 1, 0};
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, zero);
    glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
    glMaterialfv(GL_FRONT, GL_SPECULAR, zero);
    glLightfv(GL_LIGHT0, GL_POSITION, position);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glNormal3f(0, 0, 1);
    glBegin(GL_QUADS);
    glVertex2f(-0.7f, -0.7f);
    glVertex2f(0.7f, -0.7f);
    glVertex2f(0.7f, 0.7f);
    glVertex2f(-0.7f, 0.7f);
    glEnd();
    glDisable(GL_LIGHT0);
    glDisable(GL_LIGHTING);
    if (!expect_rgb_near(TEST_W / 2, TEST_H / 2, 255, 0, 0, 2))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* Half-alpha red over black must blend to mid red, and alpha test must cull. */
static int test_blending(void)
{
    reset_frame();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(1, 0, 0, 0.5f);
    glBegin(GL_QUADS);
    glVertex2f(-0.8f, -0.8f);
    glVertex2f(0.8f, -0.8f);
    glVertex2f(0.8f, 0.8f);
    glVertex2f(-0.8f, 0.8f);
    glEnd();
    glDisable(GL_BLEND);
    if (!expect_rgb_near(TEST_W / 2, TEST_H / 2, 128, 0, 0, 2))
        return 0;

    reset_frame();
    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.9f);
    glColor4f(0, 1, 0, 0.5f);
    glBegin(GL_QUADS);
    glVertex2f(-0.8f, -0.8f);
    glVertex2f(0.8f, -0.8f);
    glVertex2f(0.8f, 0.8f);
    glVertex2f(-0.8f, 0.8f);
    glEnd();
    glColor4f(0, 1, 0, 1);
    glBegin(GL_QUADS);
    glVertex2f(-0.4f, -0.4f);
    glVertex2f(0.4f, -0.4f);
    glVertex2f(0.4f, 0.4f);
    glVertex2f(-0.4f, 0.4f);
    glEnd();
    glDisable(GL_ALPHA_TEST);
    if (!expect_rgb(20, 20, 0, 0, 0))
        return 0;
    if (!expect_rgb(TEST_W / 2, TEST_H / 2, 0, 255, 0))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* Scissor keeps the left half red and leaves the right half black. */
static int test_scissor(void)
{
    reset_frame();
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, TEST_W / 2, TEST_H);
    glColor3f(1, 0, 0);
    glBegin(GL_QUADS);
    glVertex2f(-0.9f, -0.9f);
    glVertex2f(0.9f, -0.9f);
    glVertex2f(0.9f, 0.9f);
    glVertex2f(-0.9f, 0.9f);
    glEnd();
    glDisable(GL_SCISSOR_TEST);
    if (!expect_rgb(20, 60, 255, 0, 0))
        return 0;
    if (!expect_rgb(TEST_W - 20, 60, 0, 0, 0))
        return 0;
    return glGetError() == GL_NO_ERROR;
}

/* glReadPixels uses a bottom-left origin; row 0 is the last framebuffer row. */
static int test_read_pixels(void)
{
    reset_frame();
    glColor3f(0, 1, 1);
    glBegin(GL_QUADS);
    glVertex2f(-0.5f, -0.5f);
    glVertex2f(0.5f, -0.5f);
    glVertex2f(0.5f, 0.5f);
    glVertex2f(-0.5f, 0.5f);
    glEnd();
    glReadPixels(0, 0, TEST_W, TEST_H, GL_RGBA, GL_UNSIGNED_BYTE,
                 g_readback);
    unsigned int top = (unsigned int)g_readback[0] |
                       ((unsigned int)g_readback[1] << 8) |
                       ((unsigned int)g_readback[2] << 16);
    unsigned int middle_index = ((unsigned long)(TEST_H / 2) * TEST_W +
                                 TEST_W / 2) * 4;
    unsigned int middle = (unsigned int)g_readback[middle_index] |
                          ((unsigned int)g_readback[middle_index + 1] << 8) |
                          ((unsigned int)g_readback[middle_index + 2] << 16);
    /* RGBA bytes are R, G, B, A: cyan assembles to 0xFFFF00. */
    if (middle != 0xFFFF00)
        return 0;
    if (top != 0)
        return 0;
    return glGetError() == GL_NO_ERROR;
}

int user_main(long *stack)
{
    (void)stack;
    for (int i = 0; i < TEST_W * TEST_H; i++)
        g_frame[i] = 0;
    zosgl_attach(g_frame, TEST_W, TEST_H);
    if (glGetError() != GL_NO_ERROR) {
        u_puts("[gl ] attach failed\n");
        return 1;
    }
    glViewport(0, 0, TEST_W, TEST_H);

    if (!test_quad()) {
        u_puts("[gl ] quad FAIL\n");
        return 1;
    }
    u_puts("[gl ] quad PASS\n");

    if (!test_gouraud()) {
        u_puts("[gl ] gouraud FAIL\n");
        return 1;
    }
    u_puts("[gl ] gouraud PASS\n");

    if (!test_depth()) {
        u_puts("[gl ] depth FAIL\n");
        return 1;
    }
    u_puts("[gl ] depth PASS\n");

    if (!test_cull()) {
        u_puts("[gl ] cull FAIL\n");
        return 1;
    }
    u_puts("[gl ] cull PASS\n");

    if (!test_matrices()) {
        u_puts("[gl ] matrix FAIL\n");
        return 1;
    }
    u_puts("[gl ] matrix PASS\n");

    if (!test_perspective()) {
        u_puts("[gl ] perspective FAIL\n");
        return 1;
    }
    u_puts("[gl ] perspective PASS\n");

    if (!test_texture()) {
        u_puts("[gl ] texture FAIL\n");
        return 1;
    }
    u_puts("[gl ] texture PASS\n");

    if (!test_lighting()) {
        u_puts("[gl ] lighting FAIL\n");
        return 1;
    }
    u_puts("[gl ] lighting PASS\n");

    if (!test_blending()) {
        u_puts("[gl ] blend FAIL\n");
        return 1;
    }
    u_puts("[gl ] blend PASS\n");

    if (!test_scissor()) {
        u_puts("[gl ] scissor FAIL\n");
        return 1;
    }
    u_puts("[gl ] scissor PASS\n");

    if (!test_read_pixels()) {
        u_puts("[gl ] readpixels FAIL\n");
        return 1;
    }
    u_puts("[gl ] readpixels PASS\n");

    zosgl_detach();
    u_puts("[gl ] OpenGL PASS\n");
    return 0;
}
