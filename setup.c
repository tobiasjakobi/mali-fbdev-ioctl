#include "common.h"

#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <assert.h>

#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <exynos_drmif.h>

#include <pthread.h>
#include <poll.h>

typedef void (*setupcbfnc)(hsetupfnc, hsetupfnc, hflipfnc, hbufferfnc);

static pthread_mutex_t hook_mutex = PTHREAD_MUTEX_INITIALIZER;

extern const struct video_config vconf;

struct exynos_page {
  struct exynos_bo *bo;
  uint32_t buf_id;
  int fd;

  struct hook_data *base;

  bool used; /* Set if page is currently used. */
  bool clear; /* Set if page has to be cleared. */
};

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

/* Restore the original CRTC. */
static void restore_crtc(struct exynos_drm *d, int fd) {
  if (d == NULL) return;
  if (d->orig_crtc == NULL) return;

  drmModeSetCrtc(fd, d->orig_crtc->crtc_id,
                 d->orig_crtc->buffer_id,
                 d->orig_crtc->x,
                 d->orig_crtc->y,
                 &d->connector_id, 1, &d->orig_crtc->mode);

  drmModeFreeCrtc(d->orig_crtc);
  d->orig_crtc = NULL;
}

static void clean_up_drm(struct exynos_drm *d, int fd) {
  if (d) {
    if (d->encoder) drmModeFreeEncoder(d->encoder);
    if (d->connector) drmModeFreeConnector(d->connector);
    if (d->resources) drmModeFreeResources(d->resources);
  }

  free(d);
  close(fd);
}

static void clean_up_pages(struct exynos_page *p, unsigned cnt) {
  unsigned i;

  for (i = 0; i < cnt; ++i) {
    if (p[i].bo != NULL) {
      if (p[i].buf_id != 0)
        drmModeRmFB(p[i].buf_id, p[i].bo->handle);

      exynos_bo_destroy(p[i].bo);
    }
  }
}

static int exynos_open(struct hook_data *data) {
  char buf[32];
  int devidx;

  int fd = -1;
  struct exynos_drm *drm = NULL;
  struct exynos_fliphandler *fliphandler = NULL;
  unsigned i;

  assert(data->drm_fd == -1);

  devidx = get_device_index();
  if (devidx != -1) {
    snprintf(buf, sizeof(buf), "/dev/dri/card%d", devidx);
  } else {
    fprintf(stderr, "[exynos_open] error: no compatible DRM device found\n");
    return -1;
  }

  fd = data->open(buf, O_RDWR, 0);
  if (fd == -1) {
    fprintf(stderr, "[exynos_open] error: failed to open DRM device\n");
    return -1;
  }

  if (vconf.use_screen == 0) {
    fprintf(stderr, "[exynos_open] info: skipping screen initialization\n");

    data->drm_fd = fd;
    return 0;
  }

  drm = calloc(1, sizeof(struct exynos_drm));
  if (drm == NULL) {
    fprintf(stderr, "[exynos_open] error: failed to allocate DRM\n");
    close(fd);
    return -1;
  }

  drm->resources = drmModeGetResources(fd);
  if (drm->resources == NULL) {
    fprintf(stderr, "[exynos_open] error: failed to get DRM resources\n");
    goto fail;
  }

  for (i = 0; i < drm->resources->count_connectors; ++i) {
    drm->connector = drmModeGetConnector(fd, drm->resources->connectors[i]);
    if (drm->connector == NULL)
      continue;

    if (drm->connector->connection == DRM_MODE_CONNECTED &&
        drm->connector->count_modes > 0)
      break;

    drmModeFreeConnector(drm->connector);
    drm->connector = NULL;
  }

  if (i == drm->resources->count_connectors) {
    fprintf(stderr, "[exynos_open] error: no currently active connector found\n");
    goto fail;
  }

  for (i = 0; i < drm->resources->count_encoders; i++) {
    drm->encoder = drmModeGetEncoder(fd, drm->resources->encoders[i]);

    if (drm->encoder == NULL) continue;

    if (drm->encoder->encoder_id == drm->connector->encoder_id)
      break;

    drmModeFreeEncoder(drm->encoder);
    drm->encoder = NULL;
  }

  fliphandler = calloc(1, sizeof(struct exynos_fliphandler));
  if (fliphandler == NULL) {
    fprintf(stderr, "[exynos_open] error: failed to allocate fliphandler\n");
    goto fail;
  }

  /* Setup the flip handler. */
  fliphandler->fds.fd = fd;
  fliphandler->fds.events = POLLIN;
  fliphandler->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  fliphandler->evctx.page_flip_handler = NULL /* TODO: page_flip_handler */;

  data->drm_fd = fd;
  data->drm = drm;
  data->fliphandler = fliphandler;

  fprintf(stderr, "[exynos_open] info: using DRM device \"%s\" with connector id %u\n",
          buf, data->drm->connector->connector_id);

  return 0;

fail:
  free(fliphandler);
  clean_up_drm(drm, fd);

  return -1;
}

/* Counterpart to exynos_open. */
static void exynos_close(struct hook_data *data) {
  free(data->fliphandler);
  data->fliphandler = NULL;

  clean_up_drm(data->drm, data->drm_fd);
  data->drm_fd = -1;
  data->drm = NULL;
}

static int exynos_init(struct hook_data *data, unsigned bpp) {
  struct exynos_drm *drm = data->drm;
  const int fd = data->drm_fd;

  unsigned i;

  if (vconf.use_screen == 0) {
    fprintf(stderr, "[exynos_init] info: skipping init\n");

    data->width = vconf.width;
    data->height = vconf.height;

    goto out;
  }

  if (vconf.width != 0 && vconf.height != 0) {
    for (i = 0; i < drm->connector->count_modes; i++) {
      if (drm->connector->modes[i].hdisplay == vconf.width &&
          drm->connector->modes[i].vdisplay == vconf.height) {
        drm->mode = &drm->connector->modes[i];
        break;
      }
    }

    if (drm->mode == NULL) {
      fprintf(stderr, "[exynos_init] error: requested resolution (%dx%d) not available\n",
              vconf.width, vconf.height);
      goto fail;
    }

  } else {
    /* Select first mode, which is the native one. */
    drm->mode = &drm->connector->modes[0];
  }

  if (drm->mode->hdisplay == 0 || drm->mode->vdisplay == 0) {
    fprintf(stderr, "[exynos_init] error: failed to select sane resolution\n");
    goto fail;
  }

  drm->crtc_id = drm->encoder->crtc_id;
  drm->connector_id = drm->connector->connector_id;
  drm->orig_crtc = drmModeGetCrtc(fd, drm->crtc_id);
  if (!drm->orig_crtc)
    fprintf(stderr, "[exynos_init] warning: cannot find original crtc\n");

  data->width = drm->mode->hdisplay;
  data->height = drm->mode->vdisplay;

out:
  data->num_pages = vconf.num_buffers;

  data->bpp = bpp;
  data->pitch = bpp * data->width;
  data->size = data->pitch * data->height;

  fprintf(stderr, "[exynos_init] info: selected %ux%u resolution with %u bpp\n",
          data->width, data->height, data->bpp);

  return 0;

fail:
  restore_crtc(drm, fd);

  drm->mode = NULL;

  return -1;
}

/* Counterpart to exynos_init. */
static void exynos_deinit(struct hook_data *data) {
  struct exynos_drm *drm = data->drm;

  restore_crtc(drm, data->drm_fd);

  drm = NULL;

  data->width = 0;
  data->height = 0;

  data->bpp = 0;
  data->pitch = 0;
  data->size = 0;
}

static int exynos_alloc(struct hook_data *data) {
  struct exynos_device *device;
  struct exynos_bo *bo;
  struct exynos_page *pages;
  struct drm_prime_handle req = { 0 };
  unsigned i;

  const unsigned flags = 0;

  device = exynos_device_create(data->drm_fd);
  if (device == NULL) {
    fprintf(stderr, "[exynos_init] error: failed to create device from fd\n");
    return -1;
  }

  pages = calloc(data->num_pages, sizeof(struct exynos_page));
  if (pages == NULL) {
    fprintf(stderr, "[exynos_init] error: failed to allocate pages\n");
    goto fail_alloc;
  }

  for (i = 0; i < data->num_pages; ++i) {
    bo = exynos_bo_create(device, data->size, flags);
    if (bo == NULL) {
      fprintf(stderr, "[exynos_init] error: failed to create buffer object\n");
      goto fail;
    }

    req.handle = bo->handle;

    if (drmIoctl(data->drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req) < 0) {
      fprintf(stderr, "[exynos_init] error: failed to get fd from bo\n");
      exynos_bo_destroy(bo);
      goto fail;
    }

    /* Don't map the BO, since we don't access it through userspace. */

    pages[i].bo = bo;
    pages[i].fd = req.fd;
    pages[i].base = data;

    pages[i].used = false;
    pages[i].clear = true;
  }

  if (vconf.use_screen == 1) {
    const uint32_t pixel_format = (data->bpp == 2) ? DRM_FORMAT_RGB565 : DRM_FORMAT_XRGB8888;
    uint32_t handles[4] = {0}, pitches[4] = {0}, offsets[4] = {0};

    pitches[0] = data->pitch;
    offsets[0] = 0;

    for (i = 0; i < data->num_pages; ++i) {
      handles[0] = pages[i].bo->handle;

      if (drmModeAddFB2(data->drm_fd, data->width, data->height,
                        pixel_format, handles, pitches, offsets,
                        &pages[i].buf_id, flags)) {
        fprintf(stderr, "[exynos_init] error: failed to add bo %u to fb\n", i);
        goto fail;
      }
    }
  }

  data->pages = pages;
  data->device = device;

  if (vconf.use_screen == 1) {
    /* Setup CRTC: display the last allocated page. */
    drmModeSetCrtc(data->drm_fd, data->drm->crtc_id, pages[data->num_pages - 1].buf_id,
                   0, 0, &data->drm->connector_id, 1, data->drm->mode);
  }

  return 0;

fail:
  clean_up_pages(pages, data->num_pages);

fail_alloc:
  exynos_device_destroy(device);

  return -1;
}

/* Counterpart to exynos_alloc. */
static void exynos_free(struct hook_data *data) {
  unsigned i;

  if (vconf.use_screen == 1) {
    /* Disable the CRTC. */
    drmModeSetCrtc(data->drm_fd, data->drm->crtc_id, 0,
                   0, 0, &data->drm->connector_id, 1, NULL);
  }

  clean_up_pages(data->pages, data->num_pages);

  free(data->pages);
  data->pages = NULL;

  exynos_device_destroy(data->device);
  data->device = NULL;
}

static void init_var_screeninfo(struct hook_data *data) {
  if (data->fake_vscreeninfo) return;

  const struct fb_var_screeninfo vscreeninfo = {
    .xres = data->width,
    .yres = data->height,
    .xres_virtual = data->width,
    .yres_virtual = data->height * data->num_pages,
    .bits_per_pixel = data->bpp * 8,
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
    .line_length = data->pitch
  };

  data->fake_fscreeninfo = malloc(sizeof(struct fb_fix_screeninfo));
  memcpy(data->fake_fscreeninfo, &fscreeninfo, sizeof(struct fb_fix_screeninfo));
}

static int hook_initialize(struct hook_data *data) {
  int ret;

  pthread_mutex_lock(&hook_mutex);

  if (data->initialized) return 0;

  if (exynos_open(data)) {
    fprintf(stderr, "[hook_initialize] error: opening device failed\n");
    goto fail;
  }

  if (exynos_init(data, vconf.bpp != 0 ? vconf.bpp : 4) != 0) {
    fprintf(stderr, "[hook_initialize] error: initialization failed\n");
    goto fail_init;
  }

  if (exynos_alloc(data)) {
    fprintf(stderr, "[hook_initialize] error: allocation failed\n");
    goto fail_alloc;
  }

  data->base_addr = 0x67900000;

  init_var_screeninfo(data);
  init_fix_screeninfo(data);

  data->initialized = 1;

  ret = 0;
  goto out;

fail_alloc:
  exynos_deinit(data);

fail_init:
  exynos_close(data);

fail:
  ret = -1;

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
  data->base_addr = 0;

  exynos_free(data);
  exynos_deinit(data);
  exynos_close(data);

  data->initialized = 0;

  pthread_mutex_unlock(&hook_mutex);

  return 0;
}

static int hook_flip(struct hook_data *data, unsigned bufidx) {
  pthread_mutex_lock(&hook_mutex);

#ifndef NDEBUG
  fprintf(stderr, "DEBUG: in hook_flip (bufidx = %u)\n", bufidx);
#endif

  /* TODO: implement */

  pthread_mutex_unlock(&hook_mutex);

  /* fake success */
  return 0;
}

static int hook_buffer(struct hook_data* data, unsigned bufidx) {
  int fd;

  pthread_mutex_lock(&hook_mutex);
  fd = (bufidx < data->num_pages) ? data->pages[bufidx].fd : -1;
  pthread_mutex_unlock(&hook_mutex);

  return fd;
}

void setup_hook() {
  setupcbfnc setup_hook_callback;
  const char* err;

  err = dlerror();
  setup_hook_callback = dlsym(RTLD_DEFAULT, "setup_hook_callback");
  err = dlerror();

  if ((err != NULL) || (setup_hook_callback == NULL)) {
    fprintf(stderr, "dlsym(setup_hook_callback) failed\n");
    fprintf(stderr, "dlerror = %s\n", err);
  } else {
    setup_hook_callback(hook_initialize, hook_free, hook_flip, hook_buffer);
  }
}
