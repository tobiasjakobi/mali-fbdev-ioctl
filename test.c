#include <EGL/eglplatform_fb.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "common.h"

/* Some envvars that the blob seems to use:
 * MALI_NOCLEAR
 * MALI_MAX_WINDOW_BUFFERS (defaults seems to be 2)
 * FRAMEBUFFER (fbdev device used by the blob, e.g. "/dev/fb1")
 * MALI_FBDEV (read, but apparantly not used)
 * MALI_SINGLEBUFFER (do all rendering in one buffer)
 * MALI_FLIP_PIXMAP
 * MALI_NEVERBLIT
 */

struct color3f {
  float r, g, b;
};

static const struct color3f testcolors[3] = {
  {1.0, 0.5, 0.0},
  {0.0, 1.0, 0.5},
  {0.5, 0.0, 1.0}
};

const struct video_config vconf = {
  .width = 1280,
  .height = 720,
  .num_buffers = 3
};

extern void setup_hook();

int main(int argc, char* argv[]) {
  EGLDisplay disp;
  EGLContext ctx;
  EGLConfig conf;
  EGLSurface surf;

  struct mali_native_window nwin;

  EGLint major, minor;
  EGLint nconf;
  EGLBoolean ret;
  unsigned int i;

  setup_hook();

  static const EGLint attribs[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };

  disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (disp == EGL_NO_DISPLAY) {
    fprintf(stderr, "error: eglGetDisplay failed\n");
    return -2;
  }

  fprintf(stderr, "info: calling eglInitialize\n");
  ret = eglInitialize(disp, &major, &minor);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglInitialize failed\n");
    return -3;
  } else {
    fprintf(stderr, "info: eglInitialize: major = %d, minor = %d\n", major, minor);
  }

  ret = eglChooseConfig(disp, attribs, &conf, 1, &nconf);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglChooseConfig failed\n");
    return -4;
  } else {
    fprintf(stderr, "info: configuration number = %d\n", nconf);
  }

  nwin.width = vconf.width;
  nwin.height = vconf.height;

  fprintf(stderr, "info: calling eglCreateWindowSurface\n");
  surf = eglCreateWindowSurface(disp, conf, &nwin, NULL);
  if (surf == EGL_NO_SURFACE) {
    fprintf(stderr, "error: eglCreateWindowSurface failed\n");
    return -5;
  }

  static const EGLint ctxattribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  ctx = eglCreateContext(disp, conf, 0, ctxattribs);
  if (ctx == EGL_NO_CONTEXT) {
    fprintf(stderr, "error: eglCreateContext failed\n");
    return -6;
  }

  ret = eglMakeCurrent(disp, surf, surf, ctx);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglMakeCurrent failed\n");
    return -7;
  }

  eglSwapInterval(disp, 1);

  for (i = 0; i < 30; ++i) {
    const unsigned cidx = i % 3;

    glClearColor(testcolors[cidx].r, testcolors[cidx].g, testcolors[cidx].b, 1.0);
    glFlush();
    fprintf(stderr, "info: calling glClear\n");
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    fprintf(stderr, "info: calling eglSwapBuffers\n");
    eglSwapBuffers(disp, surf);
  }

  eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  fprintf(stderr, "info: calling eglDestroyContext\n");
  eglDestroyContext(disp, ctx);
  fprintf(stderr, "info: calling eglDestroySurface\n");
  eglDestroySurface(disp, surf);
  fprintf(stderr, "info: calling eglTerminate\n");
  eglTerminate(disp);

  return 0;
}
