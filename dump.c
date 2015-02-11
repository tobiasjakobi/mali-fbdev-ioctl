#include "common.h"

static int fbdev_fd = -1;

void dump_u32(const void *ptr) {
  fprintf(stderr, "%lu\n", *((const unsigned long*)ptr));
}

void dump_fb_bitfield(const struct fb_bitfield *bf) {
  fprintf(stderr, "offset = %u, length = %u, msb_right = %u\n",
    bf->offset, bf->length, bf->msb_right);
}

void dump_var_screeninfo(const void *ptr) {
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

void dump_fix_screeninfo(const void *ptr) {
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

int open(const char *pathname, int flags, mode_t mode) {
  static openfnc fptr = NULL;
  int fd;

  if (fptr == NULL)
    fptr = (openfnc)dlsym(RTLD_NEXT, "open");

  fd = fptr(pathname, flags, mode);

  if (strcmp(pathname, fbdev_name) == 0) {
    fprintf(stderr, "open called (pathname = %s) = %d\n", pathname, fd);
    fbdev_fd = fd;
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

  if (fd > 0 && fd == fbdev_fd) {
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

      case IOCTL_GET_FB_DMA_BUF:
        fprintf(stderr, "IOCTL_GET_FB_DMA_BUF called\n");
        ret = fptr(fd, request, p);
        fprintf(stderr, "retval = "); dump_u32(p);
        break;

      default:
        fprintf(stderr, "unknown ioctl (0x%x) called\n", (unsigned int)request);
        break;
    }
  } else {
    /* pass-through */
    ret = fptr(fd, request, p);
  }

  return ret;
}
