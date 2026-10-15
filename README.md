# PCSX2

![Windows Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/windows_build_matrix.yml?label=%F0%9F%96%A5%EF%B8%8F%20Windows%20Builds)
![Linux Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/linux_build_matrix.yml?label=%F0%9F%90%A7%20Linux%20Builds)
![MacOS Build Status](https://img.shields.io/github/actions/workflow/status/PCSX2/pcsx2/macos_build_matrix.yml?label=%F0%9F%8D%8E%20MacOS%20Builds)
[![Codacy Badge](https://app.codacy.com/project/badge/Grade/1f7c0d75fec74d6daa6adb084e5b4f71)](https://app.codacy.com/gh/PCSX2/pcsx2/dashboard?utm_source=github.com&utm_medium=referral&utm_content=PCSX2/pcsx2&utm_campaign=Badge_Grade)
[![Discord Server](https://img.shields.io/discord/309643527816609793?color=%235CA8FA&label=PCSX2%20Discord&logo=discord&logoColor=white)](https://discord.com/invite/TCz3t9k)

PCSX2 is a free and open-source PlayStation 2 (PS2) emulator. Its purpose is to emulate the PS2's hardware, using a combination of MIPS CPU [Interpreters](<https://en.wikipedia.org/wiki/Interpreter_(computing)>), [Recompilers](https://en.wikipedia.org/wiki/Dynamic_recompilation) and a [Virtual Machine](https://en.wikipedia.org/wiki/Virtual_machine) which manages hardware states and PS2 system memory. This allows you to play PS2 games on your PC, with many additional features and benefits.

## Project Details

PCSX2 has been in development for more than 20 years. Past versions could only run a few public domain game demos, but newer versions can run most games at full speed, including popular titles such as Final Fantasy X and Devil May Cry 3. Visit the [PCSX2 compatibility list](https://pcsx2.net/compat/) to check the latest compatibility status of games (with more than 2500 titles tested).

Installers and binaries for both stable and nightly builds are available from [our website](https://pcsx2.net/downloads/).

## System Requirements

PCSX2 supports Windows, Linux, and Mac platforms. Our [setup documentation page](https://pcsx2.net/docs/setup/requirements) contains additional details on software and hardware requirements.

Please note that a BIOS dump from a legitimately-owned PS2 console is required to use the emulator. For more information, visit [this page](https://pcsx2.net/docs/setup/bios/).

## Contributing / Building

PCSX2 supports translation into other languages using [Crowdin](https://crowdin.com/project/pcsx2-emulator).

See the [Contribution Guide](https://pcsx2.net/docs/contributing/) for more info on how to contribute.

### Building this fork on an Intel Mac

Use the root build script with exactly one mode:

```sh
./build-macos.sh dev      # or: ./build-macos.sh d
./build-macos.sh release  # or: ./build-macos.sh r
./build-macos.sh native   # or: ./build-macos.sh n
```

Append `o` or `open` to launch that mode's app after a successful build, including
packaging and validation for release/native:

```sh
./build-macos.sh d o
./build-macos.sh release open
./build-macos.sh n o
```

Without this optional second argument, the script only builds. A failed build or
packaging step never launches the app. The launch option does not change build
settings or invalidate the incremental cache.

With no argument (or an invalid argument), the script prints usage and exits without
building. Full Xcode, its Metal tools, CMake 3.25+, Ninja, pkg-config, NASM, Git, and
Python 3 are required. Homebrew can supply build tools (`brew install cmake ninja
pkg-config nasm`); Xcode must be selected with `xcode-select`.

The first build downloads, verifies, and compiles the pinned runtime dependencies.
This can take a long time and substantial disk space. All modes reuse the same
dependency cache under `build/macos-deps`; changing the toolchain, SDK, dependency
recipe, or checkout location selects a fresh cache. The copied `deps` directory and
the previous checkout are not used. Dependency build output is saved as `build.log`
inside the selected cache directory.

Verified source archives are retained separately in `build/macos-downloads` and
reused across retries and dependency-cache changes. Each download is verified
against its pinned SHA-256 before use. Failed transfers or checksum mismatches are
retried three times; an unsuccessful download stops the build immediately. Rerun
the same command to retry without downloading valid cached archives again.

Development uses Ninja, native CPU instructions, `-O2`, debug symbols, precompiled
headers, and no LTO. Repeating the command rebuilds only affected objects and avoids
unnecessary configuration and Qt packaging. Optional `ccache` is detected
automatically, with cache and PCH settings scoped to this project. Outputs are in
`build/macos-dev/pcsx2-qt/PCSX2.app`.

After a relink or bundle-resource change, development builds repair library paths
and verify/sign the bundle so the executable and Qt plugins load the same bundled
Qt libraries. Unchanged builds skip this finalization step as well as Qt deployment.

Release uses `-O3` and whole-project ThinLTO with a persistent linker cache. It
retains the SSE4.1 baseline and PCSX2's runtime AVX/AVX2 selection for compatible
Intel Macs, with baseline scheduling tuned for this Mac's Coffee Lake CPU. It
preserves the project's floating-point rules and macOS 11 deployment target. It
bundles runtime libraries and checks their paths and deployment targets before
creating `build/macos-release/PCSX2-macos-intel.zip`. The app is in
`build/macos-release/pcsx2-qt/PCSX2.app`. Packaging ad-hoc signs all bundled libraries
and plugins before signing the app, then verifies the complete bundle. Developer ID
signing and notarization are separate.

Native uses the same `-O3`, ThinLTO, bundling, and validation as release, but selects
the project's `-march=native` path instead of the portable SIMD variants. It targets
the Intel CPU running the build and may require instructions absent on other Macs.
The app is in `build/macos-native/pcsx2-qt/PCSX2.app` and its archive is
`build/macos-native/PCSX2-macos-intel-native.zip`. It shares the portable dependency
cache and the same Metal shader settings; native targeting applies to code compiled
as part of the emulator build. Actual performance gains require measurement.

All three build directories are incremental and independent; switching modes does
not discard them. The default job count uses physical cores with a limit of one
job per 2 GiB of RAM (eight jobs on the current i9/32 GiB Mac). Override it with
`JOBS=12 ./build-macos.sh d`. Keep the build directories for fast rebuilds. Removing
the relevant build directory deliberately forces a fresh build. A shared lock
prevents simultaneous invocations from changing the dependency cache.

These modes optimize compilation and linking without changing emulator code or
game settings. Release and native prioritize runtime optimization while development
prioritizes edit/build/test time; the actual game speed difference depends on the
workload. PGO training is not required or enabled.
