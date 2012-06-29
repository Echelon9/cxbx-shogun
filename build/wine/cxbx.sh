#!/bin/bash

WINEDLLPATH=${0%/*}/d3dx8:${0%/*} ${0%/*}/cxbx.exe $*

