#!/bin/bash

LIBGL_DRIVERS_PATH=/usr/lib32/dri:$LIBGL_DRIVERS_PATH WINEDLLPATH=${0%/*}/d3dx8:${0%/*} ${0%/*}/cxbx.exe $*

