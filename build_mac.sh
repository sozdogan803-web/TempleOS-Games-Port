#!/bin/sh
# build_mac.sh <Game ...>  -- builds out/mac/<Game>.app from gen/ + runtime/ with clang
#   all games:  sh build_mac.sh $(sed 's#.*/\(.*\)/Run.HC#\1#; s#.*/##; s#\.HC$##' tools/test/games.txt)
# The library and runtime objects are cached in obj/mac and rebuilt when their sources' hash
# changes (gen.sh rewrites gen/tos_lib.c even when it comes out identical).  Games build in parallel.
set -e
cd "$(dirname "$0")"
CC="${CC:-clang}"
# zero-init: HolyC code reads locals before setting them (Line's _x), which MSVC tolerates but clang optimizes away
# Universal: Intel from macOS 10.6 Snow Leopard (core2: the first 64-bit Macs), Apple Silicon from 11
# (clang raises arm64's minimum to 11.0 by itself).  TOS_ARCHS="-arch arm64" builds one only.
ARCHS="${TOS_ARCHS:--arch arm64 -arch x86_64}"
CF="-std=c11 ${TOS_OPT:--O2} -g -w -ftrivial-auto-var-init=zero $ARCHS -mmacosx-version-min=10.6 -Xarch_x86_64 -march=core2 -Iruntime -Igen"
O=obj/mac
mkdir -p "$O" out/mac

# obj <out.o> <src> [extra flags]: compile when the source or headers changed
obj() {
  sum=$( (echo "$CC $CF $3"; cat "$2" runtime/*.h gen/tos_gen.h) | shasum | cut -c1-40)
  [ -f "$1" ] && [ "$(cat "$1.sum" 2>/dev/null)" = "$sum" ] && return 0
  echo "compiling $2"
  $CC $CF $3 -c "$2" -o "$1"
  echo "$sum" > "$1.sum"
}

if [ "$1" != "--one" ]; then
  [ $# -gt 0 ] || { echo "usage: $0 <Game ...>" >&2; exit 1; }
  obj $O/tos_lib.o gen/tos_lib.c &
  for f in rt_core rt_files rt_gr; do obj $O/$f.o runtime/$f.c & done
  obj $O/rt_mac.o runtime/rt_mac.m &
  wait
  for f in tos_lib rt_core rt_files rt_gr rt_mac; do [ -f $O/$f.o ] || exit 1; done
  printf '%s\n' "$@" | xargs -P "$(sysctl -n hw.ncpu)" -n 1 sh "$0" --one
  exit $?
fi

G="$2"
APP="out/mac/$G.app"
$CC $CF -c "gen/$G.c" -o "$O/$G.o"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
$CC $ARCHS -mmacosx-version-min=10.6 -o "$APP/Contents/MacOS/$G" $O/tos_lib.o "$O/$G.o" $O/rt_core.o $O/rt_files.o $O/rt_gr.o $O/rt_mac.o \
  -framework Cocoa -framework QuartzCore -framework AudioToolbox
if [ -f "out/icons/$G.ico" ]; then   # the game's TempleOS icon (largest image in the .ico)
  sips -s format png "out/icons/$G.ico" --out "$O/$G.png" >/dev/null
  sips -s format icns "$O/$G.png" --out "$APP/Contents/Resources/$G.icns" >/dev/null
fi
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>$G</string>
  <key>CFBundleIdentifier</key><string>org.templeos.games.$G</string>
  <key>CFBundleName</key><string>$G</string>
  <key>CFBundleIconFile</key><string>$G</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>5.03</string>
  <key>LSMinimumSystemVersion</key><string>10.6</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF
codesign --force -s - "$APP" 2>/dev/null   # ad-hoc: arm64 refuses unsigned code
echo "built $APP"
