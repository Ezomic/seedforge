#!/usr/bin/env bash
# Assembles a Linux folder and tarball of Ichor, the counterpart of bin/package on Windows.
#
#   tools/package_linux.sh [-p preset] [-b build-dir] [-o out-dir] [--smoke] [--no-build]
#
# The folder holds ichor, the shared libraries the build tree supplied, content/, shaders/, scripts/, data/, art/ and ichor.sh,
# which starts `ichor --forged` with a default seed and grid. Everything is found beside the binary, so the folder can be moved
# anywhere. The tarball is <out-dir>.tar.gz. --smoke unpacks the tarball into a fresh folder and runs the unpacked binary headless
# from a different directory. It needs the build tree of a preset with ENGINE_RUNTIME on (linux-gpu); linux-headless-asan has no app.
# -b names the build tree that is built (when not --no-build) and packaged; it must be the tree the preset configures.
# Env: ICHOR_PACKAGE_SEED (default 1), ICHOR_PACKAGE_GRID (default 128).
set -u

preset=linux-gpu
bdir=
out=
smoke=0
build=1
while [ $# -gt 0 ]; do
  case "$1" in
    -p|--preset) [ $# -ge 2 ] || { echo "package: $1 needs a value" >&2; exit 2; }; preset=$2; shift 2 ;;
    -b|--build-dir) [ $# -ge 2 ] || { echo "package: $1 needs a value" >&2; exit 2; }; bdir=$2; shift 2 ;;
    -o|--out) [ $# -ge 2 ] || { echo "package: $1 needs a value" >&2; exit 2; }; out=$2; shift 2 ;;
    --smoke) smoke=1; shift ;;
    --no-build) build=0; shift ;;
    -h|--help) sed -n '2,10p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
    *) echo "package: unknown option $1" >&2; exit 2 ;;
  esac
done

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
bdir=${bdir:-$repo/engine/build/$preset}
out=${out:-$repo/dist/ichor-linux}
seed=${ICHOR_PACKAGE_SEED:-1}
grid=${ICHOR_PACKAGE_GRID:-128}

case "$seed" in ''|*[!0-9]*) echo "package: ICHOR_PACKAGE_SEED must be a whole number" >&2; exit 2 ;; esac
case "$grid" in ''|*[!0-9]*) echo "package: ICHOR_PACKAGE_GRID must be a whole number" >&2; exit 2 ;; esac
mkdir -p "$(dirname "$out")" || exit 1
outabs=$(cd "$(dirname "$out")" && pwd)/$(basename "$out")
case "$repo/" in "$outabs"/*) echo "package: refusing to replace $outabs, which contains the repository" >&2; exit 2 ;; esac
case "$outabs/" in "$repo/engine/"*|"$repo/bin/"*|"$repo/tools/"*|"$repo/data/"*|"$repo/art/"*|"$repo/docs/"*|"$repo/.github/"*|"$repo/.git/"*) echo "package: refusing to replace $outabs, which is inside the repository sources" >&2; exit 2 ;; esac

if [ "$build" = 1 ]; then
  (cd "$repo/engine" && cmake --preset "$preset" && cmake --build "$bdir" --target ichor) || { echo "package: build failed" >&2; exit 1; }
fi

exe=$bdir/app/ichor
[ -f "$exe" ] || { echo "package: $exe not found (does the preset build the app, ENGINE_RUNTIME on?)" >&2; exit 1; }
for dir in "$bdir/content" "$bdir/shaders"; do
  [ -d "$dir" ] || { echo "package: $dir not found" >&2; exit 1; }
done
compgen -G "$bdir/shaders/*.spv" >/dev/null || { echo "package: no compiled shaders in $bdir/shaders" >&2; exit 1; }

rm -rf "$out" "$outabs.tar.gz" || exit 1
mkdir -p "$out/shaders" "$out/scripts" "$out/art" || exit 1

cp "$exe" "$out/ichor" || exit 1
chmod 755 "$out/ichor" || exit 1
cp -r "$bdir/content" "$out/content" || exit 1
cp "$bdir"/shaders/*.spv "$out/shaders/" || exit 1
cp "$repo"/engine/content/scripts/*.lua "$out/scripts/" || exit 1
cp -r "$repo/data" "$out/data" || exit 1
cp -r "$repo/art/exile" "$repo/art/settlement_kit" "$out/art/" || exit 1

# Libraries the build tree supplied (vcpkg) go beside the binary; system libraries stay the target machine's own.
if [ "$(uname -s)" = Linux ] && command -v ldd >/dev/null 2>&1; then
  libs=$(ldd "$exe") || { echo "package: ldd failed on $exe" >&2; exit 1; }
  if echo "$libs" | grep -q "not found"; then
    echo "package: $exe has unresolved libraries:" >&2
    echo "$libs" | grep "not found" >&2
    exit 1
  fi
  mkdir -p "$out/lib" || exit 1
  echo "$libs" | awk '/=> \//{print $3}' | while read -r lib; do
    case "$lib" in "$bdir"/*|"$repo"/*|*/vcpkg_installed/*) cp -L "$lib" "$out/lib/" || exit 1 ;; esac
  done || exit 1
  rmdir "$out/lib" 2>/dev/null
fi

cat > "$out/ichor.sh" <<SCRIPT
#!/bin/sh
here=\$(dirname "\$(readlink -f "\$0")")
cd "\$here" || exit 1
LD_LIBRARY_PATH="\$here/lib\${LD_LIBRARY_PATH:+:\$LD_LIBRARY_PATH}" exec "\$here/ichor" --forged --seed $seed --grid $grid --campaign default "\$@"
SCRIPT
chmod 755 "$out/ichor.sh" || exit 1

(cd "$(dirname "$out")" && tar --owner=0 --group=0 --numeric-owner -czf "$outabs.tar.gz" "$(basename "$out")") || { echo "package: tar failed" >&2; exit 1; }
echo "package: $out ($(du -sh "$out" | cut -f1))"
echo "package: $outabs.tar.gz ($(du -sh "$outabs.tar.gz" | cut -f1))"

if [ "$smoke" = 1 ]; then
  work=$(mktemp -d) || exit 1
  trap 'rm -rf "$work"' EXIT
  mkdir "$work/unpacked" "$work/elsewhere" || exit 1
  tar -xzf "$outabs.tar.gz" -C "$work/unpacked" || { echo "package: the tarball did not unpack" >&2; exit 1; }
  unpacked=$work/unpacked/$(basename "$out")
  if [ "$(uname -s)" = Linux ] && command -v ldd >/dev/null 2>&1; then
    # Every library must come from the unpacked folder or the system, never from the build tree the binary was linked in.
    stray=$(LD_LIBRARY_PATH="$unpacked/lib" ldd "$unpacked/ichor" | awk '/=> \//{print $3}' | grep -E "^($bdir|$repo)/|/vcpkg_installed/" | grep -v "^$unpacked/")
    [ -z "$stray" ] || { echo "package: the binary still loads libraries from outside the package:" >&2; echo "$stray" >&2; exit 1; }
  fi
  echo "package: smoke run of $unpacked from $work/elsewhere"
  log=$work/smoke.log
  (cd "$work/elsewhere" && LD_LIBRARY_PATH="$unpacked/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" "$unpacked/ichor" --headless --forged --seed "$seed" --grid 64 --frames 30) > "$log" 2>&1
  status=$?
  cat "$log"
  [ "$status" = 0 ] || { echo "package: smoke run failed ($status)" >&2; exit 1; }
  grep -qF "packaged build: assets are read from $unpacked" "$log" || { echo "package: the binary did not resolve its assets beside itself" >&2; exit 1; }
  grep -q "exit after 30 frames" "$log" || { echo "package: the smoke run did not finish its frames" >&2; exit 1; }
  echo "package: start script run from $work/elsewhere"
  (cd "$work/elsewhere" && "$unpacked/ichor.sh" --headless --grid 64 --frames 30) > "$log" 2>&1
  status=$?
  cat "$log"
  [ "$status" = 0 ] || { echo "package: the start script run failed ($status)" >&2; exit 1; }
  grep -q "exit after 30 frames" "$log" || { echo "package: the start script run did not finish its frames" >&2; exit 1; }
  echo "package: smoke run passed"
fi
