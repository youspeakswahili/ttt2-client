#!/bin/sh
# Builds the Windows package with llvm-mingw: TTT2 Client.exe + xinput9_1_0.dll (static SDL3, no extra DLLs).
set -e
W=${W:-/home/claude/work}
T=/opt/llvm-mingw/bin/x86_64-w64-mingw32
HERE=$(cd "$(dirname "$0")" && pwd)
I=$HERE/third_party/imgui
OUT=$W/out
mkdir -p "$OUT" "$W/obj"
W=$W "$HERE/input/build.sh"
$T-windres "$HERE/client/res/client.rc" -O coff -o "$W/obj/client_res.o"
$T-clang++ -O2 -std=c++20 -DNDEBUG -mwindows -static \
  -I"$I" -I"$I/backends" -I"$W/sdl-install/include" \
  "$HERE/client/main.cpp" "$HERE/client/app.cpp" "$HERE/client/builder.cpp" "$HERE/client/platform_win.cpp" \
  "$I/imgui.cpp" "$I/imgui_draw.cpp" "$I/imgui_tables.cpp" "$I/imgui_widgets.cpp" \
  "$I/backends/imgui_impl_sdl3.cpp" "$I/backends/imgui_impl_sdlrenderer3.cpp" "$W/obj/client_res.o" \
  -o "$OUT/TTT2 Client.exe" -L"$W/sdl-install/lib" -lSDL3 \
  -lwinhttp -ldwmapi -lversion -lsetupapi -lwinmm -limm32 -lole32 -loleaut32 -luuid -lcfgmgr32 -lgdi32 -luser32 \
  -ladvapi32 -lshell32 -lhid -Wl,--strip-all
echo "built: $OUT"
