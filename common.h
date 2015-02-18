#ifndef _COMMON_H_
#define _COMMON_H_

#include <stdio.h>
#include <stdarg.h>
#include <dlfcn.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <linux/ioctl.h>
#include <linux/fb.h>

#include <string.h>

/* define from fcntl.h */
#define O_RDONLY  00000000
#define O_RDWR    00000002

/* mmap defines */
#define PROT_WRITE 0x2
#define MAP_SHARED 0x01


/* ioctl used by the Mali blob */
#define IOCTL_GET_FB_DMA_BUF _IOWR('m', 0xF9, __u32)

typedef int (*openfnc)(const char*, int, mode_t);
typedef int (*closefnc)(int);
typedef int (*ioctlfnc)(int, unsigned long, ...);
typedef void* (*mmapfnc)(void*, size_t, int, int, int, off_t);
typedef int (*munmapfnc)(void*, size_t);

/* forward declarations */
struct exynos_page;
struct exynos_fliphandler;
struct exynos_drm;

struct hook_data {
  int fbdev_fd;
  int mali_fd;
  int drm_fd;

  /* hooked system calls */
  openfnc open;
  closefnc close;
  ioctlfnc ioctl;
  mmapfnc mmap;
  munmapfnc munmap;

  unsigned width;
  unsigned height;
  unsigned long base_addr;
  unsigned initialized;

  void *fake_mmap;

  struct fb_var_screeninfo *fake_vscreeninfo;
  struct fb_fix_screeninfo *fake_fscreeninfo;
  unsigned pitch;
  unsigned size;
  unsigned bpp;

  struct exynos_device *edev;
  struct exynos_drm *drm;
  struct exynos_fliphandler *fliphandler;

  struct exynos_page *pages;
  unsigned num_pages;
  struct exynos_page *cur_page; /* currently displayed page */
  unsigned pageflip_pending;
};

struct video_config {
  unsigned width;
  unsigned height;
  unsigned bpp; /* bytes per pixel */
  unsigned num_buffers;
  unsigned use_screen;
};

typedef int (*hsetupfnc)(struct hook_data*);
typedef int (*hflipfnc)(struct hook_data*, unsigned);

static const char *fbdev_name = "/dev/fb0";
static const char *mali_name = "/dev/mali";
static const char *fake_fbdev = "/dev/shm/fake_fbdev";

#endif /* _COMMON_H_ */
