%global build_timestamp %(date +"%Y%m%d")

# use sed to replace these values
%global build_version 0
%global branch 0
%global commit 0

# Keep the application-facing version strict SemVer while translating only
# the RPM package version into RPM ordering syntax. Prereleases sort below the
# final release; stable.N respins sort above it.
%global build_semver %{lua:local v=rpm.expand("%{build_version}"); if v:sub(1,1)=="v" then v=v:sub(2) end; print(v)}
%global rpm_version %{lua:local v=rpm.expand("%{build_semver}"); local p=v:find("-",1,true); if p then if v:sub(p+1,p+6)=="stable" then v=v:sub(1,p-1).."+stable"..v:sub(p+7) else v=v:sub(1,p-1).."~"..v:sub(p+1) end end; print(v)}

%undefine _hardened_build

# Define _metainfodir for OpenSUSE if not already defined
%if 0%{?suse_version}
%if !0%{?_metainfodir:1}
%global _metainfodir %{_datadir}/metainfo
%endif
%endif

Name: artlight
Version: %{rpm_version}
Release: 1%{?dist}
Summary: Self-hosted game stream host for Moonlight.
License: GPLv3-only
URL: https://github.com/onaiaku/ArtLight
Source0: tarball.tar.gz

# Common BuildRequires
BuildRequires: cmake >= 3.25.0
BuildRequires: desktop-file-utils
BuildRequires: git
BuildRequires: libcap-devel
BuildRequires: libcurl-devel
BuildRequires: libdrm-devel
BuildRequires: libevdev-devel
BuildRequires: libnotify-devel
BuildRequires: libva-devel
BuildRequires: libX11-devel
BuildRequires: libxcb-devel
BuildRequires: libXcursor-devel
BuildRequires: libXfixes-devel
BuildRequires: libXi-devel
BuildRequires: libXinerama-devel
BuildRequires: libXrandr-devel
BuildRequires: libXtst-devel
BuildRequires: openssl-devel
BuildRequires: pipewire-devel
BuildRequires: rpm-build
BuildRequires: systemd-rpm-macros
BuildRequires: wget
BuildRequires: which

%if 0%{?fedora}
# Fedora-specific BuildRequires
BuildRequires: appstream
# BuildRequires: boost-devel >= 1.86.0
BuildRequires: glslc
BuildRequires: libappstream-glib
BuildRequires: vulkan-loader-devel
BuildRequires: libayatana-appindicator3-devel
BuildRequires: libgudev
BuildRequires: mesa-libGL-devel
BuildRequires: mesa-libgbm-devel
BuildRequires: miniupnpc-devel
BuildRequires: numactl-devel
BuildRequires: opus-devel
BuildRequires: pulseaudio-libs-devel
BuildRequires: python3-jinja2
BuildRequires: python3-setuptools
BuildRequires: systemd-udev
%{?sysusers_requires_compat}
%endif

%if 0%{?suse_version}
# OpenSUSE-specific BuildRequires
BuildRequires: AppStream
BuildRequires: appstream-glib
BuildRequires: libappindicator3-devel
BuildRequires: libgudev-1_0-devel
BuildRequires: Mesa-libGL-devel
BuildRequires: libgbm-devel
BuildRequires: libminiupnpc-devel
BuildRequires: libnuma-devel
BuildRequires: libopus-devel
BuildRequires: libpulse-devel
BuildRequires: python311
BuildRequires: python311-Jinja2
BuildRequires: python311-setuptools
%if !0%{?sle_version}
BuildRequires: shaderc
%endif
BuildRequires: udev
%if !0%{?sle_version}
BuildRequires: vulkan-devel
%endif
%endif

# Conditional BuildRequires for cuda-gcc based on distribution version
%if 0%{?fedora}
%if 0%{?fedora} <= 41
BuildRequires: gcc13
BuildRequires: gcc13-c++
%global gcc_version 13
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%elif 0%{?fedora} >= 42 && 0%{?fedora} <= 43
BuildRequires: gcc14
BuildRequires: gcc14-c++
%global gcc_version 14
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%elif 0%{?fedora} >= 44
BuildRequires: gcc14
BuildRequires: gcc14-c++
%global gcc_version 14
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%endif
%endif

%if 0%{?suse_version}
%if 0%{?suse_version} <= 1699
# OpenSUSE Leap 15.x
BuildRequires: gcc14
BuildRequires: gcc14-c++
%global gcc_version 14
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%else
# OpenSUSE Tumbleweed
BuildRequires: gcc14
BuildRequires: gcc14-c++
%global gcc_version 14
%global cuda_version 12.9.1
%global cuda_build 575.57.08
%endif
%endif

%global cuda_dir %{_builddir}/cuda

# Common runtime requirements
Requires: miniupnpc >= 2.2.4
Requires: kmod
Requires: iproute
Requires: jq
Requires: /usr/bin/python3
Requires: /usr/bin/pactl
Requires: /usr/bin/parec
Requires: /usr/bin/python3
Requires: /usr/bin/wayland-info
Requires: /usr/bin/xdpyinfo
Requires: socat
Requires: usbip
Requires: util-linux
Recommends: dkms
Recommends: gcc
Recommends: kernel-devel
Recommends: make

%if 0%{?fedora}
# Fedora runtime requirements
Requires: python3 >= 3.9
Requires: libayatana-appindicator3 >= 0.5.3
Requires: libcap >= 2.22
Requires: libcurl >= 7.0
Requires: libdrm > 2.4.97
Requires: libevdev >= 1.5.6
Requires: libkscreen
Requires: libopusenc >= 0.2.1
Requires: libva >= 2.14.0
Requires: libwayland-client >= 1.20.0
Requires: libX11 >= 1.7.3.1
Requires: numactl-libs >= 2.0.14
Requires: openssl >= 3.0.2
Requires: pulseaudio-libs >= 10.0
Requires: vulkan-loader
%endif

%if 0%{?suse_version}
# OpenSUSE runtime requirements
Requires: python311
Requires: libappindicator3-1
Requires: libcap2
Requires: libcurl4
Requires: libdrm2
Requires: libevdev2
# The binary moved between openSUSE KScreen package generations; use the RPM
# file capability so zypper selects the provider for the active release.
Requires: /usr/bin/kscreen-doctor
Requires: libopusenc0
Requires: libva2
Requires: libwayland-client0
Requires: libX11-6
Requires: libnuma1
Requires: libopenssl3
Requires: libpulse0
%if !0%{?sle_version}
Requires: libvulkan1
%endif
%endif

%description
Self-hosted game stream host for Moonlight.

%prep
# extract tarball to current directory
mkdir -p %{_builddir}/Sunshine
tar -xzf %{SOURCE0} -C %{_builddir}/Sunshine

# list directory
ls -a %{_builddir}/Sunshine

%build
# exit on error
set -e

# Detect the architecture and Fedora version
architecture=$(uname -m)

cuda_supported_architectures=("x86_64" "aarch64")

# prepare CMAKE args
cmake_args=(
  "-B=%{_builddir}/Sunshine/build"
  "-G=Unix Makefiles"
  "-S=."
  "-DBUILD_DOCS=OFF"
  "-DBUILD_TESTS=ON"
  "-DBUILD_WERROR=ON"
  "-DCMAKE_BUILD_TYPE=Release"
  "-DCMAKE_INSTALL_PREFIX=%{_prefix}"
  "-DSUNSHINE_ASSETS_DIR=%{_datadir}/artlight"
  "-DSUNSHINE_EXECUTABLE_PATH=%{_bindir}/artlight"
  "-DSUNSHINE_ENABLE_DRM=ON"
  "-DSUNSHINE_ENABLE_KWIN=ON"
  "-DSUNSHINE_ENABLE_PORTAL=ON"
  "-DSUNSHINE_ENABLE_WAYLAND=ON"
  "-DSUNSHINE_ENABLE_X11=ON"
  "-DSUNSHINE_PUBLISHER_NAME=onaiaku"
  "-DSUNSHINE_PUBLISHER_WEBSITE=https://github.com/onaiaku/ArtLight"
  "-DSUNSHINE_PUBLISHER_ISSUE_URL=https://github.com/onaiaku/ArtLight/issues"
)

export CC=gcc-%{gcc_version}
export CXX=g++-%{gcc_version}

function install_cuda() {
  # check if we need to install cuda
  if [ -f "%{cuda_dir}/bin/nvcc" ]; then
    echo "cuda already installed"
    return
  fi

  local cuda_prefix="https://developer.download.nvidia.com/compute/cuda/"
  local cuda_suffix=""
  if [ "$architecture" == "aarch64" ]; then
    local cuda_suffix="_sbsa"
  fi

  local url="${cuda_prefix}%{cuda_version}/local_installers/cuda_%{cuda_version}_%{cuda_build}_linux${cuda_suffix}.run"
  echo "cuda url: ${url}"
  wget \
    "$url" \
    --progress=bar:force:noscroll \
    --retry-connrefused \
    --tries=3 \
    -q -O "%{_builddir}/cuda.run"
  chmod a+x "%{_builddir}/cuda.run"
  "%{_builddir}/cuda.run" \
    --no-drm \
    --no-man-page \
    --no-opengl-libs \
    --override \
    --silent \
    --toolkit \
    --toolkitpath="%{cuda_dir}"
  rm "%{_builddir}/cuda.run"

  # we need to patch math_functions.h depending on the CUDA major version
  # see https://forums.developer.nvidia.com/t/error-exception-specification-is-incompatible-for-cospi-sinpi-cospif-sinpif-with-glibc-2-41/323591/3
  local cuda_major
  cuda_major=$(echo "%{cuda_version}" | cut -d. -f1)
  local patch_file=""
  if [ "${cuda_major}" -eq 12 ]; then
    # CUDA 12.x: the extern declarations lack noexcept(true); add it to match glibc 2.41.
    patch_file="cuda-12-math_functions.patch"
  elif [ "${cuda_major}" -eq 13 ]; then
    # CUDA 13.x: the extern declarations already have noexcept(true), but the __func__()
    # macro invocations at the bottom still lack it, causing a redeclaration conflict.
    patch_file="cuda-13-math_functions.patch"
  else
    echo "Warning: no math_functions.h patch available for CUDA ${cuda_major}.x, skipping."
  fi

  if [ -n "${patch_file}" ]; then
    echo "Applying CUDA patch: ${patch_file}"
    patch -p2 \
      --backup \
      --directory="%{cuda_dir}" \
      --verbose \
      < "%{_builddir}/Sunshine/packaging/linux/patches/${architecture}/${patch_file}"
  fi
}

if [ -n "%{cuda_version}" ] && [[ " ${cuda_supported_architectures[@]} " =~ " ${architecture} " ]]; then
  install_cuda
  cmake_args+=("-DSUNSHINE_ENABLE_CUDA=ON" "-DSUNSHINE_REQUIRE_CUDA_PASCAL=ON")
  cmake_args+=("-DCMAKE_CUDA_COMPILER:PATH=%{cuda_dir}/bin/nvcc")
  cmake_args+=("-DCMAKE_CUDA_HOST_COMPILER=gcc-%{gcc_version}")
else
  cmake_args+=("-DSUNSHINE_ENABLE_CUDA=OFF")
fi

# setup the version
export BRANCH=%{branch}
export BUILD_VERSION=%{build_semver}
export COMMIT=%{commit}

# Disable Vulkan on openSUSE Leap (shaderc/glslang not in official repos)
%if 0%{?sle_version}
cmake_args+=("-DSUNSHINE_ENABLE_VULKAN=OFF")
cmake_args+=("-DSUNSHINE_ENABLE_PYROWAVE=OFF")
%endif

# cmake
cd %{_builddir}/Sunshine
echo "cmake args:"
echo "${cmake_args[@]}"
cmake "${cmake_args[@]}"
make -j$(nproc) -C "%{_builddir}/Sunshine/build"

%check
# validate the metainfo file
appstreamcli validate %{buildroot}%{_metainfodir}/*.metainfo.xml
appstream-util validate %{buildroot}%{_metainfodir}/*.metainfo.xml
desktop-file-validate %{buildroot}%{_datadir}/applications/*.desktop

%install
cd %{_builddir}/Sunshine/build
%make_install
# The shared CMake stage includes ALPM-only pretransaction files. RPM has its
# own package scriptlets and must not ship files outside its manifest.
rm -f %{buildroot}%{_prefix}/libexec/vibeshine/artlight-package-preflight
rm -f %{buildroot}%{_datadir}/libalpm/hooks/00-artlight-quiesce.hook
rm -f %{buildroot}%{_datadir}/artlight/arch-package-hooks
rmdir %{buildroot}%{_datadir}/libalpm/hooks %{buildroot}%{_datadir}/libalpm 2>/dev/null || true

%pre
artlight_controller=%{_prefix}/libexec/vibeshine/artlight-session-controller
artlight_legacy_host=%{_prefix}/libexec/vibeshine/artlight-machine-host
artlight_legacy_handoff=%{_prefix}/libexec/vibeshine/artlight-session-handoff
artlight_runtime_root=/run/artlight
artlight_broker_socket=/run/artlight/session-broker.sock
artlight_control_socket=/run/vibeshine/vkms-control.sock
artlight_host_upgrade_dropin_dir=/run/systemd/system/artlight.service.d
artlight_host_upgrade_dropin=$artlight_host_upgrade_dropin_dir/90-artlight-safe-upgrade.conf
artlight_controller_was_frozen=0
artlight_upgrade_kill_mode=
artlight_legacy_handoff_directory=/run/artlight/session-handoffs
artlight_legacy_restore_directory=/run/artlight/session-restores
artlight_legacy_transition_lock=/run/artlight/session-handoff.lock
artlight_legacy_prelogin_marker=/run/artlight/prelogin-handoff-complete
artlight_cgroup_is_quiescent() {
  artlight_control_group=$1
  [ -n "$artlight_control_group" ] || return 0
  case "$artlight_control_group" in /*) ;; *) return 1 ;; esac
  case "$artlight_control_group" in */../* | */..) return 1 ;; esac
  artlight_cgroup_path=/sys/fs/cgroup$artlight_control_group
  if [ ! -e "$artlight_cgroup_path" ] && [ ! -L "$artlight_cgroup_path" ]; then return 0; fi
  [ -d "$artlight_cgroup_path" ] && [ ! -L "$artlight_cgroup_path" ] && \
    [ -f "$artlight_cgroup_path/cgroup.events" ] && \
    [ ! -L "$artlight_cgroup_path/cgroup.events" ] && \
    grep -qx 'populated 0' "$artlight_cgroup_path/cgroup.events"
}
artlight_unit_is_quiescent() {
  artlight_properties=$(timeout --signal=KILL 5 systemctl show "$1" \
    --property=LoadState --property=ActiveState --property=SubState --property=MainPID \
    --property=ControlGroup 2>/dev/null) || return 1
  artlight_property_lines=$(printf '%%s\n' "$artlight_properties" | wc -l | tr -d ' ')
  case "$1:$artlight_property_lines" in
    *.socket:4 | *.socket:5 | *:5) ;;
    *) return 1 ;;
  esac
  for artlight_property in LoadState ActiveState SubState ControlGroup; do
    artlight_property_count=$(printf '%%s\n' "$artlight_properties" | \
      grep -c "^$artlight_property=" || true)
    [ "$artlight_property_count" = 1 ] || return 1
  done
  artlight_property_count=$(printf '%%s\n' "$artlight_properties" | grep -c '^MainPID=' || true)
  case "$1:$artlight_property_count" in
    *.socket:0 | *.socket:1 | *:1) ;;
    *) return 1 ;;
  esac
  artlight_load=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^LoadState=//p')
  artlight_state=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^ActiveState=//p')
  artlight_substate=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^SubState=//p')
  artlight_pid=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^MainPID=//p')
  case "$1" in *.socket) [ -n "$artlight_pid" ] || artlight_pid=0 ;; esac
  artlight_control_group=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^ControlGroup=//p')
  case "$artlight_load:$artlight_state:$artlight_substate" in
    not-found:inactive:dead | \
    loaded:inactive:dead | loaded:failed:failed | \
    masked:inactive:dead | masked:failed:failed | \
    masked-runtime:inactive:dead | masked-runtime:failed:failed) ;;
    *) return 1 ;;
  esac
  [ "$artlight_pid" = 0 ] && artlight_cgroup_is_quiescent "$artlight_control_group"
}
artlight_unit_is_disabled() {
  artlight_enabled=$(timeout --signal=KILL 5 systemctl is-enabled "$1" 2>/dev/null || true)
  case "$artlight_enabled" in
    disabled | masked | masked-runtime | static | indirect | generated | transient | linked | linked-runtime | not-found) return 0 ;;
    *) return 1 ;;
  esac
}
artlight_unit_is_masked() {
  artlight_enabled=$(timeout --signal=KILL 5 systemctl is-enabled "$1" 2>/dev/null || true)
  artlight_load=$(timeout --signal=KILL 5 systemctl show "$1" \
    --property=LoadState 2>/dev/null) || return 1
  case "$artlight_enabled" in
    masked | masked-runtime) [ "$artlight_load" = 'LoadState=masked' ] ;;
    *) return 1 ;;
  esac
}
artlight_restore_template_is_masked() {
  artlight_restore_mask=$(timeout --signal=KILL 5 systemctl is-enabled \
    'artlight-session-restore@.service' 2>/dev/null || true)
  artlight_restore_load=$(timeout --signal=KILL 5 systemctl show \
    'artlight-session-restore@.service' --property=LoadState 2>/dev/null) || return 1
  case "$artlight_restore_mask" in
    masked | masked-runtime) [ "$artlight_restore_load" = 'LoadState=masked' ] ;;
    *) return 1 ;;
  esac
}
artlight_host_unit_is_masked() {
  artlight_host_mask=$(timeout --signal=KILL 5 systemctl is-enabled \
    artlight.service 2>/dev/null || true)
  artlight_host_load=$(timeout --signal=KILL 5 systemctl show \
    artlight.service --property=LoadState 2>/dev/null) || return 1
  case "$artlight_host_mask" in
    masked | masked-runtime) [ "$artlight_host_load" = 'LoadState=masked' ] ;;
    *) return 1 ;;
  esac
}
artlight_broker_socket_is_masked() {
  artlight_socket_mask=$(timeout --signal=KILL 5 systemctl is-enabled \
    artlight-session-exec.socket 2>/dev/null || true)
  artlight_socket_load=$(timeout --signal=KILL 5 systemctl show \
    artlight-session-exec.socket --property=LoadState 2>/dev/null) || return 1
  case "$artlight_socket_mask" in
    masked | masked-runtime) [ "$artlight_socket_load" = 'LoadState=masked' ] ;;
    *) return 1 ;;
  esac
}
artlight_control_socket_is_masked() {
  artlight_socket_mask=$(timeout --signal=KILL 5 systemctl is-enabled \
    vibeshine-vkms-control.socket 2>/dev/null || true)
  artlight_socket_load=$(timeout --signal=KILL 5 systemctl show \
    vibeshine-vkms-control.socket --property=LoadState 2>/dev/null) || return 1
  case "$artlight_socket_mask" in
    masked | masked-runtime) [ "$artlight_socket_load" = 'LoadState=masked' ] ;;
    *) return 1 ;;
  esac
}
artlight_restore_unit_is_safe() {
  artlight_restore_unit=$1
  case "$artlight_restore_unit" in artlight-session-restore@*.service) ;; *) return 1 ;; esac
  artlight_restore_instance=${artlight_restore_unit#artlight-session-restore@}
  artlight_restore_instance=${artlight_restore_instance%.service}
  case "$artlight_restore_instance" in [1-9]*) ;; *) return 1 ;; esac
  case "$artlight_restore_instance" in *[!0-9]*) return 1 ;; esac
}
artlight_broker_unit_is_safe() {
  artlight_broker_unit=$1
  case "$artlight_broker_unit" in artlight-session-exec@*.service) ;; *) return 1 ;; esac
  artlight_broker_instance=${artlight_broker_unit#artlight-session-exec@}
  artlight_broker_instance=${artlight_broker_instance%.service}
  [ -n "$artlight_broker_instance" ] || return 1
  case "$artlight_broker_instance" in *[!A-Za-z0-9_.:-]*) return 1 ;; esac
}
artlight_control_unit_is_safe() {
  artlight_control_unit=$1
  case "$artlight_control_unit" in vibeshine-vkms-control@*.service) ;; *) return 1 ;; esac
  artlight_control_instance=${artlight_control_unit#vibeshine-vkms-control@}
  artlight_control_instance=${artlight_control_instance%.service}
  [ -n "$artlight_control_instance" ] || return 1
  case "$artlight_control_instance" in *[!A-Za-z0-9_.:-]*) return 1 ;; esac
}
artlight_stop_exact_unit() {
  # Never bypass a service's ordered resource teardown.  Killing systemctl
  # only abandons the client while its manager job continues; killing the unit
  # cgroup can strand live GPU imports and is therefore forbidden here.
  # Controller cleanup can legitimately spend more than 30 seconds draining
  # the host and GPU bindings; keep waiting for the manager's ordered stop.
  timeout --signal=TERM --kill-after=2 60 systemctl stop "$1" 2>/dev/null
}
artlight_bounded_unit_list() (
  artlight_unit_pattern=$1
  artlight_unit_list=$(mktemp /run/artlight-unit-list.XXXXXX) || exit 1
  trap 'rm -f -- "$artlight_unit_list"' 0
  trap 'exit 1' HUP INT TERM
  chmod 0600 "$artlight_unit_list" || return 1
  (
    ulimit -f 128 || exit 1
    timeout --signal=KILL 5 systemctl list-units --all --plain \
      --no-legend --no-pager --full "$artlight_unit_pattern" \
      >"$artlight_unit_list" 2>/dev/null
  ) || return 1
  artlight_unit_list_size=$(stat -c '%%s' -- "$artlight_unit_list") || return 1
  case "$artlight_unit_list_size" in '' | *[!0-9]*) return 1 ;; esac
  [ "$artlight_unit_list_size" -le 65536 ] || return 1
  cat -- "$artlight_unit_list"
)
artlight_control_instances_are_quiescent() (
  artlight_control_attempt=0
  artlight_control_clean_passes=0
  while [ "$artlight_control_attempt" -lt 100 ]; do
    artlight_control_dirty=0
    artlight_controls=$(artlight_bounded_unit_list \
      'vibeshine-vkms-control@*.service') || return 1
    artlight_seen_units='
'
    while IFS= read -r artlight_line || [ -n "$artlight_line" ]; do
      [ -n "$artlight_line" ] || continue
      case "$artlight_line" in *"\r"*) return 1 ;; esac
      set -f
      set -- $artlight_line
      [ "${1:-}" = '●' ] && shift
      [ "$#" -ge 4 ] || return 1
      artlight_unit=$1; artlight_load=$2
      artlight_state=$3; artlight_substate=$4
      artlight_control_unit_is_safe "$artlight_unit" || return 1
      [ "$artlight_load" = loaded ] || return 1
      case "$artlight_state:$artlight_substate" in *[!a-z:-]*) return 1 ;; esac
      case "$artlight_seen_units" in *"
$artlight_unit
"*) return 1 ;; esac
      artlight_seen_units="$artlight_seen_units$artlight_unit
"
      artlight_unit_is_quiescent "$artlight_unit" || artlight_control_dirty=1
    done <<EOF
$artlight_controls
EOF
    if [ "$artlight_control_dirty" -eq 0 ]; then
      artlight_control_clean_passes=$((artlight_control_clean_passes + 1))
      [ "$artlight_control_clean_passes" -lt 5 ] || {
        [ ! -e "$artlight_control_socket" ] && [ ! -L "$artlight_control_socket" ]
        return
      }
    else
      artlight_control_clean_passes=0
    fi
    artlight_control_attempt=$((artlight_control_attempt + 1))
    sleep 0.1
  done
  return 1
)
artlight_stop_brokers() (
  artlight_broker_attempt=0
  artlight_broker_clean_passes=0
  while [ "$artlight_broker_attempt" -lt 20 ]; do
    artlight_broker_dirty=0
    artlight_brokers=$(artlight_bounded_unit_list \
      'artlight-session-exec@*.service') || return 1
    artlight_seen_units='
'
    while IFS= read -r artlight_line || [ -n "$artlight_line" ]; do
      [ -n "$artlight_line" ] || continue
      case "$artlight_line" in *"
"*) return 1 ;; esac
      set -f
      set -- $artlight_line
      [ "${1:-}" = '●' ] && shift
      [ "$#" -ge 4 ] || return 1
      artlight_unit=$1; artlight_load=$2
      artlight_state=$3; artlight_substate=$4
      artlight_broker_unit_is_safe "$artlight_unit" || return 1
      [ "$artlight_load" = loaded ] || return 1
      case "$artlight_state:$artlight_substate" in *[!a-z:-]*) return 1 ;; esac
      case "$artlight_seen_units" in *"
$artlight_unit
"*) return 1 ;; esac
      artlight_seen_units="$artlight_seen_units$artlight_unit
"
      if ! artlight_unit_is_quiescent "$artlight_unit"; then
        artlight_broker_dirty=1
        artlight_stop_exact_unit "$artlight_unit"
        artlight_unit_is_quiescent "$artlight_unit" || return 1
      fi
    done <<EOF
$artlight_brokers
EOF
    if [ "$artlight_broker_dirty" -eq 0 ]; then
      artlight_broker_clean_passes=$((artlight_broker_clean_passes + 1))
      [ "$artlight_broker_clean_passes" -lt 5 ] || return 0
    else
      artlight_broker_clean_passes=0
    fi
    artlight_broker_attempt=$((artlight_broker_attempt + 1))
    sleep 0.1
  done
  return 1
)
artlight_stop_restore_instances() (
  artlight_restores=$(artlight_bounded_unit_list \
    'artlight-session-restore@*.service') || return 1
  artlight_seen_units='
'
  while IFS= read -r artlight_line || [ -n "$artlight_line" ]; do
    [ -n "$artlight_line" ] || continue
    case "$artlight_line" in *"
"*) return 1 ;; esac
    set -f
    set -- $artlight_line
    [ "${1:-}" = '●' ] && shift
    [ "$#" -ge 4 ] || return 1
    artlight_unit=$1; artlight_load=$2
    artlight_state=$3; artlight_substate=$4
    artlight_restore_unit_is_safe "$artlight_unit" || return 1
    case "$artlight_load" in loaded | masked) ;; *) return 1 ;; esac
    case "$artlight_state:$artlight_substate" in *[!a-z:-]*) return 1 ;; esac
    case "$artlight_seen_units" in *"
$artlight_unit
"*) return 1 ;; esac
    artlight_seen_units="$artlight_seen_units$artlight_unit
"
    artlight_stop_exact_unit "$artlight_unit"
    artlight_unit_is_quiescent "$artlight_unit" || return 1
  done <<EOF
$artlight_restores
EOF
)
artlight_brokers_are_quiescent() {
  artlight_stop_brokers
}
artlight_restore_instances_are_quiescent() (
  artlight_restores=$(artlight_bounded_unit_list \
    'artlight-session-restore@*.service') || return 1
  artlight_seen_units='
'
  while IFS= read -r artlight_line || [ -n "$artlight_line" ]; do
    [ -n "$artlight_line" ] || continue
    case "$artlight_line" in *"
"*) return 1 ;; esac
    set -f
    set -- $artlight_line
    [ "${1:-}" = '●' ] && shift
    [ "$#" -ge 4 ] || return 1
    artlight_unit=$1; artlight_load=$2
    artlight_state=$3; artlight_substate=$4
    artlight_restore_unit_is_safe "$artlight_unit" || return 1
    case "$artlight_load" in loaded | masked) ;; *) return 1 ;; esac
    case "$artlight_state:$artlight_substate" in *[!a-z:-]*) return 1 ;; esac
    case "$artlight_seen_units" in *"
$artlight_unit
"*) return 1 ;; esac
    artlight_seen_units="$artlight_seen_units$artlight_unit
"
    artlight_unit_is_quiescent "$artlight_unit" || return 1
  done <<EOF
$artlight_restores
EOF
)
artlight_privileged_helper_is_safe() {
  [ -f "$1" ] && [ ! -L "$1" ] && [ -x "$1" ] || return 1
  [ "$(stat -c '%%u:%%g:%%a:%%h:%%F' -- "$1")" = \
    '0:0:755:1:regular file' ]
}
artlight_select_upgrade_kill_mode() {
  if [ ! -e "$artlight_legacy_host" ] && [ ! -L "$artlight_legacy_host" ]; then
    artlight_unit_is_quiescent artlight.service || return 1
    artlight_upgrade_kill_mode=control-group
    return 0
  fi
  artlight_privileged_helper_is_safe "$artlight_legacy_host" || return 1
  if grep -Fqx '  trap request_host_shutdown TERM INT HUP' "$artlight_legacy_host"; then
    artlight_upgrade_kill_mode=mixed
  elif grep -Fqx '  trap mark_host_shutdown TERM INT HUP' "$artlight_legacy_host"; then
    artlight_upgrade_kill_mode=control-group
  elif grep -Fqx "  trap 'forward_host_signal TERM' TERM" "$artlight_legacy_host" && \
       grep -Fqx "  trap 'forward_host_signal INT' INT" "$artlight_legacy_host" && \
       grep -Fqx "  trap 'forward_host_signal HUP' HUP" "$artlight_legacy_host"; then
    artlight_upgrade_kill_mode=process
  else
    return 1
  fi
}
artlight_prepare_host_upgrade_fence() (
  [ ! -L "$artlight_host_upgrade_dropin_dir" ] && \
    [ ! -L "$artlight_host_upgrade_dropin" ] || exit 1
  install -d -o root -g root -m 0755 -- "$artlight_host_upgrade_dropin_dir" || exit 1
  [ "$(stat -c '%%u:%%g:%%a:%%F' -- "$artlight_host_upgrade_dropin_dir")" = \
    '0:0:755:directory' ] || exit 1
  artlight_upgrade_temporary=$(mktemp /run/artlight-host-upgrade.XXXXXX) || exit 1
  trap 'rm -f -- "$artlight_upgrade_temporary"' 0
  case "$artlight_upgrade_kill_mode" in mixed | process | control-group) ;; *) exit 1 ;; esac
  printf '[Unit]\nRefuseManualStart=yes\n\n[Service]\nKillMode=%%s\nSendSIGKILL=no\n' \
    "$artlight_upgrade_kill_mode" >"$artlight_upgrade_temporary" || exit 1
  chmod 0600 -- "$artlight_upgrade_temporary" || exit 1
  if [ -e "$artlight_host_upgrade_dropin" ]; then
    [ "$(stat -c '%%u:%%g:%%a:%%h:%%F' -- "$artlight_host_upgrade_dropin")" = \
      '0:0:644:1:regular file' ] || exit 1
    cmp -s -- "$artlight_upgrade_temporary" "$artlight_host_upgrade_dropin" || exit 1
  fi
  install -o root -g root -m 0644 -- "$artlight_upgrade_temporary" \
    "$artlight_host_upgrade_dropin" || exit 1
  [ "$(stat -c '%%u:%%g:%%a:%%h:%%F' -- "$artlight_host_upgrade_dropin")" = \
    '0:0:644:1:regular file' ] || exit 1
)
artlight_activate_host_upgrade_fence() {
  systemctl daemon-reload || exit 1
  artlight_host_stop_properties=$(timeout --signal=KILL 5 systemctl show artlight.service \
    --property=RefuseManualStart --property=KillMode --property=SendSIGKILL 2>/dev/null) || exit 1
  printf '%%s\n' "$artlight_host_stop_properties" | grep -qx 'RefuseManualStart=yes' || exit 1
  printf '%%s\n' "$artlight_host_stop_properties" | \
    grep -qx "KillMode=$artlight_upgrade_kill_mode" || exit 1
  printf '%%s\n' "$artlight_host_stop_properties" | grep -qx 'SendSIGKILL=no' || exit 1
}
artlight_host_is_stable_or_quiescent() {
  artlight_host_state=$(timeout --signal=KILL 5 systemctl show artlight.service \
    --property=ActiveState --property=SubState --property=MainPID \
    --property=ControlGroup --property=Job 2>/dev/null) || return 1
  artlight_host_active=$(printf '%%s\n' "$artlight_host_state" | sed -n 's/^ActiveState=//p')
  artlight_host_substate=$(printf '%%s\n' "$artlight_host_state" | sed -n 's/^SubState=//p')
  artlight_host_pid=$(printf '%%s\n' "$artlight_host_state" | sed -n 's/^MainPID=//p')
  artlight_host_cgroup=$(printf '%%s\n' "$artlight_host_state" | sed -n 's/^ControlGroup=//p')
  artlight_host_job=$(printf '%%s\n' "$artlight_host_state" | sed -n 's/^Job=//p')
  [ -z "$artlight_host_job" ] || return 1
  if [ "$artlight_host_active" = active ] && [ "$artlight_host_substate" = running ]; then
    case "$artlight_host_pid" in '' | 0 | *[!0-9]*) return 1 ;; esac
    case "$artlight_host_cgroup" in /*) return 0 ;; *) return 1 ;; esac
  fi
  case "$artlight_host_active:$artlight_host_substate:$artlight_host_pid" in
    inactive:dead:0 | failed:failed:0) artlight_cgroup_is_quiescent "$artlight_host_cgroup" ;;
    *) return 1 ;;
  esac
}
artlight_freeze_controller() {
  if artlight_unit_is_quiescent artlight-session-controller.service; then
    artlight_controller_was_frozen=0
    return 0
  fi
  artlight_controller_state=$(timeout --signal=KILL 5 systemctl show \
    artlight-session-controller.service --property=ActiveState --property=SubState \
    --property=MainPID --property=ControlGroup --property=FreezerState 2>/dev/null) || return 1
  artlight_controller_active=$(printf '%%s\n' "$artlight_controller_state" | sed -n 's/^ActiveState=//p')
  artlight_controller_substate=$(printf '%%s\n' "$artlight_controller_state" | sed -n 's/^SubState=//p')
  artlight_controller_pid=$(printf '%%s\n' "$artlight_controller_state" | sed -n 's/^MainPID=//p')
  artlight_controller_cgroup=$(printf '%%s\n' "$artlight_controller_state" | sed -n 's/^ControlGroup=//p')
  [ "$artlight_controller_active" = active ] && [ "$artlight_controller_substate" = running ] || return 1
  case "$artlight_controller_pid" in '' | 0 | *[!0-9]*) return 1 ;; esac
  case "$artlight_controller_cgroup" in /*) ;; *) return 1 ;; esac
  case "$artlight_controller_cgroup" in */../* | */..) return 1 ;; esac
  # A timed-out freeze may have succeeded; the abort path must attempt thaw.
  artlight_controller_was_frozen=1
  timeout --signal=TERM --kill-after=2 15 systemctl freeze \
    artlight-session-controller.service 2>/dev/null || return 1
  artlight_controller_state=$(timeout --signal=KILL 5 systemctl show \
    artlight-session-controller.service --property=ActiveState --property=SubState \
    --property=MainPID --property=ControlGroup --property=FreezerState 2>/dev/null) || return 1
  printf '%%s\n' "$artlight_controller_state" | grep -qx 'ActiveState=active' && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx 'SubState=running' && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx "MainPID=$artlight_controller_pid" && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx "ControlGroup=$artlight_controller_cgroup" && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx 'FreezerState=frozen' || return 1
  artlight_controller_freeze_path=/sys/fs/cgroup$artlight_controller_cgroup/cgroup.freeze
  artlight_controller_events_path=/sys/fs/cgroup$artlight_controller_cgroup/cgroup.events
  [ -f "$artlight_controller_freeze_path" ] && [ ! -L "$artlight_controller_freeze_path" ] && \
    grep -qx '1' "$artlight_controller_freeze_path" && \
    [ -f "$artlight_controller_events_path" ] && [ ! -L "$artlight_controller_events_path" ] && \
    grep -qx 'populated 1' "$artlight_controller_events_path" && \
    grep -qx 'frozen 1' "$artlight_controller_events_path" || return 1
}
artlight_controller_remains_frozen() {
  [ "$artlight_controller_was_frozen" -eq 1 ] || return 0
  artlight_controller_state=$(timeout --signal=KILL 5 systemctl show \
    artlight-session-controller.service --property=ActiveState --property=SubState \
    --property=MainPID --property=ControlGroup --property=FreezerState 2>/dev/null) || return 1
  printf '%%s\n' "$artlight_controller_state" | grep -qx 'ActiveState=active' && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx 'SubState=running' && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx "MainPID=$artlight_controller_pid" && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx "ControlGroup=$artlight_controller_cgroup" && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx 'FreezerState=frozen'
}
artlight_thaw_controller() {
  [ "$artlight_controller_was_frozen" -eq 1 ] || return 0
  timeout --signal=KILL 15 systemctl thaw \
    artlight-session-controller.service 2>/dev/null || return 1
  artlight_controller_was_frozen=0
  artlight_controller_state=$(timeout --signal=KILL 5 systemctl show \
    artlight-session-controller.service --property=ActiveState --property=SubState \
    --property=MainPID --property=ControlGroup --property=FreezerState 2>/dev/null) || return 1
  printf '%%s\n' "$artlight_controller_state" | grep -qx 'ActiveState=active' && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx 'SubState=running' && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx "MainPID=$artlight_controller_pid" && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx "ControlGroup=$artlight_controller_cgroup" && \
    printf '%%s\n' "$artlight_controller_state" | grep -qx 'FreezerState=running'
}
artlight_run_optional_legacy_command() {
  if [ ! -e "$artlight_legacy_host" ] && [ ! -L "$artlight_legacy_host" ]; then return 2; fi
  artlight_privileged_helper_is_safe "$artlight_legacy_host" || return 1
  timeout --signal=KILL 40 "$artlight_legacy_host" "$1" >/dev/null 2>&1
  artlight_command_status=$?
  [ "$artlight_command_status" -eq 0 ] && return 0
  [ "$artlight_command_status" -eq 2 ] && return 2
  return 1
}
artlight_runtime_root_is_safe_or_absent() {
  if [ ! -e "$artlight_runtime_root" ] && [ ! -L "$artlight_runtime_root" ]; then return 0; fi
  [ -d "$artlight_runtime_root" ] && [ ! -L "$artlight_runtime_root" ] || return 1
  [ "$(stat -c '%%u:%%g:%%a:%%F' -- "$artlight_runtime_root")" = '0:0:755:directory' ]
}
artlight_legacy_handoff_process_is_running() {
  for artlight_proc in /proc/[0-9]*; do
    [ -d "$artlight_proc" ] || continue
    if grep -Fzxq -- "$artlight_legacy_handoff" "$artlight_proc/cmdline" 2>/dev/null; then return 0; fi
  done
  return 1
}
artlight_wait_for_legacy_handoff() {
  artlight_handoff_attempt=0
  artlight_handoff_clean_passes=0
  while [ "$artlight_handoff_attempt" -lt 400 ]; do
    if artlight_legacy_handoff_process_is_running; then
      artlight_handoff_clean_passes=0
    else
      artlight_handoff_clean_passes=$((artlight_handoff_clean_passes + 1))
      [ "$artlight_handoff_clean_passes" -lt 10 ] || return 0
    fi
    artlight_handoff_attempt=$((artlight_handoff_attempt + 1))
    sleep 0.1
  done
  return 1
}
artlight_disable_legacy_handoff() {
  if [ ! -e "$artlight_legacy_handoff" ] && [ ! -L "$artlight_legacy_handoff" ]; then return 0; fi
  [ -f "$artlight_legacy_handoff" ] && [ ! -L "$artlight_legacy_handoff" ] || return 1
  artlight_handoff_attributes=$(stat -c '%%u:%%g:%%a:%%h' -- "$artlight_legacy_handoff") || return 1
  case "$artlight_handoff_attributes" in
    '0:0:755:1' | '0:0:0:1') ;;
    *) return 1 ;;
  esac
  artlight_legacy_handoff_identity=$(stat -Lc '%%d:%%i' -- "$artlight_legacy_handoff") || return 1
  chmod 000 -- "$artlight_legacy_handoff" || return 1
  artlight_handoff_attributes=$(stat -Lc '%%d:%%i:%%u:%%g:%%a:%%h' -- \
    "$artlight_legacy_handoff") || return 1
  [ "$artlight_handoff_attributes" = \
    "$artlight_legacy_handoff_identity:0:0:0:1" ] || return 1
  artlight_wait_for_legacy_handoff
}
artlight_legacy_state_file_is_safe() {
  artlight_legacy_state_path=$1
  artlight_legacy_state_name=${artlight_legacy_state_path##*/}
  case "$artlight_legacy_state_name" in [1-9]*) ;; *) return 1 ;; esac
  case "$artlight_legacy_state_name" in *[!0-9]*) return 1 ;; esac
  [ -f "$artlight_legacy_state_path" ] && [ ! -L "$artlight_legacy_state_path" ] || return 1
  artlight_legacy_state_attributes=$(stat -c '%%u:%%g:%%a:%%h:%%s' -- \
    "$artlight_legacy_state_path") || return 1
  case "$artlight_legacy_state_attributes" in
    0:0:600:1:* | 0:0:644:1:*) ;;
    *) return 1 ;;
  esac
  artlight_legacy_state_size=${artlight_legacy_state_attributes##*:}
  case "$artlight_legacy_state_size" in '' | *[!0-9]*) return 1 ;; esac
  [ "$artlight_legacy_state_size" -le 4096 ] || return 1
  {
    IFS= read -r artlight_legacy_state_user &&
      IFS= read -r artlight_legacy_state_uid &&
      IFS= read -r artlight_legacy_state_token &&
      ! IFS= read -r artlight_legacy_state_extra
  } <"$artlight_legacy_state_path" || return 1
  case "$artlight_legacy_state_user" in [a-z_]*) ;; *) return 1 ;; esac
  case "$artlight_legacy_state_user" in *[!a-z0-9_-]*) return 1 ;; esac
  [ "$artlight_legacy_state_uid" = "$artlight_legacy_state_name" ] || return 1
  [ "${#artlight_legacy_state_token}" -eq 32 ] || return 1
  case "$artlight_legacy_state_token" in *[!0-9a-f]*) return 1 ;; esac
}
artlight_remove_legacy_state_directory() {
  artlight_legacy_directory=$1
  if [ ! -e "$artlight_legacy_directory" ] && [ ! -L "$artlight_legacy_directory" ]; then return 0; fi
  [ -d "$artlight_legacy_directory" ] && [ ! -L "$artlight_legacy_directory" ] || return 1
  artlight_legacy_directory_attributes=$(stat -c '%%u:%%g:%%a' -- \
    "$artlight_legacy_directory") || return 1
  case "$artlight_legacy_directory_attributes" in
    '0:0:700' | '0:0:755') ;;
    *) return 1 ;;
  esac
  for artlight_legacy_state_path in "$artlight_legacy_directory"/* \
    "$artlight_legacy_directory"/.[!.]* "$artlight_legacy_directory"/..?*; do
    if [ ! -e "$artlight_legacy_state_path" ] && [ ! -L "$artlight_legacy_state_path" ]; then continue; fi
    artlight_legacy_state_file_is_safe "$artlight_legacy_state_path" || return 1
    rm -f -- "$artlight_legacy_state_path" || return 1
  done
  rmdir -- "$artlight_legacy_directory" || return 1
  [ ! -e "$artlight_legacy_directory" ] && [ ! -L "$artlight_legacy_directory" ]
}
artlight_remove_legacy_prelogin_marker() {
  if [ ! -e "$artlight_legacy_prelogin_marker" ] && [ ! -L "$artlight_legacy_prelogin_marker" ]; then return 0; fi
  [ -f "$artlight_legacy_prelogin_marker" ] && [ ! -L "$artlight_legacy_prelogin_marker" ] || return 1
  artlight_prelogin_marker_attributes=$(stat -c '%%u:%%g:%%a:%%h:%%s' -- \
    "$artlight_legacy_prelogin_marker") || return 1
  case "$artlight_prelogin_marker_attributes" in 0:0:644:1:*) ;; *) return 1 ;; esac
  artlight_prelogin_marker_size=${artlight_prelogin_marker_attributes##*:}
  case "$artlight_prelogin_marker_size" in '' | *[!0-9]*) return 1 ;; esac
  [ "$artlight_prelogin_marker_size" -le 256 ] || return 1
  {
    IFS= read -r artlight_prelogin_marker_user &&
      ! IFS= read -r artlight_prelogin_marker_extra
  } <"$artlight_legacy_prelogin_marker" || return 1
  case "$artlight_prelogin_marker_user" in [a-z_]*) ;; *) return 1 ;; esac
  case "$artlight_prelogin_marker_user" in *[!a-z0-9_-]*) return 1 ;; esac
  rm -f -- "$artlight_legacy_prelogin_marker" || return 1
  [ ! -e "$artlight_legacy_prelogin_marker" ] && [ ! -L "$artlight_legacy_prelogin_marker" ]
}
artlight_cleanup_legacy_transition_state() {
  (
    artlight_runtime_root_is_safe_or_absent || exit 1
    if [ -e "$artlight_legacy_transition_lock" ] || [ -L "$artlight_legacy_transition_lock" ]; then
      [ -f "$artlight_legacy_transition_lock" ] && [ ! -L "$artlight_legacy_transition_lock" ] || exit 1
      [ "$(stat -c '%%u:%%g:%%a:%%h:%%s' -- "$artlight_legacy_transition_lock")" = \
        '0:0:600:1:0' ] || exit 1
      exec 9<>"$artlight_legacy_transition_lock" || exit 1
      timeout --signal=KILL 10 flock --exclusive 9 || exit 1
    fi
    artlight_wait_for_legacy_handoff || exit 1
    artlight_remove_legacy_state_directory "$artlight_legacy_handoff_directory" || exit 1
    artlight_remove_legacy_state_directory "$artlight_legacy_restore_directory" || exit 1
    artlight_remove_legacy_prelogin_marker || exit 1
    if [ -e "$artlight_legacy_transition_lock" ] || [ -L "$artlight_legacy_transition_lock" ]; then
      rm -f -- "$artlight_legacy_transition_lock" || exit 1
    fi
    [ ! -e "$artlight_legacy_transition_lock" ] && [ ! -L "$artlight_legacy_transition_lock" ]
  )
}
artlight_abort_quiesce() {
  echo 'error: could not safely quiesce the existing ArtLight host; package replacement is blocked.' >&2

  if [ "${artlight_shutdown_started:-0}" -eq 0 ]; then
    case "${artlight_pre_handoff_identity:-}" in
      *:0:0:755:1)
        artlight_handoff_expected=$(printf '%s\n' "$artlight_pre_handoff_identity" | sed 's/:755:1$/:0:1/')
        if [ -f "$artlight_legacy_handoff" ] && [ ! -L "$artlight_legacy_handoff" ] &&
           [ "$(stat -Lc '%%d:%%i:%%u:%%g:%%a:%%h' -- "$artlight_legacy_handoff" 2>/dev/null)" = "$artlight_handoff_expected" ]; then
          chmod 0755 -- "$artlight_legacy_handoff" ||
            echo 'error: could not restore the legacy handoff executable mode.' >&2
        fi
        ;;
    esac
    if [ "${artlight_controller_was_frozen:-0}" -eq 1 ]; then
      timeout --signal=KILL 15 systemctl thaw artlight-session-controller.service 2>/dev/null || true
      artlight_controller_was_frozen=0
    fi
    echo 'error: the old host was not stopped; admission may be closed. Retry the upgrade after fixing the failed step, or reboot to clear runtime fences.' >&2
    return 0
  fi
  if [ "${artlight_controller_was_frozen:-0}" -eq 1 ]; then
    timeout --signal=KILL 15 systemctl thaw artlight-session-controller.service 2>/dev/null || true
    artlight_controller_was_frozen=0
  fi
  echo 'error: admission remains closed and the prior host may be stopped. Do not restart a populated GPU host; retry the package upgrade or reboot after investigating the failed step.' >&2
  artlight_stop_exact_unit artlight-session-controller.service
  artlight_stop_exact_unit artlight.service
}
artlight_quiesce_machine_host() {
  artlight_have_systemd=0
  if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
    artlight_have_systemd=1
    artlight_shutdown_started=0
    artlight_pre_handoff_identity=''
    if [ -f "$artlight_legacy_handoff" ] && [ ! -L "$artlight_legacy_handoff" ]; then
      artlight_pre_handoff_identity=$(stat -Lc '%%d:%%i:%%u:%%g:%%a:%%h' -- "$artlight_legacy_handoff" 2>/dev/null || true)
    fi
    artlight_select_upgrade_kill_mode || return 1
    artlight_prepare_host_upgrade_fence || return 1
    artlight_freeze_controller || return 1
    artlight_host_is_stable_or_quiescent || return 1
    artlight_activate_host_upgrade_fence || return 1
    artlight_controller_remains_frozen || return 1
    artlight_host_is_stable_or_quiescent || return 1
    timeout --signal=KILL 15 systemctl mask --runtime \
      'artlight-session-restore@.service' 2>/dev/null || return 1
    artlight_restore_template_is_masked || return 1
    artlight_disable_legacy_handoff || return 1
    timeout --signal=KILL 15 systemctl mask --runtime vibeshine-vkms-control.socket 2>/dev/null || return 1
    artlight_control_socket_is_masked || return 1
    artlight_stop_exact_unit vibeshine-vkms-control.socket
    artlight_unit_is_quiescent vibeshine-vkms-control.socket || return 1
    artlight_control_instances_are_quiescent || return 1
    artlight_shutdown_started=1
    timeout --signal=KILL 15 systemctl mask --runtime artlight-session-exec.socket 2>/dev/null || return 1
    artlight_broker_socket_is_masked || return 1
    artlight_stop_exact_unit artlight-session-exec.socket
    artlight_unit_is_quiescent artlight-session-exec.socket || return 1
    artlight_stop_exact_unit artlight.service
    artlight_unit_is_quiescent artlight.service || return 1
    timeout --signal=KILL 15 systemctl mask --runtime artlight.service 2>/dev/null || return 1
    artlight_host_unit_is_masked || return 1
    artlight_stop_restore_instances || return 1
    artlight_stop_brokers || return 1
    artlight_controller_remains_frozen || return 1
    # systemd refuses StopUnit for a frozen service. Admission and the host are
    # already masked here, so thaw the controller only for its ordered stop.
    artlight_thaw_controller || return 1
    artlight_stop_exact_unit artlight-session-controller.service
    artlight_unit_is_quiescent artlight-session-controller.service || return 1
    artlight_stop_exact_unit artlight-prelogin.service
    artlight_stop_exact_unit artlight-machine-prepare.service
  fi

  if [ "$artlight_have_systemd" -eq 0 ]; then artlight_disable_legacy_handoff || return 1; fi
  artlight_runtime_root_is_safe_or_absent || return 1

  if [ "$artlight_have_systemd" -eq 0 ] && \
     { [ -e "$artlight_controller" ] || [ -L "$artlight_controller" ]; }; then
    artlight_privileged_helper_is_safe "$artlight_controller" || return 1
    timeout --signal=KILL 40 "$artlight_controller" cleanup || return 1
  elif [ "$artlight_have_systemd" -eq 0 ]; then
    artlight_run_optional_legacy_command cleanup
    artlight_legacy_status=$?
    case "$artlight_legacy_status" in 0 | 2) ;; *) return 1 ;; esac
  fi

  artlight_run_optional_legacy_command remove-pam
  artlight_legacy_status=$?
  case "$artlight_legacy_status" in 0 | 2) ;; *) return 1 ;; esac

  if [ "$artlight_have_systemd" -eq 1 ]; then
    artlight_restore_template_is_masked || return 1
    artlight_host_unit_is_masked || return 1
    artlight_broker_socket_is_masked || return 1
    artlight_stop_restore_instances || return 1
    artlight_stop_exact_unit artlight.service
    artlight_unit_is_quiescent artlight.service || return 1
    artlight_stop_exact_unit artlight-session-exec.socket
    artlight_stop_brokers || return 1
    for artlight_unit in artlight-session-exec.socket artlight-session-controller.service \
      artlight.service artlight-prelogin.service artlight-machine-prepare.service; do
      artlight_stop_exact_unit "$artlight_unit"
      artlight_unit_is_quiescent "$artlight_unit" || return 1
    done
    artlight_brokers_are_quiescent || return 1
    artlight_restore_instances_are_quiescent || return 1
  fi
  artlight_cleanup_legacy_transition_state || return 1
  if [ -S "$artlight_broker_socket" ] && [ ! -L "$artlight_broker_socket" ]; then
    rm -f -- "$artlight_broker_socket" || return 1
  fi
  [ ! -e "$artlight_broker_socket" ] && [ ! -L "$artlight_broker_socket" ] && \
    [ ! -e "$artlight_legacy_handoff_directory" ] && [ ! -L "$artlight_legacy_handoff_directory" ] && \
    [ ! -e "$artlight_legacy_restore_directory" ] && [ ! -L "$artlight_legacy_restore_directory" ] && \
    [ ! -e "$artlight_legacy_transition_lock" ] && [ ! -L "$artlight_legacy_transition_lock" ] && \
    [ ! -e "$artlight_legacy_prelogin_marker" ] && [ ! -L "$artlight_legacy_prelogin_marker" ]
}
if ! artlight_quiesce_machine_host; then
  artlight_abort_quiesce
  echo "error: installed ArtLight services did not quiesce; replacement is blocked and admission remains disabled." >&2
  exit 1
fi

%post
# Note: this is copied from the postinst script

artlight_controller=%{_prefix}/libexec/vibeshine/artlight-session-controller
artlight_session_record=/run/artlight/session.env
artlight_legacy_acl=/run/artlight/runtime-acl
artlight_broker_socket=/run/artlight/session-broker.sock
artlight_host_upgrade_dropin_dir=/run/systemd/system/artlight.service.d
artlight_host_upgrade_dropin=$artlight_host_upgrade_dropin_dir/90-artlight-safe-upgrade.conf
artlight_privileged_helper_is_safe() {
  [ -f "$1" ] && [ ! -L "$1" ] && [ -x "$1" ] || return 1
  [ "$(stat -c '%%u:%%g:%%a:%%h:%%F' -- "$1")" = \
    '0:0:755:1:regular file' ]
}
artlight_unmask_host_for_controller() {
  artlight_unit_is_quiescent artlight-session-controller.service || return 1
  artlight_unit_is_quiescent artlight-session-exec.socket || return 1
  if [ -e "$artlight_host_upgrade_dropin" ] || [ -L "$artlight_host_upgrade_dropin" ]; then
    [ -f "$artlight_host_upgrade_dropin" ] && [ ! -L "$artlight_host_upgrade_dropin" ] || return 1
    [ "$(stat -c '%%u:%%g:%%a:%%h:%%F' -- "$artlight_host_upgrade_dropin")" = \
      '0:0:644:1:regular file' ] || return 1
    artlight_upgrade_contents=$(sed -n '1,6p' -- "$artlight_host_upgrade_dropin") || return 1
    case "$artlight_upgrade_contents" in
      '[Unit]
RefuseManualStart=yes

[Service]
KillMode=process
SendSIGKILL=no' | \
      '[Unit]
RefuseManualStart=yes

[Service]
KillMode=control-group
SendSIGKILL=no' | \
      '[Unit]
RefuseManualStart=yes

[Service]
KillMode=mixed
SendSIGKILL=no') ;;
      *) return 1 ;;
    esac
    rm -f -- "$artlight_host_upgrade_dropin" || return 1
    rmdir -- "$artlight_host_upgrade_dropin_dir" 2>/dev/null || true
  fi
  systemctl daemon-reload || return 1
  timeout --signal=KILL 15 systemctl unmask --runtime artlight.service 2>/dev/null || return 1
  systemctl daemon-reload || return 1
  artlight_new_host_properties=$(timeout --signal=KILL 5 systemctl show artlight.service \
    --property=RefuseManualStart --property=KillMode --property=SendSIGKILL 2>/dev/null) || return 1
  printf '%%s\n' "$artlight_new_host_properties" | grep -qx 'RefuseManualStart=no' && \
    printf '%%s\n' "$artlight_new_host_properties" | grep -qx 'KillMode=mixed' && \
    printf '%%s\n' "$artlight_new_host_properties" | grep -qx 'SendSIGKILL=no' || return 1
  timeout --signal=KILL 15 systemctl unmask --runtime vibeshine-vkms-control.socket 2>/dev/null || return 1
  systemctl start vibeshine-vkms-control.socket || return 1
  systemctl is-active --quiet vibeshine-vkms-control.socket || return 1
  timeout --signal=KILL 15 systemctl unmask --runtime artlight-session-exec.socket 2>/dev/null || return 1
  artlight_host_state=$(timeout --signal=KILL 5 systemctl is-enabled \
    artlight.service 2>/dev/null || true)
  artlight_host_load=$(timeout --signal=KILL 5 systemctl show \
    artlight.service --property=LoadState 2>/dev/null) || return 1
  artlight_socket_state=$(timeout --signal=KILL 5 systemctl is-enabled \
    artlight-session-exec.socket 2>/dev/null || true)
  artlight_socket_load=$(timeout --signal=KILL 5 systemctl show \
    artlight-session-exec.socket --property=LoadState 2>/dev/null) || return 1
  case "$artlight_socket_state" in
    disabled | static | indirect | generated | transient | linked | linked-runtime)
      [ "$artlight_socket_load" = 'LoadState=loaded' ] || return 1 ;;
    *) return 1 ;;
  esac
  case "$artlight_host_state" in
    disabled | static | indirect | generated | transient | linked | linked-runtime)
      [ "$artlight_host_load" = 'LoadState=loaded' ] ;;
    *) return 1 ;;
  esac
}
artlight_cgroup_is_quiescent() {
  artlight_control_group=$1
  [ -n "$artlight_control_group" ] || return 0
  case "$artlight_control_group" in /*) ;; *) return 1 ;; esac
  case "$artlight_control_group" in */../* | */..) return 1 ;; esac
  artlight_cgroup_path=/sys/fs/cgroup$artlight_control_group
  if [ ! -e "$artlight_cgroup_path" ] && [ ! -L "$artlight_cgroup_path" ]; then return 0; fi
  [ -d "$artlight_cgroup_path" ] && [ ! -L "$artlight_cgroup_path" ] && \
    [ -f "$artlight_cgroup_path/cgroup.events" ] && \
    [ ! -L "$artlight_cgroup_path/cgroup.events" ] && \
    grep -qx 'populated 0' "$artlight_cgroup_path/cgroup.events"
}
artlight_unit_is_quiescent() {
  artlight_properties=$(timeout --signal=KILL 5 systemctl show "$1" \
    --property=LoadState --property=ActiveState --property=SubState --property=MainPID \
    --property=ControlGroup 2>/dev/null) || return 1
  artlight_property_lines=$(printf '%%s\n' "$artlight_properties" | wc -l | tr -d ' ')
  case "$1:$artlight_property_lines" in
    *.socket:4 | *.socket:5 | *:5) ;;
    *) return 1 ;;
  esac
  for artlight_property in LoadState ActiveState SubState ControlGroup; do
    artlight_property_count=$(printf '%%s\n' "$artlight_properties" | \
      grep -c "^$artlight_property=" || true)
    [ "$artlight_property_count" = 1 ] || return 1
  done
  artlight_property_count=$(printf '%%s\n' "$artlight_properties" | grep -c '^MainPID=' || true)
  case "$1:$artlight_property_count" in
    *.socket:0 | *.socket:1 | *:1) ;;
    *) return 1 ;;
  esac
  artlight_load=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^LoadState=//p')
  artlight_state=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^ActiveState=//p')
  artlight_substate=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^SubState=//p')
  artlight_pid=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^MainPID=//p')
  case "$1" in *.socket) [ -n "$artlight_pid" ] || artlight_pid=0 ;; esac
  artlight_control_group=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^ControlGroup=//p')
  case "$artlight_load:$artlight_state:$artlight_substate" in
    not-found:inactive:dead | \
    loaded:inactive:dead | loaded:failed:failed | \
    masked:inactive:dead | masked:failed:failed | \
    masked-runtime:inactive:dead | masked-runtime:failed:failed) ;;
    *) return 1 ;;
  esac
  [ "$artlight_pid" = 0 ] && artlight_cgroup_is_quiescent "$artlight_control_group"
}
artlight_unit_is_disabled() {
  artlight_enabled=$(timeout --signal=KILL 5 systemctl is-enabled "$1" 2>/dev/null || true)
  case "$artlight_enabled" in
    disabled | masked | masked-runtime | static | indirect | generated | transient | linked | linked-runtime | not-found) return 0 ;;
    *) return 1 ;;
  esac
}
artlight_broker_unit_is_safe() {
  artlight_broker_unit=$1
  case "$artlight_broker_unit" in artlight-session-exec@*.service) ;; *) return 1 ;; esac
  artlight_broker_instance=${artlight_broker_unit#artlight-session-exec@}
  artlight_broker_instance=${artlight_broker_instance%.service}
  [ -n "$artlight_broker_instance" ] || return 1
  case "$artlight_broker_instance" in *[!A-Za-z0-9_.:-]*) return 1 ;; esac
}
artlight_stop_exact_unit() {
  # Never bypass a service's ordered resource teardown.  Killing systemctl
  # only abandons the client while its manager job continues; killing the unit
  # cgroup can strand live GPU imports and is therefore forbidden here.
  timeout --signal=TERM --kill-after=2 60 systemctl stop "$1" 2>/dev/null
}
artlight_bounded_broker_list() (
  artlight_broker_list=$(mktemp /run/artlight-broker-units.XXXXXX) || exit 1
  trap 'rm -f -- "$artlight_broker_list"' 0
  trap 'exit 1' HUP INT TERM
  chmod 0600 "$artlight_broker_list" || return 1
  (
    ulimit -f 128 || exit 1
    timeout --signal=KILL 5 systemctl list-units --all --plain \
      --no-legend --no-pager --full 'artlight-session-exec@*.service' \
      >"$artlight_broker_list" 2>/dev/null
  ) || return 1
  artlight_broker_list_size=$(stat -c '%%s' -- "$artlight_broker_list") || return 1
  case "$artlight_broker_list_size" in '' | *[!0-9]*) return 1 ;; esac
  [ "$artlight_broker_list_size" -le 65536 ] || return 1
  cat -- "$artlight_broker_list"
)
artlight_stop_brokers() (
  artlight_broker_attempt=0
  artlight_broker_clean_passes=0
  while [ "$artlight_broker_attempt" -lt 20 ]; do
    artlight_broker_dirty=0
    artlight_brokers=$(artlight_bounded_broker_list) || return 1
    artlight_seen_units='
'
    while IFS= read -r artlight_line || [ -n "$artlight_line" ]; do
      [ -n "$artlight_line" ] || continue
      case "$artlight_line" in *"
"*) return 1 ;; esac
      set -f
      set -- $artlight_line
      [ "${1:-}" = '●' ] && shift
      [ "$#" -ge 4 ] || return 1
      artlight_unit=$1; artlight_load=$2
      artlight_state=$3; artlight_substate=$4
      artlight_broker_unit_is_safe "$artlight_unit" || return 1
      [ "$artlight_load" = loaded ] || return 1
      case "$artlight_state:$artlight_substate" in *[!a-z:-]*) return 1 ;; esac
      case "$artlight_seen_units" in *"
$artlight_unit
"*) return 1 ;; esac
      artlight_seen_units="$artlight_seen_units$artlight_unit
"
      if ! artlight_unit_is_quiescent "$artlight_unit"; then
        artlight_broker_dirty=1
        artlight_stop_exact_unit "$artlight_unit"
        artlight_unit_is_quiescent "$artlight_unit" || return 1
      fi
    done <<EOF
$artlight_brokers
EOF
    if [ "$artlight_broker_dirty" -eq 0 ]; then
      artlight_broker_clean_passes=$((artlight_broker_clean_passes + 1))
      [ "$artlight_broker_clean_passes" -lt 5 ] || return 0
    else
      artlight_broker_clean_passes=0
    fi
    artlight_broker_attempt=$((artlight_broker_attempt + 1))
    sleep 0.1
  done
  return 1
)
artlight_brokers_are_quiescent() {
  artlight_stop_brokers
}
artlight_quiesce_machine_host() {
  if ! command -v systemctl >/dev/null 2>&1 || [ ! -d /run/systemd/system ]; then
    [ ! -e "$artlight_broker_socket" ] && [ ! -L "$artlight_broker_socket" ] && \
      [ ! -e "$artlight_session_record" ] && [ ! -L "$artlight_session_record" ] && \
      [ ! -e "$artlight_legacy_acl" ] && [ ! -L "$artlight_legacy_acl" ]
    return
  fi
  timeout --signal=KILL 15 systemctl mask --runtime artlight.service \
    artlight-session-exec.socket 2>/dev/null || return 1
  artlight_unit_is_masked artlight.service || return 1
  artlight_unit_is_masked artlight-session-exec.socket || return 1
  artlight_stop_exact_unit artlight-session-exec.socket
  artlight_unit_is_quiescent artlight-session-exec.socket || return 1
  artlight_stop_exact_unit artlight.service
  artlight_unit_is_quiescent artlight.service || return 1
  artlight_stop_brokers || return 1
  for artlight_unit in artlight-session-controller.service artlight.service \
    artlight-prelogin.service artlight-machine-prepare.service; do
    artlight_stop_exact_unit "$artlight_unit"
  done
  timeout --signal=KILL 15 systemctl disable artlight-session-controller.service --now 2>/dev/null || true
  timeout --signal=KILL 15 systemctl disable artlight.service --now 2>/dev/null || true
  timeout --signal=KILL 15 systemctl disable artlight-prelogin.service --now 2>/dev/null || true
  timeout --signal=KILL 15 systemctl disable artlight-machine-prepare.service --now 2>/dev/null || true
  if [ -e "$artlight_controller" ] || [ -L "$artlight_controller" ]; then
    artlight_privileged_helper_is_safe "$artlight_controller" || return 1
    timeout --signal=KILL 40 "$artlight_controller" cleanup || return 1
  fi
  artlight_unit_is_masked artlight.service || return 1
  artlight_unit_is_masked artlight-session-exec.socket || return 1
  artlight_stop_exact_unit artlight-session-exec.socket
  artlight_stop_exact_unit artlight.service
  artlight_unit_is_quiescent artlight.service || return 1
  artlight_stop_brokers || return 1
  for artlight_unit in artlight-session-exec.socket artlight-session-controller.service \
    artlight.service artlight-prelogin.service artlight-machine-prepare.service; do
    artlight_stop_exact_unit "$artlight_unit"
    artlight_unit_is_quiescent "$artlight_unit" || return 1
    artlight_unit_is_disabled "$artlight_unit" || return 1
  done
  artlight_stop_brokers || return 1
  if [ -S "$artlight_broker_socket" ] && [ ! -L "$artlight_broker_socket" ]; then
    rm -f -- "$artlight_broker_socket" || return 1
  fi
  [ ! -e "$artlight_broker_socket" ] && [ ! -L "$artlight_broker_socket" ] && \
    [ ! -e "$artlight_session_record" ] && [ ! -L "$artlight_session_record" ] && \
    [ ! -e "$artlight_legacy_acl" ] && [ ! -L "$artlight_legacy_acl" ]
}
if ! artlight_quiesce_machine_host; then
  echo "error: could not quiesce the machine host after package replacement; ArtLight remains disabled." >&2
  exit 1
fi

for artlight_executable in \
  %{_bindir}/artlight \
  %{_prefix}/libexec/vibeshine/artlight-session-exec; do
  if [ ! -f "$artlight_executable" ] || [ -L "$artlight_executable" ]; then
    echo "error: native executable is missing or unsafe: $artlight_executable" >&2
    exit 1
  fi
  chown root:root "$artlight_executable" || exit 1
  chmod 0755 "$artlight_executable" || exit 1
  setcap -r "$artlight_executable" 2>/dev/null || true
  if [ -n "$(getcap "$artlight_executable" 2>/dev/null)" ]; then
    echo "error: unsafe capabilities remain on $artlight_executable." >&2
    exit 1
  fi
done

# Load uhid (DS5 emulation)
echo "Loading uhid kernel module for DS5 emulation."
modprobe uhid

# Check if we're in an rpm-ostree environment
if [ ! -x "$(command -v rpm-ostree)" ]; then
  echo "Not in an rpm-ostree environment, proceeding with post install steps."

  systemd-sysusers %{_prefix}/lib/sysusers.d/vibeshine-vkms.conf || \
    echo "warning: could not create the dedicated vibeshine-vkms control group."
  systemd-sysusers %{_prefix}/lib/sysusers.d/artlight.conf || {
    echo "error: could not create the dedicated ArtLight service account." >&2
    exit 1
  }
  artlight_session_broker=%{_prefix}/libexec/vibeshine/artlight-session-broker
  if [ ! -f "$artlight_session_broker" ] || [ -L "$artlight_session_broker" ]; then
    echo "error: root-only session broker is missing or unsafe." >&2
    exit 1
  fi
  chown root:root "$artlight_session_broker" || exit 1
  chmod 0700 "$artlight_session_broker" || exit 1
  setcap cap_kill,cap_setgid,cap_setuid=p "$artlight_session_broker" || exit 1
  if [ "$(stat -c '%%U:%%G:%%a:%%F' -- "$artlight_session_broker")" != \
       'root:root:700:regular file' ] ||
     [ "$(getcap "$artlight_session_broker" 2>/dev/null)" != \
       "$artlight_session_broker cap_kill,cap_setgid,cap_setuid=p" ]; then
    echo "error: root-only session broker permissions or capabilities are unsafe." >&2
    exit 1
  fi

  artlight_private_host=%{_prefix}/libexec/vibeshine/artlight-host
  if [ ! -f "$artlight_private_host" ] || [ -L "$artlight_private_host" ]; then
    echo "error: private ArtLight host is missing or unsafe." >&2
    exit 1
  fi
  artlight_public_identity=$(stat -Lc '%%d:%%i' -- %{_bindir}/artlight) || exit 1
  artlight_broker_identity=$(stat -Lc '%%d:%%i' -- "$artlight_session_broker") || exit 1
  artlight_private_identity=$(stat -Lc '%%d:%%i' -- "$artlight_private_host") || exit 1
  if [ "$artlight_public_identity" = "$artlight_private_identity" ] ||
     [ "$artlight_broker_identity" = "$artlight_private_identity" ]; then
    echo "error: privileged broker and private/public hosts must be distinct inodes." >&2
    exit 1
  fi
  chown root:artlight "$artlight_private_host" || exit 1
  chmod 0750 "$artlight_private_host" || exit 1
  setcap cap_sys_admin,cap_sys_nice=p "$artlight_private_host" || exit 1
  if [ "$(stat -c '%%U:%%G:%%a:%%F' -- "$artlight_private_host")" != \
       'root:artlight:750:regular file' ] ||
     [ "$(getcap "$artlight_private_host" 2>/dev/null)" != \
       "$artlight_private_host cap_sys_admin,cap_sys_nice=p" ]; then
    echo "error: private ArtLight host permissions or capabilities are unsafe." >&2
    exit 1
  fi
  if [ "$(getcap "$artlight_session_broker" 2>/dev/null)" != \
       "$artlight_session_broker cap_kill,cap_setgid,cap_setuid=p" ]; then
    echo "error: private-host setup altered the session broker capabilities." >&2
    exit 1
  fi
  for artlight_executable in \
    %{_bindir}/artlight \
    %{_prefix}/libexec/vibeshine/artlight-session-exec; do
    if [ -n "$(getcap "$artlight_executable" 2>/dev/null)" ]; then
      echo "error: a public ArtLight entrypoint gained file capabilities: $artlight_executable" >&2
      exit 1
    fi
  done

  # Trigger udev rule reload for /dev/uinput, /dev/uhid and input devices
  path_to_udevadm=$(command -v udevadm 2>/dev/null || true)
  if [ -x "$path_to_udevadm" ]; then
    echo "Reloading udev rules."
    $path_to_udevadm control --reload-rules
    $path_to_udevadm trigger --property-match=DEVNAME=/dev/uinput
    $path_to_udevadm trigger --property-match=DEVNAME=/dev/uhid
    # Input devices are matched by subsystem: a shared keyboard has no fixed DEVNAME, and no
    # node at all until something is sharing it. Without this line one already present when
    # the rules changed keeps root:input, and the host cannot open it.
    $path_to_udevadm trigger --subsystem-match=input
    echo "Udev rules reloaded successfully."
  else
    echo "error: udevadm not found or not executable."
  fi

  artlight_restore_kwin_capability() {
    # Earlier ArtLight builds removed cap_sys_nice from the distro KWin binary
    # so the GPU bridge could be preloaded. The bridge is now a trusted
    # set-user-ID library, so give KWin its realtime capability back.
    kwin=/usr/bin/kwin_wayland
    marker=user.vibeshine.cap_sys_nice_removed
    timeout --signal=KILL 15 systemctl disable artlight-kwin-capability.path vibeshine-kwin-capability.path --now 2>/dev/null || true
    rm -f /etc/systemd/system/multi-user.target.wants/artlight-kwin-capability.path /etc/systemd/system/multi-user.target.wants/vibeshine-kwin-capability.path
    [ -f "$kwin" ] && [ ! -L "$kwin" ] || return 0
    if command -v getfattr >/dev/null 2>&1; then
      getfattr -n "$marker" --only-values "$kwin" >/dev/null 2>&1 || return 0
    elif command -v python3 >/dev/null 2>&1; then
      python3 -c 'import os, sys; os.getxattr(sys.argv[1], sys.argv[2])' "$kwin" "$marker" 2>/dev/null || return 0
    else
      return 0
    fi
    if setcap cap_sys_nice=ep "$kwin"; then
      setfattr -x "$marker" "$kwin" 2>/dev/null || \
        python3 -c 'import os, sys; os.removexattr(sys.argv[1], sys.argv[2])' "$kwin" "$marker" 2>/dev/null || true
      echo "restored cap_sys_nice on $kwin (removed by an earlier ArtLight build)"
    else
      echo "warning: could not restore cap_sys_nice on $kwin; reinstall the kwin package." >&2
    fi
  }
  artlight_restore_kwin_capability || true

  if %{_prefix}/libexec/vibeshine/vibeshine-drm-install install; then
    :
  else
    vibeshine_drm_rc=$?
    if [ "$vibeshine_drm_rc" -eq 4 ]; then
      echo "warning: ArtLight DRM was updated, but the loaded module is stale; reboot before using managed virtual displays."
    else
      echo "warning: ArtLight DRM installation failed; managed virtual displays are unavailable."
    fi
  fi
  if ! %{_prefix}/libexec/vibeshine/vibeshine-ds5-install install; then
    echo "warning: DualSense USB haptics are unavailable or require a reboot; see the module error above." >&2
  fi
  artlight_machine_helper=%{_prefix}/libexec/vibeshine/artlight-machine-host
  artlight_privileged_helper_is_safe "$artlight_machine_helper" || {
    echo "error: installed ArtLight machine helper is unsafe." >&2
    exit 1
  }
  "$artlight_machine_helper" remove-pam || \
    echo "warning: could not remove the obsolete Plasma Login Manager handoff hook."
  systemctl disable --now artlight-prelogin.service 2>/dev/null || true
  systemctl daemon-reload || exit 1
  if "$artlight_machine_helper" configure-auto; then
    if ! systemctl enable artlight-session-controller.service; then
      artlight_quiesce_machine_host || true
      echo "error: could not enable the ArtLight controller safely; all machine-host units remain off." >&2
      exit 1
    fi
    if ! artlight_unmask_host_for_controller || \
       ! systemctl start artlight-session-controller.service; then
      timeout --signal=KILL 15 systemctl mask --runtime artlight.service 2>/dev/null || true
      artlight_quiesce_machine_host || true
      echo "error: could not start the ArtLight controller safely; all machine-host units remain off." >&2
      exit 1
    fi
  else
    echo "==> ACTION REQUIRED: ArtLight could not prepare the machine profile; review the preceding setup or migration error." >&2
    echo "    Run:  sudo artlight configure USER" >&2
    echo "    then: sudo systemctl enable --now artlight-session-controller.service" >&2
  fi
else
  echo "rpm-ostree environment detected, skipping post install steps. Restart to apply the changes."
fi

%preun
artlight_controller=%{_prefix}/libexec/vibeshine/artlight-session-controller
artlight_machine_host=%{_prefix}/libexec/vibeshine/artlight-machine-host
artlight_session_record=/run/artlight/session.env
artlight_legacy_acl=/run/artlight/runtime-acl
artlight_broker_socket=/run/artlight/session-broker.sock
artlight_preun_privileged_helper_is_safe() {
  [ -f "$1" ] && [ ! -L "$1" ] && [ -x "$1" ] || return 1
  [ "$(stat -c '%%u:%%g:%%a:%%h:%%F' -- "$1")" = \
    '0:0:755:1:regular file' ]
}
artlight_preun_cgroup_is_quiescent() {
  artlight_control_group=$1
  [ -n "$artlight_control_group" ] || return 0
  case "$artlight_control_group" in /*) ;; *) return 1 ;; esac
  case "$artlight_control_group" in */../* | */..) return 1 ;; esac
  artlight_cgroup_path=/sys/fs/cgroup$artlight_control_group
  if [ ! -e "$artlight_cgroup_path" ] && [ ! -L "$artlight_cgroup_path" ]; then return 0; fi
  [ -d "$artlight_cgroup_path" ] && [ ! -L "$artlight_cgroup_path" ] && \
    [ -f "$artlight_cgroup_path/cgroup.events" ] && \
    [ ! -L "$artlight_cgroup_path/cgroup.events" ] && \
    grep -qx 'populated 0' "$artlight_cgroup_path/cgroup.events"
}
artlight_preun_unit_is_quiescent() {
  artlight_properties=$(timeout --signal=KILL 5 systemctl show "$1" \
    --property=LoadState --property=ActiveState --property=SubState --property=MainPID \
    --property=ControlGroup 2>/dev/null) || return 1
  artlight_property_lines=$(printf '%%s\n' "$artlight_properties" | wc -l | tr -d ' ')
  case "$1:$artlight_property_lines" in
    *.socket:4 | *.socket:5 | *:5) ;;
    *) return 1 ;;
  esac
  for artlight_property in LoadState ActiveState SubState ControlGroup; do
    artlight_property_count=$(printf '%%s\n' "$artlight_properties" | \
      grep -c "^$artlight_property=" || true)
    [ "$artlight_property_count" = 1 ] || return 1
  done
  artlight_property_count=$(printf '%%s\n' "$artlight_properties" | grep -c '^MainPID=' || true)
  case "$1:$artlight_property_count" in
    *.socket:0 | *.socket:1 | *:1) ;;
    *) return 1 ;;
  esac
  artlight_load=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^LoadState=//p')
  artlight_state=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^ActiveState=//p')
  artlight_substate=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^SubState=//p')
  artlight_pid=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^MainPID=//p')
  case "$1" in *.socket) [ -n "$artlight_pid" ] || artlight_pid=0 ;; esac
  artlight_control_group=$(printf '%%s\n' "$artlight_properties" | sed -n 's/^ControlGroup=//p')
  case "$artlight_load:$artlight_state:$artlight_substate" in
    not-found:inactive:dead | \
    loaded:inactive:dead | loaded:failed:failed | \
    masked:inactive:dead | masked:failed:failed | \
    masked-runtime:inactive:dead | masked-runtime:failed:failed) ;;
    *) return 1 ;;
  esac
  [ "$artlight_pid" = 0 ] && artlight_preun_cgroup_is_quiescent "$artlight_control_group"
}
artlight_preun_unit_is_disabled() {
  artlight_enabled=$(timeout --signal=KILL 5 systemctl is-enabled "$1" 2>/dev/null || true)
  case "$artlight_enabled" in
    disabled | masked | masked-runtime | static | indirect | generated | transient | linked | linked-runtime | not-found) return 0 ;;
    *) return 1 ;;
  esac
}
artlight_preun_unit_is_masked() {
  artlight_enabled=$(timeout --signal=KILL 5 systemctl is-enabled "$1" 2>/dev/null || true)
  artlight_load=$(timeout --signal=KILL 5 systemctl show "$1" --property=LoadState 2>/dev/null) || return 1
  case "$artlight_enabled" in
    masked | masked-runtime) [ "$artlight_load" = 'LoadState=masked' ] ;;
    *) return 1 ;;
  esac
}
artlight_preun_broker_unit_is_safe() {
  artlight_broker_unit=$1
  case "$artlight_broker_unit" in artlight-session-exec@*.service) ;; *) return 1 ;; esac
  artlight_broker_instance=${artlight_broker_unit#artlight-session-exec@}
  artlight_broker_instance=${artlight_broker_instance%.service}
  [ -n "$artlight_broker_instance" ] || return 1
  case "$artlight_broker_instance" in *[!A-Za-z0-9_.:-]*) return 1 ;; esac
}
artlight_preun_stop_exact_unit() {
  # Never bypass a service's ordered resource teardown.  Killing systemctl
  # only abandons the client while its manager job continues; killing the unit
  # cgroup can strand live GPU imports and is therefore forbidden here.
  timeout --signal=TERM --kill-after=2 60 systemctl stop "$1" 2>/dev/null
}
artlight_preun_bounded_broker_list() (
  artlight_broker_list=$(mktemp /run/artlight-broker-units.XXXXXX) || exit 1
  trap 'rm -f -- "$artlight_broker_list"' 0
  trap 'exit 1' HUP INT TERM
  chmod 0600 "$artlight_broker_list" || return 1
  (
    ulimit -f 128 || exit 1
    timeout --signal=KILL 5 systemctl list-units --all --plain \
      --no-legend --no-pager --full 'artlight-session-exec@*.service' \
      >"$artlight_broker_list" 2>/dev/null
  ) || return 1
  artlight_broker_list_size=$(stat -c '%%s' -- "$artlight_broker_list") || return 1
  case "$artlight_broker_list_size" in '' | *[!0-9]*) return 1 ;; esac
  [ "$artlight_broker_list_size" -le 65536 ] || return 1
  cat -- "$artlight_broker_list"
)
artlight_preun_stop_brokers() (
  artlight_broker_attempt=0
  artlight_broker_clean_passes=0
  while [ "$artlight_broker_attempt" -lt 20 ]; do
    artlight_broker_dirty=0
    artlight_brokers=$(artlight_preun_bounded_broker_list) || return 1
    artlight_seen_units='
'
    while IFS= read -r artlight_line || [ -n "$artlight_line" ]; do
      [ -n "$artlight_line" ] || continue
      case "$artlight_line" in *"
"*) return 1 ;; esac
      set -f
      set -- $artlight_line
      [ "${1:-}" = '●' ] && shift
      [ "$#" -ge 4 ] || return 1
      artlight_unit=$1; artlight_load=$2
      artlight_state=$3; artlight_substate=$4
      artlight_preun_broker_unit_is_safe "$artlight_unit" || return 1
      [ "$artlight_load" = loaded ] || return 1
      case "$artlight_state:$artlight_substate" in *[!a-z:-]*) return 1 ;; esac
      case "$artlight_seen_units" in *"
$artlight_unit
"*) return 1 ;; esac
      artlight_seen_units="$artlight_seen_units$artlight_unit
"
      if ! artlight_preun_unit_is_quiescent "$artlight_unit"; then
        artlight_broker_dirty=1
        artlight_preun_stop_exact_unit "$artlight_unit"
        artlight_preun_unit_is_quiescent "$artlight_unit" || return 1
      fi
    done <<EOF
$artlight_brokers
EOF
    if [ "$artlight_broker_dirty" -eq 0 ]; then
      artlight_broker_clean_passes=$((artlight_broker_clean_passes + 1))
      [ "$artlight_broker_clean_passes" -lt 5 ] || return 0
    else
      artlight_broker_clean_passes=0
    fi
    artlight_broker_attempt=$((artlight_broker_attempt + 1))
    sleep 0.1
  done
  return 1
)
artlight_preun_remove_pam() {
  if [ ! -e "$artlight_machine_host" ] && [ ! -L "$artlight_machine_host" ]; then return 0; fi
  artlight_preun_privileged_helper_is_safe "$artlight_machine_host" || return 1
  timeout --signal=KILL 40 "$artlight_machine_host" remove-pam >/dev/null 2>&1
}
artlight_preun_quiesce() {
  artlight_have_systemd=0
  if command -v systemctl >/dev/null 2>&1 && [ -d /run/systemd/system ]; then
    artlight_have_systemd=1
    timeout --signal=KILL 15 systemctl mask --runtime artlight.service \
      artlight-session-exec.socket 2>/dev/null || return 1
    artlight_preun_unit_is_masked artlight.service || return 1
    artlight_preun_unit_is_masked artlight-session-exec.socket || return 1
    artlight_preun_stop_exact_unit artlight-session-exec.socket
    artlight_preun_unit_is_quiescent artlight-session-exec.socket || return 1
    artlight_preun_stop_exact_unit artlight.service
    artlight_preun_unit_is_quiescent artlight.service || return 1
    artlight_preun_stop_brokers || return 1
    for artlight_unit in artlight-session-controller.service artlight.service \
      artlight-prelogin.service artlight-machine-prepare.service; do
      artlight_preun_stop_exact_unit "$artlight_unit"
    done
    timeout --signal=KILL 15 systemctl disable artlight-session-controller.service --now 2>/dev/null || true
    timeout --signal=KILL 15 systemctl disable artlight.service --now 2>/dev/null || true
    timeout --signal=KILL 15 systemctl disable artlight-prelogin.service --now 2>/dev/null || true
    timeout --signal=KILL 15 systemctl disable artlight-machine-prepare.service --now 2>/dev/null || true
  fi
  if [ -e "$artlight_controller" ] || [ -L "$artlight_controller" ]; then
    artlight_preun_privileged_helper_is_safe "$artlight_controller" || return 1
    [ "$artlight_have_systemd" -eq 1 ] || return 1
    timeout --signal=KILL 40 "$artlight_controller" cleanup || return 1
  fi
  artlight_preun_remove_pam || return 1
  if [ "$artlight_have_systemd" -eq 1 ]; then
    artlight_preun_unit_is_masked artlight.service || return 1
    artlight_preun_unit_is_masked artlight-session-exec.socket || return 1
    artlight_preun_stop_exact_unit artlight-session-exec.socket
    artlight_preun_stop_exact_unit artlight.service
    artlight_preun_unit_is_quiescent artlight.service || return 1
    artlight_preun_stop_brokers || return 1
    for artlight_unit in artlight-session-exec.socket artlight-session-controller.service \
      artlight.service artlight-prelogin.service artlight-machine-prepare.service; do
      artlight_preun_stop_exact_unit "$artlight_unit"
      artlight_preun_unit_is_quiescent "$artlight_unit" || return 1
      artlight_preun_unit_is_disabled "$artlight_unit" || return 1
    done
    artlight_preun_stop_brokers || return 1
  fi
  if [ -S "$artlight_broker_socket" ] && [ ! -L "$artlight_broker_socket" ]; then
    rm -f -- "$artlight_broker_socket" || return 1
  fi
  [ ! -e "$artlight_broker_socket" ] && [ ! -L "$artlight_broker_socket" ] && \
    [ ! -e "$artlight_session_record" ] && [ ! -L "$artlight_session_record" ] && \
    [ ! -e "$artlight_legacy_acl" ] && [ ! -L "$artlight_legacy_acl" ]
}
if [ "$1" -eq 0 ]; then
  artlight_preun_quiesce || {
    echo "error: refusing to uninstall while ArtLight cgroups or session state remain." >&2
    exit 1
  }
  timeout --signal=KILL 30 systemctl stop vibeshine-vkms.service 2>/dev/null || true
  timeout --signal=KILL 30 systemctl stop vibeshine-drm-setup.service 2>/dev/null || true
  %{_prefix}/libexec/vibeshine/vibeshine-ds5-install remove || \
    echo "warning: could not remove the DualSense USB module cleanly."
  %{_prefix}/libexec/vibeshine/vibeshine-drm-install remove || \
    echo "warning: could not remove the ArtLight HDR DRM module cleanly."
fi

%files
# Executables
%attr(0755,root,root) %{_prefix}/libexec/vibeshine/artlight-display-power
%{_bindir}/artlight
%{_bindir}/artlight-mangohud
%{_prefix}/libexec/vibeshine/vibeshine-drm-install
%{_prefix}/libexec/vibeshine/vibeshine-ds5-install
%{_prefix}/libexec/vibeshine/vibeshine-vkms
%{_prefix}/libexec/vibeshine/vibeshine-vkms-quiesce
%{_prefix}/libexec/vibeshine/vibeshine-vkms-peercred
%{_prefix}/libexec/vibeshine/artlight-session-controller
%attr(0755,root,root) %{_prefix}/libexec/vibeshine/artlight-session-exec
%attr(0700,root,root) %caps(cap_kill,cap_setgid,cap_setuid+p) %{_prefix}/libexec/vibeshine/artlight-session-broker
%{_prefix}/libexec/vibeshine/artlight-provider-scan
%attr(0755,root,root) %{_prefix}/libexec/vibeshine/artlight-global-limiter.py
%attr(0755,root,root) %{_prefix}/libexec/vibeshine/artlight-steam-launch
%{_prefix}/libexec/vibeshine/artlight-profile-import
%attr(0755,root,root) %{_prefix}/libexec/vibeshine/artlight-app-supervisor
%{_prefix}/libexec/vibeshine/artlight-machine-host
%attr(0755,root,root) %{_prefix}/libexec/vibeshine/artlight-kwin-session-environment
# 0755 root:root and NOT setuid, with no file capabilities: not privileged by what it is, only by
# what pkexec grants it for the length of a single call.
%attr(0755,root,root) %{_prefix}/libexec/vibeshine/artlight-input-service
%attr(0750,root,artlight) %caps(cap_sys_admin,cap_sys_nice+p) %{_prefix}/libexec/vibeshine/artlight-host
%attr(4755,root,root) %{_libdir}/libvibeshine-kwin-gpu.so

# Dedicated access group for the privileged virtual-display control socket
%{_prefix}/lib/sysusers.d/vibeshine-vkms.conf
%{_prefix}/lib/sysusers.d/artlight.conf

# Versioned DKMS/direct-build source tree
/usr/src/vibeshine-drm-*
/usr/src/vibeshine-ds5-*

# KWin user-unit drop-ins; Linux does not install the generic app service.
%{_userunitdir}/plasma-kwin_wayland.service.d/vibeshine-kwin-gpu.conf
%{_userunitdir}/plasma-kwin_wayland.service.d/artlight-kwin-session-environment.conf
%{_userunitdir}/plasma-login-kwin_wayland.service.d/vibeshine-kwin-gpu.conf
%{_userunitdir}/plasma-login-kwin_wayland.service.d/artlight-kwin-session-environment.conf

# Privileged virtual-display provisioning service
%{_unitdir}/vibeshine-drm-setup.service
%{_unitdir}/vibeshine-vkms-control.socket
%{_unitdir}/vibeshine-vkms-control@.service
%{_unitdir}/vibeshine-vkms.service
%{_unitdir}/artlight-session-exec.socket
%{_unitdir}/artlight-session-exec@.service
%{_unitdir}/artlight-input-service.socket
%{_unitdir}/artlight-input-service@.service
%{_unitdir}/system-artlight-input-service.slice
%{_unitdir}/artlight-session-controller.service
%{_unitdir}/artlight.service

# Udev rules
%{_udevrulesdir}/*-sunshine.rules
%{_udevrulesdir}/70-artlight-uinput.rules

# Native firewall profiles and PipeWire capture defaults
%{_prefix}/lib/firewalld/services/artlight.xml
%{_sysconfdir}/ufw/applications.d/artlight
%{_datadir}/pipewire/pipewire.conf.d/50-artlight-audio.conf

# The USB input service is reached over a socket systemd serves, NOT through pkexec. The polkit
# action that used to authorise that call is deliberately gone: it could never have worked for this
# caller (the server is the privileged machine host and refuses setuid transitions for itself and
# everything it spawns) and it was a standing grant of root execution to the local active session
# while nothing used it.

# Modules-load configuration
%{_modulesloaddir}/*-sunshine.conf
%{_modulesloaddir}/70-vibeshine-ds5.conf
%{_modulesloaddir}/70-artlight-usbip.conf

# Desktop entries
%{_datadir}/applications/*.desktop

# Icons
%{_datadir}/icons/hicolor/scalable/apps/apollo.svg
%{_datadir}/icons/hicolor/scalable/status/apollo*.svg

# Metainfo
%{_datadir}/metainfo/*.metainfo.xml

# Assets
%{_datadir}/artlight/**

%changelog
