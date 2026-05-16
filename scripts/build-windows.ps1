# scripts/build-windows.ps1
#
# Build rsync.exe on Windows with MSVC, static CRT, static vcpkg deps.
# Run from an "x64 Native Tools Command Prompt for VS 2022" (or any shell
# where cl.exe / lib.exe / link.exe are on PATH — vcvars64.bat sets INCLUDE,
# LIB, and PATH appropriately).
#
# Requires: MSYS2 installed at C:\msys64 (autotools shell only), and vcpkg
# deps installed via 'vcpkg install --triplet x64-windows-static
# --x-manifest-root=vcpkg'. See scripts\bootstrap-windows.ps1 for setup.

$ErrorActionPreference = 'Stop'

$repoRoot = (Get-Item $PSScriptRoot).Parent.FullName
Set-Location $repoRoot

$vcpkgInstalled = Join-Path $repoRoot 'vcpkg\vcpkg_installed\x64-windows-static'
if (-not (Test-Path "$vcpkgInstalled\lib\libcrypto.lib")) {
    throw "vcpkg static deps not found at $vcpkgInstalled. Run: C:\vcpkg\vcpkg.exe install --triplet x64-windows-static --x-manifest-root=vcpkg"
}

# Convert Windows paths to MSYS form (C:\foo -> /c/foo) for bash.
$repoMsys     = ($repoRoot       -replace '\\', '/' -replace '^([A-Za-z]):', '/$1').ToLower()
$vcpkgMsys    = ($vcpkgInstalled -replace '\\', '/' -replace '^([A-Za-z]):', '/$1').ToLower()

# Sanity-check that the parent shell exposes cl.exe — otherwise vcvars64.bat
# wasn't sourced and MSYS bash won't find the compiler either.
if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "cl.exe is not on PATH. Run from an x64 Native Tools Command Prompt for VS 2022, or wrap this script with 'cmd /c call vcvars64.bat && powershell -F ...'."
}

# Make INCLUDE / LIB visible to the child process explicitly (MSYS bash
# uppercases env var names but preserves them; cl.exe reads INCLUDE/LIB
# to find headers/libs without /LIBPATH flags).
$env:INCLUDE = "$vcpkgInstalled\include;$env:INCLUDE"
$env:LIB     = "$vcpkgInstalled\lib;$env:LIB"

# Tell MSYS bash to import Windows PATH unchanged (with drive letters
# rewritten to /c/...). Without this, /etc/profile may reorder things and
# place MSYS /usr/bin/link.exe ahead of MSVC link.exe.
$env:MSYS2_PATH_TYPE = 'inherit'

# Run autotools + make inside MSYS2. We invoke bash with -c (NOT -lc) so
# /etc/profile doesn't reset PATH; vcvars64.bat has set up INCLUDE/LIB/PATH
# for MSVC and we want bash to see them intact. PowerShell expands
# $repoMsys before bash sees the script; literal bash variables are
# protected with a backtick.
& 'C:\msys64\usr\bin\bash.exe' -c @"
set -euo pipefail
cd '$repoMsys'

# Make sure MSYS utilities (autoreconf, make, sed, ...) are findable.
export PATH=`"/usr/bin:`$PATH`"

# rsync's hand-written Makefile.in has a self-regen rule that produces
# configure.sh (not configure) and aborts the build the FIRST time it
# detects a change. Generate configure.sh + config.h.in directly with
# the same commands the Makefile uses, then bypass autoreconf -fiv.
# Touch the matching .old files up-front so the safety check sees no
# diff on this run.
aclocal -I m4
autoconf -o configure.sh
autoheader && touch config.h.in
cp -p configure.sh configure.sh.old
cp -p config.h.in config.h.in.old
chmod +x configure.sh

# build-aux/compile wraps cl.exe to accept unix CLI flags (-c -o, -L, -l).
# build-aux/ar-lib does the same for lib.exe.
export CC='$repoMsys/build-aux/compile cl.exe -nologo'
export CXX='$repoMsys/build-aux/compile cl.exe -nologo -TP -EHsc'
export AR='$repoMsys/build-aux/ar-lib lib.exe -nologo'
export RANLIB=true
export CFLAGS='-MT -O2 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DWIN32_NATIVE -Z7'
export CXXFLAGS='-MT -O2 -DNDEBUG -D_CRT_SECURE_NO_WARNINGS -DWIN32_NATIVE -Z7'
export LDFLAGS='-Wl,-MAP:rsync.map -Wl,-DEBUG'
# cl.exe accepts .lib filenames directly on the command line; INCLUDE/LIB
# env vars (set by vcvars64.bat + extended above) tell it where to look.
export LIBS='libcrypto.lib zstd.lib lz4.lib xxhash.lib iconv.lib ws2_32.lib advapi32.lib iphlpapi.lib crypt32.lib secur32.lib userenv.lib bcrypt.lib user32.lib'

# Pre-seed iconv-related autoconf cache entries: configure runs link tests
# under MSYS bash which can't find iconv via standard probes (it expects
# pkg-config / GNU layout). We installed vcpkg's libiconv whose iconv.h
# #defines iconv_open -> libiconv_open and links via iconv.lib.
export ac_cv_header_iconv_h=yes
export ac_cv_search_iconv_open='none required'
export ac_cv_search_libiconv_open='none required'
export am_cv_proto_iconv_arg1=
export am_cv_proto_iconv='extern size_t iconv (iconv_t cd, char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);'

./configure.sh \
    --host=x86_64-pc-windows \
    --build=x86_64-pc-msys \
    --disable-acl-support \
    --disable-xattr-support \
    --disable-md2man \
    --disable-locale \
    --enable-roll-simd \
    --enable-roll-asm \
    --enable-ipv6

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
