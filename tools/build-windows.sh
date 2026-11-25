#!/bin/sh
# Builds the Windows zip on Linux: build-win/rt860cps-<version>-windows-x64.zip
#
#   tools/build-windows.sh [version]
#
# Needs: cmake, ninja, zip, pipx (or aqt on the PATH) and the MinGW-w64 compiler
#   sudo apt install cmake ninja-build zip pipx g++-mingw-w64-x86-64-posix
# The first run downloads Qt for Windows, and the same Qt for Linux for its build
# tools (moc), into build-win/qt: about 100 MB.
# With wine installed it also runs the core tests on the Windows build.
set -eu
cd "$(dirname "$0")/.."

QT=${QT_VERSION:-6.8.2}
OUT=build-win
qt_win=$PWD/$OUT/qt/$QT/mingw_64
qt_host=$PWD/$OUT/qt/$QT/gcc_64

get_qt() { if command -v aqt >/dev/null; then aqt "$@"; else pipx run aqtinstall "$@"; fi; }
mkdir -p "$OUT/qt"
[ -d "$qt_win" ] || get_qt install-qt -O "$OUT/qt" windows desktop "$QT" win64_mingw --archives qtbase
[ -d "$qt_host" ] || get_qt install-qt -O "$OUT/qt" linux desktop "$QT" linux_gcc_64 --archives qtbase icu

set -- ${1:+-DRT860_VERSION="$1"}
cmake -S . -B "$OUT" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/packaging/mingw-w64.cmake" \
  -DCMAKE_PREFIX_PATH="$qt_win" -DQT_HOST_PATH="$qt_host" "$@"
cmake --build "$OUT"

if command -v wine >/dev/null; then
  gcc_dir=$(dirname "$(x86_64-w64-mingw32-g++-posix -print-file-name=libstdc++-6.dll)")
  pthread_dir=$(dirname "$(x86_64-w64-mingw32-g++-posix -print-file-name=libwinpthread-1.dll)")
  WINEDEBUG=-all WINEPATH="$qt_win/bin;$gcc_dir;$pthread_dir" QT_QPA_PLATFORM=offscreen wine "$OUT/test_core.exe"
fi

(cd "$OUT" && cpack -G ZIP)
ls -l "$OUT"/*.zip
