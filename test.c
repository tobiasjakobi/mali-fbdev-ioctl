#include <EGL/egl.h>

#include "common.h"

typedef int (*setupcbfnc)(unsigned long, callbackfnc);

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

static int emulate_get_var_screeninfo(void *ptr) {
  struct fb_var_screeninfo *data = ptr;

  memcpy(data, &fake_vscreeninfo, sizeof(struct fb_var_screeninfo));

  return 0;
}

static int emulate_put_var_screeninfo(void *ptr) {
  // TODO: implement
  return 0;
}

static int emulate_get_fix_screeninfo(void *ptr) {
  // TODO: implement
  return 0;
}

static int emulate_pan_display(void *ptr) {
  // TODO: implement
  return 0;
}

static int emulate_waitforvsync(void *ptr) {
  // TODO: implement
  return 0;
}

static int emulate_get_fb_dma_buf(void *ptr) {
  // TODO: implement
  return 0;
}

int main(int argc, char* argv[])
{
  static setupcbfnc setup_hook_callback;
  const char* err;

  EGLDisplay disp;
  EGLContext ctx;
  EGLConfig conf;

  //struct fbdev_window nwin;

  EGLint major, minor;
  EGLint nconf;
  EGLBoolean ret;

  err = dlerror();
  setup_hook_callback = dlsym(RTLD_DEFAULT, "setup_hook_callback");
  err = dlerror();

  if ((err != NULL) || (setup_hook_callback == NULL)) {
    fprintf(stderr, "dlsym(setup_hook_callback) failed\n");
    fprintf(stderr, "dlerror = %s\n", err);
    return -1;
  }

  setup_hook_callback(FBIOGET_VSCREENINFO, emulate_get_var_screeninfo);
  setup_hook_callback(FBIOPUT_VSCREENINFO, emulate_put_var_screeninfo);
  setup_hook_callback(FBIOGET_FSCREENINFO, emulate_get_fix_screeninfo);
  setup_hook_callback(FBIOPAN_DISPLAY, emulate_pan_display);
  setup_hook_callback(FBIO_WAITFORVSYNC, emulate_waitforvsync);
  setup_hook_callback(IOCTL_GET_FB_DMA_BUF, emulate_get_fb_dma_buf);

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
  }

  ret = eglChooseConfig(disp, attribs, &conf, 1, &nconf);
  if (ret != EGL_TRUE) {
    printf("error: eglChooseConfig failed\n");
    return -4;
  } else {
    printf("info: configuration number = %d\n", nconf);
  }

  return 0;
}
