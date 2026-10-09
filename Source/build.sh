#!/bin/sh
# Cross-compiles DesktopRestore.exe for Windows ARM64 and x64 with llvm-mingw
# (https://github.com/mstorsjo/llvm-mingw). Usage: LLVM_MINGW=/path/to/llvm-mingw ./build.sh
set -e
cd "$(dirname "$0")"
[ -n "$LLVM_MINGW" ] && PATH="$LLVM_MINGW/bin:$PATH"

SRC="src/main.cpp src/app.cpp src/cli.cpp src/desktop.cpp src/storage.cpp src/menu.cpp"
CXXFLAGS="-std=c++17 -O2 -Wall -Wextra -Werror -municode -DUNICODE -D_UNICODE \
  -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -DNTDDI_VERSION=0x0A000000 -ffunction-sections -fdata-sections"
LIBS="-lole32 -loleaut32 -luuid -lshell32 -lshlwapi -lcomctl32 -lcomdlg32 -ladvapi32 -luser32 -lgdi32"

for arch in aarch64 x86_64; do
  out="build/$arch"
  mkdir -p "$out"
  "$arch-w64-mingw32-windres" -I src src/resource.rc -O coff -o "$out/resource.o"
  "$arch-w64-mingw32-clang++" $CXXFLAGS $SRC "$out/resource.o" -o "$out/DesktopRestore.exe" \
    -mwindows -static -s -Wl,--gc-sections $LIBS
  echo "built $out/DesktopRestore.exe"
done

# Unit tests (x64, run with Wine on Linux or natively on Windows).
x86_64-w64-mingw32-windres -I src src/resource.rc -O coff -o build/x86_64/resource_t.o
x86_64-w64-mingw32-clang++ $CXXFLAGS -mconsole -Isrc tests/tests.cpp src/app.cpp src/cli.cpp src/desktop.cpp \
  src/storage.cpp src/menu.cpp build/x86_64/resource_t.o -o build/x86_64/tests.exe -static $LIBS
echo "built build/x86_64/tests.exe"
