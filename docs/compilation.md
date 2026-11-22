# Building piccolo

How to build piccolo from source, one section per platform. Every dependency
except the toolchain and the Vulkan SDK headers is a git submodule in `deps/`,
so a checkout with its submodules builds on its own.

- [Linux (Debian / Ubuntu)](#linux-debian--ubuntu)
- [Windows (MSYS2)](#windows-msys2)

## Linux (Debian / Ubuntu)

piccolo builds with the distribution's GCC, CMake and Vulkan headers; SDL and
every other library come from `deps/`. Other distributions work the same with
their own package names (SDL's `deps/SDL/docs/README-linux.md` lists them for
Fedora and others).

`tools/build-linux.sh` does it all in one go from a clone: with `--deps` it
first installs the packages of step 1 (apt, with sudo), then fetches the
submodules not checked out yet (step 2), builds piccolo and librashader and
installs them to `~/.local/share/piccolo` (`--debug`, `--test`,
`--no-librashader`; `--help` lists them). It adds no menu entry: that's for
the packages.

### 1. The packages

```sh
sudo apt-get install --no-install-recommends git build-essential pkg-config cmake ninja-build \
    libvulkan-dev libssl-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev \
    libxss-dev libxtst-dev libxkbcommon-dev libwayland-dev libdecor-0-dev \
    libegl-dev libgl-dev libgles-dev libdrm-dev libgbm-dev \
    libasound2-dev libpulse-dev libpipewire-0.3-dev \
    libdbus-1-dev libudev-dev libibus-1.0-dev \
    cargo
```

| Package        | What for |
|----------------|----------|
| `build-essential`, `cmake`, `ninja-build`, `pkg-config` | the build (CMake 3.25 or newer) |
| `libvulkan-dev` | the Vulkan headers and loader to link against |
| `libssl-dev`   | TLS / DTLS for WebRTC (`deps/libdatachannel`) |
| `libx11-dev` ... `libibus-1.0-dev` | SDL's X11, Wayland, OpenGL, audio and input backends |
| `cargo`        | only for `librashader.so` (shader presets, step 5); optional |

librashader needs a recent Rust. If the distribution's `cargo` is too old
(the build fails in step 5), install a current one with
[rustup](https://rustup.rs/) instead.

### 2. Get the source

The same as on Windows ([step 2](#2-get-the-source) there, without the
`core.autocrlf` line): the clone, the submodules and, optionally, the Test
libretro core.

### 3. Configure and build

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

After building, piccolo is copied to its folder, `~/.local/share/piccolo`
(`$XDG_DATA_HOME/piccolo`): `piccolo`, `lang/`, `layouts/`, `themes/`,
`shaders/` and `cores/test_libretro.so`. Files already there are left alone.
The options are the ones of the Windows table below
(`-DMULTITAP_PICCOLO_DIR`, `-DMULTITAP_COPY_FROM_WORKTREE=ON`, ...).

### 4. Run the tests (optional)

```sh
ctest --test-dir build --output-on-failure
```

### 5. librashader (optional: shader presets)

piccolo loads `librashader.so` at run time from its own folder (see the
Windows [step 6](#6-librashader-optional-shader-presets) for the pinned
commit):

```sh
CARGO_TARGET_DIR=build-librashader \
    cargo build --manifest-path deps/librashader/Cargo.toml -p librashader-capi \
    --release --locked --no-default-features --features runtime-vulkan
cp build-librashader/release/liblibrashader_capi.so ~/.local/share/piccolo/librashader.so
cp deps/librashader/LICENSE.md ~/.local/share/piccolo/librashader-LICENSE.md
```

## Windows (MSYS2)

piccolo is built natively on Windows with [MSYS2](https://www.msys2.org/):
the **UCRT64** environment (GCC, libstdc++ and the Universal CRT) on x86_64,
**CLANGARM64** (clang, libc++) on ARM64. The C/C++ runtimes are linked
statically, so the resulting `piccolo.exe` needs only Windows' own DLLs,
`vulkan-1.dll` (it comes with the GPU driver) and OpenSSL's `libssl` and
`libcrypto` from MSYS2 (WebRTC; step 4 copies them). The same steps are what CI runs
(`.github/workflows/windows.yml`).

Once the packages and the source are there (steps 1 and 2),
`tools/build-windows.sh` does steps 3 to 6 in one go: it builds piccolo and
librashader and deploys them to `%USERPROFILE%\piccolo` with the DLLs they
need (`--debug`, `--test`, `--no-librashader`; `--help` lists them).

### 1. Install MSYS2 and the packages

Install MSYS2 from <https://www.msys2.org/>, then open the **MSYS2 UCRT64**
shell (not "MSYS2 MSYS") and update everything first:

```sh
pacman -Syu
```

If it closes the shell to update itself, open UCRT64 again and run
`pacman -Syu` once more until nothing is left.

Then the packages piccolo needs:

```sh
pacman -S --needed git \
    mingw-w64-ucrt-x86_64-gcc \
    mingw-w64-ucrt-x86_64-cmake \
    mingw-w64-ucrt-x86_64-ninja \
    mingw-w64-ucrt-x86_64-vulkan-devel \
    mingw-w64-ucrt-x86_64-openssl \
    mingw-w64-ucrt-x86_64-ntldd \
    mingw-w64-ucrt-x86_64-rust
```

| Package        | What for |
|----------------|----------|
| `git`          | the checkout and its submodules |
| `gcc`          | the compiler (C and C++) |
| `cmake`, `ninja` | the build (CMake 3.25 or newer) |
| `vulkan-devel` | the Vulkan headers and loader to link against |
| `openssl`      | TLS / DTLS for WebRTC (`deps/libdatachannel`) |
| `ntldd`        | finds the MSYS2 DLLs to copy next to piccolo (step 4) |
| `rust`         | only for `librashader.dll` (shader presets, step 6); optional |

On ARM64 use the **MSYS2 CLANGARM64** shell and the `mingw-w64-clang-aarch64-`
prefix, with `clang` instead of `gcc`.

Never install a package with `pacman -Sy <package>` (refresh without
upgrading): MSYS2 doesn't support partial upgrades, and the new package can
need newer libraries than the ones installed (rust, for example, then fails to
start because it looks for a newer `libLLVM-NN.dll`). If that happens, run
`pacman -Syu`.

### 2. Get the source

Check out with LF line endings: themes, layouts and translations are copied
into the piccolo folder as they are.

```sh
git config --global core.autocrlf false
git clone https://github.com/multitap-org/piccolo.git
cd piccolo
```

The submodules the Windows build uses (`deps/librashader` without
`--recursive`: its own submodules are only its tests):

```sh
git submodule update --init --recursive \
    deps/SDL deps/imgui deps/libretro-common deps/libsamplerate \
    deps/gamemode deps/miniz deps/7zip deps/enet deps/monocypher deps/qrcodegen deps/beacon \
    deps/piccolo-shaders deps/piccolo-shaders-legacy \
    deps/common/hosts-common deps/common/frontends-common \
    deps/common/retroui deps/libretro/host-libretro deps/libretro/frontend-libretro \
    deps/libdatachannel
git submodule update --init deps/librashader
```

`deps/piccolo-shaders` (the curated slang presets) and
`deps/piccolo-shaders-legacy` (the OpenGL ES ones) are optional: without them
piccolo builds and runs with its own pass 0 and scale presets only.

Optionally the Test libretro core (every package ships it, and the tests use
it). Its nested submodules have SSH URLs; without a GitHub SSH key, fetch them
over HTTPS for this one command:

```sh
git -c url."https://github.com/".insteadOf="git@github.com:" \
    submodule update --init --recursive --checkout deps/test-libretro
```

### 3. Configure and build

From the UCRT64 shell, in the checkout:

```sh
cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
    -DCMAKE_SHARED_LINKER_FLAGS="-static -static-libgcc -static-libstdc++" \
    -DCMAKE_MODULE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build build
```

The three linker flags give `piccolo.exe` and `test_libretro.dll` the static
C/C++ runtime; without them they need `libstdc++-6.dll`, `libgcc_s_seh-1.dll`
and `libwinpthread-1.dll` from MSYS2 next to them.

After building, piccolo is copied to its folder, `%USERPROFILE%\piccolo` by
default: `piccolo.exe`, `lang\`, `layouts\`, `themes\`, `shaders\` and
`cores\test_libretro.dll`. Files already there (saves, states, settings,
cores) are left alone. Useful options:

| Option | Meaning |
|--------|---------|
| `-DMULTITAP_PICCOLO_DIR=C:/path/piccolo` | copy piccolo somewhere else (empty: don't copy) |
| `-DMULTITAP_COPY_FROM_WORKTREE=ON` | copy also when building from a git worktree (off by default, so parallel worktrees don't overwrite the installed piccolo) |
| `-DCMAKE_BUILD_TYPE=Debug` | debug build; turn on `-DMULTITAP_WARNINGS_AS_ERRORS=ON` while developing (Release builds give two harmless GCC warnings) |
| `-DMULTITAP_BUILD_TESTS=OFF` | skip the tests |

### 4. Copy the MSYS2 DLLs

What piccolo still links from MSYS2 (OpenSSL, for WebRTC with
`deps/libdatachannel`) has to sit next to it. From piccolo's folder, copy
every DLL that `ntldd` resolves to MSYS2's bin folder; the loop runs three
times so the DLLs just copied are followed too. `vulkan-1.dll` is left out:
the GPU driver's loader must win, and Windows looks in piccolo's folder first.
(`librashader.dll` is loaded at run time, so `ntldd` doesn't see it: step 6
copies its DLLs.) CI runs the same loop.

```sh
msys_bin=$(cygpath -w "$MSYSTEM_PREFIX/bin")
cd "$USERPROFILE/piccolo"
for i in $(seq 3); do
    for bin in $(ntldd -R *.exe | grep -iF "$msys_bin" | grep -iv 'vulkan-1\.dll' | cut -d">" -f2 | cut -d" " -f2); do
        cp -vu "$bin" .
    done
done
```

### 5. Run the tests (optional)

```sh
ctest --test-dir build --output-on-failure
```

### 6. librashader (optional: shader presets)

piccolo loads `librashader.dll` at run time from its own folder. Without it
the picture is drawn without shader presets. Only its C API with the Vulkan
runtime is built.

`deps/librashader` is pinned to upstream commit `9d0116f8` (0.12.1 plus one
fix, 2026-11-19), not to a release tag: 0.12.1 leaks Vulkan objects
(pipeline layouts, descriptor pools and sets) every time a preset is loaded,
and the validation layer reports them when piccolo quits. It goes back to a
release tag once one includes that fix. Build the DLL from the submodule as it
is checked out; a `librashader.dll` built from 0.12.1 still works, but it
leaks.

```sh
RUSTFLAGS="-C target-feature=+crt-static" CARGO_TARGET_DIR=build-librashader \
    cargo build --manifest-path deps/librashader/Cargo.toml -p librashader-capi \
    --release --locked --no-default-features --features runtime-vulkan
cp build-librashader/release/librashader_capi.dll "$USERPROFILE/piccolo/librashader.dll"
cp deps/librashader/LICENSE.md "$USERPROFILE/piccolo/librashader-LICENSE.md"
cp /ucrt64/bin/libstdc++-6.dll /ucrt64/bin/libgcc_s_seh-1.dll \
    /ucrt64/bin/libwinpthread-1.dll "$USERPROFILE/piccolo/"
```

The first build takes a few minutes. Built with MSYS2's Rust (the GNU
target), the DLL links GCC's C++ runtime dynamically (librashader's shader
compilers are C++), so it needs the three GCC DLLs copied next to it, from the
same MSYS2 that built it. `piccolo.exe` itself still doesn't use them.

CI builds it instead with Rust from [rustup](https://rustup.rs/): the MSVC
target, which also needs the Visual Studio Build Tools. That DLL needs no
runtime DLL at all (`+crt-static`); the same `cargo build` command works from
PowerShell there.

### 7. Check the DLLs (optional)

`piccolo.exe` should list no C/C++ runtime or SDL DLL (`libstdc++`, `libgcc`,
`libwinpthread`, `SDL3`, ...): only Windows' own, `vulkan-1.dll`,
`libssl-4-x64.dll` and `libcrypto-4-x64.dll` (step 4).
`librashader.dll` built with MSYS2's Rust lists `libstdc++-6.dll` too (step 6):

```sh
objdump -p "$USERPROFILE/piccolo/piccolo.exe" | grep 'DLL Name'
objdump -p "$USERPROFILE/piccolo/librashader.dll" | grep 'DLL Name'
```
