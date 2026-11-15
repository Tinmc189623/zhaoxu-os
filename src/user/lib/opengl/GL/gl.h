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

#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401

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

void glFinish(void);
void glFlush(void);
GLenum glGetError(void);
void glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                  GLenum format, GLenum type, void *pixels);

#endif
