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


/* ioctl used by the Mali blob.
 * FIXME: Apparantly the ioctl isn't used after all,
 * at least the fbdev blob doesn't call it. */
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
  /* file descriptors */
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
  unsigned pitch;
  unsigned bpp; /* bytes per pixel */
  unsigned size; /* size of one page */

  struct fb_var_screeninfo *fake_vscreeninfo;
  struct fb_fix_screeninfo *fake_fscreeninfo;
  unsigned long base_addr;
  void *fake_mmap;
  unsigned initialized;

  struct exynos_device *device;
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
  unsigned monitor_index;
};

typedef int (*hsetupfnc)(struct hook_data*);
typedef int (*hflipfnc)(struct hook_data*, unsigned);
typedef int (*hbufferfnc)(struct hook_data*, unsigned);

static const char *fbdev_name = "/dev/fb0";
static const char *mali_name = "/dev/mali";
static const char *fake_fbdev = "/dev/shm/fake_fbdev";

#endif /* _COMMON_H_ */
