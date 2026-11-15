#ifndef ZHAOXU_GL_H
#define ZHAOXU_GL_H

/* Zhaoxu OS - OpenGL 1.1 immediate-mode subset.
 *
 * This is a real software rasterizer for the classic fixed-function calls a
 * desktop demo needs: matrix stacks, glBegin/glEnd, perspective/ortho
 * projection, Gouraud colour, depth test and back-face culling.  It is not a
 * complete OpenGL implementation; extensions, lighting, textures, display
 * lists, evaluators and shaders are outside this subset. */

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef float GLclampf;
typedef unsigned char GLubyte;

#define GL_FALSE 0
#define GL_TRUE 1

#define GL_POINTS 0x0000
#define GL_LINES 0x0001
#define GL_LINE_LOOP 0x0002
#define GL_LINE_STRIP 0x0003
#define GL_TRIANGLES 0x0004
#define GL_TRIANGLE_STRIP 0x0005
#define GL_TRIANGLE_FAN 0x0006
#define GL_QUADS 0x0007

#define GL_MODELVIEW 0x1700
#define GL_PROJECTION 0x1701
#define GL_TEXTURE 0x1702

#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100

#define GL_DEPTH_TEST 0x0B71
#define GL_CULL_FACE 0x0B44
#define GL_BACK 0x0405
#define GL_FRONT 0x0404
#define GL_FRONT_AND_BACK 0x0408
#define GL_CCW 0x0901
#define GL_CW 0x0900
#define GL_SMOOTH 0x1D01
#define GL_FLAT 0x1D00
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_NEVER 0x0200
#define GL_ALWAYS 0x0207

#define GL_NO_ERROR 0
#define GL_INVALID_ENUM 0x0500
#define GL_INVALID_VALUE 0x0501
#define GL_INVALID_OPERATION 0x0502
#define GL_OUT_OF_MEMORY 0x0505

#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406

#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_REPEAT 0x2901
#define GL_CLAMP 0x2900
#define GL_TEXTURE_ENV 0x2300
#define GL_TEXTURE_ENV_MODE 0x2200
#define GL_MODULATE 0x2100
#define GL_DECAL 0x2101
#define GL_REPLACE 0x1E01
#define GL_ADD 0x0104

#define GL_LIGHTING 0x0B50
#define GL_LIGHT0 0x4000
#define GL_LIGHT1 0x4001
#define GL_LIGHT2 0x4002
#define GL_LIGHT3 0x4003
#define GL_LIGHT4 0x4004
#define GL_LIGHT5 0x4005
#define GL_LIGHT6 0x4006
#define GL_LIGHT7 0x4007
#define GL_POSITION 0x1203
#define GL_AMBIENT 0x1200
#define GL_DIFFUSE 0x1201
#define GL_SPECULAR 0x1202
#define GL_EMISSION 0x1600
#define GL_SHININESS 0x1601
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_LIGHT_MODEL_AMBIENT 0x0B53
#define GL_NORMALIZE 0x0BA1

#define GL_BLEND 0x0BE2
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0

#define GL_ALPHA_TEST 0x0BC0
#define GL_GREATER 0x0204
#define GL_GEQUAL 0x0206
#define GL_SCISSOR_TEST 0x0C11

/* Bind a 0xAARRGGBB framebuffer.  Width and height are in pixels; the depth
 * buffer is allocated by the library. */
void zosgl_attach(unsigned int *pixels, int width, int height);
void zosgl_detach(void);

void glClearColor(GLclampf red, GLclampf green, GLclampf blue,
                  GLclampf alpha);
void glClear(GLbitfield mask);
void glViewport(GLint x, GLint y, GLsizei width, GLsizei height);
void glEnable(GLenum capability);
void glDisable(GLenum capability);
void glDepthFunc(GLenum function);
void glCullFace(GLenum mode);
void glFrontFace(GLenum mode);
void glShadeModel(GLenum mode);

void glMatrixMode(GLenum mode);
void glLoadIdentity(void);
void glPushMatrix(void);
void glPopMatrix(void);
void glLoadMatrixf(const GLfloat *matrix);
void glMultMatrixf(const GLfloat *matrix);
void glTranslatef(GLfloat x, GLfloat y, GLfloat z);
void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
void glScalef(GLfloat x, GLfloat y, GLfloat z);
void glFrustum(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
               GLfloat near_plane, GLfloat far_plane);
void glOrtho(GLfloat left, GLfloat right, GLfloat bottom, GLfloat top,
             GLfloat near_plane, GLfloat far_plane);

void glBegin(GLenum mode);
void glEnd(void);
void glColor3f(GLfloat red, GLfloat green, GLfloat blue);
void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
void glVertex2f(GLfloat x, GLfloat y);
void glVertex3f(GLfloat x, GLfloat y, GLfloat z);
void glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w);

/* Textures.  GL_TEXTURE_2D supports RGBA/RGB unsigned-byte images, nearest or
 * linear filtering and repeat/clamp wrapping. */
void glGenTextures(GLsizei count, GLuint *textures);
void glBindTexture(GLenum target, GLuint texture);
void glDeleteTextures(GLsizei count, const GLuint *textures);
void glTexImage2D(GLenum target, GLint level, GLint internal_format,
                  GLsizei width, GLsizei height, GLint border, GLenum format,
                  GLenum type, const void *pixels);
void glTexParameteri(GLenum target, GLenum name, GLint value);
void glTexEnvi(GLenum target, GLenum name, GLint value);
void glTexCoord2f(GLfloat s, GLfloat t);

/* Per-vertex lighting for GL_LIGHT0..7.  Attenuation and spot cones are not
 * implemented; positions are stored in eye space when set. */
void glNormal3f(GLfloat x, GLfloat y, GLfloat z);
void glLightfv(GLenum light, GLenum name, const GLfloat *params);
void glLightModelfv(GLenum name, const GLfloat *params);
void glMaterialfv(GLenum face, GLenum name, const GLfloat *params);

/* Alpha blending, alpha test and scissor clipping. */
void glBlendFunc(GLenum source, GLenum destination);
void glAlphaFunc(GLenum function, GLclampf reference);
void glScissor(GLint x, GLint y, GLsizei width, GLsizei height);

void glFinish(void);
void glFlush(void);
GLenum glGetError(void);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels);

#endif
