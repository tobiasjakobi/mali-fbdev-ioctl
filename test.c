#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "common.h"

typedef int (*setupcbfnc)(unsigned long, callbackfnc);

struct color3f {
  float r, g, b;
};

static const struct color3f testcolors[3] = {
  {1.0, 0.0, 0.0},
  {0.0, 1.0, 0.0},
  {0.0, 0.0, 1.0}
};

static const struct fb_var_screeninfo fake_vscreeninfo = {
  .xres = 1280,
  .yres = 720,
  .xres_virtual = 1280,
  .yres_virtual = 720 * 3,
  .bits_per_pixel = 32,
  .red = {
    .offset = 16,
    .length = 8
  },
  .green = {
    .offset = 8,
    .length = 8
  },
  .blue = {
    .offset = 0,
    .length = 8
  },
  .transp = {
    .offset = 0,
    .length = 0
  },
  .height = 0xffffffff,
  .width = 0xffffffff,
  .accel_flags = 1
};

static const struct fb_fix_screeninfo fake_fscreeninfo = {
  .smem_start = 0x67900000,
  .smem_len = 0, /* TODO */
  .visual = FB_VISUAL_TRUECOLOR,
  .xpanstep = 1,
  .ypanstep = 1,
  .line_length = 1280 * 4
};

static int emulate_get_var_screeninfo(void *ptr) {
  struct fb_var_screeninfo *data = ptr;

  fprintf(stderr, "info: emulate_get_var_screeninfo called\n");

  memcpy(data, &fake_vscreeninfo, sizeof(struct fb_var_screeninfo));

  return 0;
}

static int emulate_put_var_screeninfo(void *ptr) {
  fprintf(stderr, "info: emulate_put_var_screeninfo called\n");

  // TODO: implement
  return -1;
}

static int emulate_get_fix_screeninfo(void *ptr) {
  struct fb_fix_screeninfo *data = ptr;

  fprintf(stderr, "info: emulate_get_fix_screeninfo called\n");

  memcpy(data, &fake_fscreeninfo, sizeof(struct fb_fix_screeninfo));

  return 0;
}

static int emulate_pan_display(void *ptr) {
  fprintf(stderr, "info: emulate_pan_display called\n");

  // TODO: implement
  return -1;
}

static int emulate_waitforvsync(void *ptr) {
  fprintf(stderr, "info: emulate_waitforvsync called\n");

  // TODO: implement
  return -1;
}

static int emulate_get_fb_dma_buf(void *ptr) {
  fprintf(stderr, "info: emulate_get_fb_dma_buf called\n");

  // TODO: implement
  return -1;
}

int main(int argc, char* argv[])
{
  static setupcbfnc setup_hook_callback;
  const char* err;

  EGLDisplay disp;
  EGLContext ctx;
  EGLConfig conf;
  EGLSurface surf;

  struct fbdev_window nwin;

  EGLint major, minor;
  EGLint nconf;
  EGLBoolean ret;
  unsigned int i;

  err = dlerror();
  setup_hook_callback = dlsym(RTLD_DEFAULT, "setup_hook_callback");
  err = dlerror();

  if ((err != NULL) || (setup_hook_callback == NULL)) {
    fprintf(stderr, "dlsym(setup_hook_callback) failed\n");
    fprintf(stderr, "dlerror = %s\n", err);
  } else {
    setup_hook_callback(FBIOGET_VSCREENINFO, emulate_get_var_screeninfo);
    setup_hook_callback(FBIOPUT_VSCREENINFO, emulate_put_var_screeninfo);
    setup_hook_callback(FBIOGET_FSCREENINFO, emulate_get_fix_screeninfo);
    setup_hook_callback(FBIOPAN_DISPLAY, emulate_pan_display);
    setup_hook_callback(FBIO_WAITFORVSYNC, emulate_waitforvsync);
    setup_hook_callback(IOCTL_GET_FB_DMA_BUF, emulate_get_fb_dma_buf);
  }

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
    printf("error: eglGetDisplay failed\n");
    return -2;
  }

  ret = eglInitialize(disp, &major, &minor);
  if (ret != EGL_TRUE) {
    printf("error: eglInitialize failed\n");
    return -3;
  } else {
    printf("info: eglInitialize: major = %d, minor = %d\n", major, minor);
  }

  ret = eglChooseConfig(disp, attribs, &conf, 1, &nconf);
  if (ret != EGL_TRUE) {
    printf("error: eglChooseConfig failed\n");
    return -4;
  } else {
    printf("info: configuration number = %d\n", nconf);
  }

  nwin.width = 1280;
  nwin.height = 720;

  surf = eglCreateWindowSurface(disp, conf, (NativeWindowType)&nwin, 0);
  if (surf == EGL_NO_SURFACE) {
    printf("error: eglCreateWindowSurface failed\n");
    return -5;
  }

  static const EGLint ctxattribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  ctx = eglCreateContext(disp, conf, 0, ctxattribs);
  if (ctx == EGL_NO_CONTEXT) {
    printf("error: eglCreateContext failed\n");
    return -6;
  }

  ret = eglMakeCurrent(disp, surf, surf, ctx);
  if (ret != EGL_TRUE) {
    printf("error: eglMakeCurrent failed\n");
    return -7;
  }

  for (i = 0; i < 3; ++i) {
    glClearColor(testcolors[i].r, testcolors[i].g, testcolors[i].b, 1.0);
    glFlush();
    printf("info: calling glClear\n");
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    printf("info: calling eglSwapBuffers\n");
    eglSwapBuffers(disp, surf);
  }

  // TODO: deinitialization

  return 0;
}
