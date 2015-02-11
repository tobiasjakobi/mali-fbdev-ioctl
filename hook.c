#include "common.h"

static int fbdev_fd = -1;

static int emulate_get_var_screeninfo(void *ptr) {
  struct fb_var_screeninfo *data = ptr;

  memset(data, 0, sizeof(struct fb_var_screeninfo));

  data->xres = 1280;
  data->yres = 720;

  data->xres_virtual = 1280;
  data->yres_virtual = 720;

  data->bits_per_pixel = 16;

  data->red.offset = 10;
  data->green.offset = 5;
  data->blue.offset = 0;
  data->red.length = 5;
  data->green.length = 5;
  data->blue.length = 5;
  data->transp.length = 0;
  data->transp.offset = 0;

  return 0;
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
        fprintf(stderr, "FBIOGET_VSCREENINFO called\n");
        ret = emulate_get_var_screeninfo(p);
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
