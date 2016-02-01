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

struct exynos_prop {
  uint32_t object_type;
  const char *prop_name;
  uint32_t prop_id;
};

struct exynos_page {
  struct exynos_bo *bo;
  uint32_t buf_id;
  int fd;

  drmModeAtomicReq *atomic_request;

  struct hook_data *base;

  bool used; /* Set if page is currently used. */
  bool clear; /* Set if page has to be cleared. */
};

struct exynos_fliphandler {
  struct pollfd fds;
  drmEventContext evctx;
};

struct exynos_drm {
  /* IDs for connector, CRTC and plane objects. */
  uint32_t connector_id;
  uint32_t crtc_id;
  uint32_t primary_plane_id;
  uint32_t overlay_plane_id;
  uint32_t mode_blob_id;

  struct exynos_prop *properties;

  /* Atomic requests for the initial and the restore modeset. */
  drmModeAtomicReq *modeset_request;
  drmModeAtomicReq *restore_request;
};

static const struct exynos_prop prop_template[] = {
  /* Property IDs of the connector object. */
  { DRM_MODE_OBJECT_CONNECTOR, "CRTC_ID", 0 },

  /* Property IDs of the CRTC object. */
  { DRM_MODE_OBJECT_CRTC, "ACTIVE", 0 },
  { DRM_MODE_OBJECT_CRTC, "MODE_ID", 0 },

  /* Property IDs of the plane object. */
  { DRM_MODE_OBJECT_PLANE, "FB_ID", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_ID", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_X", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_Y", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_W", 0 },
  { DRM_MODE_OBJECT_PLANE, "CRTC_H", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_X", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_Y", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_W", 0 },
  { DRM_MODE_OBJECT_PLANE, "SRC_H", 0 }
};

enum e_exynos_prop {
  connector_prop_crtc_id = 0,
  crtc_prop_active,
  crtc_prop_mode_id,
  plane_prop_fb_id,
  plane_prop_crtc_id,
  plane_prop_crtc_x,
  plane_prop_crtc_y,
  plane_prop_crtc_w,
  plane_prop_crtc_h,
  plane_prop_src_x,
  plane_prop_src_y,
  plane_prop_src_w,
  plane_prop_src_h
};

struct prop_assign {
  enum e_exynos_prop prop;
  uint64_t value;
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

static void clean_up_drm(struct exynos_drm *d, int fd) {
  if (d) {
    drmModeAtomicFree(d->modeset_request);
    drmModeAtomicFree(d->restore_request);
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

    drmModeAtomicFree(p[i].atomic_request);
  }
}

/* The main pageflip handler which is used by drmHandleEvent.         *
 * Decreases the pending pageflip count and updates the current page. */
static void page_flip_handler(int fd, unsigned frame, unsigned sec,
                              unsigned usec, void *data) {
  struct exynos_page *page = data;

#ifndef NDEBUG
  fprintf(stderr, "[page_flip_handler] info: page = %p\n", page);
#endif

  if (page->base->cur_page != NULL) {
    page->base->cur_page->used = false;
  }

  page->base->pageflip_pending--;
  page->base->cur_page = page;
}

static void wait_flip(struct exynos_fliphandler *fh) {
  const int timeout = -1;

  fh->fds.revents = 0;

  if (poll(&fh->fds, 1, timeout) < 0)
    return;

  if (fh->fds.revents & (POLLHUP | POLLERR))
    return;

  if (fh->fds.revents & POLLIN)
    drmHandleEvent(fh->fds.fd, &fh->evctx);
}

/* Get the ID of an object's property using the property name. */
static bool get_propid_by_name(int fd, uint32_t object_id, uint32_t object_type,
                               const char *name, uint32_t *prop_id) {
  drmModeObjectProperties *properties;
  unsigned i;
  bool found = false;

  properties = drmModeObjectGetProperties(fd, object_id, object_type);

  if (!properties)
    goto out;

  for (i = 0; i < properties->count_props; ++i) {
    drmModePropertyRes *prop;

    prop = drmModeGetProperty(fd, properties->props[i]);
    if (!prop)
      continue;

    if (strcmp(prop->name, name) == 0) {
      *prop_id = prop->prop_id;
      found = true;
    }

    drmModeFreeProperty(prop);

    if (found)
      break;
  }

  drmModeFreeObjectProperties(properties);

out:
  return found;
}

/* Get the value of an object's property using the ID. */
static bool get_propval_by_id(int fd, uint32_t object_id, uint32_t object_type,
                              uint32_t id, uint64_t *prop_value) {
  drmModeObjectProperties *properties;
  unsigned i;
  bool found = false;

  properties = drmModeObjectGetProperties(fd, object_id, object_type);

  if (!properties)
    goto out;

  for (i = 0; i < properties->count_props; ++i) {
    drmModePropertyRes *prop;

    prop = drmModeGetProperty(fd, properties->props[i]);
    if (!prop)
      continue;

    if (prop->prop_id == id) {
      *prop_value = properties->prop_values[i];
      found = true;
    }

    drmModeFreeProperty(prop);

    if (found)
      break;
  }

  drmModeFreeObjectProperties(properties);

out:
  return found;
}

static bool check_connector_type(uint32_t connector_type) {
  unsigned t;

  switch (connector_type) {
    case DRM_MODE_CONNECTOR_HDMIA:
    case DRM_MODE_CONNECTOR_HDMIB:
      t = connector_hdmi;
      break;

    case DRM_MODE_CONNECTOR_VGA:
      t = connector_vga;
      break;

    default:
      t = connector_other;
      break;
  }

  return (t == vconf.connector_type);
}

static int exynos_open(struct hook_data *data) {
  char buf[32];
  int devidx;

  int fd = -1;
  struct exynos_drm *drm;
  struct exynos_fliphandler *fliphandler = NULL;
  unsigned i, j;

  drmModeRes *resources = NULL;
  drmModePlaneRes *plane_resources = NULL;
  drmModeConnector *connector = NULL;
  drmModePlane *planes[2] = {NULL, NULL};

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

  /* Request atomic DRM support. This also enables universal planes. */
  if (drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
    fprintf(stderr, "[exynos_open] error: failed to enable atomic support\n");
    close(fd);
    return -1;
  }

  drm = calloc(1, sizeof(struct exynos_drm));
  if (drm == NULL) {
    fprintf(stderr, "[exynos_open] error: failed to allocate DRM\n");
    close(fd);
    return -1;
  }

  resources = drmModeGetResources(fd);
  if (resources == NULL) {
    fprintf(stderr, "[exynos_open] error: failed to get DRM resources\n");
    goto fail;
  }

  plane_resources = drmModeGetPlaneResources(fd);
  if (plane_resources == NULL) {
    fprintf(stderr, "[exynos_open] error: failed to get DRM plane resources\n");
    goto fail;
  }

  for (i = 0; i < resources->count_connectors; ++i) {
    connector = drmModeGetConnector(fd, resources->connectors[i]);
    if (connector == NULL)
      continue;

    if (check_connector_type(connector->connector_type) &&
        connector->connection == DRM_MODE_CONNECTED &&
        connector->count_modes > 0)
      break;

    drmModeFreeConnector(connector);
    connector = NULL;
  }

  if (i == resources->count_connectors) {
    fprintf(stderr, "[exynos_open] error: no currently active connector found\n");
    goto fail;
  }

  drm->connector_id = connector->connector_id;

  for (i = 0; i < connector->count_encoders; i++) {
    drmModeEncoder *encoder = drmModeGetEncoder(fd, connector->encoders[i]);

    if (!encoder)
      continue;

    /* Find a CRTC that is compatible with the encoder. */
    for (j = 0; j < resources->count_crtcs; ++j) {
      if (encoder->possible_crtcs & (1 << j))
        break;
    }

    drmModeFreeEncoder(encoder);

    /* Stop when a suitable CRTC was found. */
    if (j != resources->count_crtcs)
      break;
  }

  if (i == connector->count_encoders) {
    fprintf(stderr, "[exynos_open] error: no compatible encoder found\n");
    goto fail;
  }

  drm->crtc_id = resources->crtcs[j];

  for (i = 0; i < plane_resources->count_planes; ++i) {
    drmModePlane *plane;
    uint32_t plane_id, prop_id;
    uint64_t type;

    plane_id = plane_resources->planes[i];
    plane = drmModeGetPlane(fd, plane_id);

    if (!plane)
      continue;

    /* Make sure that the plane can be used with the selected CRTC. */
    if (!(plane->possible_crtcs & (1 << j)) ||
        !get_propid_by_name(fd, plane_id, DRM_MODE_OBJECT_PLANE, "type", &prop_id) ||
        !get_propval_by_id(fd, plane_id, DRM_MODE_OBJECT_PLANE, prop_id, &type)) {
      drmModeFreePlane(plane);
      continue;
    }

    switch (type) {
      case DRM_PLANE_TYPE_PRIMARY:
        if (planes[0])
          fprintf(stderr, "[exynos_open] warn: found more than one primary plane\n");
        else
          planes[0] = plane;
        break;

      case DRM_PLANE_TYPE_CURSOR:
        if (!planes[1])
          planes[1] = plane;
        break;

      case DRM_PLANE_TYPE_OVERLAY:
      default:
        drmModeFreePlane(plane);
        break;
    }
  }

  if (!planes[0] || !planes[1]) {
    fprintf(stderr, "[exynos_open] error: no primary plane or overlay plane found\n");
    goto fail;
  }

  /* Check that the primary plane supports XRGB8888. */
  for (i = 0; i < planes[0]->count_formats; ++i) {
    if (planes[0]->formats[i] == DRM_FORMAT_XRGB8888)
      break;
  }

  if (i == planes[0]->count_formats) {
    fprintf(stderr, "[exynos_open] error: primary plane has no support for XRGB8888\n");
    goto fail;
  }

  drm->primary_plane_id = planes[0]->plane_id;
  drm->overlay_plane_id = planes[1]->plane_id;

  fliphandler = calloc(1, sizeof(struct exynos_fliphandler));
  if (fliphandler == NULL) {
    fprintf(stderr, "[exynos_open] error: failed to allocate fliphandler\n");
    goto fail;
  }

  /* Setup the flip handler. */
  fliphandler->fds.fd = fd;
  fliphandler->fds.events = POLLIN;
  fliphandler->evctx.version = DRM_EVENT_CONTEXT_VERSION;
  fliphandler->evctx.page_flip_handler = page_flip_handler;

  fprintf(stderr, "[exynos_open] info: using DRM device \"%s\" with connector id %u\n",
          buf, drm->connector_id);

  fprintf(stderr, "[exynos_open] info: primary plane has ID %u, overlay plane has ID %u\n",
          drm->primary_plane_id, drm->overlay_plane_id);

  data->drm_fd = fd;
  data->drm = drm;
  data->fliphandler = fliphandler;

  return 0;

fail:
  free(fliphandler);

  drmModeFreePlane(planes[0]);
  drmModeFreePlane(planes[1]);

  drmModeFreeConnector(connector);
  drmModeFreePlaneResources(plane_resources);
  drmModeFreeResources(resources);

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

static uint32_t get_id_from_type(struct exynos_drm *drm, uint32_t object_type) {
  switch (object_type) {
    case DRM_MODE_OBJECT_CONNECTOR:
      return drm->connector_id;

    case DRM_MODE_OBJECT_CRTC:
      return drm->crtc_id;

    case DRM_MODE_OBJECT_PLANE:
      return drm->primary_plane_id;

    default:
      assert(false);
      return 0;
  }
}

static int exynos_get_properties(int fd, struct exynos_drm *drm) {
  const unsigned num_props = sizeof(prop_template) / sizeof(prop_template[0]);
  unsigned i;

  assert(!drm->properties);

  drm->properties = calloc(num_props, sizeof(struct exynos_prop));

  for (i = 0; i < num_props; ++i) {
    const uint32_t object_type = prop_template[i].object_type;
    const uint32_t object_id = get_id_from_type(drm, object_type);
    const char* prop_name = prop_template[i].prop_name;

    uint32_t prop_id;

    if (!get_propid_by_name(fd, object_id, object_type, prop_name, &prop_id))
      goto fail;

    drm->properties[i] = (struct exynos_prop){ object_type, prop_name, prop_id };
  }

  return 0;

fail:
  free(drm->properties);
  drm->properties = NULL;

  return -1;
}

static int exynos_create_restore_req(int fd, struct exynos_drm *drm) {
  static const enum e_exynos_prop restore_props[] = {
    connector_prop_crtc_id,
    crtc_prop_active,
    crtc_prop_mode_id,
    plane_prop_fb_id,
    plane_prop_crtc_id,
    plane_prop_crtc_x, plane_prop_crtc_y,
    plane_prop_crtc_w, plane_prop_crtc_h,
    plane_prop_src_x, plane_prop_src_y,
    plane_prop_src_w, plane_prop_src_h
  };
  const unsigned num_props = sizeof(restore_props) / sizeof(restore_props[0]);

  uint64_t temp;
  unsigned i;

  assert(!drm->restore_request);

  drm->restore_request = drmModeAtomicAlloc();

  for (i = 0; i < num_props; ++i) {
    const struct exynos_prop* prop = &drm->properties[restore_props[i]];
    const uint32_t object_type = prop->object_type;
    const uint32_t object_id = get_id_from_type(drm, object_type);

    uint64_t prop_value;

    if (!get_propval_by_id(fd, object_id, object_type, prop->prop_id, &prop_value))
      goto fail;

    if (drmModeAtomicAddProperty(drm->restore_request, object_id, prop->prop_id, prop_value) < 0)
      goto fail;
  }

  return 0;

fail:
  drmModeAtomicFree(drm->restore_request);
  drm->restore_request = NULL;

  return -1;
}

static int exynos_create_modeset_req(int fd, struct exynos_drm *drm, unsigned w, unsigned h) {
  uint64_t temp;
  unsigned i;

  const struct prop_assign assign[] = {
    { plane_prop_crtc_id, drm->crtc_id },
    { plane_prop_crtc_x, 0 },
    { plane_prop_crtc_y, 0 },
    { plane_prop_crtc_w, w },
    { plane_prop_crtc_h, h },
    { plane_prop_src_x, 0 },
    { plane_prop_src_y, 0 },
    { plane_prop_src_w, w << 16 },
    { plane_prop_src_h, h << 16 }
  };

  const unsigned num_assign = sizeof(assign) / sizeof(assign[0]);

  assert(!drm->modeset_request);

  drm->modeset_request = drmModeAtomicAlloc();

  if (drmModeAtomicAddProperty(drm->modeset_request, drm->connector_id,
      drm->properties[connector_prop_crtc_id].prop_id, drm->crtc_id) < 0)
    goto fail;

  if (drmModeAtomicAddProperty(drm->modeset_request, drm->crtc_id,
      drm->properties[crtc_prop_active].prop_id, 1) < 0)
    goto fail;

  if (drmModeAtomicAddProperty(drm->modeset_request, drm->crtc_id,
      drm->properties[crtc_prop_mode_id].prop_id, drm->mode_blob_id) < 0)
    goto fail;

  for (i = 0; i < num_assign; ++i) {
    if (drmModeAtomicAddProperty(drm->modeset_request, drm->primary_plane_id,
        drm->properties[assign[i].prop].prop_id, assign[i].value) < 0)
      goto fail;
  }

  return 0;

fail:
  drmModeAtomicFree(drm->modeset_request);
  drm->modeset_request = NULL;

  return -1;
}

static int exynos_init(struct hook_data *data, unsigned bpp) {
  struct exynos_drm *drm = data->drm;
  const int fd = data->drm_fd;

  drmModeConnector *connector = NULL;
  drmModeModeInfo *mode = NULL;
  unsigned i;

  if (vconf.use_screen == 0) {
    fprintf(stderr, "[exynos_init] info: skipping init\n");

    data->width = vconf.width;
    data->height = vconf.height;

    goto out;
  }

  connector = drmModeGetConnector(fd, drm->connector_id);

  if (vconf.width != 0 && vconf.height != 0) {
    for (i = 0; i < connector->count_modes; i++) {
      if (connector->modes[i].hdisplay == vconf.width &&
          connector->modes[i].vdisplay == vconf.height) {
        mode = &connector->modes[i];
        break;
      }
    }

    if (!mode) {
      fprintf(stderr, "[exynos_init] error: requested resolution (%dx%d) not available\n",
              vconf.width, vconf.height);
      goto fail;
    }

  } else {
    /* Select first mode, which is the native one. */
    mode = &connector->modes[0];
  }

  if (mode->hdisplay == 0 || mode->vdisplay == 0) {
    fprintf(stderr, "[exynos_init] error: failed to select sane resolution\n");
    goto fail;
  }

  if (drmModeCreatePropertyBlob(fd, mode, sizeof(drmModeModeInfo), &drm->mode_blob_id)) {
    fprintf(stderr, "[exynos_init] error: failed to blobify mode info\n");
    goto fail;
  }

  if (exynos_get_properties(fd, drm)) {
    fprintf(stderr, "[exynos_init] error: failed to get object properties\n");
    goto fail;
  }

  if (exynos_create_restore_req(fd, drm)) {
    fprintf(stderr, "[exynos_init] error: failed to create restore atomic request\n");
    goto fail;
  }

  if (exynos_create_modeset_req(fd, drm, mode->hdisplay, mode->vdisplay)) {
    fprintf(stderr, "[exynos_init] error: failed to create modeset atomic request\n");
    goto fail;
  }

  data->width = mode->hdisplay;
  data->height = mode->vdisplay;

  drmModeFreeConnector(connector);

out:
  data->num_pages = vconf.num_buffers != 0 ? vconf.num_buffers : 2;

  data->bpp = bpp;
  data->pitch = bpp * data->width;
  data->size = data->pitch * data->height;

  fprintf(stderr, "[exynos_init] info: selected %ux%u resolution with %u bpp\n",
          data->width, data->height, data->bpp);

  return 0;

fail:
  drmModeDestroyPropertyBlob(fd, drm->mode_blob_id);
  drmModeFreeConnector(connector);

  return -1;
}

/* Counterpart to exynos_init. */
static void exynos_deinit(struct hook_data *data) {
  struct exynos_drm *drm = data->drm;

  drmModeDestroyPropertyBlob(data->drm_fd, data->drm->mode_blob_id);

  drm = NULL;

  data->width = 0;
  data->height = 0;

  data->bpp = 0;
  data->pitch = 0;
  data->size = 0;
}

static int exynos_create_page_req(struct exynos_page *p) {
  struct exynos_drm *drm;

  assert(p);

  drm = p->base->drm;

  assert(p->atomic_request == NULL);

  p->atomic_request = drmModeAtomicAlloc();
  if (!p->atomic_request)
    goto fail;

  if (drmModeAtomicAddProperty(p->atomic_request, drm->primary_plane_id,
      drm->properties[plane_prop_fb_id].prop_id, p->buf_id) < 0) {
    drmModeAtomicFree(p->atomic_request);
    p->atomic_request = NULL;
    goto fail;
  }

  return 0;

fail:
  return -1;
}

static int initial_modeset(int fd, struct exynos_page *page, struct exynos_drm *drm) {
  int ret = 0;
  drmModeAtomicReq *request = NULL;

  request = drmModeAtomicDuplicate(drm->modeset_request);
  if (!request) {
    ret = -1;
    goto out;
  }

  if (drmModeAtomicMerge(request, page->atomic_request)) {
    ret = -2;
    goto out;
  }

  if (drmModeAtomicCommit(fd, request, DRM_MODE_ATOMIC_ALLOW_MODESET, NULL))
    ret = -3;

out:
  drmModeAtomicFree(request);
  return ret;
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
    fprintf(stderr, "[exynos_alloc] error: failed to create device from fd\n");
    return -1;
  }

  pages = calloc(data->num_pages, sizeof(struct exynos_page));
  if (pages == NULL) {
    fprintf(stderr, "[exynos_alloc] error: failed to allocate pages\n");
    goto fail_alloc;
  }

  for (i = 0; i < data->num_pages; ++i) {
    bo = exynos_bo_create(device, data->size, flags);
    if (bo == NULL) {
      fprintf(stderr, "[exynos_alloc] error: failed to create buffer object\n");
      goto fail;
    }

    req.handle = bo->handle;

    if (drmIoctl(data->drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req) < 0) {
      fprintf(stderr, "[exynos_alloc] error: failed to get fd from bo\n");
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
        fprintf(stderr, "[exynos_alloc] error: failed to add bo %u to fb\n", i);
        goto fail;
      }

      if (exynos_create_page_req(&pages[i])) {
        fprintf(stderr, "[exynos_alloc] error: failed to create atomic request for page %u\n", i);
        goto fail;
      }
    }

    /* Setup framebuffer: display the last allocated page. */
    if (initial_modeset(data->drm_fd, &pages[data->num_pages - 1], data->drm)) {
      fprintf(stderr, "[exynos_alloc] error: initial atomic modeset failed\n");
      goto fail;
    }
  }

  data->pages = pages;
  data->device = device;

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
    /* Disable/restore the display. */
    if (drmModeAtomicCommit(data->drm_fd, data->drm->restore_request,
        DRM_MODE_ATOMIC_ALLOW_MODESET, NULL)) {
      fprintf(stderr, "[exynos_free] warning: failed to disable/restore the display\n");
    }
  }

  clean_up_pages(data->pages, data->num_pages);

  free(data->pages);
  data->pages = NULL;

  exynos_device_destroy(data->device);
  data->device = NULL;
}

static int exynos_flip(struct hook_data *data, struct exynos_page *page) {
  /* We don't queue multiple page flips. */
  if (data->pageflip_pending > 0) {
    wait_flip(data->fliphandler);
  }

  /* Issue a page flip at the next vblank interval. */
  if (drmModeAtomicCommit(data->drm_fd, page->atomic_request,
                          DRM_MODE_PAGE_FLIP_EVENT, page)) {
    fprintf(stderr, "[exynos_flip] error: failed to issue atomic page flip\n");
    return -1;
  } else {
    data->pageflip_pending++;
  }

  /* On startup no frame is displayed. We therefore wait for the initial flip to finish. */
  if (data->cur_page == NULL) wait_flip(data->fliphandler);

  return 0;
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

  if (vconf.bpp != 0 && vconf.bpp != 4) {
    fprintf(stderr, "[hook_initialize] error: only bpp=4 supported at the moment\n");
    goto fail;
  }

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
  int ret;

  pthread_mutex_lock(&hook_mutex);

  if (vconf.use_screen == 0) {
    ret = 0;
    goto out;
  }

  assert(data->num_pages != 0);

  switch (data->num_pages) {
    case 1:
      ret = 0;
    break;

    case 2:
      if (exynos_flip(data, &data->pages[bufidx])) {
        ret = -1;
      } else {
        wait_flip(data->fliphandler);
        ret = 0;
      }
    break;

    default: /* three or more pages */
      if (exynos_flip(data, &data->pages[bufidx]))
        ret = -1;
      else
        ret = 0;
    break;
  }

out:
  pthread_mutex_unlock(&hook_mutex);

  return ret;
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
