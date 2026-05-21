#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 3 ]]; then
  echo "Usage: $0 <stage-dir> <artifact-name> <dist-dir>" >&2
  exit 2
fi

stage_dir=$1
artifact_name=$2
dist_dir=$3

stage_dir=$(cd "${stage_dir}" && pwd)
mkdir -p "${dist_dir}"
dist_dir=$(cd "${dist_dir}" && pwd)

bin_dir="${stage_dir}/bin"
lib_dir="${stage_dir}/lib"

if [[ ! -x "${bin_dir}/skewer-render" ]]; then
  echo "Missing ${bin_dir}/skewer-render" >&2
  exit 1
fi

if [[ ! -x "${bin_dir}/loom" ]]; then
  echo "Missing ${bin_dir}/loom" >&2
  exit 1
fi

mkdir -p "${lib_dir}" "${dist_dir}"

cat > "${stage_dir}/README.txt" <<EOF
Skewer portable command-line bundle

Run from this extracted directory:

  ./bin/skewer-render --help
  ./bin/loom --help

The lib directory contains the runtime libraries needed by these binaries.
EOF

is_macos_system_lib() {
  case "$1" in
    @*|/usr/lib/*|/System/Library/*)
      return 0
      ;;
    *)
      return 1
      ;;
  esac
}

is_linux_system_lib() {
  local dep=$1
  local base
  base=$(basename "${dep}")

  case "${base}" in
    ld-linux*.so.*|libc.so.*|libm.so.*|libpthread.so.*|libdl.so.*|librt.so.*|libresolv.so.*|libutil.so.*|libnsl.so.*|libanl.so.*)
      return 0
      ;;
  esac

  return 1
}

collect_macos_deps_for() {
  local file=$1
  otool -L "${file}" | tail -n +2 | awk '{print $1}' | while IFS= read -r dep; do
    [[ -z "${dep}" ]] && continue
    is_macos_system_lib "${dep}" && continue
    [[ -f "${dep}" ]] && printf '%s\n' "${dep}"
  done
}

collect_linux_deps_for() {
  local file=$1
  if ldd "${file}" | grep -q "not found"; then
    ldd "${file}" >&2
    exit 1
  fi

  ldd "${file}" | awk '
    /=>/ && $(NF - 1) ~ /^\// { print $(NF - 1) }
    /^[[:space:]]*\// { print $1 }
  ' | while IFS= read -r dep; do
    [[ -z "${dep}" ]] && continue
    is_linux_system_lib "${dep}" && continue
    [[ -f "${dep}" ]] && printf '%s\n' "${dep}"
  done
}

bundle_macos() {
  local queue=()
  local seen_file
  seen_file=$(mktemp)

  enqueue_dep() {
    local dep=$1
    if ! grep -Fxq "${dep}" "${seen_file}"; then
      printf '%s\n' "${dep}" >> "${seen_file}"
      queue+=("${dep}")
    fi
  }

  while IFS= read -r dep; do enqueue_dep "${dep}"; done < <(collect_macos_deps_for "${bin_dir}/skewer-render")
  while IFS= read -r dep; do enqueue_dep "${dep}"; done < <(collect_macos_deps_for "${bin_dir}/loom")

  local idx=0
  while [[ ${idx} -lt ${#queue[@]} ]]; do
    local dep=${queue[${idx}]}
    idx=$((idx + 1))

    local dst="${lib_dir}/$(basename "${dep}")"
    if [[ ! -f "${dst}" ]]; then
      cp -p "${dep}" "${dst}"
      chmod u+w "${dst}"
    fi

    while IFS= read -r child; do enqueue_dep "${child}"; done < <(collect_macos_deps_for "${dst}")
  done

  patch_macos_target() {
    local target=$1
    local prefix=$2

    while IFS= read -r dep; do
      [[ -z "${dep}" ]] && continue
      is_macos_system_lib "${dep}" && continue
      [[ -f "${lib_dir}/$(basename "${dep}")" ]] || continue
      install_name_tool -change "${dep}" "${prefix}/$(basename "${dep}")" "${target}"
    done < <(otool -L "${target}" | tail -n +2 | awk '{print $1}')
  }

  for dylib in "${lib_dir}"/*.dylib; do
    [[ -e "${dylib}" ]] || continue
    install_name_tool -id "@rpath/$(basename "${dylib}")" "${dylib}"
    patch_macos_target "${dylib}" "@loader_path"
  done

  patch_macos_target "${bin_dir}/skewer-render" "@executable_path/../lib"
  patch_macos_target "${bin_dir}/loom" "@executable_path/../lib"

  if command -v codesign >/dev/null 2>&1; then
    for target in "${lib_dir}"/*.dylib "${bin_dir}/skewer-render" "${bin_dir}/loom"; do
      [[ -e "${target}" ]] || continue
      codesign --force --sign - "${target}" >/dev/null 2>&1 || true
    done
  fi

  for target in "${bin_dir}/skewer-render" "${bin_dir}/loom" "${lib_dir}"/*.dylib; do
    [[ -e "${target}" ]] || continue
    if otool -L "${target}" | tail -n +2 | awk '{print $1}' | grep -Ev '^(@|/usr/lib/|/System/Library/)' >/dev/null; then
      echo "Found unbundled macOS dependency in ${target}:" >&2
      otool -L "${target}" >&2
      exit 1
    fi
  done
}

bundle_linux() {
  if ! command -v patchelf >/dev/null 2>&1; then
    echo "patchelf is required for Linux release bundles" >&2
    exit 1
  fi

  local queue=()
  local seen_file
  seen_file=$(mktemp)

  enqueue_dep() {
    local dep=$1
    if ! grep -Fxq "${dep}" "${seen_file}"; then
      printf '%s\n' "${dep}" >> "${seen_file}"
      queue+=("${dep}")
    fi
  }

  while IFS= read -r dep; do enqueue_dep "${dep}"; done < <(collect_linux_deps_for "${bin_dir}/skewer-render")
  while IFS= read -r dep; do enqueue_dep "${dep}"; done < <(collect_linux_deps_for "${bin_dir}/loom")

  local idx=0
  while [[ ${idx} -lt ${#queue[@]} ]]; do
    local dep=${queue[${idx}]}
    idx=$((idx + 1))

    local dst="${lib_dir}/$(basename "${dep}")"
    if [[ ! -f "${dst}" ]]; then
      cp -L "${dep}" "${dst}"
      chmod u+w "${dst}"
    fi

    while IFS= read -r child; do enqueue_dep "${child}"; done < <(collect_linux_deps_for "${dst}")
  done

  patchelf --set-rpath '$ORIGIN/../lib' "${bin_dir}/skewer-render"
  patchelf --set-rpath '$ORIGIN/../lib' "${bin_dir}/loom"

  for so in "${lib_dir}"/*.so*; do
    [[ -e "${so}" ]] || continue
    patchelf --set-rpath '$ORIGIN' "${so}" || true
  done
}

case "$(uname -s)" in
  Darwin)
    bundle_macos
    ;;
  Linux)
    bundle_linux
    ;;
  *)
    echo "Unsupported platform for $0: $(uname -s)" >&2
    exit 1
    ;;
esac

"${bin_dir}/skewer-render" --help >/dev/null 2>&1
"${bin_dir}/loom" --help >/dev/null 2>&1

archive="${dist_dir}/${artifact_name}.tar.gz"
rm -f "${archive}"
(
  cd "$(dirname "${stage_dir}")"
  tar -czf "${archive}" "$(basename "${stage_dir}")"
)

if command -v shasum >/dev/null 2>&1; then
  shasum -a 256 "${archive}" > "${archive}.sha256"
else
  sha256sum "${archive}" > "${archive}.sha256"
fi
echo "Created ${archive}"
