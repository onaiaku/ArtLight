#!/usr/bin/env bash
# Exercise installation outcomes without touching real modules or services.
set -euo pipefail
source "$1"
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
MODULES_ROOT="$work/modules"
SYS_MODULE_ROOT="$work/sys"
SOURCE_DIR="$work/source"
mkdir -p "$SYS_MODULE_ROOT/$MODULE_NAME" "$SOURCE_DIR" "$MODULES_ROOT"
printf 'NEW\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
modprobe() { return "${load_result:-0}"; }
current_kernel_release() { printf 'test-kernel\n'; }
dkms() { :; }
install_with_dkms() { return "${dkms_result:-0}"; }
install_direct() { touch "$work/direct"; return "${direct_result:-0}"; }

# module_status asks about the installed file first, because a bare module name
# resolves through the module index and a stale index reads as "not installed".
# Both call shapes are therefore controlled independently here.
modinfo_name_result=0
modinfo_name_srcversion='NEW'
modinfo_file_result=0
modinfo_file_srcversion='NEW'
modinfo() {
  local target="${!#}"
  case "$target" in
    /*) [[ "$modinfo_file_result" -eq 0 ]] || return "$modinfo_file_result"
        printf '%s\n' "$modinfo_file_srcversion" ;;
    *)  [[ "$modinfo_name_result" -eq 0 ]] || return "$modinfo_name_result"
        printf '%s\n' "$modinfo_name_srcversion" ;;
  esac
}

installed_module() { # place a module file for the tested kernel
  mkdir -p "$MODULES_ROOT/test-kernel/updates/dkms"
  : > "$MODULES_ROOT/test-kernel/updates/dkms/${MODULE_NAME}.ko"
}

expect_status() {
  local expected=$1 result=0
  shift
  "$@" || result=$?
  [[ $result == "$expected" ]] || { echo "expected $expected, got $result: $*" >&2; exit 1; }
}

# Missing package sources must fail before any install attempt.
expect_status 1 install_module
for file in Makefile build-module dkms.conf vibeshine_ds5_uapi.h \
            vibeshine_ds5.h vibeshine_ds5_udc.c vibeshine_ds5_gadget.c \
            vibeshine_ds5_main.c; do touch "$SOURCE_DIR/$file"; done
expect_status 1 validate_source_tree
chmod 755 "$SOURCE_DIR/build-module"
expect_status 0 install_module
[[ ! -e "$work/direct" ]]

# Loading failure must not be hidden behind a successful DKMS build.
load_result=1
expect_status 1 install_module
load_result=0
printf 'OLD\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 4 install_module
expect_status 4 module_status
printf 'NEW\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 0 module_status

# DKMS failure falls back to a direct build, whose errors also propagate.
dkms_result=1
direct_result=1
expect_status 1 install_module
[[ -f "$work/direct" ]]
direct_result=0
expect_status 0 install_module
load_result=1
expect_status 1 install_module

# A module that is installed and loaded must never be reported as a failure
# just because its srcversion could not be read. The comparison is a staleness
# heuristic; its absence is not evidence of a failed install.
installed_module
rm "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 0 module_status                       # kernel exposes no srcversion
printf 'NEW\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 0 module_status                       # attribute back, matches file
rm "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"

# A stale module index must not mask a healthy install: the file on disk is the
# authority, and it agrees with what the kernel has loaded.
load_result=0
dkms_result=0
direct_result=0
modinfo_name_result=1
modinfo_file_result=0
installed_module
printf 'NEW\n' > "$SYS_MODULE_ROOT/$MODULE_NAME/srcversion"
expect_status 0 module_status
expect_status 0 install_module

# With the index stale AND the loaded copy genuinely older, the honest answer is
# "reboot", not "unavailable".
modinfo_file_srcversion='NEWER'
expect_status 4 module_status
modinfo_file_srcversion='NEW'

# Nothing on disk, nothing loaded, and no index entry: this must still fail.
modinfo_name_result=1
modinfo_file_result=1
rm -rf "$MODULES_ROOT/test-kernel" "$SYS_MODULE_ROOT/$MODULE_NAME"
expect_status 1 module_status
modinfo_name_result=0
modinfo_file_result=0

printf 'DualSense install: missing sources, DKMS/direct failures, load failures, stale modules, an unreadable srcversion, a stale module index, and the not-installed case covered.\n'
