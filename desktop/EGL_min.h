// Minimal EGL declarations for the headless desktop preview (links against Mesa's libEGL).
#pragma once
#include <stdint.h>
extern "C" {
typedef void* EGLDisplay;
typedef void* EGLConfig;
typedef void* EGLContext;
typedef void* EGLSurface;
typedef int32_t EGLint;
typedef unsigned int EGLBoolean;
typedef unsigned int EGLenum;
typedef intptr_t EGLAttrib;
#define EGL_DEFAULT_DISPLAY ((void*)0)
#define EGL_NO_CONTEXT ((EGLContext)0)
#define EGL_NO_SURFACE ((EGLSurface)0)
#define EGL_NONE 0x3038
#define EGL_RENDERABLE_TYPE 0x3040
#define EGL_OPENGL_ES3_BIT 0x00000040
#define EGL_OPENGL_ES_API 0x30A0
#define EGL_CONTEXT_MAJOR_VERSION 0x3098
#define EGL_PLATFORM_SURFACELESS_MESA 0x31DD
EGLDisplay eglGetPlatformDisplay(EGLenum, void*, const EGLAttrib*);
EGLBoolean eglInitialize(EGLDisplay, EGLint*, EGLint*);
EGLBoolean eglBindAPI(EGLenum);
EGLBoolean eglChooseConfig(EGLDisplay, const EGLint*, EGLConfig*, EGLint, EGLint*);
EGLContext eglCreateContext(EGLDisplay, EGLConfig, EGLContext, const EGLint*);
EGLBoolean eglMakeCurrent(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
}
