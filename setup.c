#include "common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <exynos_drmif.h>

#include <pthread.h>
#include <poll.h>

typedef void (*setupcbfnc)(hsetupfnc, hsetupfnc);

static pthread_mutex_t hook_mutex = PTHREAD_MUTEX_INITIALIZER;

extern const struct video_config vconf;

struct exynos_fliphandler {
  struct pollfd fds;
  drmEventContext evctx;
};

struct exynos_drm {
  drmModeRes *resources;
  drmModeConnector *connector;
  drmModeEncoder *encoder;
  drmModeModeInfo *mode;
  drmModeCrtc *orig_crtc;

  uint32_t crtc_id;
  uint32_t connector_id;
};

/* Find the index of a compatible DRM device. */
static int get_device_index() {
  char buf[32];
  drmVersionPtr ver;

  int index = 0;
  int fd;
  bool found = false;

  while (!found) {
    snprintf(buf, sizeof(buf), "/dev/dri/card%d", index);

    fd = open(buf, O_RDWR);
    if (fd == -1) break;

    ver = drmGetVersion(fd);

    if (strcmp("exynos", ver->name) == 0)
      found = true;
    else
      ++index;

    drmFreeVersion(ver);
    close(fd);
  }

  return (found ? index : -1);
}

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
  int fd, ret, devidx;
  struct exynos_device *dev;
  struct exynos_bo *bo;
  struct drm_prime_handle req;

  unsigned i;
  char buf[32];

  pthread_mutex_lock(&hook_mutex);

  if (data->initialized) return 0;

  devidx = get_device_index();
  if (devidx != -1) {
    snprintf(buf, sizeof(buf), "/dev/dri/card%d", devidx);
  } else {
    fprintf(stderr, "[hook_initialize] error: no compatible DRM device found\n");

    ret = -1;
    goto out;
  }

  fd = data->open(buf, O_RDWR, 0);
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

void setup_hook() {
  static setupcbfnc setup_hook_callback;
  const char* err;

  err = dlerror();
  setup_hook_callback = dlsym(RTLD_DEFAULT, "setup_hook_callback");
  err = dlerror();

  if ((err != NULL) || (setup_hook_callback == NULL)) {
    fprintf(stderr, "dlsym(setup_hook_callback) failed\n");
    fprintf(stderr, "dlerror = %s\n", err);
  } else {
    setup_hook_callback(hook_initialize, hook_free);
  }
}
