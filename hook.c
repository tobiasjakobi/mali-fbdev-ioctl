#include "common.h"

static int fbdev_fd = -1;

static callbackfnc get_var_screeninfo_cb = NULL;
static callbackfnc put_var_screeninfo_cb = NULL;
static callbackfnc get_fix_screeninfo_cb = NULL;
static callbackfnc pan_display_cb = NULL;
static callbackfnc waitforvsync_cb = NULL;
static callbackfnc get_fb_dma_buf_cb = NULL;

int setup_hook_callback(unsigned long req, callbackfnc cb) {
  int ret = 0;

	switch (req) {
    case FBIOGET_VSCREENINFO:
      get_var_screeninfo_cb = cb;
      break;

    case FBIOPUT_VSCREENINFO:
      put_var_screeninfo_cb = cb;
      break;

    case FBIOGET_FSCREENINFO:
      get_fix_screeninfo_cb = cb;
      break;

    case FBIOPAN_DISPLAY:
      pan_display_cb = cb;
      break;

    case FBIO_WAITFORVSYNC:
      waitforvsync_cb = cb;
      break;

    case IOCTL_GET_FB_DMA_BUF:
      get_fb_dma_buf_cb = cb;
      break;

    default:
      fprintf(stderr, "error: callback for unknown ioctl (0x%x)\n", (unsigned int)req);
      ret = -1;
      break;
  }

  return ret;
}

int open(const char *pathname, int flags, mode_t mode) {
  static openfnc fptr = NULL;
  int fd;

  if (fptr == NULL)
    fptr = (openfnc)dlsym(RTLD_NEXT, "open");

  if (strcmp(pathname, fbdev_name) == 0) {
    fprintf(stderr, "open called (pathname = %s)\n", pathname);
    fbdev_fd = fptr(fake_fbdev, O_RDONLY, 0);
    fprintf(stderr, "fake fbdev fd = %d\n", fbdev_fd);
    fd = fbdev_fd;
  } else {
    fd = fptr(pathname, flags, mode);
  }

  return fd;
}

int close(int fd) {
  static closefnc fptr = NULL;

  if (fptr == NULL)
    fptr = (closefnc)dlsym(RTLD_NEXT, "close");

  if (fd == fbdev_fd) {
    fprintf(stderr, "close called on fake fbdev fd\n");
    fbdev_fd = -1;
  }

  return fptr(fd);
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
        if (get_var_screeninfo_cb)
          ret = get_var_screeninfo_cb(p);
        break;

      case FBIOPUT_VSCREENINFO:
        if (put_var_screeninfo_cb)
          ret = put_var_screeninfo_cb(p);
        break;

      case FBIOGET_FSCREENINFO:
        if (get_fix_screeninfo_cb)
          ret = get_fix_screeninfo_cb(p);
        break;

      case FBIOPAN_DISPLAY:
        if (pan_display_cb)
          ret = pan_display_cb(p);
        break;

      case FBIO_WAITFORVSYNC:
        if (waitforvsync_cb)
          ret = waitforvsync_cb(p);
        break;

      case IOCTL_GET_FB_DMA_BUF:
        if (get_fb_dma_buf_cb)
          ret = get_fb_dma_buf_cb(p);

      default:
        fprintf(stderr, "error: unknown ioctl (0x%x) called\n", (unsigned int)request);
        break;
    }
  } else {
    /* pass-through */
    ret = fptr(fd, request, p);
  }

  return ret;
}
