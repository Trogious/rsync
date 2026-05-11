# scripts/build-windows.ps1
#
# Build rsync.exe on Windows with MSVC, static CRT, static vcpkg deps.
# Run from an "x64 Native Tools Command Prompt for VS 2022" (or any shell
# where cl.exe / lib.exe / link.exe are on PATH).
#
# Requires: MSYS2 installed at C:\msys64 (for the autotools shell), and
# vcpkg deps installed at C:\vcpkg with the x64-windows-static triplet.
# See scripts\bootstrap-windows.ps1 for the one-time setup.

$ErrorActionPreference = 'Stop'

$repoRoot = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $repoRoot

$vcpkgInstalled = 'C:\vcpkg\installed\x64-windows-static'
if (-not (Test-Path "$vcpkgInstalled\lib\libcrypto.lib")) {
    throw "vcpkg static deps not found at $vcpkgInstalled. Run: C:\vcpkg\vcpkg.exe install --triplet x64-windows-static --x-manifest-root=vcpkg"
}

# Convert Windows path to MSYS form (e.g. C:\foo -> /c/foo) for the bash invocation.
$repoMsys = ($repoRoot -replace '\\', '/' -replace '^([A-Za-z]):', '/$1').ToLower()

# Run autotools + make inside MSYS2 (MSYS shell, NOT mingw — we want plain
# bash with no toolchain pollution; MSVC tools come from the host PATH).
& 'C:\msys64\usr\bin\bash.exe' -lc @"
set -euo pipefail
cd '$repoMsys'

# Make MSVC tools visible to MSYS-bash by inheriting them from the host
# environment. The wrappers in gnulib's build-aux turn unix-style flags
# into MSVC-style.
export PATH=`"`$PATH:/usr/bin`"

autoreconf -fiv

# gnulib's build-aux/compile script wraps cl.exe to accept unix CLI flags.
# build-aux/ar-lib does the same for lib.exe.
export CC='`$PWD/build-aux/compile cl.exe -nologo'
export AR='`$PWD/build-aux/ar-lib lib.exe -nologo'
export CFLAGS='-MT -O2 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DWIN32_NATIVE -I$vcpkgInstalled/include -Igl'
export LDFLAGS='-LIBPATH:$vcpkgInstalled/lib /DEFAULTLIB:libcmt /NODEFAULTLIB:msvcrt'
export LIBS='libcrypto.lib zstd_static.lib lz4.lib xxhash.lib zlibstatic.lib ws2_32.lib advapi32.lib iphlpapi.lib crypt32.lib secur32.lib userenv.lib'

./configure \
    --host=x86_64-pc-windows \
    --build=x86_64-pc-msys \
    --disable-acl-support \
    --disable-xattr-support \
    --disable-md2man \
    --disable-locale \
    --disable-iconv-open

make -j`$(nproc) rsync.exe
"@

if (-not (Test-Path 'rsync.exe')) {
    throw 'rsync.exe was not produced'
}

# Embed the long-path / UTF-8 manifest.
$mt = Get-Command mt.exe -ErrorAction SilentlyContinue
if ($mt) {
    & $mt.Source -nologo -manifest win32\rsync.manifest -outputresource:'rsync.exe;1'
} else {
    Write-Warning 'mt.exe not found on PATH; skipping manifest embed.'
}

$sz = (Get-Item rsync.exe).Length
Write-Host ("Built rsync.exe ({0:N0} bytes)" -f $sz)
