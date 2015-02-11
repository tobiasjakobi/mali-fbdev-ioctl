#include "common.h"

#include "mali_ioctl.h"

static int fbdev_fd = -1;
static int mali_fd = -1;

static callbackfnc get_var_screeninfo_cb = NULL;
static callbackfnc put_var_screeninfo_cb = NULL;
static callbackfnc get_fix_screeninfo_cb = NULL;
static callbackfnc pan_display_cb = NULL;
static callbackfnc waitforvsync_cb = NULL;
static callbackfnc get_fb_dma_buf_cb = NULL;

static callbackfnc mali_mem_map_ext_cb = NULL;

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

    case MALI_IOC_MEM_MAP_EXT:
      mali_mem_map_ext_cb = cb;
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
    fprintf(stderr, "open called (fbdev)\n");
    fbdev_fd = fptr(fake_fbdev, O_RDWR, 0);
    fprintf(stderr, "fake fbdev fd = %d\n", fbdev_fd);
    fd = fbdev_fd;
  } else if (strcmp(pathname, mali_name) == 0) {
    fprintf(stderr, "open called (mali)\n");
    mali_fd = fptr(pathname, flags, mode);
    fprintf(stderr, "mali fd = %d\n", mali_fd);
    fd = mali_fd;
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
  } else if (fd == mali_fd) {
    fprintf(stderr, "close called on mali fd\n");
    mali_fd = -1;
  }

  return fptr(fd);
}

int ioctl(int fd, unsigned long request, ...) {
  static ioctlfnc fptr = NULL;
  int ret = -1;
  callbackfnc cb;

  if (fptr == NULL)
    fptr = (ioctlfnc)dlsym(RTLD_NEXT, "ioctl");

  va_list args;

  va_start(args, request);
  void *p = va_arg(args, void *);
  va_end(args);

  if (fd == fbdev_fd) {
    switch (request) {
      case FBIOGET_VSCREENINFO:
        cb = get_var_screeninfo_cb;
        break;

      case FBIOPUT_VSCREENINFO:
        cb = put_var_screeninfo_cb;
        break;

      case FBIOGET_FSCREENINFO:
        cb = get_fix_screeninfo_cb;
        break;

      case FBIOPAN_DISPLAY:
        cb = pan_display_cb;
        break;

      case FBIO_WAITFORVSYNC:
        cb = waitforvsync_cb;
        break;

      case IOCTL_GET_FB_DMA_BUF:
        cb = get_fb_dma_buf_cb;
        break;

      default:
        cb = NULL;
        break;
    }

    if (cb) {
      ret = cb(p);
    } else {
      fprintf(stderr, "info: unhooked fbdev ioctl (0x%x) called\n", (unsigned int)request);
      ret = fptr(fd, request, p);
    }
  } else if (fd == mali_fd) {
    switch (request) {
      case MALI_IOC_MEM_MAP_EXT:
        cb = mali_mem_map_ext_cb;
        break;

      default:
        cb = NULL;
        break;
    }

    if (cb) {
      ret = cb(p);
    } else {
      fprintf(stderr, "info: unhooked mali ioctl (0x%x) called\n", (unsigned int)request);
      ret = fptr(fd, request, p);
    }
  } else {
    /* pass-through */
    ret = fptr(fd, request, p);
  }

  return ret;
}
