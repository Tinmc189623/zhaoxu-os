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
    float texcoord[2];
    float invw;
};

#define GL_MAX_TEXTURES 32
#define GL_MAX_TEXTURE_DIM 512
#define GL_MAX_LIGHTS 8

struct gl_texture {
    int used;
    int width;
    int height;
    unsigned int *pixels;
    GLenum min_filter;
    GLenum mag_filter;
    GLenum wrap_s;
    GLenum wrap_t;
};

struct gl_light {
    int enabled;
    float position[4];
    float ambient[4];
    float diffuse[4];
    float specular[4];
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

static struct gl_texture g_textures[GL_MAX_TEXTURES];
static GLuint g_next_texture = 1;
static GLuint g_bound_texture;
static int g_texture_2d;
static GLenum g_texenv = GL_MODULATE;
static GLfloat g_texcoord[2];
static GLfloat g_normal[3] = {0, 0, 1};

static struct gl_light g_lights[GL_MAX_LIGHTS];
static GLfloat g_light_model_ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
static GLfloat g_material_ambient[4] = {0.2f, 0.2f, 0.2f, 1.0f};
static GLfloat g_material_diffuse[4] = {0.8f, 0.8f, 0.8f, 1.0f};
static GLfloat g_material_specular[4] = {0, 0, 0, 1};
static GLfloat g_material_emission[4] = {0, 0, 0, 1};
static GLfloat g_material_shininess;
static int g_lighting;

static int g_blend;
static GLenum g_blend_src = GL_ONE;
static GLenum g_blend_dst = GL_ZERO;
static int g_alpha_test;
static GLenum g_alpha_func = GL_ALWAYS;
static GLfloat g_alpha_reference;
static int g_scissor_test;
static GLint g_scissor_x;
static GLint g_scissor_y;
static GLsizei g_scissor_w;
static GLsizei g_scissor_h;

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

/* Floor for texture wrapping without libm. */
static float gl_floor(float x)
{
    int truncated = (int)x;
    if ((float)truncated > x)
        truncated--;
    return (float)truncated;
}

/* Integer-power helper for the specular exponent. */
static float gl_pow(float base, float exponent)
{
    int count = (int)exponent;
    float result = 1;
    while (count-- > 0)
        result *= base;
    return result;
}

/* Normalise a three-component vector in place. */
static void normalize3(float v[3])
{
    float length = gl_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length == 0)
        return;
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
}

/* Find a texture object by name, or NULL for 0/missing names. */
static struct gl_texture *texture_lookup(GLuint name)
{
    if (name == 0 || name > GL_MAX_TEXTURES)
        return 0;
    struct gl_texture *texture = &g_textures[name - 1];
    return texture->used ? texture : 0;
}

/* Wrap a texture coordinate and map it to an integer texel index. */
static int wrap_index(float coordinate, int size, GLenum wrap)
{
    if (wrap == GL_REPEAT) {
        coordinate -= gl_floor(coordinate);
    } else {
        if (coordinate < 0)
            coordinate = 0;
        if (coordinate > 0.999999f)
            coordinate = 0.999999f;
    }
    int index = (int)(coordinate * (float)size);
    if (index < 0)
        index = 0;
    if (index >= size)
        index = size - 1;
    return index;
}

/* Fetch one texel as floating RGBA. */
static void texture_texel(const struct gl_texture *texture, int x, int y,
                          float out[4])
{
    unsigned int color = texture->pixels[y * texture->width + x];
    out[0] = (float)((color >> 16) & 0xFF) / 255.0f;
    out[1] = (float)((color >> 8) & 0xFF) / 255.0f;
    out[2] = (float)(color & 0xFF) / 255.0f;
    out[3] = (float)((color >> 24) & 0xFF) / 255.0f;
}

/* Sample the bound 2D texture with nearest or bilinear filtering. */
static void sample_texture(float s, float t, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 1;
    struct gl_texture *texture = texture_lookup(g_bound_texture);
    if (!g_texture_2d || !texture || !texture->pixels)
        return;

    if (texture->mag_filter != GL_LINEAR) {
        int x = wrap_index(s, texture->width, texture->wrap_s);
        int y = wrap_index(t, texture->height, texture->wrap_t);
        texture_texel(texture, x, y, out);
        return;
    }

    float u = s * (float)texture->width - 0.5f;
    float v = t * (float)texture->height - 0.5f;
    int x0 = (int)gl_floor(u);
    int y0 = (int)gl_floor(v);
    float fx = u - (float)x0;
    float fy = v - (float)y0;
    int x1 = x0 + 1;
    int y1 = y0 + 1;
    if (texture->wrap_s == GL_REPEAT) {
        x0 = ((x0 % texture->width) + texture->width) % texture->width;
        x1 = ((x1 % texture->width) + texture->width) % texture->width;
    } else {
        if (x0 < 0) x0 = 0;
        if (x1 >= texture->width) x1 = texture->width - 1;
    }
    if (texture->wrap_t == GL_REPEAT) {
        y0 = ((y0 % texture->height) + texture->height) % texture->height;
        y1 = ((y1 % texture->height) + texture->height) % texture->height;
    } else {
        if (y0 < 0) y0 = 0;
        if (y1 >= texture->height) y1 = texture->height - 1;
    }
    float c00[4], c10[4], c01[4], c11[4];
    texture_texel(texture, x0, y0, c00);
    texture_texel(texture, x1, y0, c10);
    texture_texel(texture, x0, y1, c01);
    texture_texel(texture, x1, y1, c11);
    for (int i = 0; i < 4; i++) {
        float top = c00[i] + (c10[i] - c00[i]) * fx;
        float bottom = c01[i] + (c11[i] - c01[i]) * fx;
        out[i] = top + (bottom - top) * fy;
    }
}

/* Evaluate the fixed-function lighting equation for one vertex. */
static void shade_vertex(const GLfloat eye[4], const GLfloat normal_object[3],
                         float out[4])
{
    GLfloat *mv = g_modelview[g_mv_top];
    float normal[3];
    normal[0] = mv[0] * normal_object[0] + mv[4] * normal_object[1] +
                mv[8] * normal_object[2];
    normal[1] = mv[1] * normal_object[0] + mv[5] * normal_object[1] +
                mv[9] * normal_object[2];
    normal[2] = mv[2] * normal_object[0] + mv[6] * normal_object[1] +
                mv[10] * normal_object[2];
    normalize3(normal);

    float view[3] = {-eye[0], -eye[1], -eye[2]};
    normalize3(view);

    float result[4];
    for (int i = 0; i < 4; i++)
        result[i] = g_material_emission[i] +
                    g_material_ambient[i] * g_light_model_ambient[i];

    for (int i = 0; i < GL_MAX_LIGHTS; i++) {
        if (!g_lights[i].enabled)
            continue;
        float direction[3];
        if (g_lights[i].position[3] != 0) {
            direction[0] = g_lights[i].position[0] - eye[0];
            direction[1] = g_lights[i].position[1] - eye[1];
            direction[2] = g_lights[i].position[2] - eye[2];
        } else {
            direction[0] = g_lights[i].position[0];
            direction[1] = g_lights[i].position[1];
            direction[2] = g_lights[i].position[2];
        }
        normalize3(direction);
        float diffuse = normal[0] * direction[0] + normal[1] * direction[1] +
                        normal[2] * direction[2];
        if (diffuse < 0)
            diffuse = 0;

        float half[3] = {direction[0] + view[0], direction[1] + view[1],
                         direction[2] + view[2]};
        normalize3(half);
        float specular = normal[0] * half[0] + normal[1] * half[1] +
                         normal[2] * half[2];
        if (specular < 0)
            specular = 0;
        if (diffuse <= 0)
            specular = 0;
        else
            specular = gl_pow(specular, g_material_shininess);

        for (int c = 0; c < 4; c++) {
            result[c] += g_material_ambient[c] * g_lights[i].ambient[c] +
                         g_material_diffuse[c] * g_lights[i].diffuse[c] *
                             diffuse +
                         g_material_specular[c] * g_lights[i].specular[c] *
                             specular;
        }
    }

    for (int i = 0; i < 4; i++) {
        if (result[i] < 0)
            result[i] = 0;
        if (result[i] > 1)
            result[i] = 1;
        out[i] = result[i];
    }
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

    g_texture_2d = 0;
    g_bound_texture = 0;
    g_texenv = GL_MODULATE;
    g_texcoord[0] = g_texcoord[1] = 0;
    g_normal[0] = g_normal[1] = 0;
    g_normal[2] = 1;
    g_lighting = 0;
    memset(g_lights, 0, sizeof(g_lights));
    g_lights[0].diffuse[0] = g_lights[0].diffuse[1] = 1;
    g_lights[0].diffuse[2] = 1;
    g_lights[0].diffuse[3] = 1;
    g_lights[0].specular[0] = g_lights[0].specular[1] = 1;
    g_lights[0].specular[2] = 1;
    g_lights[0].specular[3] = 1;
    g_lights[0].position[2] = 1;
    g_light_model_ambient[0] = 0.2f;
    g_light_model_ambient[1] = 0.2f;
    g_light_model_ambient[2] = 0.2f;
    g_light_model_ambient[3] = 1.0f;
    g_material_ambient[0] = g_material_ambient[1] = 0.2f;
    g_material_ambient[2] = 0.2f;
    g_material_ambient[3] = 1.0f;
    g_material_diffuse[0] = g_material_diffuse[1] = 0.8f;
    g_material_diffuse[2] = 0.8f;
    g_material_diffuse[3] = 1.0f;
    g_material_specular[0] = g_material_specular[1] = 0;
    g_material_specular[2] = 0;
    g_material_specular[3] = 1.0f;
    g_material_emission[0] = g_material_emission[1] = 0;
    g_material_emission[2] = 0;
    g_material_emission[3] = 1.0f;
    g_material_shininess = 0;
    g_blend = 0;
    g_blend_src = GL_ONE;
    g_blend_dst = GL_ZERO;
    g_alpha_test = 0;
    g_alpha_func = GL_ALWAYS;
    g_alpha_reference = 0;
    g_scissor_test = 0;
    g_scissor_x = 0;
    g_scissor_y = 0;
    g_scissor_w = width;
    g_scissor_h = height;
}

/* Release the depth buffer and stop drawing. */
/* Release the depth buffer and every uploaded texture. */
void zosgl_detach(void)
{
    free(g_depth);
    g_depth = 0;
    for (int i = 0; i < GL_MAX_TEXTURES; i++) {
        free(g_textures[i].pixels);
        memset(&g_textures[i], 0, sizeof(g_textures[i]));
    }
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
    int min_x = 0, min_y = 0, max_x = g_width, max_y = g_height;
    if (g_scissor_test) {
        min_x = g_scissor_x;
        min_y = g_scissor_y;
        max_x = g_scissor_x + g_scissor_w;
        max_y = g_scissor_y + g_scissor_h;
        if (min_x < 0) min_x = 0;
        if (min_y < 0) min_y = 0;
        if (max_x > g_width) max_x = g_width;
        if (max_y > g_height) max_y = g_height;
    }
    unsigned int color = pack_color(g_clear);
    for (int y = min_y; y < max_y; y++) {
        for (int x = min_x; x < max_x; x++) {
            if (mask & GL_COLOR_BUFFER_BIT)
                g_pixels[y * g_width + x] = color;
            if ((mask & GL_DEPTH_BUFFER_BIT) && g_depth)
                g_depth[y * g_width + x] = 0xFFFF;
        }
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

/* Enable one fixed-function capability, including lights 0..7. */
void glEnable(GLenum capability)
{
    if (capability == GL_DEPTH_TEST)
        g_depth_test = 1;
    else if (capability == GL_CULL_FACE)
        g_cull = 1;
    else if (capability == GL_TEXTURE_2D)
        g_texture_2d = 1;
    else if (capability == GL_LIGHTING)
        g_lighting = 1;
    else if (capability == GL_BLEND)
        g_blend = 1;
    else if (capability == GL_ALPHA_TEST)
        g_alpha_test = 1;
    else if (capability == GL_SCISSOR_TEST)
        g_scissor_test = 1;
    else if (capability >= GL_LIGHT0 && capability <= GL_LIGHT7)
        g_lights[capability - GL_LIGHT0].enabled = 1;
    else
        set_error(GL_INVALID_ENUM);
}

/* Disable one fixed-function capability. */
void glDisable(GLenum capability)
{
    if (capability == GL_DEPTH_TEST)
        g_depth_test = 0;
    else if (capability == GL_CULL_FACE)
        g_cull = 0;
    else if (capability == GL_TEXTURE_2D)
        g_texture_2d = 0;
    else if (capability == GL_LIGHTING)
        g_lighting = 0;
    else if (capability == GL_BLEND)
        g_blend = 0;
    else if (capability == GL_ALPHA_TEST)
        g_alpha_test = 0;
    else if (capability == GL_SCISSOR_TEST)
        g_scissor_test = 0;
    else if (capability >= GL_LIGHT0 && capability <= GL_LIGHT7)
        g_lights[capability - GL_LIGHT0].enabled = 0;
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

/* Allocate texture names and mark their objects usable. */
void glGenTextures(GLsizei count, GLuint *textures)
{
    if (!textures || count < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        while (g_next_texture <= GL_MAX_TEXTURES &&
               g_textures[g_next_texture - 1].used)
            g_next_texture++;
        if (g_next_texture > GL_MAX_TEXTURES) {
            textures[i] = 0;
            continue;
        }
        struct gl_texture *texture = &g_textures[g_next_texture - 1];
        memset(texture, 0, sizeof(*texture));
        texture->used = 1;
        texture->min_filter = GL_NEAREST;
        texture->mag_filter = GL_NEAREST;
        texture->wrap_s = GL_REPEAT;
        texture->wrap_t = GL_REPEAT;
        textures[i] = g_next_texture++;
    }
}

/* Bind a texture name to the 2D target; 0 restores the untextured state. */
void glBindTexture(GLenum target, GLuint texture)
{
    if (target != GL_TEXTURE_2D) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    if (texture > GL_MAX_TEXTURES) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    if (texture != 0) {
        struct gl_texture *object = &g_textures[texture - 1];
        if (!object->used) {
            memset(object, 0, sizeof(*object));
            object->used = 1;
            object->min_filter = GL_NEAREST;
            object->mag_filter = GL_NEAREST;
            object->wrap_s = GL_REPEAT;
            object->wrap_t = GL_REPEAT;
        }
    }
    g_bound_texture = texture;
}

/* Free texture objects and their image storage. */
void glDeleteTextures(GLsizei count, const GLuint *textures)
{
    if (!textures || count < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    for (GLsizei i = 0; i < count; i++) {
        GLuint name = textures[i];
        if (name == 0 || name > GL_MAX_TEXTURES)
            continue;
        struct gl_texture *texture = &g_textures[name - 1];
        free(texture->pixels);
        memset(texture, 0, sizeof(*texture));
        if (g_bound_texture == name)
            g_bound_texture = 0;
    }
}

/* Upload an RGBA/RGB unsigned-byte image into the bound texture. */
void glTexImage2D(GLenum target, GLint level, GLint internal_format,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void *pixels)
{
    if (target != GL_TEXTURE_2D || level != 0 || border != 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    if (width <= 0 || height <= 0 || width > GL_MAX_TEXTURE_DIM ||
        height > GL_MAX_TEXTURE_DIM) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    if ((format != GL_RGBA && format != GL_RGB) ||
        (internal_format != GL_RGBA && internal_format != GL_RGB) ||
        type != GL_UNSIGNED_BYTE) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    struct gl_texture *texture = texture_lookup(g_bound_texture);
    if (!texture) {
        set_error(GL_INVALID_OPERATION);
        return;
    }
    unsigned long count = (unsigned long)width * (unsigned long)height;
    unsigned int *image = malloc(count * sizeof(unsigned int));
    if (!image) {
        set_error(GL_OUT_OF_MEMORY);
        return;
    }
    const unsigned char *source = pixels;
    for (unsigned long i = 0; i < count; i++) {
        unsigned int red = 0, green = 0, blue = 0, alpha = 255;
        if (source) {
            if (format == GL_RGBA) {
                red = source[i * 4 + 0];
                green = source[i * 4 + 1];
                blue = source[i * 4 + 2];
                alpha = source[i * 4 + 3];
            } else {
                red = source[i * 3 + 0];
                green = source[i * 3 + 1];
                blue = source[i * 3 + 2];
            }
        }
        image[i] = (alpha << 24) | (red << 16) | (green << 8) | blue;
    }
    free(texture->pixels);
    texture->pixels = image;
    texture->width = width;
    texture->height = height;
}

/* Set the filter or wrap mode of the bound texture. */
void glTexParameteri(GLenum target, GLenum name, GLint value)
{
    if (target != GL_TEXTURE_2D) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    struct gl_texture *texture = texture_lookup(g_bound_texture);
    if (!texture) {
        set_error(GL_INVALID_OPERATION);
        return;
    }
    if (name == GL_TEXTURE_MIN_FILTER || name == GL_TEXTURE_MAG_FILTER) {
        if (value != GL_NEAREST && value != GL_LINEAR) {
            set_error(GL_INVALID_ENUM);
            return;
        }
        if (name == GL_TEXTURE_MIN_FILTER)
            texture->min_filter = (GLenum)value;
        else
            texture->mag_filter = (GLenum)value;
    } else if (name == GL_TEXTURE_WRAP_S || name == GL_TEXTURE_WRAP_T) {
        if (value != GL_REPEAT && value != GL_CLAMP) {
            set_error(GL_INVALID_ENUM);
            return;
        }
        if (name == GL_TEXTURE_WRAP_S)
            texture->wrap_s = (GLenum)value;
        else
            texture->wrap_t = (GLenum)value;
    } else {
        set_error(GL_INVALID_ENUM);
    }
}

/* Select how texture colour combines with the vertex colour. */
void glTexEnvi(GLenum target, GLenum name, GLint value)
{
    if (target != GL_TEXTURE_ENV || name != GL_TEXTURE_ENV_MODE ||
        (value != GL_MODULATE && value != GL_DECAL && value != GL_REPLACE &&
         value != GL_BLEND)) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    g_texenv = (GLenum)value;
}

/* Set the texture coordinate used by the next vertex. */
void glTexCoord2f(GLfloat s, GLfloat t)
{
    g_texcoord[0] = s;
    g_texcoord[1] = t;
}

/* Set the normal used by the next lit vertex. */
void glNormal3f(GLfloat x, GLfloat y, GLfloat z)
{
    g_normal[0] = x;
    g_normal[1] = y;
    g_normal[2] = z;
}

/* Set one light parameter; GL_POSITION is transformed into eye space. */
void glLightfv(GLenum light, GLenum name, const GLfloat *params)
{
    if (light < GL_LIGHT0 || light > GL_LIGHT7 || !params) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    struct gl_light *object = &g_lights[light - GL_LIGHT0];
    if (name == GL_POSITION) {
        GLfloat *mv = g_modelview[g_mv_top];
        for (int row = 0; row < 4; row++)
            object->position[row] =
                mv[0 * 4 + row] * params[0] + mv[1 * 4 + row] * params[1] +
                mv[2 * 4 + row] * params[2] + mv[3 * 4 + row] * params[3];
    } else if (name == GL_AMBIENT) {
        for (int i = 0; i < 4; i++)
            object->ambient[i] = params[i];
    } else if (name == GL_DIFFUSE) {
        for (int i = 0; i < 4; i++)
            object->diffuse[i] = params[i];
    } else if (name == GL_SPECULAR) {
        for (int i = 0; i < 4; i++)
            object->specular[i] = params[i];
    } else {
        set_error(GL_INVALID_ENUM);
    }
}

/* Set the global ambient term used by lighting. */
void glLightModelfv(GLenum name, const GLfloat *params)
{
    if (name != GL_LIGHT_MODEL_AMBIENT || !params) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    for (int i = 0; i < 4; i++)
        g_light_model_ambient[i] = params[i];
}

/* Set front/back material properties; both faces share one material. */
void glMaterialfv(GLenum face, GLenum name, const GLfloat *params)
{
    (void)face;
    if (!params) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    if (name == GL_AMBIENT || name == GL_AMBIENT_AND_DIFFUSE) {
        for (int i = 0; i < 4; i++)
            g_material_ambient[i] = params[i];
    }
    if (name == GL_DIFFUSE || name == GL_AMBIENT_AND_DIFFUSE) {
        for (int i = 0; i < 4; i++)
            g_material_diffuse[i] = params[i];
    } else if (name == GL_SPECULAR) {
        for (int i = 0; i < 4; i++)
            g_material_specular[i] = params[i];
    } else if (name == GL_EMISSION) {
        for (int i = 0; i < 4; i++)
            g_material_emission[i] = params[i];
    } else if (name == GL_SHININESS) {
        g_material_shininess = params[0];
        if (g_material_shininess < 0)
            g_material_shininess = 0;
        if (g_material_shininess > 128)
            g_material_shininess = 128;
    } else if (name != GL_AMBIENT && name != GL_AMBIENT_AND_DIFFUSE &&
               name != GL_DIFFUSE) {
        set_error(GL_INVALID_ENUM);
    }
}

/* Select source and destination alpha-blend factors. */
void glBlendFunc(GLenum source, GLenum destination)
{
    GLenum accepted[4] = {GL_ZERO, GL_ONE, GL_SRC_ALPHA,
                          GL_ONE_MINUS_SRC_ALPHA};
    int source_ok = 0, destination_ok = 0;
    for (int i = 0; i < 4; i++) {
        if (source == accepted[i])
            source_ok = 1;
        if (destination == accepted[i])
            destination_ok = 1;
    }
    if (!source_ok || !destination_ok) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    g_blend_src = source;
    g_blend_dst = destination;
}

/* Set the alpha-test comparison used before writing a fragment. */
void glAlphaFunc(GLenum function, GLclampf reference)
{
    if (function != GL_NEVER && function != GL_LESS &&
        function != GL_GREATER && function != GL_GEQUAL &&
        function != GL_ALWAYS) {
        set_error(GL_INVALID_ENUM);
        return;
    }
    g_alpha_func = function;
    g_alpha_reference = reference;
}

/* Set the scissor rectangle in window coordinates. */
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height)
{
    if (width < 0 || height < 0) {
        set_error(GL_INVALID_VALUE);
        return;
    }
    g_scissor_x = x;
    /* OpenGL scissor Y is measured from the bottom; put_pixel is top-down. */
    g_scissor_y = g_height - (y + height);
    g_scissor_w = width;
    g_scissor_h = height;
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
    if (g_lighting)
        shade_vertex(eye, g_normal, vertex->color);
    else {
        vertex->color[0] = g_color[0];
        vertex->color[1] = g_color[1];
        vertex->color[2] = g_color[2];
        vertex->color[3] = g_color[3];
    }
    vertex->texcoord[0] = g_texcoord[0];
    vertex->texcoord[1] = g_texcoord[1];
    vertex->invw = vertex->clip[3] != 0 ? 1.0f / vertex->clip[3] : 1.0f;
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

/* Blend factor lookup for the two supported source/destination modes. */
static float blend_factor(GLenum factor, float alpha)
{
    if (factor == GL_SRC_ALPHA)
        return alpha;
    if (factor == GL_ONE_MINUS_SRC_ALPHA)
        return 1 - alpha;
    if (factor == GL_ONE)
        return 1;
    return 0;
}

/* Depth test, scissor, alpha test, blending and the final colour write. */
static void put_pixel(int x, int y, unsigned short depth, const float rgba[4])
{
    if (x < 0 || y < 0 || x >= g_width || y >= g_height)
        return;
    if (g_scissor_test &&
        (x < g_scissor_x || y < g_scissor_y ||
         x >= g_scissor_x + g_scissor_w || y >= g_scissor_y + g_scissor_h))
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

    float source[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    if (g_alpha_test) {
        int pass;
        switch (g_alpha_func) {
        case GL_NEVER:
            pass = 0;
            break;
        case GL_LESS:
            pass = source[3] < g_alpha_reference;
            break;
        case GL_GREATER:
            pass = source[3] > g_alpha_reference;
            break;
        case GL_GEQUAL:
            pass = source[3] >= g_alpha_reference;
            break;
        case GL_ALWAYS:
        default:
            pass = 1;
            break;
        }
        if (!pass)
            return;
    }

    if (g_blend) {
        unsigned int destination = g_pixels[index];
        float dst[4];
        dst[0] = (float)((destination >> 16) & 0xFF) / 255.0f;
        dst[1] = (float)((destination >> 8) & 0xFF) / 255.0f;
        dst[2] = (float)(destination & 0xFF) / 255.0f;
        dst[3] = (float)((destination >> 24) & 0xFF) / 255.0f;
        float src_factor = blend_factor(g_blend_src, source[3]);
        float dst_factor = blend_factor(g_blend_dst, source[3]);
        for (int i = 0; i < 3; i++)
            source[i] = source[i] * src_factor + dst[i] * dst_factor;
        source[3] = source[3] + dst[3] * (1 - source[3]);
    }

    g_pixels[index] = pack_color(source);
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
              vertex->color);
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
        put_pixel(x, y, 0, a->color);
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
                for (int k = 0; k < 2; k++)
                    out->texcoord[k] = a->texcoord[k] +
                                       (b->texcoord[k] - a->texcoord[k]) * t;
                out->invw = out->clip[3] != 0 ? 1.0f / out->clip[3] : 1.0f;
            }
        } else if (db >= 0) {
            float t = da / (da - db);
            struct gl_vertex *out = &clipped[count++];
            for (int k = 0; k < 4; k++)
                out->clip[k] = a->clip[k] + (b->clip[k] - a->clip[k]) * t;
            for (int k = 0; k < 4; k++)
                out->color[k] = a->color[k] + (b->color[k] - a->color[k]) * t;
            for (int k = 0; k < 2; k++)
                out->texcoord[k] =
                    a->texcoord[k] + (b->texcoord[k] - a->texcoord[k]) * t;
            out->invw = out->clip[3] != 0 ? 1.0f / out->clip[3] : 1.0f;
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
                float color[4];
                if (g_shade == GL_FLAT) {
                    for (int k = 0; k < 4; k++)
                        color[k] = a->color[k];
                } else {
                    for (int k = 0; k < 4; k++)
                        color[k] = w0 * a->color[k] + w1 * b->color[k] +
                                   w2 * c->color[k];
                }

                if (g_texture_2d) {
                    float invw = w0 * a->invw + w1 * b->invw + w2 * c->invw;
                    if (invw == 0)
                        invw = 1;
                    float u = (w0 * a->texcoord[0] * a->invw +
                               w1 * b->texcoord[0] * b->invw +
                               w2 * c->texcoord[0] * c->invw) /
                              invw;
                    float v = (w0 * a->texcoord[1] * a->invw +
                               w1 * b->texcoord[1] * b->invw +
                               w2 * c->texcoord[1] * c->invw) /
                              invw;
                    float texel[4];
                    sample_texture(u, v, texel);
                    if (g_texenv == GL_REPLACE) {
                        for (int k = 0; k < 4; k++)
                            color[k] = texel[k];
                    } else if (g_texenv == GL_DECAL) {
                        for (int k = 0; k < 3; k++)
                            color[k] = color[k] * (1 - texel[3]) +
                                       texel[k] * texel[3];
                    } else {
                        for (int k = 0; k < 4; k++)
                            color[k] *= texel[k];
                    }
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
