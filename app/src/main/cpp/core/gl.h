// Platform GL include. Android uses the NDK's real GLES3 headers.
// The desktop preview build (headless Mesa) uses the minimal declarations below.
#pragma once

#if defined(__ANDROID__)
#include <GLES3/gl3.h>
#include <EGL/egl.h>
#else
#include <stdint.h>
#include <stddef.h>
extern "C" {
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef int GLsizei;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef char GLchar;
typedef unsigned char GLubyte;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_ZERO 0
#define GL_ONE 1
#define GL_LINES 0x0001
#define GL_TRIANGLES 0x0004
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_LESS 0x0201
#define GL_LEQUAL 0x0203
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_CW 0x0900
#define GL_CCW 0x0901
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RED 0x1903
#define GL_RGB 0x1907
#define GL_RGBA 0x1908
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_LINEAR_MIPMAP_LINEAR 0x2703
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_REPEAT 0x2901
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_RGB8 0x8051
#define GL_RGBA8 0x8058
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_DEPTH_COMPONENT16 0x81A5
#define GL_DEPTH_COMPONENT24 0x81A6
#define GL_R8 0x8229
#define GL_TEXTURE0 0x84C0
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STREAM_DRAW 0x88E0
#define GL_STATIC_DRAW 0x88E4
#define GL_DYNAMIC_DRAW 0x88E8
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_DRAW_FRAMEBUFFER 0x8CA9
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_DEPTH_ATTACHMENT 0x8D00
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_MAX_SAMPLES 0x8D57

void glActiveTexture(GLenum);
void glAttachShader(GLuint, GLuint);
void glBindAttribLocation(GLuint, GLuint, const GLchar*);
void glBindBuffer(GLenum, GLuint);
void glBindFramebuffer(GLenum, GLuint);
void glBindRenderbuffer(GLenum, GLuint);
void glBindTexture(GLenum, GLuint);
void glBindVertexArray(GLuint);
void glBlendFunc(GLenum, GLenum);
void glBlitFramebuffer(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
void glBufferData(GLenum, GLsizeiptr, const void*, GLenum);
void glBufferSubData(GLenum, GLintptr, GLsizeiptr, const void*);
GLenum glCheckFramebufferStatus(GLenum);
void glClear(GLbitfield);
void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
void glCompileShader(GLuint);
GLuint glCreateProgram(void);
GLuint glCreateShader(GLenum);
void glCullFace(GLenum);
void glDeleteBuffers(GLsizei, const GLuint*);
void glDeleteFramebuffers(GLsizei, const GLuint*);
void glDeleteRenderbuffers(GLsizei, const GLuint*);
void glDeleteShader(GLuint);
void glDeleteTextures(GLsizei, const GLuint*);
void glDeleteVertexArrays(GLsizei, const GLuint*);
void glDepthFunc(GLenum);
void glDepthMask(GLboolean);
void glDisable(GLenum);
void glDrawArrays(GLenum, GLint, GLsizei);
void glEnable(GLenum);
void glEnableVertexAttribArray(GLuint);
void glFinish(void);
void glFramebufferRenderbuffer(GLenum, GLenum, GLenum, GLuint);
void glFramebufferTexture2D(GLenum, GLenum, GLenum, GLuint, GLint);
void glFrontFace(GLenum);
void glGenBuffers(GLsizei, GLuint*);
void glGenFramebuffers(GLsizei, GLuint*);
void glGenRenderbuffers(GLsizei, GLuint*);
void glGenTextures(GLsizei, GLuint*);
void glGenVertexArrays(GLsizei, GLuint*);
void glGenerateMipmap(GLenum);
GLenum glGetError(void);
void glGetIntegerv(GLenum, GLint*);
void glGetProgramInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);
void glGetProgramiv(GLuint, GLenum, GLint*);
void glGetShaderInfoLog(GLuint, GLsizei, GLsizei*, GLchar*);
void glGetShaderiv(GLuint, GLenum, GLint*);
const GLubyte* glGetString(GLenum);
GLint glGetUniformLocation(GLuint, const GLchar*);
void glLinkProgram(GLuint);
void glPixelStorei(GLenum, GLint);
void glReadPixels(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*);
void glRenderbufferStorage(GLenum, GLenum, GLsizei, GLsizei);
void glRenderbufferStorageMultisample(GLenum, GLsizei, GLenum, GLsizei, GLsizei);
void glShaderSource(GLuint, GLsizei, const GLchar* const*, const GLint*);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
void glTexParameteri(GLenum, GLenum, GLint);
void glUniform1f(GLint, GLfloat);
void glUniform1i(GLint, GLint);
void glUniform2f(GLint, GLfloat, GLfloat);
void glUniform3f(GLint, GLfloat, GLfloat, GLfloat);
void glUniform4f(GLint, GLfloat, GLfloat, GLfloat, GLfloat);
void glUniformMatrix4fv(GLint, GLsizei, GLboolean, const GLfloat*);
void glUseProgram(GLuint);
void glVertexAttribPointer(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
void glViewport(GLint, GLint, GLsizei, GLsizei);
}
#endif
