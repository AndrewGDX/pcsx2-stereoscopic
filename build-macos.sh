#!/bin/bash
# Intel macOS builds. Requires the Apple toolchain and Bash 3.2 or newer.
set -euo pipefail

usage() {
	cat >&2 <<'EOF'
Usage: ./build-macos.sh {dev|d|release|r|native|n} [open|o]

  dev, d       Optimized native development build; fast incremental Ninja rebuilds.
  release, r   Optimized portable Intel release with ThinLTO and a bundled ZIP.
  native, n    Release optimizations targeting this Intel CPU; for personal use.

Append open or o to launch the resulting app after a successful build.
Examples: ./build-macos.sh d    ./build-macos.sh d o
Optional environment: JOBS=N (default: physical cores, limited by memory).
The first build compiles and caches dependencies locally; later builds reuse them.
EOF
}

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
	usage
	exit 2
fi
case "$1" in
	dev|d) MODE=dev ;;
	release|r) MODE=release ;;
	native|n) MODE=native ;;
	*) usage; exit 2 ;;
esac
OPEN_APP=0
if [ "$#" -eq 2 ]; then
	case "$2" in
		open|o) OPEN_APP=1 ;;
		*) usage; exit 2 ;;
	esac
fi

die() { printf 'Error: %s\n' "$*" >&2; exit 1; }
require() { command -v "$1" >/dev/null 2>&1 || die "Missing $1. $2"; }
hash_stream() { shasum -a 256 | cut -d ' ' -f 1; }

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"
cd "$ROOT"
[ "$(uname -s)" = Darwin ] || die 'This script requires macOS.'
[ "$(uname -m)" = x86_64 ] || die 'This script targets Intel Macs.'
[ "$(sysctl -in sysctl.proc_translated 2>/dev/null || true)" != 1 ] || die 'Run on an Intel Mac, not under Rosetta.'
# CMake itself rejects parentheses in paths. Linker flags also require no newlines.
case "$ROOT" in *'('*|*')'*|*$'\n'*) die 'Move the checkout to a path without parentheses or newlines.' ;; esac

require xcrun 'Install full Xcode and select it with xcode-select.'
require cmake 'Install CMake 3.25 or newer (brew install cmake).'
require ninja 'Install Ninja (brew install ninja).'
require pkg-config 'Install pkg-config (brew install pkg-config).'
require nasm 'Install NASM for the pinned FFmpeg build (brew install nasm).'
require git 'Install the Xcode command line tools.'
require python3 'Install the Xcode command line tools or Python 3.'

CC="$(xcrun --find clang)"
CXX="$(xcrun --find clang++)"
SDKROOT="$(xcrun --sdk macosx --show-sdk-path)"
SDK_VERSION="$(xcrun --sdk macosx --show-sdk-version)"
xcrun --find metal >/dev/null || die 'The Metal compiler is missing from Xcode.'
xcrun --find metallib >/dev/null || die 'The Metal library tool is missing from Xcode.'
[ -d "$SDKROOT" ] || die 'The selected macOS SDK does not exist.'

PHYSICAL_CORES="$(sysctl -n hw.physicalcpu)"
MEMORY_BYTES="$(sysctl -n hw.memsize)"
MEMORY_JOBS=$((MEMORY_BYTES / 2147483648))
[ "$MEMORY_JOBS" -ge 1 ] || MEMORY_JOBS=1
DEFAULT_JOBS="$PHYSICAL_CORES"
[ "$DEFAULT_JOBS" -le "$MEMORY_JOBS" ] || DEFAULT_JOBS="$MEMORY_JOBS"
JOBS="${JOBS:-$DEFAULT_JOBS}"
case "$JOBS" in ''|*[!0-9]*|0*) die 'JOBS must be a positive integer without leading zeros.' ;; esac

# Tool locations may come from Homebrew; runtime libraries must come from our prefix.
# Prevent inherited flags/search paths from changing the selected CPU or dependencies.
unset CFLAGS CXXFLAGS CPPFLAGS LDFLAGS OBJCFLAGS OBJCXXFLAGS
unset CMAKE_PREFIX_PATH CMAKE_INCLUDE_PATH CMAKE_LIBRARY_PATH CMAKE_FRAMEWORK_PATH
unset CMAKE_TOOLCHAIN_FILE CMAKE_GENERATOR PKG_CONFIG_PATH PKG_CONFIG_LIBDIR
unset CPATH C_INCLUDE_PATH CPLUS_INCLUDE_PATH LIBRARY_PATH
unset DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH DYLD_FALLBACK_LIBRARY_PATH
export CC CXX SDKROOT
export PATH="$(dirname "$CC"):$PATH"
export MACOSX_DEPLOYMENT_TARGET=11.0
export CMAKE_BUILD_PARALLEL_LEVEL="$JOBS" NPROCS="$JOBS" MAKEFLAGS="-j$JOBS"

mkdir -p "$ROOT/build"
LOCK_DIR="$ROOT/build/.macos-build-lock"
if ! mkdir "$LOCK_DIR" 2>/dev/null; then
	die "Another macOS build is running. If a previous build was killed, remove the empty lock directory: $LOCK_DIR"
fi
trap 'rmdir "$LOCK_DIR" 2>/dev/null || true' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
START_SECONDS=$SECONDS

DEPENDENCY_BUILDER="$ROOT/.github/workflows/scripts/macos/build-dependencies.sh"
TOOLCHAIN_ID="$("$CC" --version)"
DEPS_KEY="$(
	{
		printf '%s\n' "$ROOT" "$CC" "$TOOLCHAIN_ID" "$SDKROOT" "$SDK_VERSION" 'x86_64 macOS 11.0'
		cmake --version
		shasum -a 256 "$DEPENDENCY_BUILDER" \
			"$ROOT/.github/workflows/scripts/macos/qt-macos11compat.patch" \
			"$ROOT/.github/workflows/scripts/common/shaderc-changes.patch" \
			"$ROOT/.github/workflows/scripts/common/qtapng-cmake.patch"
	} | hash_stream
)"
DEPS_DIR="$ROOT/build/macos-deps/$DEPS_KEY"
DEPS_PREFIX="$DEPS_DIR/prefix"
DEPS_STAMP="$DEPS_DIR/complete"
BUILD_DIR="$ROOT/build/macos-$MODE"
APP="$BUILD_DIR/pcsx2-qt/PCSX2.app"

printf 'Building %s | %s | SDK %s | %s jobs\n' "$MODE" "$(sysctl -n machdep.cpu.brand_string)" "$SDK_VERSION" "$JOBS"
if [ ! -f "$DEPS_STAMP" ]; then
	printf 'Building pinned dependencies once in %s\n' "$DEPS_DIR"
	mkdir -p "$DEPS_DIR/work"
	# The upstream builder uses make, so do not force the Ninja generator here.
	# It expects these variables to be defined even when empty.
	(
		cd "$DEPS_DIR/work"
		export PKG_CONFIG_PATH= LDFLAGS= CFLAGS= CXXFLAGS= BUILD_FFMPEG=1
		export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig:$DEPS_PREFIX/share/pkgconfig"
		export DEPS_DOWNLOAD_CACHE="$ROOT/build/macos-downloads"
		bash "$DEPENDENCY_BUILDER" "$DEPS_PREFIX"
	) 2>&1 | tee "$DEPS_DIR/build.log"
	printf '%s\n' "$DEPS_KEY" > "$DEPS_STAMP"
else
	printf 'Reusing cached dependencies.\n'
fi
for LIBRARY in libQt6Core.dylib libSDL3.dylib libshaderc_shared.dylib libMoltenVK.dylib libavcodec.dylib; do
	[ -f "$DEPS_PREFIX/lib/$LIBRARY" ] || die "Dependency cache is incomplete: $LIBRARY. Remove $DEPS_STAMP and retry."
done
[ -x "$DEPS_PREFIX/bin/macdeployqt" ] || die "macdeployqt is missing from $DEPS_PREFIX"
export PKG_CONFIG_LIBDIR="$DEPS_PREFIX/lib/pkgconfig:$DEPS_PREFIX/share/pkgconfig"
export PKG_CONFIG_PATH="$PKG_CONFIG_LIBDIR"

mkdir -p "$BUILD_DIR"
EXTRA_C_FLAGS=
EXTRA_CXX_FLAGS=
LINK_FLAGS=
LAUNCHER=
if [ "$MODE" = dev ]; then
	BUILD_TYPE=RelWithDebInfo
	IPO=OFF
	MULTI_ISA=OFF
	# PCH stays on; ccache is optional and its settings never change user defaults.
	if command -v ccache >/dev/null 2>&1; then
		LAUNCHER="$(command -v ccache)"
		export CCACHE_DIR="$ROOT/build/ccache" CCACHE_BASEDIR="$ROOT"
		export CCACHE_SLOPPINESS=pch_defines,time_macros
		EXTRA_C_FLAGS='-Xclang -fno-pch-timestamp'
		EXTRA_CXX_FLAGS="$EXTRA_C_FLAGS"
	fi
else
	BUILD_TYPE=Release
	IPO=ON
	if [ "$MODE" = native ]; then
		# This selects the project's -march=native path for the compiled emulator.
		MULTI_ISA=OFF
	else
		MULTI_ISA=ON
		# Scheduling tuned for Coffee Lake without requiring extra ISA features.
		EXTRA_C_FLAGS=-mtune=skylake
		EXTRA_CXX_FLAGS="$EXTRA_C_FLAGS"
	fi
	mkdir -p "$BUILD_DIR/thinlto-cache"
	LINK_FLAGS="-Xlinker -cache_path_lto -Xlinker \"$BUILD_DIR/thinlto-cache\" -Xlinker -mllvm -Xlinker -threads=$JOBS"
fi

CONFIG_ARGS=(
	-S "$ROOT" -B "$BUILD_DIR" -G Ninja
	"-DCMAKE_C_COMPILER=$CC" "-DCMAKE_CXX_COMPILER=$CXX"
	"-DCMAKE_OSX_SYSROOT=$SDKROOT" -DCMAKE_OSX_ARCHITECTURES=x86_64
	-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
	"-DCMAKE_BUILD_TYPE=$BUILD_TYPE" "-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=$IPO"
	-DLTO_PCSX2_CORE=OFF "-DDISABLE_ADVANCE_SIMD=$MULTI_ISA"
	"-DCMAKE_C_FLAGS=$EXTRA_C_FLAGS" "-DCMAKE_CXX_FLAGS=$EXTRA_CXX_FLAGS"
	-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2\ -g\ -DNDEBUG
	-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2\ -g\ -DNDEBUG
	-DCMAKE_C_FLAGS_RELEASE=-O3\ -DNDEBUG
	-DCMAKE_CXX_FLAGS_RELEASE=-O3\ -DNDEBUG
	"-DCMAKE_EXE_LINKER_FLAGS=$LINK_FLAGS"
	"-DCMAKE_C_COMPILER_LAUNCHER=$LAUNCHER" "-DCMAKE_CXX_COMPILER_LAUNCHER=$LAUNCHER"
	-DCMAKE_DISABLE_PRECOMPILE_HEADERS=OFF -DCMAKE_UNITY_BUILD=OFF
	-DCMAKE_EXPORT_COMPILE_COMMANDS=ON
	"-DCMAKE_PREFIX_PATH=$DEPS_PREFIX" "-DFFMPEG_ROOT=$DEPS_PREFIX"
	"-DCMAKE_IGNORE_PREFIX_PATH=$ROOT/deps;/usr/local;/opt/homebrew"
	-DCMAKE_FIND_USE_PACKAGE_REGISTRY=OFF -DCMAKE_FIND_USE_SYSTEM_PACKAGE_REGISTRY=OFF
	-DENABLE_QT_UI=ON -DUSE_VULKAN=ON -DUSE_LINKED_FFMPEG=ON
	-DENABLE_TESTS=OFF -DENABLE_GSRUNNER=OFF -DUSE_ASAN=OFF
	-DUSE_PGO_GENERATE=OFF -DUSE_PGO_OPTIMIZE=OFF -DUSE_VTUNE=OFF
	-DSKIP_POSTPROCESS_BUNDLE=ON
)
# Explicit component paths prevent FindFFMPEG's hardcoded Homebrew fallbacks.
for COMPONENT in avcodec avformat avutil swresample swscale; do
	CONFIG_ARGS+=("-DFFMPEG_${COMPONENT}_INCLUDE_DIR=$DEPS_PREFIX/include"
		"-DFFMPEG_${COMPONENT}_LIBRARY=$DEPS_PREFIX/lib/lib${COMPONENT}.dylib")
done
CONFIG_KEY="$(
	{
		shasum -a 256 "$ROOT/build-macos.sh"
		printf '%s\n' "$DEPS_KEY" "${CONFIG_ARGS[@]}"
		git rev-parse HEAD
		if [ "$MODE" != release ]; then sysctl -n machdep.cpu.features machdep.cpu.leaf7_features; fi
	} | hash_stream
)"
if [ ! -f "$BUILD_DIR/build.ninja" ] || [ ! -f "$BUILD_DIR/configure-key" ] ||
	[ "$(cat "$BUILD_DIR/configure-key")" != "$CONFIG_KEY" ]; then
	# find_package caches absolute library/config paths. A new dependency prefix
	# must not inherit entries pointing at the previous cache or checkout.
	if [ -f "$BUILD_DIR/CMakeCache.txt" ] && { [ ! -f "$BUILD_DIR/configured-deps" ] ||
		[ "$(cat "$BUILD_DIR/configured-deps")" != "$DEPS_KEY" ]; }; then
		cmake --fresh "${CONFIG_ARGS[@]}"
	else
		cmake "${CONFIG_ARGS[@]}"
	fi
	printf '%s\n' "$DEPS_KEY" > "$BUILD_DIR/configured-deps"
	printf '%s\n' "$CONFIG_KEY" > "$BUILD_DIR/configure-key"
fi

# Build just the app. The upstream ALL target also deploys Qt on every invocation.
cmake --build "$BUILD_DIR" --target pcsx2-qt --parallel "$JOBS"
# UUIDs survive install_name_tool and ad-hoc signing, unlike file hashes.
qt_uuid() { dwarfdump --uuid "$1" | awk '{print $2}'; }
QT_SOURCE_UUID="$(qt_uuid "$DEPS_PREFIX/lib/libQt6Core.6.dylib")"
QT_BUNDLE_UUID=
if [ -f "$APP/Contents/Frameworks/libQt6Core.6.dylib" ]; then
	QT_BUNDLE_UUID="$(qt_uuid "$APP/Contents/Frameworks/libQt6Core.6.dylib")"
fi
PACKAGE_KEY="$(printf '%s\n' "$DEPS_KEY" "$QT_SOURCE_UUID" | hash_stream)"
PACKAGED=0
if [ "$MODE" != dev ] || [ ! -f "$BUILD_DIR/packaged-deps" ] ||
	[ "$(cat "$BUILD_DIR/packaged-deps")" != "$PACKAGE_KEY" ] ||
	[ "$QT_BUNDLE_UUID" != "$QT_SOURCE_UUID" ] ||
	[ ! -f "$APP/Contents/Resources/qt.conf" ]; then
	# macdeployqt otherwise keeps existing libraries, even after a Qt upgrade.
	"$DEPS_PREFIX/bin/macdeployqt" "$APP" -no-strip -always-overwrite "-libpath=$DEPS_PREFIX/lib"
	[ "$(qt_uuid "$APP/Contents/Frameworks/libQt6Core.6.dylib")" = "$QT_SOURCE_UUID" ] ||
		die 'Deployment did not copy Qt Core from the dependency cache.'
	printf '%s\n' "$PACKAGE_KEY" > "$BUILD_DIR/packaged-deps"
	PACKAGED=1
fi

FINALIZE_STAMP="$BUILD_DIR/bundle-finalized"
if [ "$MODE" != dev ] || [ "$PACKAGED" -eq 1 ] || [ ! -f "$FINALIZE_STAMP" ] ||
	[ "$ROOT/build-macos.sh" -nt "$FINALIZE_STAMP" ] ||
	[ -n "$(find "$APP" -newer "$FINALIZE_STAMP" -print -quit)" ]; then
	# Relinking restores absolute dependency paths. Finalize changed development
	# bundles too, so the executable and deployed Qt plugins use the same libraries.
	python3 - "$APP" "$DEPS_PREFIX" <<'PY'
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

app = Path(sys.argv[1]).resolve()
dependency_prefix = str(Path(sys.argv[2]).resolve()) + '/'
executable_dir = app / 'Contents/MacOS'
magics = {0xFEEDFACE, 0xFEEDFACF, 0xCEFAEDFE, 0xCFFAEDFE, 0xCAFEBABE, 0xBEBAFECA}
binaries = []
for path in app.rglob('*'):
    if not path.is_file() or path.is_symlink():
        continue
    with path.open('rb') as stream:
        magic = stream.read(4)
    if len(magic) == 4 and struct.unpack('>I', magic)[0] in magics:
        binaries.append(path)

def output(*args):
    return subprocess.check_output(args, text=True)

commands = {binary: output('otool', '-l', str(binary)) for binary in binaries}
dependency_pattern = re.compile(
    r'cmd LC_(?:LOAD_DYLIB|LOAD_WEAK_DYLIB|REEXPORT_DYLIB|LOAD_UPWARD_DYLIB|LAZY_LOAD_DYLIB)'
    r'\s+cmdsize \d+\s+name (.*?) \(offset')

def runpaths(binary):
    paths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', commands[binary])
    return [path for path in paths if not path.startswith('/') or path.startswith(('/usr/lib/', '/System/Library/'))]

def expand(path, binary):
    return Path(path.replace('@loader_path', str(binary.parent)).replace('@executable_path', str(executable_dir)))

def internal(path):
    return os.path.commonpath([str(app), str(path.resolve())]) == str(app)

fixups = {}
for binary in binaries:
    options = []
    for dependency in dependency_pattern.findall(commands[binary]):
        if dependency.startswith(dependency_prefix):
            bundled = app / 'Contents/Frameworks' / Path(dependency).name
            if bundled.is_file():
                replacement = '@executable_path/../Frameworks/' + bundled.name
                options.extend(['-change', dependency, replacement])
                # Validate the planned paths before editing any binary.
                commands[binary] = commands[binary].replace(
                    'name ' + dependency + ' (offset', 'name ' + replacement + ' (offset')
    paths = re.findall(r'cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset', commands[binary])
    for rpath in paths:
        if rpath.startswith('/') and not rpath.startswith(('/usr/lib/', '/System/Library/')):
            options.extend(['-delete_rpath', rpath])
    identity = re.search(r'cmd LC_ID_DYLIB\s+cmdsize \d+\s+name (.*?) \(offset', commands[binary])
    if identity and identity.group(1).startswith('/'):
        try:
            relative = binary.relative_to(app / 'Contents/Frameworks')
        except ValueError:
            relative = Path(binary.name)
        options.extend(['-id', '@rpath/' + relative.as_posix()])
    if options:
        fixups[binary] = options

main = executable_dir / 'PCSX2'
if main not in binaries:
    sys.exit(f'Missing Mach-O app executable: {main}')
main_rpaths = [expand(path, main) for path in runpaths(main)]
errors = []
for binary in binaries:
    load_commands = commands[binary]
    deployment = re.findall(r'cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform \d+\s+minos ([\d.]+)', load_commands)
    deployment += re.findall(r'cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)', load_commands)
    if any(tuple(map(int, value.split('.')[:2])) > (11, 0) for value in deployment):
        errors.append(f'{binary}: deployment target exceeds macOS 11: {deployment}')
    rpaths = [expand(path, binary) for path in runpaths(binary)] + main_rpaths
    # otool -L also lists LC_ID_DYLIB (the library's own name). Inspect load
    # commands directly so identities are never treated as imported libraries.
    dependencies = dependency_pattern.findall(load_commands)
    for dependency in dependencies:
        if dependency.startswith(('/usr/lib/', '/System/Library/')):
            continue
        if dependency.startswith('@rpath/'):
            candidates = [base / dependency[len('@rpath/'):] for base in rpaths]
        elif dependency.startswith(('@loader_path/', '@executable_path/')):
            candidates = [expand(dependency, binary)]
        else:
            candidates = []
        if not any(path.exists() and internal(path) for path in candidates):
            errors.append(f'{binary}: dependency does not resolve inside the app: {dependency}')
if errors:
    sys.exit('\n'.join(errors))
# Apply metadata edits only after validation succeeds.
for binary, options in fixups.items():
    subprocess.run(['install_name_tool', *options, str(binary)], check=True)
# Deployment can reuse unsigned plugins. Sign every nested Mach-O, including
# unmodified libraries, before sealing the app; do this even when no fixups ran.
for binary in sorted(binaries, key=lambda path: len(path.parts), reverse=True):
    if binary != main:
        subprocess.run(['codesign', '--force', '--sign', '-', str(binary)], check=True)
subprocess.run(['codesign', '--force', '--sign', '-', str(app)], check=True)
subprocess.run(['codesign', '--verify', '--deep', '--strict', '--verbose=2', str(app)], check=True)
print(f'Bundle audit passed: {len(binaries)} Mach-O files; no external runtime libraries.')
PY
	touch "$FINALIZE_STAMP"
fi

if [ "$MODE" != dev ]; then
	ARCHIVE="$BUILD_DIR/PCSX2-macos-intel.zip"
	if [ "$MODE" = native ]; then
		ARCHIVE="$BUILD_DIR/PCSX2-macos-intel-native.zip"
	fi
	ditto -c -k --sequesterRsrc --keepParent "$APP" "$ARCHIVE"
	printf 'Archive: %s\n' "$ARCHIVE"
fi
printf 'App: %s\nFinished in %s seconds.\n' "$APP" "$((SECONDS - START_SECONDS))"
if [ "$OPEN_APP" -eq 1 ]; then
	printf 'Opening: %s\n' "$APP"
	open "$APP" || die "Build succeeded, but macOS could not open $APP"
fi
