#include <EGL/egl.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
  EGLDisplay disp;
  EGLContext ctx;
  EGLConfig conf;

  //struct fbdev_window nwin;

  EGLint major, minor;
  EGLint nconf;
  EGLBoolean ret;

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
    printf("error: eglGetDisplay failed\n");
    return -1;
  }

  ret = eglInitialize(disp, &major, &minor);
  if (ret != EGL_TRUE) {
    printf("error: eglInitialize failed\n");
    return -2;
  }

  ret = eglChooseConfig(disp, attribs, &conf, 1, &nconf);
  if (ret != EGL_TRUE) {
    printf("error: eglChooseConfig failed\n");
    return -2;
  } else {
    printf("info: configuration number = %d\n", nconf);
  }

  return 0;
}
