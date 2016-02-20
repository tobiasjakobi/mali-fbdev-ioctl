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

#define EGL_EGLEXT_PROTOTYPES

#include <EGL/eglplatform_fb.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <EGL/fbdev_window.h>

#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <exynos_drmif.h>

#include "common.h"

#include <stdlib.h>
#include <assert.h>

typedef int (*getdrmfbcbfnc)();

/* Some envvars that the blob seems to use:
 * MALI_NOCLEAR
 * MALI_MAX_WINDOW_BUFFERS (defaults seems to be 2)
 * FRAMEBUFFER (fbdev device used by the blob, e.g. "/dev/fb1")
 * MALI_FBDEV (read, but apparantly not used)
 * MALI_SINGLEBUFFER (do all rendering in one buffer)
 * MALI_FLIP_PIXMAP
 * MALI_NEVERBLIT
 */

struct color3f {
  float r, g, b;
};

struct egl_attr {
  const char* name;
  EGLint id;
};

struct dmabuf_obj {
  int fd;
  uint32_t padding[15];
};

struct bo_obj {
  struct exynos_bo *bo;
  int fd;
};

static const struct color3f testcolors[3] = {
  {1.0, 0.5, 0.0},
  {0.0, 1.0, 0.5},
  {0.5, 0.0, 1.0}
};

const struct video_config vconf = {
  .width = 1280,
  .height = 720,
  .bpp = 4,
  .num_buffers = 3,
  .use_screen = 1,
  .connector_type = connector_hdmi
};

extern void setup_hook();

typedef void GL_APIENTRY (*fncEGLImageTargetRenderbufferStorageOES)(GLenum, GLeglImageOES);
typedef void GL_APIENTRY (*fncEGLImageTargetTexture2DOES)(GLenum, GLeglImageOES);

const char* egl_error_string(EGLint error) {
  if (error < 0x3000 || error > 0x300D)
    return "unknown error code";

  static const char *table[] = {
    "success",
    "not initialized",
    "bad access",
    "bad alloc",
    "bad attribute",
    "bad config",
    "bad context",
    "bad current surface",
    "bad display",
    "bad match",
    "bad native pixmap",
    "bad native window",
    "bad parameter",
    "bad surface"
  };

  return table[error - 0x3000];
}

/* Dump information about a EGL surface. */
static int dump_egl_surface(EGLDisplay dpy, EGLSurface surf) {
  /*
   * Taken from:
   * https://www.khronos.org/registry/egl/sdk/docs/man/html/eglSurfaceAttrib.xhtml
   */
  static const struct egl_attr attrs[] = {
    { .name = "EGL_MIPMAP_LEVEL", .id = EGL_MIPMAP_LEVEL },
    { .name = "EGL_MULTISAMPLE_RESOLVE", .id = EGL_MULTISAMPLE_RESOLVE },
    { .name = "EGL_SWAP_BEHAVIOR", .id = EGL_SWAP_BEHAVIOR }
  };
  static const unsigned num_attrs = sizeof(attrs) / sizeof(attrs[0]);

  EGLBoolean ret;
  EGLint val;
  unsigned i;

  for (i = 0; i < num_attrs; ++i) {
    ret = eglQuerySurface(dpy, surf, attrs[i].id, &val);

    if (ret != EGL_TRUE)
      return -1;

    fprintf(stderr, "\t%s = 0x%X\n", attrs[i].name, val);
  }

  return 0;
}

static struct bo_obj* alloc_bo(unsigned bytes) {
  struct exynos_device *dev;
  struct exynos_bo *bo;
  struct bo_obj* obj;

  static getdrmfbcbfnc hook_get_drm_fd = NULL;

  struct drm_prime_handle req = { 0 };
  int drm_fd;

  assert(bytes);

  if (!hook_get_drm_fd) {
    const char* err;

    err = dlerror();
    hook_get_drm_fd = dlsym(RTLD_DEFAULT, "hook_get_drm_fd");
    err = dlerror();

    if (err || !hook_get_drm_fd)
      return NULL;
  }

  drm_fd = hook_get_drm_fd();
  if (drm_fd < 0)
    return NULL;

  dev = exynos_device_create(drm_fd);
  if (!dev) {
    close(drm_fd);
    return NULL;
  }

  bo = exynos_bo_create(dev, bytes, 0x0);
  if (!bo)
    goto fail;

  req.handle = bo->handle;

  if (drmIoctl(drm_fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &req) < 0)
    goto fail_prime;

  obj = calloc(1, sizeof(struct bo_obj));

  obj->bo = bo;
  obj->fd = req.fd;

  return obj;

fail_prime:
  exynos_bo_destroy(bo);

fail:
  exynos_device_destroy(dev);

  return NULL;
}

static void free_bo(struct bo_obj *obj) {
  struct exynos_device *dev;

  assert(obj);

  dev = obj->bo->dev;
  exynos_bo_destroy(obj->bo);
  exynos_device_destroy(dev);

  free(obj);
}

static void dump_bo(struct bo_obj *obj) {
  uint32_t *addr;

  assert(obj);

  addr = exynos_bo_map(obj->bo);
  if (!addr)
    return;

  fprintf(stderr, "info: dma-buf %d: pixel[0] = 0x%X\n",
    obj->fd, addr[0]);
}

int pixmap_test(EGLDisplay dpy, EGLContext ctx,
                EGLConfig cnf, EGLSurface cursurf) {
  EGLSurface pixsurf;
  EGLBoolean ret;
  struct bo_obj* obj;

  struct dmabuf_obj dobj = {
    /*
     * When the blob issues dma-buf related ioctl to the kernel driver
     * (attach, get size, etc.) the fd is the value that ends up
     * in the mem_fd field.
     */
    .fd = -1
  };

  fbdev_pixmap pixmap = {
    .height = 640,
    .width = 480,
    .bytes_per_pixel = 4,
    .buffer_size = 32,
    .red_size = 8,
    .green_size = 8,
    .blue_size = 8,
    .alpha_size = 8,
    .luminance_size = 0,

    /* Abuse the UMP flag to let the blob issue a dma-buf import. */
    .flags = FBDEV_PIXMAP_SUPPORTS_UMP,

    .data = NULL,

    /*
     * The format field is a mystery. The blob uses the field in
     * mali_image_get_yuv_info() and checks it against a bunch
     * of 0x30fX (e.g. 0x30f1) values.
     * I don't recognize any of these values as identifiers
     * for YUV formats.
     */
    .format = 0x0
  };

  obj = alloc_bo(pixmap.height * pixmap.width * pixmap.bytes_per_pixel);
  if (!obj) {
    fprintf(stderr, "error: buffer object allocation failed\n");
    return -1;
  }

  dobj.fd = obj->fd;
  pixmap.data = (void*)&dobj;

  fprintf(stderr, "info: calling eglCreatePixmapSurface()\n");
  eglGetError();
  pixsurf = eglCreatePixmapSurface(dpy, cnf, (NativePixmapType)(&pixmap), NULL);
  if (pixsurf == EGL_NO_SURFACE) {
    fprintf(stderr, "error: eglCreatePixmapSurface() failed\n");
    fprintf(stderr, "errcode = %s\n", egl_error_string(eglGetError()));
    return -2;
  }

  fprintf(stderr, "info: dumping EGL pixmap surface\n");

  if (dump_egl_surface(dpy, pixsurf) < 0) {
    fprintf(stderr, "error: dumping failed\n");
    return -3;
  }

  fprintf(stderr, "info: calling eglMakeCurrent() on pixmap surface\n");
  ret = eglMakeCurrent(dpy, pixsurf, pixsurf, ctx);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglMakeCurrent() (on pixmap) failed\n");
    return -4;
  }

  fprintf(stderr, "info: clearing pixmap surface\n");
  glClearColor(1.0, 0.5, 0.5, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);

  fprintf(stderr, "info: calling glFinish()\n");
  glFinish();

  fprintf(stderr, "info: calling eglSwapBuffers()\n");
  eglSwapBuffers(dpy, pixsurf);

  fprintf(stderr, "info: calling eglWaitClient()\n");
  ret = eglWaitClient();
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglWaitClient() failed\n");
    return -5;
  }

  ret = eglMakeCurrent(dpy, cursurf, cursurf, ctx);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglMakeCurrent() (on regular) failed\n");
    return -6;
  }

  fprintf(stderr, "info: calling eglDestroySurface() (on pixmap)\n");
  eglDestroySurface(dpy, pixsurf);

  dump_bo(obj);
  free_bo(obj);

  return 0;
}

static int egl_image_test(EGLDisplay dpy) {
  EGLImageKHR image;
  GLuint renderbuffer, framebuffer;
  EGLBoolean ret;
  struct bo_obj* obj;

  struct dmabuf_obj dobj = {
    .fd = -1
  };

  /* For comments see pixmap_test(). */
  fbdev_pixmap pixmap = {
    .height = 640,
    .width = 480,
    .bytes_per_pixel = 4,
    .buffer_size = 32,
    .red_size = 8,
    .green_size = 8,
    .blue_size = 8,
    .alpha_size = 8,
    .luminance_size = 0,
    .flags = FBDEV_PIXMAP_SUPPORTS_UMP,
    .data = NULL,
    .format = 0x0
  };

  fncEGLImageTargetRenderbufferStorageOES glEGLImageTargetRenderbufferStorageOES;
  fncEGLImageTargetTexture2DOES glEGLImageTargetTexture2DOES;

  glEGLImageTargetRenderbufferStorageOES = (fncEGLImageTargetRenderbufferStorageOES)
    eglGetProcAddress("glEGLImageTargetRenderbufferStorageOES");
  glEGLImageTargetTexture2DOES = (fncEGLImageTargetTexture2DOES)
    eglGetProcAddress("glEGLImageTargetTexture2DOES");

  if (glEGLImageTargetRenderbufferStorageOES == 0 || glEGLImageTargetTexture2DOES == 0) {
    fprintf(stderr, "error: failed to get EGLImage function pointers\n");
    return -1;
  }

  obj = alloc_bo(pixmap.height * pixmap.width * pixmap.bytes_per_pixel);
  if (!obj) {
    fprintf(stderr, "error: buffer object allocation failed\n");
    return -2;
  }

  dobj.fd = obj->fd;
  pixmap.data = (void*)&dobj;

  /*
   * Call stack for eglCreateImageKHR():
   * eglCreateImageKHR
   * > _egl_create_image_KHR
   *   > _egl_create_image_KHR_pixmap
   *     > __egl_platform_pixmap_valid [this e.g. checks the UMP flag of the pixmap]
   *     > __egl_platform_map_pixmap
   *       > __egl_platform_pixmap_is_yuv [checks for YUV format]
   *         > mali_image_supported_yuv_format
   *           > mali_image_get_yuv_info [checks fbdev_pixmap.format]
   *       > __egl_platform_map_pixmap_rgb [RGB path]
   *         > __egl_platform_pixmap_support_ump [!]
   *         > mali_image_create_from_external_memory
   *           > mali_image_create_from_cpu_memory [1]
   *           > mali_image_create_from_ump_or_mali_memory [2]
   *             > _mali_base_common_mem_wrap_dma_buf
   *               > _mali_base_arch_mem_dma_buf_get_size
   *       > __egl_platform_map_pixmap_yuv [YUV path]
   *         > __egl_platform_get_pixmap_format
   *
   * We want to force the blob to use non-CPU memory here (branch [2]).
   *
   * Mali blob patching:
   * __egl_platform_pixmap_support_ump : always return 'supported'
   * __egl_platform_pixmap_valid : ignore the UMP flag
   */

  fprintf(stderr, "info: calling eglCreateImageKHR\n");
  eglGetError();
  image = (EGLImageKHR)eglCreateImageKHR(dpy, EGL_NO_CONTEXT, EGL_NATIVE_PIXMAP_KHR, (EGLClientBuffer)(&pixmap), NULL);
  if (image == EGL_NO_IMAGE_KHR) {
    fprintf(stderr, "error: eglCreateImageKHR() failed\n");
    fprintf(stderr, "errcode = %s\n", egl_error_string(eglGetError()));
    return -3;
  }

  /* bind image to renderbuffer */
  eglGetError();
  glGenRenderbuffers(1, &renderbuffer);
  glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
  glEGLImageTargetRenderbufferStorageOES(GL_RENDERBUFFER, image);
  fprintf(stderr, "EGLImageTargetRenderbufferStorageOES: status = %s\n",
    egl_error_string(eglGetError()));

  /* bind renderbuffer to framebuffer */
  eglGetError();
  glGenFramebuffers(1, &framebuffer);
  glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, renderbuffer);
  fprintf(stderr, "glFramebufferRenderbuffer: status = %s\n",
    egl_error_string(eglGetError()));

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
    fprintf(stderr, "info: framebuffer not complete\n");

  fprintf(stderr, "info: clearing EGL image\n");
  glClearColor(0.5, 1.0, 0.5, 0.75);
  glClear(GL_COLOR_BUFFER_BIT);

  fprintf(stderr, "info: calling glFinish()\n");
  glFinish();

  fprintf(stderr, "info: calling eglWaitClient()\n");
  ret = eglWaitClient();
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglWaitClient() failed\n");
    return -5;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  glDeleteFramebuffers(1, &framebuffer);
  glDeleteRenderbuffers(1, &renderbuffer);

  eglDestroyImageKHR(dpy, image);

  dump_bo(obj);
  free_bo(obj);

  return 0;
}

/* Dump information about a EGL configuration. */
static int dump_cfg(EGLDisplay dpy, EGLConfig cfg) {
  static const struct egl_attr attrs[] = {
    { .name = "EGL_ALPHA_SIZE", .id = EGL_ALPHA_SIZE },
    { .name = "EGL_ALPHA_MASK_SIZE", .id = EGL_ALPHA_MASK_SIZE },
    { .name = "EGL_BIND_TO_TEXTURE_RGB", .id = EGL_BIND_TO_TEXTURE_RGB },
    { .name = "EGL_BIND_TO_TEXTURE_RGBA", .id = EGL_BIND_TO_TEXTURE_RGBA },
    { .name = "EGL_BLUE_SIZE", .id = EGL_BLUE_SIZE },
    { .name = "EGL_BUFFER_SIZE", .id = EGL_BUFFER_SIZE },
    { .name = "EGL_COLOR_BUFFER_TYPE", .id = EGL_COLOR_BUFFER_TYPE },
    { .name = "EGL_CONFIG_CAVEAT", .id = EGL_CONFIG_CAVEAT},
    { .name = "EGL_CONFIG_ID", .id = EGL_CONFIG_ID},
    { .name = "EGL_CONFORMANT", .id = EGL_CONFORMANT},
    { .name = "EGL_DEPTH_SIZE", .id = EGL_DEPTH_SIZE},
    { .name = "EGL_GREEN_SIZE", .id = EGL_GREEN_SIZE},
    { .name = "EGL_LEVEL", .id = EGL_LEVEL},
    { .name = "EGL_LUMINANCE_SIZE", .id = EGL_LUMINANCE_SIZE},
    { .name = "EGL_MAX_PBUFFER_WIDTH", .id = EGL_MAX_PBUFFER_WIDTH},
    { .name = "EGL_MAX_PBUFFER_HEIGHT", .id = EGL_MAX_PBUFFER_HEIGHT},
    { .name = "EGL_MAX_PBUFFER_PIXELS", .id = EGL_MAX_PBUFFER_PIXELS},
    { .name = "EGL_MAX_SWAP_INTERVAL", .id = EGL_MAX_SWAP_INTERVAL},
    { .name = "EGL_MIN_SWAP_INTERVAL", .id = EGL_MIN_SWAP_INTERVAL},
    { .name = "EGL_NATIVE_RENDERABLE", .id = EGL_NATIVE_RENDERABLE},
    { .name = "EGL_NATIVE_VISUAL_ID", .id = EGL_NATIVE_VISUAL_ID},
    { .name = "EGL_NATIVE_VISUAL_TYPE", .id = EGL_NATIVE_VISUAL_TYPE },
    { .name = "EGL_RED_SIZE", .id = EGL_RED_SIZE },
    { .name = "EGL_RENDERABLE_TYPE", .id = EGL_RENDERABLE_TYPE },
    { .name = "EGL_SAMPLE_BUFFERS", .id = EGL_SAMPLE_BUFFERS },
    { .name = "EGL_SAMPLES", .id = EGL_SAMPLES },
    { .name = "EGL_STENCIL_SIZE", .id = EGL_STENCIL_SIZE },
    { .name = "EGL_SURFACE_TYPE", .id = EGL_SURFACE_TYPE },
    { .name = "EGL_TRANSPARENT_TYPE", .id = EGL_TRANSPARENT_TYPE },
    { .name = "EGL_TRANSPARENT_RED_VALUE", .id = EGL_TRANSPARENT_RED_VALUE },
    { .name = "EGL_TRANSPARENT_GREEN_VALUE", .id = EGL_TRANSPARENT_GREEN_VALUE },
    { .name = "EGL_TRANSPARENT_BLUE_VALUE", .id = EGL_TRANSPARENT_BLUE_VALUE }
  };
  static const unsigned num_attrs = sizeof(attrs) / sizeof(attrs[0]);

  EGLBoolean ret;
  EGLint val;
  unsigned i;

  for (i = 0; i < num_attrs; ++i) {
    ret = eglGetConfigAttrib(dpy, cfg, attrs[i].id, &val);

    if (ret != EGL_TRUE)
      return -1;

    fprintf(stderr, "\t%s = 0x%X\n", attrs[i].name, val);
  }

  return 0;
}

/* Dump all EGL configurations. */
static int dump_configs(EGLDisplay dpy) {
  EGLBoolean ret;
  EGLConfig *cfgs;
  EGLint num_cfgs, temp;

  unsigned i;

  ret = eglGetConfigs(dpy, NULL, 0, &num_cfgs);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglGetConfigs() [1] failed\n");
    return -1;
  }

  if (!num_cfgs) {
    fprintf(stderr, "error: zero EGL configs available\n");
    return -2;
  }

  cfgs = calloc(num_cfgs, sizeof(EGLConfig));

  ret = eglGetConfigs(dpy, cfgs, num_cfgs, &temp);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglGetConfigs() [2] failed\n");

    free(cfgs);
    return -3;
  }

  fprintf(stderr, "info: a total of %u EGL configs available\n", num_cfgs);

  for (i = 0; i < num_cfgs; ++i) {
    fprintf(stderr, "info: dumping EGL config (index = %u)\n", i);

    if (dump_cfg(dpy, cfgs[i]) < 0) {
      fprintf(stderr, "error: dumping failed\n");

      free(cfgs);
      return -4;
    }
  }

  free(cfgs);
  return 0;
}

int main(int argc, char* argv[]) {
  EGLDisplay disp;
  EGLContext ctx;
  EGLConfig conf;
  EGLSurface surf;

  struct mali_native_window nwin;

  EGLint major, minor;
  EGLint nconf;
  EGLBoolean ret;
  unsigned i;

  setup_hook();

  /* TODO: Try adding EGL_PIXMAP_BIT for EGL_SURFACE_TYPE.
   *       Also look into EGL_MATCH_NATIVE_PIXMAP. */
  static const EGLint attribs[] = {
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
    EGL_BLUE_SIZE, 8,
    EGL_GREEN_SIZE, 8,
    EGL_RED_SIZE, 8,
    EGL_ALPHA_SIZE, 8,
    EGL_NONE
  };

  disp = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  if (disp == EGL_NO_DISPLAY) {
    fprintf(stderr, "error: eglGetDisplay() failed\n");
    return -2;
  }

  fprintf(stderr, "info: calling eglInitialize()\n");
  ret = eglInitialize(disp, &major, &minor);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglInitialize() failed\n");
    return -3;
  } else {
    fprintf(stderr, "info: eglInitialize(): major = %d, minor = %d\n", major, minor);
  }

  static const struct {
    EGLint name;
    const char *string;
  } queries[] = {
    {EGL_CLIENT_APIS, "Client APIs"},
    {EGL_VENDOR, "Vendor"},
    {EGL_VERSION, "Version"},
    {EGL_EXTENSIONS, "Extensions"}
  };

  fprintf(stderr, "Querying information about the EGL connection:\n");
  for (i = 0; i < 4; ++i) {
    fprintf(stderr, "%s: %s\n", queries[i].string,
            eglQueryString(disp, queries[i].name));
  }

#ifndef NDEBUG
  if (dump_configs(disp) < 0) {
    fprintf(stderr, "error: dumping EGL configs failed\n");
    return -4;
  }
#endif

  ret = eglChooseConfig(disp, attribs, &conf, 1, &nconf);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglChooseConfig() failed\n");
    return -5;
  } else {
    EGLint cfg_id;
    eglGetConfigAttrib(disp, conf, EGL_CONFIG_ID, &cfg_id);

    fprintf(stderr, "info: EGL configuration ID = 0x%X\n", cfg_id);
  }

  nwin.width = vconf.width;
  nwin.height = vconf.height;

  fprintf(stderr, "info: calling eglCreateWindowSurface()\n");
  surf = eglCreateWindowSurface(disp, conf, &nwin, NULL);
  if (surf == EGL_NO_SURFACE) {
    fprintf(stderr, "error: eglCreateWindowSurface() failed\n");
    return -6;
  }

  static const EGLint ctxattribs[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
  };

  ctx = eglCreateContext(disp, conf, 0, ctxattribs);
  if (ctx == EGL_NO_CONTEXT) {
    fprintf(stderr, "error: eglCreateContext() failed\n");
    return -7;
  }

  ret = eglMakeCurrent(disp, surf, surf, ctx);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglMakeCurrent() failed\n");
    return -8;
  }

  if (pixmap_test(disp, ctx, conf, surf) < 0) {
    fprintf(stderr, "error: pixmap surface test failed\n");
    return -9;
  }

  if (egl_image_test(disp) < 0) {
    fprintf(stderr, "error: EGL image test failed\n");
    return -10;
  }

  ret = eglSwapInterval(disp, 1);
  if (ret != EGL_TRUE) {
    fprintf(stderr, "error: eglSwapInterval() failed\n");
    return -11;
  }

  fprintf(stderr, "info: start for glClear() loop\n");
  for (i = 0; i < 30; ++i) {
    const unsigned cidx = i % 3;

    glClearColor(testcolors[cidx].r, testcolors[cidx].g, testcolors[cidx].b, 1.0);
    glFlush();
    fprintf(stderr, "info: calling glClear()\n");
    glClear(GL_COLOR_BUFFER_BIT);
    glFlush();
    fprintf(stderr, "info: calling eglSwapBuffers()\n");
    eglSwapBuffers(disp, surf);
  }
  fprintf(stderr, "info: loop finished\n");

  eglMakeCurrent(disp, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  fprintf(stderr, "info: calling eglDestroyContext()\n");
  eglDestroyContext(disp, ctx);
  fprintf(stderr, "info: calling eglDestroySurface()\n");
  eglDestroySurface(disp, surf);
  fprintf(stderr, "info: calling eglTerminate()\n");
  eglTerminate(disp);

  return 0;
}
