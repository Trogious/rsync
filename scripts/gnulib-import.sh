#!/usr/bin/env bash
# scripts/gnulib-import.sh
#
# Imports the gnulib modules we need into the source tree under gl/ (and
# gl/m4/). Run from the repo root.
#
# Why a custom location? Upstream rsync's lib/ and m4/ already contain
# rsync's own files (lib/md5.c, lib/getaddrinfo.c from PostgreSQL, etc.).
# Writing gnulib polyfills into lib/ would clobber or mix with them, so we
# segregate gnulib into gl/ — the conventional name for gnulib's drop site
# when a project doesn't already use gnulib pervasively.
#
# Re-running this script after a gnulib bump is idempotent: it will only
# update the files it manages.

set -euo pipefail

GNULIB_TOOL="third_party/gnulib/gnulib-tool"
if [[ ! -x "$GNULIB_TOOL" ]]; then
    echo "ERROR: gnulib-tool not found at $GNULIB_TOOL."
    echo "Initialize the submodule with: git submodule update --init --recursive"
    exit 1
fi

mkdir -p gl gl/m4 build-aux

"$GNULIB_TOOL" --import \
    --dir=. \
    --lib=libgnu \
    --source-base=gl \
    --m4-base=gl/m4 \
    --doc-base=gl/doc \
    --tests-base=gl/tests \
    --aux-dir=build-aux \
    --no-conditional-dependencies \
    --no-vc-files \
    --macro-prefix=gl \
    posix_spawn \
    posix_spawnp \
    posix_spawn_file_actions_init \
    posix_spawn_file_actions_addclose \
    posix_spawn_file_actions_adddup2 \
    posix_spawn_file_actions_addopen \
    posix_spawnattr_init \
    posix_spawnattr_setflags \
    spawn-pipe \
    execute \
    wait-process \
    getopt-gnu \
    strdup-posix \
    strndup \
    strerror_r-posix \
    fnmatch \
    glob \
    sys_select \
    select \
    sys_socket \
    netinet_in \
    arpa_inet \
    canonicalize-lgpl \
    readlink \
    symlink \
    lstat \
    utimens \
    futimens \
    sigaction \
    sigprocmask \
    sleep \
    nanosleep
