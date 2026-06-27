#!/usr/bin/env bash
# build.sh — Build solakon-one-ui inside a Docker container.
#
# Usage:
#   ./docker/build.sh [--distro <alias>|all] [--build-type Release|Debug]
#
# Available distro aliases:
#   opensuse-tumbleweed-x86_64   openSUSE Tumbleweed, x86_64, Qt6
#   ubuntu-24.04-x86_64          Ubuntu 24.04,         x86_64, Qt6
#   all                          Build all of the above
#
# Build type (default: Release):
#   Release   Optimized, no debug output.
#   Debug     Debug symbols + qDebug() output enabled.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
APP_NAME="solakon-one-ui"

DISTRO="all"
BUILD_TYPE="Release"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --distro)       DISTRO="$2";     shift 2 ;;
        --distro=*)     DISTRO="${1#*=}"; shift   ;;
        --build-type)   BUILD_TYPE="$2"; shift 2 ;;
        --build-type=*) BUILD_TYPE="${1#*=}"; shift ;;
        -h|--help)
            sed -n '/^# Usage:/,/^[^#]/{ /^[^#]/d; s/^# \{0,2\}//; p }' "$0"
            exit 0 ;;
        *)
            echo "ERROR: Unknown argument: $1" >&2; exit 1 ;;
    esac
done

case "$DISTRO" in
    opensuse-tumbleweed-x86_64|ubuntu-24.04-x86_64|all) ;;
    *)
        echo "ERROR: --distro must be one of: opensuse-tumbleweed-x86_64, ubuntu-24.04-x86_64, all" >&2
        exit 1 ;;
esac

BUILD_TYPE_LOWER="${BUILD_TYPE,,}"
case "$BUILD_TYPE_LOWER" in
    release) BUILD_TYPE="Release" ;;
    debug)   BUILD_TYPE="Debug"   ;;
    *)
        echo "ERROR: --build-type must be Release or Debug (got: $BUILD_TYPE)" >&2
        exit 1 ;;
esac

dockerfile_for_distro() {
    case "$1" in
        opensuse-tumbleweed-x86_64) echo "tumbleweed"  ;;
        ubuntu-24.04-x86_64)        echo "ubuntu24.04" ;;
    esac
}

family_for_distro() {
    case "$1" in
        ubuntu-*) echo "ubuntu"   ;;
        *)        echo "opensuse" ;;
    esac
}

dir_name_for_distro() {
    case "$1" in
        opensuse-tumbleweed-x86_64) echo "tumbleweed" ;;
        ubuntu-24.04-x86_64)        echo "24.04"      ;;
    esac
}

arch_for_distro() {
    case "$1" in
        *) echo "x86_64" ;;
    esac
}

deb_arch_for_arch() {
    case "$1" in
        x86_64) echo "amd64" ;;
        *)      echo "$1"    ;;
    esac
}

build_for_distro() {
    local distro="$1"
    local dockerfile_suffix
    dockerfile_suffix="$(dockerfile_for_distro "${distro}")"
    local image="${APP_NAME}-builder-${dockerfile_suffix}"
    local dockerfile="${SCRIPT_DIR}/Dockerfile.${dockerfile_suffix}"

    local version
    version="$(grep -A3 "^project(${APP_NAME}" "${PROJECT_ROOT}/CMakeLists.txt" \
                | grep -oP 'VERSION\s+\K\d+\.\d+\.\d+')"
    local release="1"

    local arch; arch="$(arch_for_distro "${distro}")"
    local deb_arch; deb_arch="$(deb_arch_for_arch "${arch}")"
    local family; family="$(family_for_distro "${distro}")"
    local dir_name; dir_name="$(dir_name_for_distro "${distro}")"

    local dir_arch
    case "${family}" in
        ubuntu) dir_arch="${deb_arch}" ;;
        *)      dir_arch="${arch}"     ;;
    esac

    local pkg_type="RPM"
    case "${distro}" in
        ubuntu-*) pkg_type="DEB" ;;
    esac

    local build_dir="${PROJECT_ROOT}/build/${family}/${dir_name}/${dir_arch}"
    local out_dir="${PROJECT_ROOT}/out/${family}/${dir_name}/${dir_arch}"

    echo ""
    echo "════════════════════════════════════════════════════════════"
    echo "  Building for: ${distro}  (${version}  ${arch})"
    echo "════════════════════════════════════════════════════════════"

    echo ""
    echo "[1/4] Building Docker image '${image}' ..."
    docker build \
        --pull \
        --file "${dockerfile}" \
        --tag  "${image}" \
        "${PROJECT_ROOT}"
    echo "      Docker image ready."

    mkdir -p "${build_dir}" "${out_dir}"

    echo ""
    echo "[2/4] Running cmake + ninja + cpack inside container ..."
    docker run --rm \
        --user "$(id -u):$(id -g)" \
        -v "${PROJECT_ROOT}:/src:ro" \
        -v "${build_dir}:/build" \
        -v "/etc/passwd:/etc/passwd:ro" \
        -v "/etc/group:/etc/group:ro" \
        "${image}" \
        bash -c "
            set -euo pipefail
            rm -f /build/CMakeCache.txt
            cmake /src -B /build -G Ninja \
                -DCMAKE_BUILD_TYPE=${BUILD_TYPE} \
                -DCPACK_GENERATOR=${pkg_type}
            ninja -C /build
            cd /build && cpack --config CPackConfig.cmake
        "
    echo "      Build + package succeeded."

    echo ""
    echo "[3/4] Copying binary ..."
    if [[ ! -f "${build_dir}/${APP_NAME}" ]]; then
        echo "ERROR: Expected binary '${build_dir}/${APP_NAME}' not found." >&2
        exit 1
    fi
    cp "${build_dir}/${APP_NAME}" "${out_dir}/${APP_NAME}"
    echo "      Binary : ${out_dir}/${APP_NAME}"

    echo ""
    echo "[4/4] Copying ${pkg_type} package ..."
    local pkg_file=""
    case "${pkg_type}" in
        RPM) pkg_file="$(find "${build_dir}" -maxdepth 1 -name "${APP_NAME}-*.rpm" | sort | tail -1)" ;;
        DEB) pkg_file="$(find "${build_dir}" -maxdepth 1 -name "${APP_NAME}-*.deb" | sort | tail -1)" ;;
    esac

    if [[ -z "${pkg_file}" ]]; then
        echo "WARNING: No ${pkg_type} file found in '${build_dir}'." >&2
    else
        local pkg_out=""
        case "${pkg_type}" in
            RPM) pkg_out="${out_dir}/${APP_NAME}-${version}-${release}.${arch}.rpm" ;;
            DEB) pkg_out="${out_dir}/${APP_NAME}_${version}-${release}_${deb_arch}.deb" ;;
        esac
        cp "${pkg_file}" "${pkg_out}"
        echo "      Package: ${pkg_out}"
    fi
}

if [[ "$DISTRO" == "all" ]]; then
    build_for_distro "opensuse-tumbleweed-x86_64"
    build_for_distro "ubuntu-24.04-x86_64"
else
    build_for_distro "$DISTRO"
fi

echo ""
echo "Done."
