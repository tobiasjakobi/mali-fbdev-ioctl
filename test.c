/* This file is part of mali-fbdev-ioctl.
 * Copyright (C) 2014-2015 - Tobias Jakobi
 *
 * mali-fbdev-ioctl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * mali-fbdev-ioctl is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with mali-fbdev-ioctl. If not, see <http://www.gnu.org/licenses/>.
 */

#include <EGL/eglplatform_fb.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/fbdev_window.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "common.h"

#include <stdlib.h>

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
  .bpp = 4,
  .num_buffers = 3,
  .use_screen = 0,
  .monitor_index = 0
};

extern void setup_hook();

typedef void GL_APIENTRY (*fncEGLImageTargetRenderbufferStorageOES)(GLenum, GLeglImageOES);
typedef void GL_APIENTRY (*fncEGLImageTargetTexture2DOES)(GLenum, GLeglImageOES);

const char* egl_error_string(EGLint error) {
  if (error < 0x3000 || error > 0x300D)
    return "unknown error code";

  static const char *table[] = {
    "success",
    "not initialized",
    "bad access",
    "bad alloc",
    "bad attribute",
    "bad config",
    "bad context",
    "bad current surface",
    "bad display",
    "bad match",
    "bad native pixmap",
    "bad native window",
    "bad parameter",
    "bad surface"
  };

  return table[error - 0x3000];
}

int pixmap_test(EGLDisplay dpy, EGLContext ctx,
                EGLConfig cnf, EGLSurface cursurf) {
  EGLSurface pixsurf;
  EGLImageKHR image;
  GLuint renderbuffer;

  void *pixdata;
  int ret;

  fncEGLImageTargetRenderbufferStorageOES glEGLImageTargetRenderbufferStorageOES;
  fncEGLImageTargetTexture2DOES glEGLImageTargetTexture2DOES;

  glEGLImageTargetRenderbufferStorageOES = (fncEGLImageTargetRenderbufferStorageOES)
    eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
  glEGLImageTargetTexture2DOES = (fncEGLImageTargetTexture2DOES)
    eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (glEGLImageTargetRenderbufferStorageOES == 0 || glEGLImageTargetTexture2DOES == 0) {
    fprintf(stderr, "error: failed to get EGLImage function pointers\n");
    return -5;
  }

  pixdata = malloc(640 * 480 * 4);
  const fbdev_pixmap pixmap = {
    .height = 640,
    .width = 480,
    .bytes_per_pixel = 4,
    .buffer_size = 32,
    .red_size = 8,
    .green_size = 8,
    .blue_size = 8,
    .alpha_size = 8,
    .luminance_size = 0,
    .flags = FBDEV_PIXMAP_DEFAULT, /* any other flag seems to fail */
    .data = pixdata,
    .format = 0
  };

  fprintf(stderr, "info: calling eglCreatePixmapSurface\n");
  eglGetError();
  pixsurf = eglCreatePixmapSurface(dpy, cnf, (NativePixmapType)(&pixmap), NULL);
  if (pixsurf == EGL_NO_SURFACE) {
    fprintf(stderr, "error: eglCreatePixmapSurface failed\n");
    fprintf(stderr, "errcode = 0x%x\n", eglGetError());
    return -1;
  }

  ret = eglMakeCurrent(dpy, pixsurf, pixsurf, ctx);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglMakeCurrent (pixmap) failed\n");
    return -2;
  }

  ret = eglMakeCurrent(dpy, cursurf, cursurf, ctx);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglMakeCurrent (regular) failed\n");
    return -3;
  }

  fprintf(stderr, "info: calling eglDestroySurface (pixmap)\n");
  eglDestroySurface(dpy, pixsurf);

  fprintf(stderr, "info: calling eglCreateImageKHR\n");
  eglGetError();
  image = (EGLImageKHR)eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)(&pixmap), NULL);
  if (image == EGL_NO_IMAGE_KHR) {
    fprintf(stderr, "error: eglCreateImageKHR failed\n");
    fprintf(stderr, "errcode = 0x%x\n", eglGetError());
    return -4;
  }

  eglGetError();
  glGenRenderbuffers(1, &renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, image);
  fprintf(stderr, "EGLImageTargetRenderbufferStorageOES: status = %s\n", egl_error_string(eglGetError()));

  eglDestroyImageKHR(dpy, image);
  free(pixdata);

  return 0;
}

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

  /* TODO: Try adding EGL_PIXMAP_BIT for EGL_SURFACE_TYPE.
   *       Also look into EGL_MATCH_NATIVE_PIXMAP. */
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

  static const struct {
    EGLint name;
    const char *string;
  } queries[] = {
    {EGL_CLIENT_APIS, "Client APIs"},
    {EGL_VENDOR, "Vendor"},
    {EGL_VERSION, "Version"},
    {EGL_EXTENSIONS, "Extensions"}
  };

  fprintf(stderr, "Querying information about the EGL connection:\n");
  for (i = 0; i < 4; ++i) {
    fprintf(stderr, "%s: %s\n", queries[i].string,
            eglQueryString(disp, queries[i].name));
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

  pixmap_test(disp, ctx, conf, surf);

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
