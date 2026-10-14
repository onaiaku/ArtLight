#!/usr/bin/env bash
set -euo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd -P)
source "$repo/scripts/linux_install.sh"
workdir=$(mktemp -d /tmp/artlight-installer-test.XXXXXXXX)
local_package="$workdir/artlight.pkg.tar.zst"
printf 'artlight_quiesce_or_abort() { return 1; }\n' > "$workdir/.INSTALL"
bsdtar -cf "$local_package" -C "$workdir" .INSTALL
stage_upgrade_guard "$local_package"
grep -Fxq 'AbortOnFail' "$workdir/hooks/00-artlight-quiesce.hook"
grep -Fxq "Exec = $workdir/preflight" "$workdir/hooks/00-artlight-quiesce.hook"
if "$workdir/preflight"; then exit 1; fi
calls="$workdir/calls"
source_root="$workdir/usr/src"
backup_root="$workdir/backups"
install_root="$workdir/root"
mkdir -p "$source_root" "$backup_root" "$install_root"
# Route only the fixture through alternate roots; never inspect real driver files.
eval "$(declare -f prepare_driver_replacement | sed '1s/prepare_driver_replacement/prepare_driver_replacement_fixture/')"
prepare_driver_replacement() { prepare_driver_replacement_fixture "$source_root" "$backup_root" "$install_root"; }
stat() {
  # The production /usr/src ancestry is root-owned and not world-writable.
  # Our fixture's /tmp ancestor is intentionally the only mocked metadata.
  if [[ "${!#}" == /tmp ]]; then printf '%s 755\n' "$EUID"; else command stat "$@"; fi
}
package_name=artlight
directory_owner=artlight
ownership_error=0
repository_metadata_available=1
pacman() {
  printf '%s\n' "$*" >> "$calls"
  if [[ "$1" == -Si ]]; then ((repository_metadata_available)); return; fi
  if [[ "$1" == -Sw ]]; then
    mkdir -p "$workdir/cache"
    command cp "$local_package" "$workdir/cache/artlight-1.0-1-x86_64.pkg.tar.zst"
    return
  fi
  if [[ "$1" == -Qp ]]; then printf '%s 1.0-1\n' "$package_name"; fi
  if [[ "$1" == -Qoq ]]; then
    local path=${!#}
    if [[ -d "$path" ]]; then
      if [[ $directory_owner == unowned ]]; then
        printf 'error: No package owns %s\n' "$path" >&2
        return 1
      fi
      printf '%s\n' "$directory_owner"
    elif [[ ${path##*/} == owned.h ]]; then
      printf 'unrelated-driver\n'
    else
      if ((ownership_error)); then printf 'error: database unavailable\n' >&2; return 1; fi
      printf 'error: No package owns %s\n' "$path" >&2
      return 1
    fi
  fi
}
parse_args --yes
install_from_package
! grep -Eq -- '^-S' "$calls"
grep -Fx -- "-U --noconfirm --ask=4 --hookdir $workdir/hooks -- $local_package" "$calls"
[[ $(wc -l < "$calls") == 2 ]]
: > "$calls"
package_name=unrelated
if (install_from_package); then exit 1; fi
[[ $(wc -l < "$calls") == 1 ]]
! grep -Eq -- '^-R|^-U|^-S' "$calls"
: > "$calls"
package_name=artlight
pacman_confirm=(); replacement_confirm=()
install_from_package
grep -Fx -- "-U --hookdir $workdir/hooks -- $local_package" "$calls"
printf 'Native installer validates package identity and confines conflict answers to replacement.\n'

directory="$source_root/vibeshine-drm-1.19.0"
mkdir "$directory"
printf 'old driver header\n' > "$directory/vibeshine_drm_vrr.h"
printf 'owned\n' > "$directory/owned.h"
printf 'unrelated\n' > "$workdir/other.h"
ln -s "$workdir/other.h" "$directory/linked.h"
printf 'literal wildcard\n' > "$directory/unsafe*.h"
: > "$calls"
install_from_package
expected='--overwrite usr/src/vibeshine-drm-1.19.0/vibeshine_drm_vrr.h'
! grep -Eq -- '^-S' "$calls"
grep -Fx -- "-U $expected --hookdir $workdir/hooks -- $local_package" "$calls"
[[ ${#driver_overwrite[@]} == 2 ]]
backups=("$backup_root"/vibeshine-driver-backup.*/vibeshine-drm-1.19.0/vibeshine_drm_vrr.h)
[[ ${#backups[@]} == 1 ]]
cmp "$directory/vibeshine_drm_vrr.h" "${backups[0]}"
[[ $(command stat -c %a "${backups[0]%/vibeshine-drm-1.19.0/*}") == 700 ]]
! grep -- '^-U.*owned.h\|^-U.*linked.h\|^-U.*unsafe' "$calls"

# No adoption from source directories owned by unknown packages.
directory_owner=unrelated
prepare_driver_replacement
[[ ${#driver_overwrite[@]} == 0 ]]
directory_owner=artlight

# Ownership-query failures are not evidence of an unowned file.
ownership_error=1
: > "$calls"
if (install_from_package); then exit 1; fi
! grep -Eq -- '^-S|^-U' "$calls"
ownership_error=0

# Do not follow source-directory symlinks or trust writable parents.
mkdir "$source_root/vibeshine-drm-2.0.0"
chmod 777 "$source_root/vibeshine-drm-2.0.0"
if (prepare_driver_replacement); then exit 1; fi
chmod 700 "$source_root/vibeshine-drm-2.0.0"
ln -s "$directory" "$source_root/vibeshine-drm-3.0.0"
prepare_driver_replacement
[[ ${#driver_overwrite[@]} == 2 ]]

# Repository installs use the same exact-path preflight, including --yes.
configure_pacman_repo() { :; }
parse_args --yes
: > "$calls"
install_from_repo
grep -Fx -- "-S --noconfirm --ask=4 $expected --cachedir $workdir/cache --hookdir $workdir/hooks artlight" "$calls"
! grep -Eq -- '^-S[^ ]*[yu]' "$calls"
download_release_package() { printf 'download-release\n' >> "$calls"; }
repository_metadata_available=0
: > "$calls"
install_from_repo
grep -Fx -- 'download-release' "$calls"
grep -Fx -- "-U --noconfirm --ask=4 $expected --hookdir $workdir/hooks -- $local_package" "$calls"
! grep -Eq -- '^-S[^ ]*[yu]' "$calls"
repository_metadata_available=1

# A failed package transaction leaves both the originals and private backups.
eval "$(declare -f pacman | sed '1s/pacman/pacman_fixture/')"
pacman() { if [[ "$1" == -U ]]; then return 1; fi; pacman_fixture "$@"; }
if (install_from_package); then exit 1; fi
cmp "$directory/vibeshine_drm_vrr.h" "${backups[0]}"

# A backup failure must stop before either pacman transaction starts.
cp() { return 1; }
: > "$calls"
if (install_from_package); then exit 1; fi
! grep -Eq -- '^-S|^-U' "$calls"
unset -f cp
printf 'Legacy driver leftovers are backed up and narrowly adopted without changing owned files.\n'

# Exact unowned DS5 package files from a manual development install are adopted.
mkdir -p "$install_root/usr/lib/modules-load.d" "$install_root/usr/libexec/vibeshine"
printf 'vibeshine_ds5\n' > "$install_root/usr/lib/modules-load.d/70-vibeshine-ds5.conf"
printf '#!/usr/bin/env bash\n' > "$install_root/usr/libexec/vibeshine/vibeshine-ds5-install"
prepare_driver_replacement
for relative in usr/lib/modules-load.d/70-vibeshine-ds5.conf \
  usr/libexec/vibeshine/vibeshine-ds5-install; do
  [[ " ${driver_overwrite[*]} " == *" $relative "* ]]
  package_backups=("$backup_root"/vibeshine-driver-backup.*/"$relative")
  [[ ${#package_backups[@]} == 1 ]]
  cmp "$install_root/$relative" "${package_backups[0]}"
done

# A manually installed DS5 tree must be adoptable by its first native package.
(
  directory="$source_root/vibeshine-ds5-2.0.0"
  mkdir "$directory"
  printf 'manual DS5 source\n' > "$directory/vibeshine_ds5_main.c"
  directory_owner=unowned
  prepare_driver_replacement
  [[ " ${driver_overwrite[*]} " == *" usr/src/vibeshine-ds5-2.0.0/vibeshine_ds5_main.c "* ]]
  ds5_backups=("$backup_root"/vibeshine-driver-backup.*/vibeshine-ds5-2.0.0/vibeshine_ds5_main.c)
  [[ ${#ds5_backups[@]} == 1 ]]
  cmp "$directory/vibeshine_ds5_main.c" "${ds5_backups[0]}"
)

# Check the installed driver's status and build it if the package hook failed.
driver_calls="$workdir/driver-calls"
driver_helper="$workdir/driver-helper"
printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\n" "$*" >> "$driver_calls"' \
  'if [[ "$1" == status ]]; then exit "$driver_status"; fi' \
  '[[ "$1" == install-package ]] || exit 99' 'exit "$driver_install_status"' > "$driver_helper"
chmod 700 "$driver_helper"
export driver_calls driver_status=3 driver_install_status=0
kernel_release=7.2.2-1-cachyos
modinfo() {
  [[ "$*" == "-k $kernel_release -F version vibeshine_drm" ]] || return 1
  ((module_present)) || return 1
  printf '1.19.0\n'
}
module_present=1
eval "$(declare -f install_kernel_headers | sed '1s/install_kernel_headers/install_kernel_headers_fixture/')"
install_kernel_headers() { printf 'headers\n' >> "$driver_calls"; }
install_virtual_driver "$driver_helper"
[[ $(<"$driver_calls") == $'status\nheaders\ninstall-package' ]]
for driver_install_status in 4 5; do
  reboot_required=0
  install_virtual_driver "$driver_helper"
  [[ $reboot_required == 1 ]]
done
driver_install_status=1
if (install_virtual_driver "$driver_helper"); then exit 1; fi
driver_install_status=0; module_present=0
if (install_virtual_driver "$driver_helper"); then exit 1; fi
module_present=1
for driver_status in 0 4; do
  : > "$driver_calls"
  install_virtual_driver "$driver_helper"
  [[ $(<"$driver_calls") == status ]]
done
if (install_virtual_driver "$workdir/missing-helper"); then exit 1; fi
headers_package=''
if (install_kernel_headers_fixture); then exit 1; fi
headers_package=missing-fixture-headers
if (install_kernel_headers_fixture); then exit 1; fi
driver_status=3
install_kernel_headers() { die 'fixture: matching headers unavailable'; }
: > "$driver_calls"
if (install_virtual_driver "$driver_helper"); then exit 1; fi
[[ $(<"$driver_calls") == status ]]
printf 'Driver installation is required; genuine build failures cannot report installer success.\n'

# DualSense package-hook failures must not be silently accepted either.
(
  printf '%s\n' '#!/usr/bin/env bash' 'printf "%s\n" "$*" >> "$driver_calls"' \
    'if [[ "$1" == status ]]; then exit "$driver_status"; fi' \
    '[[ "$1" == install ]] || exit 99' 'exit "$driver_install_status"' > "$driver_helper"
  install_kernel_headers() { printf 'headers\n' >> "$driver_calls"; }
  driver_status=1; driver_install_status=0
  : > "$driver_calls"
  install_dualsense_driver "$driver_helper"
  [[ $(<"$driver_calls") == $'status\nheaders\ninstall' ]]
  driver_install_status=1
  if (install_dualsense_driver "$driver_helper"); then exit 1; fi
  driver_install_status=4; reboot_required=0
  install_dualsense_driver "$driver_helper"
  [[ $reboot_required == 1 ]]
  for driver_status in 0 4; do
    : > "$driver_calls"
    install_dualsense_driver "$driver_helper"
    [[ $(<"$driver_calls") == status ]]
  done
  if (install_dualsense_driver "$workdir/missing-ds5-helper"); then exit 1; fi
)
printf 'DualSense installation failures cannot report installer success.\n'

# Reproduce an upgraded kernel package while the removed old kernel still runs.
(
  modules_root="$workdir/modules"
  mkdir -p "$modules_root/7.2.3-1-cachyos/build"
  touch "$modules_root/7.2.3-1-cachyos/pkgbase" "$modules_root/7.2.3-1-cachyos/build/Makefile"
  uname() { printf '7.2.2-1-cachyos\n'; }
  pacman() {
    printf '%s\n' "$*" >> "$calls"
    [[ "$1" == -Qqo && "${!#}" == "$modules_root/7.2.3-1-cachyos/pkgbase" ]] || return 1
    printf 'linux-cachyos\n'
  }
  : > "$calls"
  if (check_kernel "$modules_root") > "$workdir/kernel-error" 2>&1; then exit 1; fi
  grep -F 'Reboot required: running kernel 7.2.2-1-cachyos' "$workdir/kernel-error"
  grep -F '7.2.3-1-cachyos (linux-cachyos)' "$workdir/kernel-error"
  ! grep -Eq -- '^-S|^-U' "$calls"

  # A relocated image still maps through package-owned pkgbase metadata.
  uname() { printf '7.2.3-1-cachyos\n'; }
  check_kernel "$modules_root"
  [[ "$kernel_package" == linux-cachyos && "$headers_package" == linux-cachyos-headers ]]

  # Unknown/orphan directories are not evidence of an installed boot target.
  pacman() { return 1; }
  kernel_release=7.2.2-1-cachyos
  check_retired_kernel "$modules_root"
  if kernel_package_for 7.2.3-1-cachyos "$modules_root"; then exit 1; fi
  # Reject ambiguous ownership instead of constructing an invalid package name.
  pacman() { printf 'linux-cachyos\nother-package\n'; }
  if kernel_package_for 7.2.3-1-cachyos "$modules_root"; then exit 1; fi
)
printf 'Retired running kernels require a reboot, not mismatched header installation.\n'

# First install must request a reboot even if driver status later reports loaded.
(
  pacman() { [[ "$*" == '-Qq artlight' ]] && return "$package_status"; }
  package_status=1; reboot_required=0
  check_session_restart
  [[ $reboot_required == 1 ]]
  package_status=0
  check_session_restart
  [[ $reboot_required == 1 ]]
  reboot_required=0
  check_session_restart
  [[ $reboot_required == 0 ]]
)
printf 'Fresh installs require a compositor restart even when the driver loads immediately.\n'

# Upgrades must match the running supervisor's signal-forwarding contract.
# Evaluate only this pure selector from each package hook; never execute a
# package hook's top-level host mutations in the test environment.
for package_hook in \
  "$repo/packaging/linux/Arch/artlight.install" \
  "$repo/packaging/linux/vibeshine-preinst.in" \
  "$repo/packaging/linux/copr/Sunshine.spec"; do
  (
    eval "$(sed -n '/^artlight_select_upgrade_kill_mode() {$/,/^}$/p' "$package_hook")"
    declare -F artlight_select_upgrade_kill_mode >/dev/null
    artlight_legacy_host="$workdir/supervisor-fixture"
    helper_safe=1
    host_quiescent=1
    artlight_privileged_helper_is_safe() { ((helper_safe)); }
    artlight_unit_is_quiescent() { [[ "$1" == artlight.service ]] && ((host_quiescent)); }

    printf '%s\n' '  trap request_host_shutdown TERM INT HUP' > "$artlight_legacy_host"
    artlight_select_upgrade_kill_mode
    [[ "$artlight_upgrade_kill_mode" == mixed ]]
    # Prefer the new contract if a transitional helper retains the old marker.
    printf '%s\n' '  trap mark_host_shutdown TERM INT HUP' >> "$artlight_legacy_host"
    artlight_select_upgrade_kill_mode
    [[ "$artlight_upgrade_kill_mode" == mixed ]]
    helper_safe=0
    if artlight_select_upgrade_kill_mode; then exit 1; fi
    helper_safe=1

    printf '%s\n' '  trap mark_host_shutdown TERM INT HUP' > "$artlight_legacy_host"
    artlight_select_upgrade_kill_mode
    [[ "$artlight_upgrade_kill_mode" == control-group ]]
    printf '%s\n' \
      "  trap 'forward_host_signal TERM' TERM" \
      "  trap 'forward_host_signal INT' INT" \
      "  trap 'forward_host_signal HUP' HUP" > "$artlight_legacy_host"
    artlight_select_upgrade_kill_mode
    [[ "$artlight_upgrade_kill_mode" == process ]]

    printf '%s\n' "  trap 'forward_host_signal TERM' TERM" > "$artlight_legacy_host"
    if artlight_select_upgrade_kill_mode; then exit 1; fi
    printf '%s\n' 'unrecognized supervisor' > "$artlight_legacy_host"
    if artlight_select_upgrade_kill_mode; then exit 1; fi

    artlight_legacy_host="$workdir/absent-supervisor"
    host_quiescent=0
    if artlight_select_upgrade_kill_mode; then exit 1; fi
    host_quiescent=1
    artlight_select_upgrade_kill_mode
    [[ "$artlight_upgrade_kill_mode" == control-group ]]
  )
done
printf 'Package upgrades preserve new and legacy supervisor shutdown contracts.\n'

# Removal and post-install recovery must leave broker/app workers untouched
# when the GPU host has not drained, even if the admission socket has stopped.
for hook_case in \
  'packaging/linux/artlight-prerm.in:artlight_quiesce_for_removal' \
  'packaging/linux/artlight-postinst.in:artlight_quiesce_machine_host' \
  'packaging/linux/copr/Sunshine.spec:artlight_quiesce_machine_host' \
  'packaging/linux/copr/Sunshine.spec:artlight_preun_quiesce'; do
  (
    package_hook="$repo/${hook_case%%:*}"
    quiesce_function=${hook_case#*:}
    # The RPM post-install definition intentionally supersedes its pre-install
    # namesake. Redirect runtime-presence checks and paths into this fixture.
    eval "$(sed -n "/^${quiesce_function}() {$/,/^}$/p" "$package_hook" |
      sed -e "s|/run/systemd/system|$workdir|g" \
          -e "s|/run/artlight/|$workdir/absent-runtime/|g")"
    declare -F "$quiesce_function" >/dev/null
    artlight_controller="$workdir/absent-controller"
    artlight_broker_socket="$workdir/absent-runtime/session-broker.sock"
    artlight_session_record="$workdir/absent-runtime/session.env"
    artlight_legacy_acl="$workdir/absent-runtime/session.acl"
    hook_calls="$workdir/hook-calls"
    host_verified=0
    host_quiescent=0
    systemctl() { return 0; }
    timeout() { return 0; }
    artlight_stop_exact_unit() { printf 'stop %s\n' "$1" >> "$hook_calls"; }
    artlight_unit_is_quiescent() {
      if [[ "$1" == artlight.service ]]; then
        ((host_quiescent)) || return 1
        host_verified=1
        printf 'host drained\n' >> "$hook_calls"
      fi
    }
    artlight_stop_brokers() {
      ((host_verified)) || return 1
      printf 'stop brokers\n' >> "$hook_calls"
    }
    artlight_unit_is_masked() { return 0; }
    artlight_unit_is_disabled() { return 0; }
    artlight_remove_pam_hook() { return 0; }
    artlight_preun_stop_exact_unit() { artlight_stop_exact_unit "$@"; }
    artlight_preun_unit_is_quiescent() { artlight_unit_is_quiescent "$@"; }
    artlight_preun_stop_brokers() { artlight_stop_brokers; }
    artlight_preun_unit_is_masked() { return 0; }
    artlight_preun_unit_is_disabled() { return 0; }
    artlight_preun_remove_pam() { return 0; }

    : > "$hook_calls"
    if "$quiesce_function"; then exit 1; fi
    grep -Fxq 'stop artlight.service' "$hook_calls"
    ! grep -Fxq 'stop brokers' "$hook_calls"

    host_quiescent=1
    "$quiesce_function"
    grep -Fxq 'host drained' "$hook_calls"
    grep -Fxq 'stop brokers' "$hook_calls"
  )
done
printf 'Removal and repair refuse broker teardown until the GPU host has drained.\n'

# A pre-replacement failure must not stop the GPU host before broker admission
# was touched. Once that shutdown begins, retain the runtime restart fence and
# recover the installed unit enablement for the next boot.
for hook in packaging/linux/vibeshine-preinst.in packaging/linux/Arch/artlight.install packaging/linux/copr/Sunshine.spec; do
  (
    eval "$(sed -n '/^artlight_abort_quiesce() {$/,/^}$/p' "$repo/$hook")"
    actions=''
    systemctl() { actions="$actions $*"; }
    timeout() { shift 2; "$@"; }
    stat() { printf '%s\n' '1:2:0:0:0:1'; }
    chmod() { actions="$actions CHMOD:$*"; }
    artlight_legacy_handoff="$workdir/handoff-fixture"
    : > "$artlight_legacy_handoff"
    artlight_pre_handoff_identity='1:2:0:0:755:1'
    artlight_stop_exact_unit() { actions="$actions STOP:$1"; }
    artlight_unit_is_quiescent() { return 1; }
    artlight_controller_was_frozen=1
    artlight_shutdown_started=0
    artlight_quiesce_step='early fixture failure'
    artlight_abort_quiesce 2>/dev/null
    [[ "$actions" == *'thaw artlight-session-controller.service'* ]]
    [[ "$actions" == *'CHMOD:0755 --'* ]]
    [[ "$actions" == *'CHMOD:0755 --'*'thaw artlight-session-controller.service'* ]]
    [[ "$actions" != *'STOP:'* && "$actions" != *'unmask'* ]]
    actions=''
    artlight_controller_was_frozen=0
    artlight_shutdown_started=1
    artlight_pre_controller_enabled=enabled
    artlight_pre_host_enabled=disabled
    artlight_pre_prelogin_enabled=disabled
    artlight_pre_prepare_enabled=disabled
    artlight_broker_socket="$workdir/absent-broker-socket"
    artlight_control_socket="$workdir/absent-control-socket"
    artlight_abort_quiesce 2>/dev/null
    [[ "$actions" == *'STOP:artlight-session-controller.service'* ]]
    [[ "$actions" == *'STOP:artlight.service'* ]]
    [[ "$actions" != *'enable '* && "$actions" != *'disable '* && "$actions" != *'unmask'* ]]
  )
done
printf 'Failed pre-upgrade quiesce preserves the early host, prior enablement, and late restart fences.\n'
# Removal lifts most of the fence but can leave runtime masks on artlight.service
# and artlight-session-exec.socket behind, and a reinstall before a reboot then
# reaches this function with the unit masked. A masked unit cannot be started, so
# there is nothing to fence and the install must not fail there. An unmasked live
# host without the drop-in must still be refused. All three package formats carry
# their own copy of the guard.
for fence_hook in packaging/linux/vibeshine-preinst.in \
  packaging/linux/Arch/artlight.install \
  packaging/linux/copr/Sunshine.spec; do
  (
    fence_function=$(sed -n '/^artlight_activate_host_upgrade_fence() {$/,/^}$/p' "$repo/$fence_hook")
    if [[ "$fence_hook" == *.spec ]]; then fence_function=${fence_function//%%/%}; fi
    eval "$fence_function"
    systemctl() { :; }
    timeout() { printf '%s\n' "$property_fixture"; }
    artlight_upgrade_kill_mode=control-group
    for load_state in not-found masked masked-runtime; do
      property_fixture=$'LoadState='"$load_state"$'\nRefuseManualStart=no\nKillMode=control-group\nSendSIGKILL=yes'
      artlight_activate_host_upgrade_fence
    done
    property_fixture=$'LoadState=loaded\nRefuseManualStart=no\nKillMode=control-group\nSendSIGKILL=yes'
    if ( artlight_activate_host_upgrade_fence ) 2>/dev/null; then exit 1; fi
    property_fixture=$'LoadState=loaded\nRefuseManualStart=yes\nKillMode=control-group\nSendSIGKILL=no'
    artlight_activate_host_upgrade_fence
  )
done
printf 'A masked or absent host is never fenced; an unmasked live host still is.\n'

# Socket units have no MainPID property. All package formats must accept a
# stopped four-property socket while still rejecting a malformed service.
for package_hook in packaging/linux/vibeshine-preinst.in \
  packaging/linux/artlight-postinst.in \
  packaging/linux/artlight-prerm.in \
  packaging/linux/copr/Sunshine.spec; do
  (
    unit_function=$(sed -n '/^artlight_unit_is_quiescent() {$/,/^}$/p' "$repo/$package_hook")
    if [[ "$package_hook" == *.spec ]]; then unit_function=${unit_function//%%/%}; fi
    eval "$unit_function"
    timeout() { printf '%s\n' "$property_fixture"; }
    artlight_cgroup_is_quiescent() { [[ -z "$1" ]]; }
    property_fixture=$'LoadState=loaded\nActiveState=inactive\nSubState=dead\nControlGroup='
    artlight_unit_is_quiescent artlight-session-exec.socket
    if artlight_unit_is_quiescent artlight.service; then exit 1; fi
    property_fixture+=$'\nMainPID=1'
    if artlight_unit_is_quiescent artlight-session-exec.socket; then exit 1; fi
    property_fixture=${property_fixture%$'\nMainPID=1'}$'\nMainPID=0'
    artlight_unit_is_quiescent artlight-session-exec.socket
  )
done
printf 'Debian and RPM package guards accept stopped sockets without MainPID.\n'
(
  preun_function=$(sed -n '/^artlight_preun_unit_is_quiescent() {$/,/^}$/p' \
    "$repo/packaging/linux/copr/Sunshine.spec")
  preun_function=${preun_function//%%/%}
  eval "$preun_function"
  timeout() { printf '%s\n' "$property_fixture"; }
  artlight_preun_cgroup_is_quiescent() { [[ -z "$1" ]]; }
  property_fixture=$'LoadState=loaded\nActiveState=inactive\nSubState=dead\nControlGroup='
  artlight_preun_unit_is_quiescent artlight-session-exec.socket
  if artlight_preun_unit_is_quiescent artlight.service; then exit 1; fi
)
printf 'RPM removal accepts stopped sockets without MainPID.\n'

(
  source "$repo/packaging/linux/Arch/artlight.install"
  fixture=$(mktemp -d /tmp/artlight-masked-links.XXXXXXXX)
  trap 'rm -rf -- "$fixture"' EXIT
  mkdir -p "$fixture/usr/lib/systemd/system" \
    "$fixture/etc/systemd/system/graphical.target.wants" \
    "$fixture/etc/systemd/system/sockets.target.wants" \
    "$fixture/run/systemd/system"
  for unit in artlight.service artlight-session-exec.socket; do
    printf '[Unit]\nDescription=Fixture\n' > "$fixture/usr/lib/systemd/system/$unit"
    ln -s /dev/null "$fixture/run/systemd/system/$unit"
  done
  ln -s "$fixture/usr/lib/systemd/system/artlight.service" \
    "$fixture/etc/systemd/system/graphical.target.wants/artlight.service"
  ln -s "$fixture/usr/lib/systemd/system/artlight-session-exec.socket" \
    "$fixture/etc/systemd/system/sockets.target.wants/artlight-session-exec.socket"
  [[ $(systemctl --root="$fixture" is-enabled artlight.service) == masked-runtime ]]
  artlight_retire_obsolete_boot_links "$fixture"
  [[ ! -e "$fixture/etc/systemd/system/graphical.target.wants/artlight.service" ]]
  [[ ! -e "$fixture/etc/systemd/system/sockets.target.wants/artlight-session-exec.socket" ]]
  [[ $(systemctl --root="$fixture" is-enabled artlight.service) == masked-runtime ]]
  ln -s /dev/null "$fixture/etc/systemd/system/graphical.target.wants/artlight.service"
  if artlight_retire_obsolete_boot_links "$fixture"; then exit 1; fi
  [[ -L "$fixture/etc/systemd/system/graphical.target.wants/artlight.service" ]]
)
printf 'Arch upgrade retires only matching boot links while runtime masks remain.\n'

# Fedora: the dnf half of the installer. These fixtures stand in for dnf, rpm and
# curl, so the suite needs neither dnf nor rpm installed. Negative checks use
# `if ...; then exit 1; fi` because `! cmd` never trips set -e.
command -v jq >/dev/null || { printf 'jq is required for the release-asset checks\n' >&2; exit 1; }
dnf_calls="$workdir/dnf-calls"
# Earlier sections replace some installer functions with stubs; reload the real
# definitions into a subshell-safe copy before exercising the dnf paths.
eval "$(sed -n '/^download_release_package() {/,/^}/p' "$repo/scripts/linux_install.sh")"

# Package-manager selection: pacman wins when present, dnf needs rpm beside it,
# and neither is a hard error. Real binaries are hidden behind an empty PATH.
(
  mkdir -p "$workdir/empty-path"
  PATH="$workdir/empty-path"
  unset -f pacman
  dnf() { :; }
  rpm() { :; }
  use_repo=1
  require_package_manager
  [[ $pkg_mgr == dnf && $use_repo == 0 ]]
  unset -f rpm
  if (require_package_manager) 2>/dev/null; then exit 1; fi
  unset -f dnf
  if (require_package_manager) 2>/dev/null; then exit 1; fi
  pacman() { :; }
  dnf() { :; }
  rpm() { :; }
  use_repo=1
  require_package_manager
  [[ $pkg_mgr == pacman && $use_repo == 1 ]]
)
printf 'Package-manager selection prefers pacman, requires rpm beside dnf, and fails without either.\n'

(
  pkg_mgr=dnf
  pacman() { printf 'pacman %s\n' "$*" >> "$dnf_calls"; return 1; }
  dnf() { printf 'dnf %s\n' "$*" >> "$dnf_calls"; }
  rpm() {
    printf 'rpm %s\n' "$*" >> "$dnf_calls"
    case $1 in
      -q) return "$rpm_query_status" ;;
      -qf) [[ -n $rpm_owner ]] && printf '%s\n' "$rpm_owner" ;;
      -qp) [[ -n $rpm_identity ]] && printf '%s\n' "$rpm_identity" ;;
    esac
  }
  rpm_query_status=0
  rpm_owner=''
  rpm_identity=''

  # pkg_install and pkg_is_installed speak dnf and rpm, never pacman.
  : > "$dnf_calls"
  dnf_confirm=(-y)
  pkg_install jq kernel-devel
  grep -Fxq -- 'dnf install -y -- jq kernel-devel' "$dnf_calls"
  dnf_confirm=()
  pkg_install jq
  grep -Fxq -- 'dnf install -- jq' "$dnf_calls"
  pkg_is_installed artlight
  grep -Fxq -- 'rpm -q --quiet -- artlight' "$dnf_calls"
  rpm_query_status=1
  if pkg_is_installed artlight; then exit 1; fi
  rpm_query_status=0
  if grep -q '^pacman' "$dnf_calls"; then exit 1; fi
  printf 'dnf package helpers install with dnf and query with rpm.\n'

  # The running kernel's package comes from rpm's owner of vmlinuz, and only a
  # plain package name is accepted.
  : > "$dnf_calls"
  rpm_owner=kernel-core
  [[ $(kernel_package_for 7.2.7-200.fc44.x86_64 /fixture/modules) == kernel-core ]]
  grep -Fxq -- 'rpm -qf --qf %{NAME}\n -- /fixture/modules/7.2.7-200.fc44.x86_64/vmlinuz' "$dnf_calls"
  rpm_owner='kernel-core; reboot'
  if kernel_package_for 7.2.7-200.fc44.x86_64 /fixture/modules >/dev/null; then exit 1; fi
  rpm_owner=''
  if kernel_package_for 7.2.7-200.fc44.x86_64 /fixture/modules >/dev/null; then exit 1; fi
  if grep -q '^pacman' "$dnf_calls"; then exit 1; fi
  printf 'dnf kernel lookup asks rpm for the vmlinuz owner and rejects unsafe names.\n'

  # A local RPM is identity-checked with rpm and installed with dnf alone: the
  # RPM's own %pre quiesces the host, so no pacman hook or driver adoption runs.
  stage_upgrade_guard() { printf 'stage-upgrade-guard\n' >> "$dnf_calls"; }
  prepare_driver_replacement() { printf 'prepare-driver-replacement\n' >> "$dnf_calls"; }
  local_package="$workdir/artlight-1.5.4-1.fc44.x86_64.rpm"
  : > "$local_package"
  : > "$dnf_calls"
  rpm_identity=artlight
  dnf_confirm=(-y)
  install_from_package >/dev/null
  grep -Fxq -- "rpm -qp --qf %{NAME}\n -- $local_package" "$dnf_calls"
  grep -Fxq -- "dnf install -y -- $local_package" "$dnf_calls"
  [[ $(grep -c '^dnf' "$dnf_calls") == 1 ]]
  if grep -q '^pacman\|^stage-upgrade-guard\|^prepare-driver-replacement' "$dnf_calls"; then exit 1; fi
  : > "$dnf_calls"
  rpm_identity=artlight-debuginfo
  if (install_from_package) >/dev/null 2>&1; then exit 1; fi
  rpm_identity=''
  if (install_from_package) >/dev/null 2>&1; then exit 1; fi
  if grep -q '^dnf' "$dnf_calls"; then exit 1; fi
  rpm_identity=artlight
  local_package="$workdir/missing.rpm"
  if (install_from_package) >/dev/null 2>&1; then exit 1; fi
  if grep -q '^dnf' "$dnf_calls"; then exit 1; fi
  printf 'Local RPM installs check identity with rpm and use dnf without pacman hooks.\n'

  # Release lookup picks the binary x86_64 RPM, never a source, debuginfo or
  # Arch package, and honours the pre-release setting.
  releases_fixture='[
    {"tag_name":"1.6.0","draft":false,"prerelease":false,"assets":[
      {"name":"artlight-1.6.0-1-x86_64.pkg.tar.zst","browser_download_url":"https://fixture/arch"}]},
    {"tag_name":"1.5.9","draft":false,"prerelease":true,"assets":[
      {"name":"artlight-1.5.9-1.fc44.x86_64.rpm","browser_download_url":"https://fixture/prerelease-rpm"}]},
    {"tag_name":"1.5.8","draft":false,"prerelease":false,"assets":[
      {"name":"artlight-debuginfo-1.5.8-1.fc44.x86_64.rpm","browser_download_url":"https://fixture/debuginfo"},
      {"name":"artlight-1.5.8-1.fc44.src.rpm","browser_download_url":"https://fixture/srpm"},
      {"name":"artlight-1.5.8-1.fc44.x86_64.rpm","browser_download_url":"https://fixture/rpm"}]}]'
  curl() {
    local arg previous='' output='' url=${!#}
    for arg in "$@"; do
      [[ $previous == -o ]] && output=$arg
      previous=$arg
    done
    printf 'curl %s\n' "$url" >> "$dnf_calls"
    case $url in
      */releases\?per_page=30) printf '%s' "$releases_fixture" ;;
      */release-provenance.json) return 22 ;;
      *) [[ -n $output ]] && printf 'fixture\n' > "$output" ;;
    esac
  }
  requested_version=''
  : > "$dnf_calls"
  allow_prerelease=0
  download_release_package >/dev/null
  [[ $local_package == "$workdir/artlight-1.5.8-1.fc44.x86_64.rpm" ]]
  grep -Fxq -- 'curl https://fixture/rpm' "$dnf_calls"
  if grep -q 'fixture/\(arch\|srpm\|debuginfo\|prerelease-rpm\)' "$dnf_calls"; then exit 1; fi
  allow_prerelease=1
  download_release_package >/dev/null
  [[ $local_package == "$workdir/artlight-1.5.9-1.fc44.x86_64.rpm" ]]
  releases_fixture='[{"tag_name":"1.6.0","draft":false,"prerelease":false,"assets":[
    {"name":"artlight-1.6.0-1-x86_64.pkg.tar.zst","browser_download_url":"https://fixture/arch"}]}]'
  if message=$( (download_release_package) 2>&1 >/dev/null); then exit 1; fi
  [[ $message == *'a Fedora RPM'* && $message == *'--package'* ]]
  if grep -q '^pacman' "$dnf_calls"; then exit 1; fi
  printf 'Release lookup on Fedora selects only the binary x86_64 RPM.\n'

  # The same lookup on Arch still finds the Arch package (the pattern is passed
  # to jq with --arg, so it must not carry the inline program's extra escaping).
  pkg_mgr=pacman
  releases_fixture='[{"tag_name":"1.6.0","draft":false,"prerelease":false,"assets":[
    {"name":"artlight-1.6.0-1.fc44.x86_64.rpm","browser_download_url":"https://fixture/rpm"},
    {"name":"artlight-1.6.0-1-x86_64.pkg.tar.zst","browser_download_url":"https://fixture/arch"}]}]'
  : > "$dnf_calls"
  download_release_package >/dev/null
  [[ $local_package == "$workdir/artlight-1.6.0-1-x86_64.pkg.tar.zst" ]]
  grep -Fxq -- 'curl https://fixture/arch' "$dnf_calls"
  if grep -q 'fixture/rpm' "$dnf_calls"; then exit 1; fi
  printf 'Release lookup on Arch still selects the Arch package.\n'
)
