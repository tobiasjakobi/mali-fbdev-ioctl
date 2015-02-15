#include <EGL/eglplatform_fb.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

#include "common.h"

#include <stdlib.h>
#include <exynos_drmif.h>
#include <pthread.h>

#include "mali_ioctl.h"

typedef void (*setupcbfnc)(hsetupfnc, hsetupfnc);

struct color3f {
  float r, g, b;
};

struct video_config {
  unsigned width;
  unsigned height;
  unsigned num_buffers;
};

static const struct color3f testcolors[3] = {
  {1.0, 0.0, 0.0},
  {0.0, 1.0, 0.0},
  {0.0, 0.0, 1.0}
};

static const struct video_config vconf = {
  .width = 1280,
  .height = 720,
  .num_buffers = 3
};

static pthread_mutex_t hook_mutex = PTHREAD_MUTEX_INITIALIZER;

static void init_var_screeninfo(struct hook_data *data) {
  if (data->fake_vscreeninfo) return;

  const struct fb_var_screeninfo vscreeninfo = {
    .xres = data->width,
    .yres = data->height,
    .xres_virtual = data->width,
    .yres_virtual = data->height * data->num_buffers,
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

  data->fake_vscreeninfo = malloc(sizeof(struct fb_var_screeninfo));
  memcpy(data->fake_vscreeninfo, &vscreeninfo, sizeof(struct fb_var_screeninfo));
}

static void init_fix_screeninfo(struct hook_data *data) {
  if (data->fake_fscreeninfo) return;

  const struct fb_fix_screeninfo fscreeninfo = {
    .smem_start = data->base_addr,
    .smem_len = 0,
    .visual = FB_VISUAL_TRUECOLOR,
    .xpanstep = 1,
    .ypanstep = 1,
    .line_length = data->width * 4
  };

  data->fake_fscreeninfo = malloc(sizeof(struct fb_fix_screeninfo));
  memcpy(data->fake_fscreeninfo, &fscreeninfo, sizeof(struct fb_fix_screeninfo));
}

static int hook_initialize(struct hook_data *data) {
  int fd, ret;
  struct exynos_device *dev;
  struct exynos_bo *bo;
  struct drm_prime_handle req;

  unsigned i;

  pthread_mutex_lock(&hook_mutex);

  if (data->initialized) return 0;

  fd = data->open("/dev/dri/card0", O_RDWR, 0);
  if (fd < 0) {
    fprintf(stderr, "[hook_initialize] error: failed to open DRM device\n");

    ret = -1;
    goto out;
  }

  dev = exynos_device_create(fd);

  data->bos = malloc(sizeof(struct exynos_bo*) * vconf.num_buffers);
  data->bo_fds = malloc(sizeof(int) * vconf.num_buffers);

  for (i = 0; i < vconf.num_buffers; ++i) {
    data->bos[i] = NULL;
    data->bo_fds[i] = -1;

    bo = exynos_bo_create(dev, vconf.width * vconf.height * 4, 0);

    if (bo == NULL) {
      fprintf(stderr, "[hook_initialize] error: failed to create buffer object (index = %u)\n", i);
      break;
    }

    memset(&req, 0, sizeof(struct drm_prime_handle));
    req.handle = bo->handle;

    ret = drmIoctl(fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req);
    if (ret < 0) {
      fprintf(stderr, "[hook_initialize] error: failed to get fd from bo\n");
      break;
    }

    data->bos[i] = bo;
    data->bo_fds[i] = req.fd;
  }

  if (i != vconf.num_buffers) {
    while (i-- > 0)
      exynos_bo_destroy(data->bos[i]);

    free(data->bos);
    free(data->bo_fds);

    data->bos = NULL;
    data->bo_fds = NULL;

    exynos_device_destroy(dev);
    close(fd);

    ret = -1;
    goto out;
  }

  data->drm_fd = fd;
  data->width = vconf.width;
  data->height = vconf.height;
  data->num_buffers = vconf.num_buffers;
  data->base_addr = 0x67900000;
  data->edev = dev;

  init_var_screeninfo(data);
  init_fix_screeninfo(data);

  data->size = vconf.width * vconf.height * 4;

  data->initialized = 1;
  ret = 0;

out:
  pthread_mutex_unlock(&hook_mutex);

  return ret;
}

static int hook_free(struct hook_data *data) {
  unsigned i;

  pthread_mutex_lock(&hook_mutex);

  if (data->initialized == 0)
    return 0;

  free(data->fake_vscreeninfo);
  free(data->fake_fscreeninfo);

  data->fake_vscreeninfo = NULL;
  data->fake_fscreeninfo = NULL;
  data->size = 0;

  for (i = 0; i < vconf.num_buffers; ++i)
    exynos_bo_destroy(data->bos[i]);

  free(data->bos);
  free(data->bo_fds);

  data->bos = NULL;
  data->bo_fds = NULL;

  exynos_device_destroy(data->edev);
  close(data->drm_fd);

  data->edev = NULL;
  data->drm_fd = -1;

  data->width = 0;
  data->height = 0;
  data->num_buffers = 0;
  data->base_addr = 0;

  data->initialized = 0;

  pthread_mutex_unlock(&hook_mutex);

  return 0;
}

int main(int argc, char* argv[])
{
  static setupcbfnc setup_hook_callback;
  const char* err;

  EGLDisplay disp;
  EGLContext ctx;
  EGLConfig conf;
  EGLSurface surf;

  struct mali_native_window nwin;

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
    setup_hook_callback(hook_initialize, hook_free);
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
    fprintf(stderr, "error: eglGetDisplay failed\n");
    return -2;
  }

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

  for (i = 0; i < 3; ++i) {
    glClearColor(testcolors[i].r, testcolors[i].g, testcolors[i].b, 1.0);
    glFlush();
    fprintf(stderr, "info: calling glClear\n");
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    fprintf(stderr, "info: calling eglSwapBuffers\n");
    eglSwapBuffers(disp, surf);
  }

  eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroyContext(disp, ctx);
  eglDestroySurface(disp, surf);
  eglTerminate(disp);

  return 0;
}
