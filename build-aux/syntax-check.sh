#!/usr/bin/env bash
# Local syntax check for macOS hosts without a full Xcode installation.
#
# The plugin itself is built by CI (Windows is the production platform), but a full CI round trip
# is a slow way to find a typo. This compiles every source with -fsyntax-only against the pinned
# OBS headers and Qt frameworks, which needs neither Xcode nor a libobs build.
#
# Usage: build-aux/syntax-check.sh

set -euo pipefail

project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_dir="${project_root}/.deps-syntax"

obs_version="$(python3 -c 'import json;print(json.load(open("'"${project_root}"'/buildspec.json"))["dependencies"]["obs-studio"]["version"])')"
qt_version="$(python3 -c 'import json;print(json.load(open("'"${project_root}"'/buildspec.json"))["dependencies"]["qt6"]["version"])')"

obs_src="${deps_dir}/obs-studio-${obs_version}"
qt_dir="${deps_dir}/qt6-${qt_version}"

mkdir -p "${deps_dir}"

if [[ ! -d "${obs_src}" ]]; then
    echo "==> fetching OBS ${obs_version} headers"
    curl -fsSL "https://github.com/obsproject/obs-studio/archive/refs/tags/${obs_version}.tar.gz" |
        tar -xz -C "${deps_dir}"
fi

if [[ ! -d "${qt_dir}" ]]; then
    echo "==> fetching Qt6 (obs-deps ${qt_version})"
    mkdir -p "${qt_dir}"
    curl -fsSL "https://github.com/obsproject/obs-deps/releases/download/${qt_version}/macos-deps-qt6-${qt_version}-universal.tar.xz" |
        tar -xJ -C "${qt_dir}"
fi

qt_lib="${qt_dir}/lib"

includes=(
    -I "${project_root}/src"
    -I "${obs_src}/libobs"
    -I "${obs_src}/UI/obs-frontend-api"
    -F "${qt_lib}"
    -I "${qt_lib}/QtCore.framework/Headers"
    -I "${qt_lib}/QtGui.framework/Headers"
    -I "${qt_lib}/QtWidgets.framework/Headers"
)

status=0
while IFS= read -r source; do
    echo "==> ${source#"${project_root}/"}"
    if ! clang++ -fsyntax-only -std=c++17 -Wall -Wextra "${includes[@]}" "${source}"; then
        status=1
    fi
done < <(find "${project_root}/src" -name '*.cpp' | sort)

if [[ ${status} -eq 0 ]]; then
    echo "==> syntax check passed"
fi
exit ${status}
