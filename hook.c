#include "common.h"

#include <stdlib.h>
#include <errno.h>

#include "mali_ioctl.h"

static struct hook_data hook = {
  .fbdev_fd = -1,
  .mali_fd = -1,
  .drm_fd = -1,

  .open = NULL,
  .close = NULL,
  .ioctl = NULL,
  .mmap = NULL,
  .munmap = NULL,

  .width = 0,
  .height = 0,
  .num_buffers = 0,
  .base_addr = 0,
  .initialized = 0,

  .fake_vscreeninfo = NULL,
  .fake_fscreeninfo = NULL,
  .size = 0,

  .edev = NULL,
  .bos = NULL,
  .bo_fds = NULL
};

static hsetupfnc hinit = NULL;
static hsetupfnc hfree = NULL;

void setup_hook_callback(hsetupfnc init, hsetupfnc free) {
#ifdef HOOK_VERBOSE
  fprintf(stderr, "info: setup_hook_callback called\n");
#endif

  hinit = init;
  hfree = free;
}

static int emulate_get_var_screeninfo(void *ptr) {
#ifdef HOOK_VERBOSE
  fprintf(stderr, "info: emulate_get_var_screeninfo called\n");
#endif

  if (hook.fake_vscreeninfo) {
    memcpy(ptr, hook.fake_vscreeninfo, sizeof(struct fb_var_screeninfo));
    return 0;
  } else {
    return -ENOTTY;
  }
}

static int emulate_put_var_screeninfo(void *ptr) {
  fprintf(stderr, "info: emulate_put_var_screeninfo called\n");

  /* TODO: implement */
  return -ENOTTY;
}

static int emulate_get_fix_screeninfo(void *ptr) {
#ifdef HOOK_VERBOSE
  fprintf(stderr, "info: emulate_get_fix_screeninfo called\n");
#endif

  if (hook.fake_fscreeninfo) {
    memcpy(ptr, hook.fake_fscreeninfo, sizeof(struct fb_fix_screeninfo));
    return 0;
  } else {
    return -ENOTTY;
  }
}

static int emulate_pan_display(void *ptr) {
  const struct fb_var_screeninfo *data = ptr;

  fprintf(stderr, "info: emulate_pan_display called\n");
  fprintf(stderr, "info: xoffset = %u, yoffset = %u\n",
    data->xoffset, data->yoffset);

  /* TODO: implement */
  return -ENOTTY;
}

static int emulate_waitforvsync(void *ptr) {
  fprintf(stderr, "info: emulate_waitforvsync called\n");

  /* TODO: implement */
  return -ENOTTY;
}

static int emulate_get_fb_dma_buf(void *ptr) {
  fprintf(stderr, "info: emulate_get_fb_dma_buf called\n");

  /* TODO: implement */
  return -ENOTTY;
}

static int emulate_mali_mem_map_ext(void *ptr) {
#ifdef HOOK_VERBOSE
  fprintf(stderr, "info: emulate_mali_mem_map_ext called\n");
#endif

  _mali_uk_map_external_mem_s *data = ptr;
  int buf_fd = -1;

  if (hook.base_addr <= data->phys_addr) {
    const unsigned long offset = data->phys_addr - hook.base_addr;

    if ((offset % hook.size) == 0)
      buf_fd = hook.bo_fds[offset / hook.size];
  }

  if (buf_fd != -1) {
#ifdef HOOK_VERBOSE
    fprintf(stderr, "info: translating to dma-buf attach\n");
#endif

    _mali_uk_attach_dma_buf_s newdata = { 0 };
    int ret;

    newdata.ctx = data->ctx;
    newdata.mem_fd = buf_fd;
    newdata.size = data->size;
    newdata.mali_address = data->mali_address;
    newdata.rights = data->rights;
    newdata.flags = data->flags;

    ret = hook.ioctl(hook.mali_fd, MALI_IOC_MEM_ATTACH_DMA_BUF, &newdata);

    data->cookie = newdata.cookie;
    return ret;
  } else {
    return -ENOTTY;
  }
}

static int emulate_mali_mem_unmap_ext(void *ptr) {
#ifdef HOOK_VERBOSE
  fprintf(stderr, "info: emulate_mali_mem_unmap_ext called\n");
  fprintf(stderr, "info: translating to dma-buf release\n");
#endif

  /* data structures are compatible */
  return hook.ioctl(hook.mali_fd, MALI_IOC_MEM_RELEASE_DMA_BUF, ptr);
}

int open(const char *pathname, int flags, mode_t mode) {
  int fd;

  if (hook.open == NULL)
    hook.open = (openfnc)dlsym(RTLD_NEXT, "open");

  if (strcmp(pathname, fbdev_name) == 0) {
    fprintf(stderr, "open called (fbdev)\n");
    hook.fbdev_fd = hook.open(fake_fbdev, O_RDWR, 0);
#ifdef HOOK_VERBOSE
    fprintf(stderr, "fake fbdev fd = %d\n", hook.fbdev_fd);
#endif

    if (hinit(&hook)) {
      fprintf(stderr, "error: hook initialization failed\n");
      hook.close(hook.fbdev_fd);
      hook.fbdev_fd = -1;
      return -1;
    }

    fd = hook.fbdev_fd;
  } else if (strcmp(pathname, mali_name) == 0) {
    fprintf(stderr, "open called (mali)\n");
    hook.mali_fd = hook.open(pathname, flags, mode);
#ifdef HOOK_VERBOSE
    fprintf(stderr, "mali fd = %d\n", hook.mali_fd);
#endif
    fd = hook.mali_fd;
  } else {
    fd = hook.open(pathname, flags, mode);
  }

  return fd;
}

int close(int fd) {
  if (hook.close == NULL)
    hook.close = (closefnc)dlsym(RTLD_NEXT, "close");

  if (fd == hook.fbdev_fd) {
    fprintf(stderr, "close called on fake fbdev fd\n");

    if (hfree(&hook)) {
      fprintf(stderr, "error: freeing hook failed\n");
      return -1;
    }

    hook.fbdev_fd = -1;
  } else if (fd == hook.mali_fd) {
    fprintf(stderr, "close called on mali fd\n");
    hook.mali_fd = -1;
  }

  return hook.close(fd);
}

int ioctl(int fd, unsigned long request, ...) {
  int ret = -1;

  if (hook.ioctl == NULL)
    hook.ioctl = (ioctlfnc)dlsym(RTLD_NEXT, "ioctl");

  va_list args;

  va_start(args, request);
  void *p = va_arg(args, void *);
  va_end(args);

  if (fd == hook.fbdev_fd) {
    switch (request) {
      case FBIOGET_VSCREENINFO:
        ret = emulate_get_var_screeninfo(p);
        break;

      case FBIOPUT_VSCREENINFO:
        ret = emulate_put_var_screeninfo(p);
        break;

      case FBIOGET_FSCREENINFO:
        ret = emulate_get_fix_screeninfo(p);
        break;

      case FBIOPAN_DISPLAY:
        ret = emulate_pan_display(p);
        break;

      case FBIO_WAITFORVSYNC:
        ret = emulate_waitforvsync(p);
        break;

      case IOCTL_GET_FB_DMA_BUF:
        ret = emulate_get_fb_dma_buf_cb(p);
        break;

      default:
        fprintf(stderr, "info: unhooked fbdev ioctl (0x%x) called\n", (unsigned int)request);
        ret = hook.ioctl(fd, request, p);
        break;
    }
  } else if (fd == hook.mali_fd) {
    switch (request) {
      case MALI_IOC_MEM_MAP_EXT:
        ret = emulate_mali_mem_map_ext(p);
        break;

      case MALI_IOC_MEM_UNMAP_EXT:
        ret = emulate_mali_mem_unmap_ext(p);
        break;

      default:
#ifdef HOOK_VERBOSE
        fprintf(stderr, "info: unhooked mali ioctl (0x%x) called\n", (unsigned int)request);
#endif
        ret = hook.ioctl(fd, request, p);
        break;
    }
  } else {
    /* pass-through */
    ret = hook.ioctl(fd, request, p);
  }

  return ret;
}
