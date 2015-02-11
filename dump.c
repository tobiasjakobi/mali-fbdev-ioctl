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
      /* TODO: _mali_uk_gp_job_suspended_s gp_job_suspended */
      break;
    case _MALI_NOTIFICATION_GP_FINISHED:
      /* TODO: _mali_uk_gp_job_finished_s  gp_job_finished */
      break;
    case _MALI_NOTIFICATION_PP_FINISHED:
      /* TODO: _mali_uk_pp_job_finished_s  pp_job_finished */
      break;
    case _MALI_NOTIFICATION_SETTINGS_CHANGED:
      /* TODO: _mali_uk_settings_changed_s setting_changed */
      break;
    case _MALI_NOTIFICATION_SOFT_ACTIVATED:
      /* TODO: _mali_uk_soft_job_activated_s soft_job_activated */
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
        fprintf(stderr, "unknown fbdev ioctl (0x%x) called\n", (unsigned int)request);
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
        fprintf(stderr, "unknown mali ioctl (0x%x) called\n", (unsigned int)request);
        break;
    }
  } else {
    /* pass-through */
    ret = fptr(fd, request, p);
  }

  return ret;
}
