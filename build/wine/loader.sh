#!/bin/bash

WINEADDRSPACESTART=1000000 WINEDLLPATH=${0%/*}/d3dx8:${0%/*} ${0%/*}/loader.exe $*

