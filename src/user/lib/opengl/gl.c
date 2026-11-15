/*
 * Zhaoxu OS - OpenGL 1.1 immediate-mode software renderer.
 *
 * The implementation keeps the OpenGL conventions that matter for classic
 * demos: column-major matrices, a bottom-left window origin, perspective
 * divide, Gouraud colours, a depth buffer and CCW front faces.  It is a
 * subset, not a full GL implementation.
 */
#include "GL/gl.h"

#include <stdlib.h>
#include <string.h>

#define GL_MAX_STACK 32
#define GL_MAX_BATCH 4096

struct gl_vertex {
    float clip[4];
    float color[4];
};

static unsigned int *g_pixels;
static unsigned short *g_depth;
static int g_width;
static int g_height;
static GLenum g_error;

static GLfloat g_clear[4];
static GLfloat g_color[4] = {1, 1, 1, 1};
static int g_depth_test;
static int g_cull;
static GLenum g_depth_func = GL_LESS;
static GLenum g_front_face = GL_CCW;
static GLenum g_shade = GL_SMOOTH;
static GLenum g_cull_face = GL_BACK;
static GLint g_vp_x;
static GLint g_vp_y;
static GLsizei g_vp_w;
static GLsizei g_vp_h;

static GLfloat g_modelview[GL_MAX_STACK][16];
static GLfloat g_projection[GL_MAX_STACK][16];
static int g_mv_top;
static int g_proj_top;
static GLenum g_matrix_mode = GL_MODELVIEW;

static GLenum g_begin_mode;
static int g_begin_active;
static struct gl_vertex g_batch[GL_MAX_BATCH];
static int g_batch_count;

/* Square root by Newton iteration; user code is freestanding and has no libm. */
static float gl_sqrt(float value)
{
    if (value <= 0)
        return 0;
    float result = value > 1 ? value : 1;
    for (int i = 0; i < 16; i++)
        result = (result + value / result) * 0.5f;
    return result;
}

/* Small-angle Taylor sine, enough for glRotatef angles. */
static float gl_sin(float x)
{
    const float pi = 3.14159265358979323846f;
    while (x > pi)
        x -= 2 * pi;
    while (x < -pi)
        x += 2 * pi;
    float x2 = x * x;
    return x * (1 - x2 / 6 + x2 * x2 / 120 - x2 * x2 * x2 / 5040 +
                x2 * x2 * x2 * x2 / 362880);
}

static float gl_cos(float x)
{
    return gl_sin(x + 1.57079632679489661923f);
}

/* Record the first error until glGetError clears it. */
static void set_error(GLenum error)
{
    if (!g_error)
        g_error = error;
}

/* Fill a column-major 4x4 matrix with the identity. */
static void identity(GLfloat *m)
{
    memset(m, 0, 16 * sizeof(GLfloat));
    m[0] = m[5] = m[10] = m[15] = 1;
}

/* Select the top of the stack for the active matrix mode. */
static GLfloat *current_matrix(void)
{
    if (g_matrix_mode == GL_PROJECTION)
        return g_projection[g_proj_top];
    return g_modelview[g_mv_top];
}

/* out = a * b for column-major matrices; out may alias a. */
static void multiply(GLfloat *out, const GLfloat *a, const GLfloat *b)
{
    GLfloat result[16];
    for (int column = 0; column < 4; column++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0;
            for (int k = 0; k < 4; k++)
                sum += a[k * 4 + row] * b[column * 4 + k];
            result[column * 4 + row] = sum;
        }
    }
    memcpy(out, result, sizeof(result));
}

/* Fill a pixel run with one packed colour. */
static void clear_color(unsigned int *pixels, int count, unsigned int color)
{
    for (int i = 0; i < count; i++)
        pixels[i] = color;
}

/* Convert clamped floating RGBA to the framebuffer's 0xAARRGGBB word. */
static unsigned int pack_color(const float color[4])
{
    int r = (int)(color[0] * 255.0f + 0.5f);
    int g = (int)(color[1] * 255.0f + 0.5f);
    int b = (int)(color[2] * 255.0f + 0.5f);
    if (r < 0)
        r = 0;
    if (r > 255)
        r = 255;
    if (g < 0)
        g = 0;
    if (g > 255)
        g = 255;
    if (b < 0)
        b = 0;
    if (b > 255)
        b = 255;
    return 0xFF000000u | ((unsigned)r << 16) | ((unsigned)g << 8) |
           (unsigned)b;
}

/* Bind a framebuffer and allocate its 16-bit depth buffer; reset all state. */
void zosgl_attach(unsigned int *pixels, int width, int height)
{
    if (!pixels || width <= 0 || height <= 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    free(g_depth);
    g_depth = malloc((unsigned long)width * (unsigned long)height *
                     sizeof(unsigned short));
    if (!g_depth) {
        g_pixels = 0;
        g_width = g_height = 0;
        set_error(GL_INVALID_OPERATION);
        return;
    }
    g_pixels = pixels;
    g_width = width;
    g_height = height;
    g_vp_x = 0;
    g_vp_y = 0;
    g_vp_w = width;
    g_vp_h = height;
    g_clear[0] = g_clear[1] = g_clear[2] = 0;
    g_clear[3] = 1;
    g_color[0] = g_color[1] = g_color[2] = g_color[3] = 1;
    g_depth_test = 0;
    g_cull = 0;
    g_depth_func = GL_LESS;
    g_front_face = GL_CCW;
    g_shade = GL_SMOOTH;
    g_cull_face = GL_BACK;
    g_mv_top = g_proj_top = 0;
    g_matrix_mode = GL_MODELVIEW;
    g_begin_active = 0;
    g_batch_count = 0;
    identity(g_modelview[0]);
    identity(g_projection[0]);
}

/* Release the depth buffer and stop drawing. */
void zosgl_detach(void)
{
    free(g_depth);
    g_depth = 0;
    g_pixels = 0;
    g_width = g_height = 0;
}

/* Set the colour used by glClear(GL_COLOR_BUFFER_BIT). */
void glClearColor(GLclampf red, GLclampf green, GLclampf blue,
                  GLclampf alpha)
{
    g_clear[0] = red;
    g_clear[1] = green;
    g_clear[2] = blue;
    g_clear[3] = alpha;
}

/* Clear the requested colour and/or depth buffers. */
void glClear(GLbitfield mask)
{
    if (!g_pixels)
        return;
    if (mask & GL_COLOR_BUFFER_BIT)
        clear_color(g_pixels, g_width * g_height, pack_color(g_clear));
    if ((mask & GL_DEPTH_BUFFER_BIT) && g_depth) {
        int count = g_width * g_height;
        for (int i = 0; i < count; i++)
            g_depth[i] = 0xFFFF;
    }
}

/* Map normalised device coordinates onto a pixel rectangle. */
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (width < 0 || height < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    g_vp_x = x;
    g_vp_y = y;
    g_vp_w = width;
    g_vp_h = height;
}

/* Enable depth testing or back-face culling. */
void glEnable(GLenum capability)
{
    if (capability == GL_DEPTH_TEST)
        g_depth_test = 1;
    else if (capability == GL_CULL_FACE)
        g_cull = 1;
    else
        set_error(GL_INVALID_ENUM);
}

/* Disable depth testing or back-face culling. */
void glDisable(GLenum capability)
{
    if (capability == GL_DEPTH_TEST)
        g_depth_test = 0;
    else if (capability == GL_CULL_FACE)
        g_cull = 0;
    else
        set_error(GL_INVALID_ENUM);
}

/* Select the depth comparison function (NEVER/LESS/LEQUAL/ALWAYS). */
void glDepthFunc(GLenum function) { g_depth_func = function; }
/* Select which face (BACK/FRONT/FRONT_AND_BACK) culling removes. */
void glCullFace(GLenum mode) { g_cull_face = mode; }
/* Select the winding that counts as a front face (CCW/CW). */
void glFrontFace(GLenum mode) { g_front_face = mode; }
/* Select smooth (Gouraud) or flat colour interpolation. */
void glShadeModel(GLenum mode) { g_shade = mode; }

/* Select the modelview or projection stack for the matrix calls below. */
void glMatrixMode(GLenum mode)
{
    if (mode != GL_MODELVIEW && mode != GL_PROJECTION && mode != GL_TEXTURE) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    g_matrix_mode = mode;
}

/* Replace the active matrix with the identity. */
void glLoadIdentity(void) { identity(current_matrix()); }

/* Duplicate the active matrix onto the next stack slot. */
void glPushMatrix(void)
{
    if (g_matrix_mode == GL_PROJECTION) {
        if (g_proj_top + 1 >= GL_MAX_STACK) {
            set_error(GL_INVALID_OPERATION);
            return;
        }
        memcpy(g_projection[g_proj_top + 1], g_projection[g_proj_top],
               sizeof(GLfloat) * 16);
        g_proj_top++;
    } else {
        if (g_mv_top + 1 >= GL_MAX_STACK) {
            set_error(GL_INVALID_OPERATION);
            return;
        }
        memcpy(g_modelview[g_mv_top + 1], g_modelview[g_mv_top],
               sizeof(GLfloat) * 16);
        g_mv_top++;
    }
}

/* Restore the matrix saved by the matching glPushMatrix. */
void glPopMatrix(void)
{
    if (g_matrix_mode == GL_PROJECTION) {
        if (g_proj_top > 0)
            g_proj_top--;
        else
            set_error(GL_INVALID_OPERATION);
    } else {
        if (g_mv_top > 0)
            g_mv_top--;
        else
            set_error(GL_INVALID_OPERATION);
    }
}

/* Replace the active matrix with a caller-supplied column-major matrix. */
void glLoadMatrixf(const GLfloat *matrix)
{
    if (matrix)
        memcpy(current_matrix(), matrix, sizeof(GLfloat) * 16);
}

/* Post-multiply the active matrix by a column-major matrix. */
void glMultMatrixf(const GLfloat *matrix)
{
    if (matrix)
        multiply(current_matrix(), current_matrix(), matrix);
}

/* Append a translation to the active matrix. */
void glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat m[16];
    identity(m);
    m[12] = x;
    m[13] = y;
    m[14] = z;
    multiply(current_matrix(), current_matrix(), m);
}

/* Append a non-uniform scale to the active matrix. */
void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    GLfloat m[16];
    identity(m);
    m[0] = x;
    m[5] = y;
    m[10] = z;
    multiply(current_matrix(), current_matrix(), m);
}

/* Append a rotation of `angle` degrees around the given axis. */
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    float length = gl_sqrt(x * x + y * y + z * z);
    if (length == 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    x /= length;
    y /= length;
    z /= length;
    float radians = angle * 3.14159265358979323846f / 180.0f;
    float c = gl_cos(radians);
    float s = gl_sin(radians);
    float t = 1 - c;
    GLfloat m[16];
    m[0] = x * x * t + c;
    m[1] = y * x * t + z * s;
    m[2] = x * z * t - y * s;
    m[3] = 0;
    m[4] = x * y * t - z * s;
    m[5] = y * y * t + c;
    m[6] = y * z * t + x * s;
    m[7] = 0;
    m[8] = x * z * t + y * s;
    m[9] = y * z * t - x * s;
    m[10] = z * z * t + c;
    m[11] = 0;
    m[12] = m[13] = m[14] = 0;
    m[15] = 1;
    multiply(current_matrix(), current_matrix(), m);
}

/* Append the standard perspective frustum matrix. */
void glFrustum(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
               GLfloat near_plane, GLfloat far_plane)
{
    if (near_plane <= 0 || far_plane <= near_plane) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    GLfloat m[16];
    memset(m, 0, sizeof(m));
    m[0] = 2 * near_plane / (right - left);
    m[5] = 2 * near_plane / (top - bottom);
    m[8] = (right + left) / (right - left);
    m[9] = (top + bottom) / (top - bottom);
    m[10] = -(far_plane + near_plane) / (far_plane - near_plane);
    m[11] = -1;
    m[14] = -(2 * far_plane * near_plane) / (far_plane - near_plane);
    multiply(current_matrix(), current_matrix(), m);
}

/* Append the standard orthographic projection matrix. */
void glOrtho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
             GLfloat near_plane, GLfloat far_plane)
{
    GLfloat m[16];
    identity(m);
    m[0] = 2 / (right - left);
    m[5] = 2 / (top - bottom);
    m[10] = -2 / (far_plane - near_plane);
    m[12] = -(right + left) / (right - left);
    m[13] = -(top + bottom) / (top - bottom);
    m[14] = -(far_plane + near_plane) / (far_plane - near_plane);
    multiply(current_matrix(), current_matrix(), m);
}

/* Set the current vertex colour, which is captured at glVertex time. */
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    g_color[0] = red;
    g_color[1] = green;
    g_color[2] = blue;
    g_color[3] = alpha;
}

/* Set the current colour with an opaque alpha. */
void glColor3f(GLfloat red, GLfloat green, GLfloat blue)
{
    glColor4f(red, green, blue, 1);
}

/* Transform and record one vertex; the colour is copied from current state. */
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    if (!g_begin_active) {
        set_error(GL_INVALID_OPERATION);
        return;
    }
    if (g_batch_count >= GL_MAX_BATCH) {
        set_error(GL_INVALID_OPERATION);
        return;
    }
    GLfloat in[4] = {x, y, z, w};
    GLfloat eye[4];
    GLfloat *mv = g_modelview[g_mv_top];
    GLfloat *proj = g_projection[g_proj_top];
    for (int row = 0; row < 4; row++)
        eye[row] = mv[0 * 4 + row] * in[0] + mv[1 * 4 + row] * in[1] +
                   mv[2 * 4 + row] * in[2] + mv[3 * 4 + row] * in[3];
    struct gl_vertex *vertex = &g_batch[g_batch_count++];
    for (int row = 0; row < 4; row++)
        vertex->clip[row] = proj[0 * 4 + row] * eye[0] +
                            proj[1 * 4 + row] * eye[1] +
                            proj[2 * 4 + row] * eye[2] +
                            proj[3 * 4 + row] * eye[3];
    vertex->color[0] = g_color[0];
    vertex->color[1] = g_color[1];
    vertex->color[2] = g_color[2];
    vertex->color[3] = g_color[3];
}

/* Record a vertex with w = 1. */
void glVertex3f(GLfloat x, GLfloat y, GLfloat z)
{
    glVertex4f(x, y, z, 1);
}

/* Record a z = 0, w = 1 vertex. */
void glVertex2f(GLfloat x, GLfloat y)
{
    glVertex4f(x, y, 0, 1);
}

/* Start a primitive batch; the mode is one of the GL_POINTS..GL_QUADS set. */
void glBegin(GLenum mode)
{
    if (g_begin_active) {
        set_error(GL_INVALID_OPERATION);
        return;
    }
    g_begin_mode = mode;
    g_begin_active = 1;
    g_batch_count = 0;
}

static void put_pixel(int x, int y, unsigned short depth, unsigned int color)
{
    if (x < 0 || y < 0 || x >= g_width || y >= g_height)
        return;
    int index = y * g_width + x;
    if (g_depth_test && g_depth) {
        unsigned short old = g_depth[index];
        int keep;
        switch (g_depth_func) {
        case GL_NEVER:
            return;
        case GL_LEQUAL:
            keep = depth <= old;
            break;
        case GL_ALWAYS:
            keep = 1;
            break;
        case GL_LESS:
        default:
            keep = depth < old;
            break;
        }
        if (!keep)
            return;
        g_depth[index] = depth;
    }
    g_pixels[index] = color;
}

static void draw_point(const struct gl_vertex *vertex)
{
    if (vertex->clip[3] <= 0)
        return;
    float inv = 1.0f / vertex->clip[3];
    float x = vertex->clip[0] * inv;
    float y = vertex->clip[1] * inv;
    float z = vertex->clip[2] * inv;
    int sx = g_vp_x + (int)((x + 1) * 0.5f * g_vp_w);
    int sy = g_vp_y + (int)((1 - y) * 0.5f * g_vp_h);
    put_pixel(sx, sy, (unsigned short)((z + 1) * 0.5f * 65535.0f),
              pack_color(vertex->color));
}

static void draw_line(const struct gl_vertex *a, const struct gl_vertex *b)
{
    if (a->clip[3] <= 0 || b->clip[3] <= 0)
        return;
    float ax = a->clip[0] / a->clip[3];
    float ay = a->clip[1] / a->clip[3];
    float bx = b->clip[0] / b->clip[3];
    float by = b->clip[1] / b->clip[3];
    int x0 = g_vp_x + (int)((ax + 1) * 0.5f * g_vp_w);
    int y0 = g_vp_y + (int)((1 - ay) * 0.5f * g_vp_h);
    int x1 = g_vp_x + (int)((bx + 1) * 0.5f * g_vp_w);
    int y1 = g_vp_y + (int)((1 - by) * 0.5f * g_vp_h);
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y1 - y0 : y0 - y1;
    int steps = dx > dy ? dx : dy;
    if (steps == 0) {
        draw_point(a);
        return;
    }
    for (int i = 0; i <= steps; i++) {
        int x = x0 + (x1 - x0) * i / steps;
        int y = y0 + (y1 - y0) * i / steps;
        put_pixel(x, y, 0, pack_color(a->color));
    }
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (px - ax) * (by - ay) - (py - ay) * (bx - ax);
}

static void draw_triangle(const struct gl_vertex *va, const struct gl_vertex *vb,
                          const struct gl_vertex *vc)
{
    const struct gl_vertex *input[4] = {va, vb, vc, 0};
    int count = 0;

    /* Clip against the near plane z >= -w. */
    struct gl_vertex clipped[8];
    for (int i = 0; i < 3; i++) {
        const struct gl_vertex *a = input[i];
        const struct gl_vertex *b = input[(i + 1) % 3];
        float da = a->clip[2] + a->clip[3];
        float db = b->clip[2] + b->clip[3];
        if (da >= 0) {
            clipped[count++] = *a;
            if (db < 0) {
                float t = da / (da - db);
                struct gl_vertex *out = &clipped[count++];
                for (int k = 0; k < 4; k++)
                    out->clip[k] = a->clip[k] + (b->clip[k] - a->clip[k]) * t;
                for (int k = 0; k < 4; k++)
                    out->color[k] =
                        a->color[k] + (b->color[k] - a->color[k]) * t;
            }
        } else if (db >= 0) {
            float t = da / (da - db);
            struct gl_vertex *out = &clipped[count++];
            for (int k = 0; k < 4; k++)
                out->clip[k] = a->clip[k] + (b->clip[k] - a->clip[k]) * t;
            for (int k = 0; k < 4; k++)
                out->color[k] = a->color[k] + (b->color[k] - a->color[k]) * t;
        }
    }
    if (count < 3)
        return;
    int triangles = count - 2;

    for (int t = 0; t < triangles; t++) {
        const struct gl_vertex *a = &clipped[0];
        const struct gl_vertex *b = &clipped[t + 1];
        const struct gl_vertex *c = &clipped[t + 2];
        float ax = a->clip[0] / a->clip[3];
        float ay = a->clip[1] / a->clip[3];
        float az = a->clip[2] / a->clip[3];
        float bx = b->clip[0] / b->clip[3];
        float by = b->clip[1] / b->clip[3];
        float bz = b->clip[2] / b->clip[3];
        float cx = c->clip[0] / c->clip[3];
        float cy = c->clip[1] / c->clip[3];
        float cz = c->clip[2] / c->clip[3];

        int x0 = g_vp_x + (int)((ax + 1) * 0.5f * g_vp_w);
        int y0 = g_vp_y + (int)((1 - ay) * 0.5f * g_vp_h);
        int x1 = g_vp_x + (int)((bx + 1) * 0.5f * g_vp_w);
        int y1 = g_vp_y + (int)((1 - by) * 0.5f * g_vp_h);
        int x2 = g_vp_x + (int)((cx + 1) * 0.5f * g_vp_w);
        int y2 = g_vp_y + (int)((1 - cy) * 0.5f * g_vp_h);

        /* Work in window coordinates throughout.  The viewport Y flip turns a
         * CCW (OpenGL front) triangle into a positive-area one here. */
        float area = edge((float)x0, (float)y0, (float)x1, (float)y1,
                          (float)x2, (float)y2);
        if (area == 0)
            continue;
        if (g_cull && ((area > 0) != (g_front_face == GL_CCW)))
            continue;

        int minx = x0 < x1 ? (x0 < x2 ? x0 : x2) : (x1 < x2 ? x1 : x2);
        int maxx = x0 > x1 ? (x0 > x2 ? x0 : x2) : (x1 > x2 ? x1 : x2);
        int miny = y0 < y1 ? (y0 < y2 ? y0 : y2) : (y1 < y2 ? y1 : y2);
        int maxy = y0 > y1 ? (y0 > y2 ? y0 : y2) : (y1 > y2 ? y1 : y2);
        if (minx < 0) minx = 0;
        if (miny < 0) miny = 0;
        if (maxx >= g_width) maxx = g_width - 1;
        if (maxy >= g_height) maxy = g_height - 1;

        for (int y = miny; y <= maxy; y++) {
            for (int x = minx; x <= maxx; x++) {
                float px = x + 0.5f;
                float py = y + 0.5f;
                float w0 = edge((float)x1, (float)y1, (float)x2, (float)y2,
                                px, py);
                float w1 = edge((float)x2, (float)y2, (float)x0, (float)y0,
                                px, py);
                float w2 = edge((float)x0, (float)y0, (float)x1, (float)y1,
                                px, py);
                if (area > 0) {
                    if (w0 < 0 || w1 < 0 || w2 < 0)
                        continue;
                } else {
                    if (w0 > 0 || w1 > 0 || w2 > 0)
                        continue;
                }
                w0 /= area;
                w1 /= area;
                w2 /= area;
                float z = w0 * az + w1 * bz + w2 * cz;
                unsigned int color;
                if (g_shade == GL_FLAT) {
                    color = pack_color(a->color);
                } else {
                    float r = w0 * a->color[0] + w1 * b->color[0] +
                              w2 * c->color[0];
                    float g = w0 * a->color[1] + w1 * b->color[1] +
                              w2 * c->color[1];
                    float bl = w0 * a->color[2] + w1 * b->color[2] +
                               w2 * c->color[2];
                    float c4[4] = {r, g, bl, 1};
                    color = pack_color(c4);
                }
                put_pixel(x, y, (unsigned short)((z + 1) * 0.5f * 65535.0f),
                          color);
            }
        }
    }
}

/* Rasterize the recorded batch according to the glBegin mode. */
void glEnd(void)
{
    if (!g_begin_active) {
        set_error(GL_INVALID_OPERATION);
        return;
    }
    g_begin_active = 0;
    int count = g_batch_count;
    switch (g_begin_mode) {
    case GL_POINTS:
        for (int i = 0; i < count; i++)
            draw_point(&g_batch[i]);
        break;
    case GL_LINES:
        for (int i = 0; i + 1 < count; i += 2)
            draw_line(&g_batch[i], &g_batch[i + 1]);
        break;
    case GL_LINE_STRIP:
        for (int i = 0; i + 1 < count; i++)
            draw_line(&g_batch[i], &g_batch[i + 1]);
        break;
    case GL_LINE_LOOP:
        for (int i = 0; i + 1 < count; i++)
            draw_line(&g_batch[i], &g_batch[i + 1]);
        if (count > 2)
            draw_line(&g_batch[count - 1], &g_batch[0]);
        break;
    case GL_TRIANGLES:
        for (int i = 0; i + 2 < count; i += 3)
            draw_triangle(&g_batch[i], &g_batch[i + 1], &g_batch[i + 2]);
        break;
    case GL_TRIANGLE_STRIP:
        for (int i = 0; i + 2 < count; i++) {
            if (i & 1)
                draw_triangle(&g_batch[i + 1], &g_batch[i], &g_batch[i + 2]);
            else
                draw_triangle(&g_batch[i], &g_batch[i + 1], &g_batch[i + 2]);
        }
        break;
    case GL_TRIANGLE_FAN:
        for (int i = 1; i + 1 < count; i++)
            draw_triangle(&g_batch[0], &g_batch[i], &g_batch[i + 1]);
        break;
    case GL_QUADS:
        for (int i = 0; i + 3 < count; i += 4) {
            draw_triangle(&g_batch[i], &g_batch[i + 1], &g_batch[i + 2]);
            draw_triangle(&g_batch[i], &g_batch[i + 2], &g_batch[i + 3]);
        }
        break;
    default:
        set_error(GL_INVALID_ENUM);
        break;
    }
    g_batch_count = 0;
}

/* No queued commands exist, so finishing and flushing are no-ops. */
void glFinish(void) {}
void glFlush(void) {}

/* Return and clear the first error recorded since the last call. */
GLenum glGetError(void)
{
    GLenum error = g_error;
    g_error = GL_NO_ERROR;
    return error;
}

/* Read back RGBA bytes with the OpenGL bottom-left origin. */
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels)
{
    if (!g_pixels || format != GL_RGBA || type != GL_UNSIGNED_BYTE) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    GLubyte *out = pixels;
    for (int row = 0; row < height; row++) {
        int source_y = g_height - 1 - (y + row);
        for (int column = 0; column < width; column++) {
            int source_x = x + column;
            unsigned int color = 0;
            if (source_x >= 0 && source_x < g_width && source_y >= 0 &&
                source_y < g_height)
                color = g_pixels[source_y * g_width + source_x];
            out[(row * width + column) * 4 + 0] =
                (GLubyte)((color >> 16) & 0xFF);
            out[(row * width + column) * 4 + 1] =
                (GLubyte)((color >> 8) & 0xFF);
            out[(row * width + column) * 4 + 2] = (GLubyte)(color & 0xFF);
            out[(row * width + column) * 4 + 3] = 0xFF;
        }
    }
}
