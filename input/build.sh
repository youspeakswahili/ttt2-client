#!/bin/sh
# Builds xinput9_1_0.dll (the controller layer) with llvm-mingw and a static SDL3.
set -e
W=${W:-/home/claude/work}
CXX=${CXX:-/opt/llvm-mingw/bin/x86_64-w64-mingw32-clang++}
mkdir -p "$W/out"
$CXX -O2 -std=c++20 -shared -static -Wall -Wextra -Wno-unused-parameter -I"$W/sdl-install/include" \
  "$(dirname "$0")/xinput_layer.cpp" "$(dirname "$0")/xinput9_1_0.def" -o "$W/out/xinput9_1_0.dll" \
  -L"$W/sdl-install/lib" -lSDL3 -lsetupapi -lwinmm -limm32 -lversion -lole32 -loleaut32 -luuid -lcfgmgr32 -lgdi32 -luser32 -ladvapi32 -lshell32 -lhid -Wl,--strip-all
