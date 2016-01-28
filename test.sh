#!/bin/bash

function ioctl_test {
  local libmali="${HOME}/local/lib/mali-r5p0-fbdev"
  local glmark="${HOME}/local/bin/glmark2-es2"

  case "${1}" in
    "trace" )
      LD_LIBRARY_PATH=$libmali strace ./test 2> trace.out ;;
    "dump" )
      LD_PRELOAD=./dump.so LD_LIBRARY_PATH=$libmali ./test ;;
    "hook" )
      touch "/dev/shm/fake_fbdev"
      LD_PRELOAD=./hook.so LD_LIBRARY_PATH=$libmali ./test ;;
    "hooktrace" )
      touch "/dev/shm/fake_fbdev"
      LD_PRELOAD=./hook.so LD_LIBRARY_PATH=$libmali strace ./test 2> trace.out ;;
    "glmark" )
      touch "/dev/shm/fake_fbdev"
      LD_PRELOAD=./hook.so LD_LIBRARY_PATH=$libmali $glmark ;;
    * )
      LD_LIBRARY_PATH=$libmali ./test ;;
    esac
}

ioctl_test "${1}"
