#!/bin/bash

function ioctl_test {
  local libmali="${HOME}/local/lib/mali-r4p0-fbdev"

  case "${1}" in
    "trace" )
      LD_LIBRARY_PATH=$libmali strace ./test 2> trace.out ;;
    "dump" )
      LD_PRELOAD=./dump.so LD_LIBRARY_PATH=$libmali ./test ;;
    "hook" )
      touch "/tmp/fake_fbdev"
      LD_PRELOAD=./hook.so LD_LIBRARY_PATH=$libmali ./test ;;
    "hooktrace" )
      touch "/tmp/fake_fbdev"
      LD_PRELOAD=./hook.so LD_LIBRARY_PATH=$libmali strace ./test 2> trace.out ;;
    * )
      LD_LIBRARY_PATH=$libmali ./test ;;
    esac
}

ioctl_test "${1}"
