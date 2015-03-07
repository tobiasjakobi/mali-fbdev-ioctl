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

#include "mali_ioctl.h"

static int fbdev_fd = -1;
static int mali_fd = -1;

static void dump_u32(const void *ptr) {
  fprintf(stderr, "%lu\n", *((const unsigned long*)ptr));
}

static void dump_fb_bitfield(const struct fb_bitfield *bf) {
  fprintf(stderr, "offset = %u, length = %u, msb_right = %u\n",
    bf->offset, bf->length, bf->msb_right);
}

static void dump_var_screeninfo(const void *ptr) {
  const struct fb_var_screeninfo *data = ptr;

  fprintf(stderr, "xres = %u, yres = %u\n", data->xres, data->yres);
  fprintf(stderr, "xres_virt = %u, yres_virt = %u\n",
    data->xres_virtual, data->yres_virtual);
  fprintf(stderr, "xoffset = %u, yoffset = %u\n",
    data->xoffset, data->yoffset);

  fprintf(stderr, "bpp = %u, grayscale = %u\n",
    data->bits_per_pixel, data->grayscale);

  fprintf(stderr, "red: "); dump_fb_bitfield(&data->red);
  fprintf(stderr, "green: "); dump_fb_bitfield(&data->green);
  fprintf(stderr, "blue: "); dump_fb_bitfield(&data->blue);
  fprintf(stderr, "transp: "); dump_fb_bitfield(&data->transp);

  fprintf(stderr, "nonstd = %u, activate = %u\n",
    data->nonstd, data->activate);

  fprintf(stderr, "height = %u, width = %u\n",
    data->height, data->width);

  fprintf(stderr, "aflags = %u\n", data->accel_flags);

  fprintf(stderr, "pixclock = %u, lmargin = %u, rmargin = %u\n",
    data->pixclock, data->left_margin, data->right_margin);
  fprintf(stderr, "umargin = %u, lmargin = %u\n",
    data->upper_margin, data->lower_margin);
  fprintf(stderr, "hslen = %u, vslen = %u, sync = %u\n",
    data->hsync_len, data->vsync_len, data->sync);
  fprintf(stderr, "vmode = %u, rotate = %u, cspace = %u\n",
    data->vmode, data->rotate, data->colorspace);
  fprintf(stderr, "res = {%u, %u, %u, %u}\n", data->reserved[0],
    data->reserved[1], data->reserved[2], data->reserved[3]);
}

static void dump_fix_screeninfo(const void *ptr) {
  const struct fb_fix_screeninfo *data = ptr;

  fprintf(stderr, "id = %s\n", data->id);
  fprintf(stderr, "smem_start = %lu, smem_len = %u\n",
    data->smem_start, data->smem_len);

  fprintf(stderr, "type = %u, type_aux = %u\n",
    data->type, data->type_aux);
  fprintf(stderr, "visual = %u, xpstep = %u, ypstep = %u\n",
    data->visual, data->xpanstep, data->ypanstep);
  fprintf(stderr, "ywstep = %u, llength = %u\n",
    data->ywrapstep, data->line_length);

  fprintf(stderr, "mmio_start = %lu, mmio_len = %u\n",
    data->mmio_start, data->mmio_len);
  fprintf(stderr, "accel = %u, caps = %u\n",
    data->accel, (unsigned int)data->capabilities);
  fprintf(stderr, "res = {%u, %u}\n", data->reserved[0], data->reserved[1]);
}

static void dump_get_vblank(const void *ptr) {
  const struct fb_vblank *data = ptr;

  fprintf(stderr, "flags = %u, count = %u\n",
    data->flags, data->count);
  fprintf(stderr, "vcount = %u, hcount = %u\n",
    data->vcount, data->hcount);
  fprintf(stderr, "reserved = {%u, %u, %u, %u}\n",
    data->reserved[0], data->reserved[0], data->reserved[0], data->reserved[0]);
}

static void dump_uk_notification_type(_mali_uk_notification_type val) {
  const char* msg;

  switch (val) {
    case _MALI_NOTIFICATION_CORE_SHUTDOWN_IN_PROGRESS:
      msg = "core shutdown in progress";
      break;
    case _MALI_NOTIFICATION_APPLICATION_QUIT:
      msg = "application quit";
      break;
    case _MALI_NOTIFICATION_SETTINGS_CHANGED:
      msg = "settings changed";
      break;
    case _MALI_NOTIFICATION_SOFT_ACTIVATED:
      msg = "soft activated";
      break;
    case _MALI_NOTIFICATION_PP_FINISHED:
      msg = "pp finished";
      break;
    case _MALI_NOTIFICATION_PP_NUM_CORE_CHANGE:
      msg = "pp num core change";
      break;
    case _MALI_NOTIFICATION_GP_FINISHED:
      msg = "gp finished";
      break;
    case _MALI_NOTIFICATION_GP_STALLED:
      msg = "gp stalled";
      break;
    default:
      msg = "unknown";
      break;
  }

  fprintf(stderr, "notification: %s\n", msg);
}

static void dump_uk_job_status(_mali_uk_job_status val) {
  const char* msg;

  switch (val) {
    case _MALI_UK_JOB_STATUS_END_SUCCESS:
      msg = "end success";
      break;
    case _MALI_UK_JOB_STATUS_END_OOM:
      msg = "end oom";
      break;
    case _MALI_UK_JOB_STATUS_END_ABORT:
      msg = "end abort";
      break;
    case _MALI_UK_JOB_STATUS_END_TIMEOUT_SW:
      msg = "end timeout sw";
      break;
    case _MALI_UK_JOB_STATUS_END_HANG:
      msg = "end hang";
      break;
    case _MALI_UK_JOB_STATUS_END_SEG_FAULT:
      msg = "end seg fault";
      break;
    case _MALI_UK_JOB_STATUS_END_ILLEGAL_JOB:
      msg = "end illegal job";
      break;
    case _MALI_UK_JOB_STATUS_END_UNKNOWN_ERR:
      msg = "end unknown err";
      break;
    case _MALI_UK_JOB_STATUS_END_SHUTDOWN:
      msg = "end shutdown";
      break;
    case _MALI_UK_JOB_STATUS_END_SYSTEM_UNUSABLE:
      msg = "end system unusable";
      break;
    default:
      msg = "unknown";
      break;
  }

  fprintf(stderr, "job status: %s\n", msg);
}

static void dump_uk_user_setting(_mali_uk_user_setting_t val) {
  const char* msg;

  switch (val) {
    case _MALI_UK_USER_SETTING_SW_EVENTS_ENABLE:
      msg = "sw events enable";
      break;
    case _MALI_UK_USER_SETTING_COLORBUFFER_CAPTURE_ENABLED:
      msg = "colorbuffer capture enabled";
      break;
    case _MALI_UK_USER_SETTING_DEPTHBUFFER_CAPTURE_ENABLED:
      msg = "depthbuffer capture enabled";
      break;
    case _MALI_UK_USER_SETTING_STENCILBUFFER_CAPTURE_ENABLED:
      msg = "stencilbuffer capture enabled";
      break;
    case _MALI_UK_USER_SETTING_PER_TILE_COUNTERS_CAPTURE_ENABLED:
      msg = "per tile counters capture enabled";
      break;
    case _MALI_UK_USER_SETTING_BUFFER_CAPTURE_COMPOSITOR:
      msg = "buffer capture compositor";
      break;
    case _MALI_UK_USER_SETTING_BUFFER_CAPTURE_WINDOW:
      msg = "buffer capture window";
      break;
    case _MALI_UK_USER_SETTING_BUFFER_CAPTURE_OTHER:
      msg = "buffer capture other";
      break;
    case _MALI_UK_USER_SETTING_BUFFER_CAPTURE_N_FRAMES:
      msg = "buffer capture n frames";
      break;
    case _MALI_UK_USER_SETTING_BUFFER_CAPTURE_RESIZE_FACTOR:
      msg = "buffer capture resize factor";
      break;
    case _MALI_UK_USER_SETTING_SW_COUNTER_ENABLED:
      msg = "sw counter enabled";
      break;
    default:
      msg = "unknown";
      break;
  }

  fprintf(stderr, "user setting: %s\n", msg);
}

static void dump_uk_gp_job_suspended(const _mali_uk_gp_job_suspended_s *data) {
  fprintf(stderr, "user_job_ptr = %u, cookie = %u\n",
    data->user_job_ptr, data->cookie);
}

static void dump_uk_gp_job_finished(const _mali_uk_gp_job_finished_s *data) {
  fprintf(stderr, "user_job_ptr = %u\n", data->user_job_ptr);
  dump_uk_job_status(data->status);
  fprintf(stderr, "heap_current_addr = %u\n@", data->heap_current_addr);
  fprintf(stderr, "perf_counter = {%u, %u}\n", data->perf_counter0, data->perf_counter1);
}

static void dump_uk_pp_job_finished(const _mali_uk_pp_job_finished_s *data) {
  unsigned i;

  fprintf(stderr, "user_job_ptr = %u\n", data->user_job_ptr);
  dump_uk_job_status(data->status);

  for (i = 0; i < _MALI_PP_MAX_SUB_JOBS; ++i) {
    fprintf(stderr, "perf_counter[%u] = {%u, %u}\n",
      i, data->perf_counter0[i], data->perf_counter1[i]);
  }

  fprintf(stderr, "perf_counter_src0 = %u, perf_counter_src1 = %u\n",
    data->perf_counter_src0, data->perf_counter_src1);
}

static void dump_uk_settings_changed(const _mali_uk_settings_changed_s *data) {
  dump_uk_user_setting(data->setting);
  fprintf(stderr, "value = %u\n", data->value);
}

static void dump_uk_soft_job_activated(const _mali_uk_soft_job_activated_s *data) {
  fprintf(stderr, "user_job = %u\n", data->user_job);
}

static void dump_mali_get_api_version(const void *ptr) {
  const _mali_uk_get_api_version_s *data = ptr;

  fprintf(stderr, "version = 0x%x, compatible = %d\n",
    data->version, data->compatible);
}

static void dump_mali_wait_for_notification(const void *ptr) {
  const _mali_uk_wait_for_notification_s *data = ptr;

  dump_uk_notification_type(data->type);

  switch (data->type) {
    case _MALI_NOTIFICATION_GP_STALLED:
      dump_uk_gp_job_suspended(&data->data.gp_job_suspended);
      break;
    case _MALI_NOTIFICATION_GP_FINISHED:
      dump_uk_gp_job_finished(&data->data.gp_job_finished);
      break;
    case _MALI_NOTIFICATION_PP_FINISHED:
      dump_uk_pp_job_finished(&data->data.pp_job_finished);
      break;
    case _MALI_NOTIFICATION_SETTINGS_CHANGED:
      dump_uk_settings_changed(&data->data.setting_changed);
      break;
    case _MALI_NOTIFICATION_SOFT_ACTIVATED:
      dump_uk_soft_job_activated(&data->data.soft_job_activated);
      break;
    default:
      break;
  }
}

static void dump_mali_get_user_settings(const void *ptr) {
  const _mali_uk_get_user_settings_s *data = ptr;
  unsigned i;

  for (i = 0; i < _MALI_UK_USER_SETTING_MAX; ++i) {
    fprintf(stderr, "settings[%u] = %u\n", i, data->settings[i]);
  }
}

static void dump_mali_mem_map_ext(const void *ptr) {
  const _mali_uk_map_external_mem_s *data = ptr;

  fprintf(stderr, "phys_addr = %u, size = %u\n",
    data->phys_addr, data->size);
  fprintf(stderr, "mali_address = %u, rights = %u\n",
    data->mali_address, data->rights);
  fprintf(stderr, "flags = %u, cookie = %u\n",
    data->flags, data->cookie);
}

static void dump_mali_post_notification(const void *ptr) {
  const _mali_uk_post_notification_s *data = ptr;

  dump_uk_notification_type(data->type);
}

static void dump_mali_pp_core_version_get(const void *ptr) {
  const _mali_uk_get_pp_core_version_s *data = ptr;

  fprintf(stderr, "version = 0x%x\n", data->version);
}

int open(const char *pathname, int flags, mode_t mode) {
  static openfnc fptr = NULL;
  int fd;

  if (fptr == NULL)
    fptr = (openfnc)dlsym(RTLD_NEXT, "open");

  fd = fptr(pathname, flags, mode);

  if (strcmp(pathname, fbdev_name) == 0) {
    fprintf(stderr, "open called (fbdev) = %d\n", fd);
    fbdev_fd = fd;
  } else if (strcmp(pathname, mali_name) == 0) {
    fprintf(stderr, "open called (mali) = %d\n", fd);
    mali_fd = fd;
  }

  return fd;
}

int close(int fd) {
  static closefnc fptr = NULL;
  int ret;

  if (fptr == NULL)
    fptr = (closefnc)dlsym(RTLD_NEXT, "close");

  ret = fptr(fd);

  if (fd == fbdev_fd) {
    fprintf(stderr, "close called on fbdev fd = %d\n", ret);
    fbdev_fd = -1;
  } else if (fd == mali_fd) {
    fprintf(stderr, "close called on mali fd = %d\n", ret);
    mali_fd = -1;
  }

  return ret;
}

int ioctl(int fd, unsigned long request, ...) {
  static ioctlfnc fptr = NULL;
  int ret = -1;

  if (fptr == NULL)
    fptr = (ioctlfnc)dlsym(RTLD_NEXT, "ioctl");

  va_list args;

  va_start(args, request);
  void *p = va_arg(args, void *);
  va_end(args);

  if (fd == fbdev_fd) {
    switch (request) {
      case FBIOGET_VSCREENINFO:
        fprintf(stderr, "FBIOGET_VSCREENINFO called\n");
        ret = fptr(fd, request, p);
        dump_var_screeninfo(p);
        break;

      case FBIOPUT_VSCREENINFO:
        fprintf(stderr, "FBIOPUT_VSCREENINFO called\n");
        dump_var_screeninfo(p);
        ret = fptr(fd, request, p);
        break;

      case FBIOGET_FSCREENINFO:
        fprintf(stderr, "FBIOGET_FSCREENINFO called\n");
        ret = fptr(fd, request, p);
        dump_fix_screeninfo(p);
        break;

      case FBIOPAN_DISPLAY:
        fprintf(stderr, "FBIOPAN_DISPLAY called\n");
        dump_var_screeninfo(p);
        ret = fptr(fd, request, p);
        break;

      case FBIO_WAITFORVSYNC:
        fprintf(stderr, "FBIO_WAITFORVSYNC called\n");
        ret = fptr(fd, request, p);
        fprintf(stderr, "retval = "); dump_u32(p);
        break;

      case FBIOGET_VBLANK:
        fprintf(stderr, "FBIOGET_VBLANK called\n");
        ret = fptr(fd, request, p);
        dump_get_vblank(p);
        break;

      case IOCTL_GET_FB_DMA_BUF:
        fprintf(stderr, "IOCTL_GET_FB_DMA_BUF called\n");
        ret = fptr(fd, request, p);
        fprintf(stderr, "retval = "); dump_u32(p);
        break;

      default:
        fprintf(stderr, "info: unknown fbdev ioctl (0x%x) called\n", (unsigned int)request);
        ret = fptr(fd, request, p);
        break;
    }
  } else if (fd == mali_fd) {
    switch (request) {
      case MALI_IOC_GET_API_VERSION:
        fprintf(stderr, "MALI_IOC_GET_API_VERSION called\n");
        ret = fptr(fd, request, p);
        dump_mali_get_api_version(p);
        break;

      case MALI_IOC_WAIT_FOR_NOTIFICATION:
        fprintf(stderr, "MALI_IOC_WAIT_FOR_NOTIFICATION called\n");
        ret = fptr(fd, request, p);
        dump_mali_wait_for_notification(p);
        break;

      case MALI_IOC_POST_NOTIFICATION:
        fprintf(stderr, "MALI_IOC_POST_NOTIFICATION called\n");
        dump_mali_post_notification(p);
        ret = fptr(fd, request, p);
        break;

      case MALI_IOC_GET_USER_SETTINGS:
        fprintf(stderr, "MALI_IOC_GET_USER_SETTINGS called\n");
        ret = fptr(fd, request, p);
        dump_mali_get_user_settings(p);
        break;

      case MALI_IOC_PP_CORE_VERSION_GET:
        fprintf(stderr, "MALI_IOC_PP_CORE_VERSION_GET called\n");
        ret = fptr(fd, request, p);
        dump_mali_pp_core_version_get(p);
      break;

      case MALI_IOC_MEM_MAP_EXT:
        fprintf(stderr, "MALI_IOC_MEM_MAP_EXT called\n");
        dump_mali_mem_map_ext(p);
        ret = fptr(fd, request, p);
        break;

      default:
        fprintf(stderr, "info: unknown mali ioctl (0x%x) called\n", (unsigned int)request);
        ret = fptr(fd, request, p);
        break;
    }
  } else {
    /* pass-through */
    ret = fptr(fd, request, p);
  }

  return ret;
}
