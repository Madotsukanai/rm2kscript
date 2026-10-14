#!/usr/bin/env bash
# scripts/build-win64.sh — Windows 向け x86_64 スタティックビルドスクリプト
#
# 必要なツール (Arch Linux):
#   sudo pacman -S mingw-w64-gcc mingw-w64-cmake ninja
#
# 使い方:
#   bash scripts/build-win64.sh            # 依存ビルド + rm2kscript.exe 生成
#   bash scripts/build-win64.sh --deps-only # 依存ビルドのみ (CI キャッシュ用)
#
# 成果物:
#   dist/rm2kscript.exe

set -euo pipefail

# ── 設定 ─────────────────────────────────────────────────────────────────────

MINGW=x86_64-w64-mingw32
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
WORK_DIR="${WORK_DIR:-$REPO_DIR/build-deps}"
PREFIX="${PREFIX:-$REPO_DIR/win-sysroot}"
DIST_DIR="$REPO_DIR/dist"
JOBS="${JOBS:-$(nproc)}"

EXPAT_VERSION="2.9.0"
INIH_VERSION="r62"
ICU_VERSION="78.3"         # Major.Minor
ICU_TAG="release-78-3"    # GitHub tag 形式
LIBLCF_REF="master"        # liblcf ブランチ/タグ

DEPS_ONLY=false
if [[ "${1:-}" == "--deps-only" ]]; then
    DEPS_ONLY=true
fi

# ── ユーティリティ ────────────────────────────────────────────────────────────

info()  { echo "[build-win64] $*"; }
error() { echo "[build-win64] ERROR: $*" >&2; exit 1; }

require_cmd() {
    for cmd in "$@"; do
        command -v "$cmd" >/dev/null 2>&1 || \
            error "'$cmd' が見つかりません。インストールしてください。"
    done
}

# ── 環境チェック ──────────────────────────────────────────────────────────────

require_cmd "${MINGW}-g++" "${MINGW}-gcc" "${MINGW}-ar" cmake ninja git curl tar

mkdir -p "$WORK_DIR" "$PREFIX"

# ── expat ────────────────────────────────────────────────────────────────────

build_expat() {
    local src="$WORK_DIR/expat-${EXPAT_VERSION}"
    local build="$WORK_DIR/build-expat"
    [[ -f "$PREFIX/lib/cmake/expat-${EXPAT_VERSION}/expat-config.cmake" ]] && \
        { info "expat: キャッシュ済み"; return; }
    info "expat ${EXPAT_VERSION} をビルド中..."
    if [[ ! -d "$src" ]]; then
        curl -fsSL "https://github.com/libexpat/libexpat/releases/download/R_${EXPAT_VERSION//./_}/expat-${EXPAT_VERSION}.tar.xz" \
            | tar xJ -C "$WORK_DIR"
    fi
    rm -rf "$build" && mkdir "$build"
    cmake "$src" -B "$build" \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_C_COMPILER="${MINGW}-gcc" \
        -DCMAKE_CXX_COMPILER="${MINGW}-g++" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_TOOLS=OFF \
        -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_DOCS=OFF \
        -DCMAKE_BUILD_TYPE=Release -G Ninja
    ninja -C "$build" -j"$JOBS"
    ninja -C "$build" install
    info "expat: 完了"
}

# ── inih ─────────────────────────────────────────────────────────────────────

build_inih() {
    [[ -f "$PREFIX/lib/libinih.a" ]] && { info "inih: キャッシュ済み"; return; }
    info "inih ${INIH_VERSION} をビルド中..."
    local src="$WORK_DIR/inih-${INIH_VERSION}"
    if [[ ! -d "$src" ]]; then
        curl -fsSL "https://github.com/benhoyt/inih/archive/refs/tags/${INIH_VERSION}.tar.gz" \
            | tar xz -C "$WORK_DIR"
    fi
    mkdir -p "$PREFIX/include" "$PREFIX/lib"
    cp "$src/ini.h" "$PREFIX/include/"
    "${MINGW}-gcc" -O2 -c "$src/ini.c" -o "$WORK_DIR/ini.o"
    "${MINGW}-ar" rcs "$PREFIX/lib/libinih.a" "$WORK_DIR/ini.o"
    # liblcf の Findinih.cmake 用 cmake 設定
    local cmake_dir="$PREFIX/lib/cmake/inih"
    mkdir -p "$cmake_dir"
    cat > "$cmake_dir/inihConfig.cmake" << 'CMAKEEOF'
if(NOT TARGET inih::inih)
  add_library(inih::inih STATIC IMPORTED)
  set_target_properties(inih::inih PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_LIST_DIR}/../../../lib/libinih.a"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_LIST_DIR}/../../../include"
  )
endif()
CMAKEEOF
    info "inih: 完了"
}

# ── ICU ──────────────────────────────────────────────────────────────────────

build_icu() {
    [[ -f "$PREFIX/lib/libicuuc.a" ]] && { info "ICU: キャッシュ済み"; return; }
    info "ICU ${ICU_VERSION} をビルド中（時間がかかります）..."
    local src="$WORK_DIR/icu/source"
    if [[ ! -d "$src" ]]; then
        curl -fsSL "https://github.com/unicode-org/icu/releases/download/${ICU_TAG}/icu4c-${ICU_VERSION//./_}-sources.tgz" \
            | tar xz -C "$WORK_DIR"
    fi
    chmod +x "$src/configure" "$src/config.guess" "$src/config.sub" \
             "$src/install-sh" 2>/dev/null || true

    # ホスト向けビルド（クロスビルドツール生成に必要）
    local host_build="$WORK_DIR/build-icu-host"
    if [[ ! -f "$host_build/bin/pkgdata" ]]; then
        info "ICU: ホスト向けビルド中..."
        mkdir -p "$host_build"
        (cd "$host_build" && "$src/configure" \
            --disable-shared --enable-static \
            --disable-samples --disable-tests --disable-extras \
            CXXFLAGS="-O2" 2>/dev/null)
        make -C "$host_build" -j"$JOBS" 2>/dev/null
    fi

    # MinGW 向けクロスビルド
    local win_build="$WORK_DIR/build-icu-win"
    mkdir -p "$win_build"
    local build_triple
    build_triple=$("$src/config.guess")
    (cd "$win_build" && "$src/configure" \
        --host="$MINGW" --build="$build_triple" \
        --with-cross-build="$host_build" \
        --disable-shared --enable-static \
        --disable-samples --disable-tests --disable-extras \
        --prefix="$PREFIX" \
        CC="${MINGW}-gcc" CXX="${MINGW}-g++" \
        AR="${MINGW}-ar" RANLIB="${MINGW}-ranlib" \
        CXXFLAGS="-O2" 2>/dev/null)

    # ライブラリとデータだけビルド（ツール不要）
    make -C "$win_build/common" -j"$JOBS" 2>/dev/null || true
    make -C "$win_build/i18n"   -j"$JOBS" 2>/dev/null || true
    make -C "$win_build/data"   -j"$JOBS" 2>/dev/null || true

    # インストール
    mkdir -p "$PREFIX/lib" "$PREFIX/include"
    cp "$win_build/lib/libsicuin.a" "$PREFIX/lib/libicui18n.a"
    cp "$win_build/lib/libsicuuc.a" "$PREFIX/lib/libicuuc.a"
    cp "$win_build/lib/sicudt.a"    "$PREFIX/lib/libicudata.a"
    cp -r "$src/common/unicode" "$PREFIX/include/"
    cp -r "$src/i18n/unicode/." "$PREFIX/include/unicode/"
    info "ICU: 完了"
}

# ── liblcf ────────────────────────────────────────────────────────────────────

build_liblcf() {
    [[ -f "$PREFIX/lib/cmake/liblcf/liblcf-config.cmake" ]] && \
        { info "liblcf: キャッシュ済み"; return; }
    info "liblcf をビルド中..."
    local src="$WORK_DIR/liblcf"
    if [[ ! -d "$src" ]]; then
        git clone --depth=1 https://github.com/EasyRPG/liblcf.git "$src"
    fi
    local build="$WORK_DIR/build-liblcf"
    rm -rf "$build" && mkdir "$build"
    cmake "$src" -B "$build" \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_CXX_COMPILER="${MINGW}-g++" \
        -DCMAKE_INSTALL_PREFIX="$PREFIX" \
        -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_BUILD_TYPE=Release \
        -DLIBLCF_WITH_ICU=ON \
        "-DICU_ROOT=$PREFIX" \
        "-DICU_INCLUDE_DIR=$PREFIX/include" \
        "-DICU_i18n_LIBRARY=$PREFIX/lib/libicui18n.a" \
        "-DICU_uc_LIBRARY=$PREFIX/lib/libicuuc.a" \
        "-DICU_data_LIBRARY=$PREFIX/lib/libicudata.a" \
        "-DCMAKE_PREFIX_PATH=$PREFIX" \
        -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
        -G Ninja
    ninja -C "$build" -j"$JOBS"
    ninja -C "$build" install
    info "liblcf: 完了"
}

# ── rm2kscript.exe ────────────────────────────────────────────────────────────

build_rm2kscript() {
    info "rm2kscript.exe をビルド中..."
    local build="$REPO_DIR/build-win"
    rm -rf "$build" && mkdir "$build"
    cmake "$REPO_DIR" -B "$build" \
        -DCMAKE_SYSTEM_NAME=Windows \
        -DCMAKE_CXX_COMPILER="${MINGW}-g++" \
        -DCMAKE_BUILD_TYPE=Release \
        "-DCMAKE_PREFIX_PATH=$PREFIX" \
        -DCMAKE_FIND_ROOT_PATH="$PREFIX" \
        -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
        -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
        "-DICU_ROOT=$PREFIX" \
        "-DICU_INCLUDE_DIR=$PREFIX/include" \
        "-DICU_i18n_LIBRARY=$PREFIX/lib/libicui18n.a" \
        "-DICU_uc_LIBRARY=$PREFIX/lib/libicuuc.a" \
        "-DICU_data_LIBRARY=$PREFIX/lib/libicudata.a" \
        -G Ninja
    ninja -C "$build" -j"$JOBS" rm2kscript
    mkdir -p "$DIST_DIR"
    cp "$build/rm2kscript.exe" "$DIST_DIR/"
    info "完了: $DIST_DIR/rm2kscript.exe ($(du -sh "$DIST_DIR/rm2kscript.exe" | cut -f1))"
}

# ── メイン ────────────────────────────────────────────────────────────────────

build_expat
build_inih
build_icu
build_liblcf

if ! $DEPS_ONLY; then
    build_rm2kscript
fi
