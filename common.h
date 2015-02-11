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
#define O_RDONLY 00000000
#define O_RDWR 00000002


/* ioctl used by the Mali blob */
#define IOCTL_GET_FB_DMA_BUF _IOWR('m', 0xF9, __u32)

typedef int (*openfnc)(const char*, int, mode_t);
typedef int (*closefnc)(int);
typedef int (*ioctlfnc)(int, unsigned long, ...);

typedef int (*callbackfnc)(void*);

struct fbdev_window {
  unsigned short width;
  unsigned short height;
};

static const char *fbdev_name = "/dev/fb0";
static const char *fake_fbdev = "/tmp/fake_fbdev";

#endif /* _COMMON_H_ */
