# rm2kscript トップレベル Makefile
#
# ビルドに関する処理はすべてこのファイルに書く (別途ビルド用スクリプトは持たない)。
#
# ターゲット一覧:
#   make                — Release ビルド (build/)
#   make debug          — Debug ビルド   (build-debug/)
#   make test           — ユニットテスト (ctest)
#   make install        — システムインストール (PREFIX=/usr/local)
#   make win64          — Windows 向けクロスビルド (dist/rm2kscript.exe)。依存は自動でビルドする
#   make win64-deps     — Windows 向けの依存 (expat / inih / ICU) だけをビルド (win-sysroot/)
#   make clean          — ビルドディレクトリを削除
#   make distclean      — ビルド + 依存キャッシュ (win-sysroot/, build-deps/) も削除

BUILD_DIR   := build
DEBUG_DIR   := build-debug
PREFIX      ?= /usr/local
JOBS        ?= $(shell nproc 2>/dev/null || echo 4)
CMAKE_FLAGS ?=

# 複数行のレシピを1つのシェルで実行し、途中のコマンドが失敗したらそこで止める
SHELL       := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.ONESHELL:

# ゲームディレクトリを指定するとラウンドトリップテストも走る
# 例: make test GAME_DIR=/path/to/RPGGame
ifdef GAME_DIR
CMAKE_FLAGS += -DGAME_DIR=$(GAME_DIR)
endif

.PHONY: all release debug test install clean distclean submodules \
        win64 win64-deps win64-check win64-deps-key

# liblcf は git submodule (third_party/liblcf)。未取得なら初期化する (git 管理下の場合のみ)
submodules:
	@if [ -e .git ] && [ ! -f third_party/liblcf/CMakeLists.txt ]; then \
		echo "[make] git submodule update --init --recursive"; \
		git submodule update --init --recursive; \
	fi

# ────────────────────────────────────────────────────────────────────────────
# Release ビルド
# ────────────────────────────────────────────────────────────────────────────

all: release

# 毎回 configure し直す (make test GAME_DIR=... など CMAKE_FLAGS の変更が既存の build/ にも反映されるように)
release: submodules
	cmake -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release $(CMAKE_FLAGS)
	cmake --build $(BUILD_DIR) -j$(JOBS)

# ────────────────────────────────────────────────────────────────────────────
# Debug ビルド
# ────────────────────────────────────────────────────────────────────────────

debug: submodules
	cmake -B $(DEBUG_DIR) -DCMAKE_BUILD_TYPE=Debug $(CMAKE_FLAGS)
	cmake --build $(DEBUG_DIR) -j$(JOBS)

# ────────────────────────────────────────────────────────────────────────────
# テスト
# ────────────────────────────────────────────────────────────────────────────

test: release
	cd $(BUILD_DIR) && ctest --output-on-failure -j$(JOBS)

# ────────────────────────────────────────────────────────────────────────────
# インストール
# ────────────────────────────────────────────────────────────────────────────

install: release
	cmake --install $(BUILD_DIR) --prefix $(PREFIX)

# ────────────────────────────────────────────────────────────────────────────
# Windows クロスビルド (MinGW-w64)
# ────────────────────────────────────────────────────────────────────────────
# 必要なツール
#   Arch Linux: sudo pacman -S mingw-w64-gcc mingw-w64-cmake ninja
#   Ubuntu    : sudo apt-get install cmake ninja-build gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64
#
# liblcf は third_party/liblcf (submodule) で、rm2kscript の CMake がソースから直接組み込む。
# ここでビルドするのは liblcf が使う依存 (expat / inih / ICU) だけ。
# 依存は $(WIN_SYSROOT)/ に成果物があれば再ビルドしない。作り直すときは make distclean。

MINGW         := x86_64-w64-mingw32
WIN_BUILD     := build-win
DIST_DIR      := dist
WORK_DIR      := build-deps
WIN_SYSROOT   := win-sysroot
WORK_ABS      := $(abspath $(WORK_DIR))
SYSROOT_ABS   := $(abspath $(WIN_SYSROOT))

EXPAT_VERSION := 2.9.0
INIH_VERSION  := r62
# ICU 78 以降はタグも資産名もドット区切り (release-78.3 / icu4c-78.3-sources.tgz)。
# 77 以前は release-77-1 / icu4c-77_1-src.tgz だったので、バージョンを下げる場合は URL の組み立ても変えること
ICU_VERSION   := 78.3

# 各依存の「ビルド完了」を示すファイル (最後に生成されるものを選んでいる)
EXPAT_STAMP   := $(WIN_SYSROOT)/lib/cmake/expat-$(EXPAT_VERSION)/expat-config.cmake
INIH_STAMP    := $(WIN_SYSROOT)/lib/libinih.a
ICU_STAMP     := $(WIN_SYSROOT)/lib/libicuuc.a

# CI のキャッシュキー用 (依存のバージョンが変わったときだけ変わる)
win64-deps-key:
	@echo expat-$(EXPAT_VERSION)-inih-$(INIH_VERSION)-icu-$(ICU_VERSION)

win64-check:
	for c in $(MINGW)-g++ $(MINGW)-gcc $(MINGW)-ar cmake ninja git curl tar; do
	    command -v "$$c" >/dev/null 2>&1 || { echo "ERROR: '$$c' が見つかりません。インストールしてください。" >&2; exit 1; }
	done

win64-deps: $(EXPAT_STAMP) $(INIH_STAMP) $(ICU_STAMP)

# ── expat ───────────────────────────────────────────────────────────────────
$(EXPAT_STAMP): | win64-check
	src="$(WORK_ABS)/expat-$(EXPAT_VERSION)"
	build="$(WORK_ABS)/build-expat"
	mkdir -p "$(WORK_ABS)" "$(SYSROOT_ABS)"
	echo "[make] expat $(EXPAT_VERSION) をビルド中..."
	if [ ! -d "$$src" ]; then
	    url="https://github.com/libexpat/libexpat/releases/download/R_$(subst .,_,$(EXPAT_VERSION))/expat-$(EXPAT_VERSION).tar.xz"
	    curl -fsSL "$$url" -o "$(WORK_ABS)/expat.tar.xz" || { echo "ERROR: expat のダウンロードに失敗しました: $$url" >&2; exit 1; }
	    tar xJf "$(WORK_ABS)/expat.tar.xz" -C "$(WORK_ABS)"
	    rm -f "$(WORK_ABS)/expat.tar.xz"
	fi
	rm -rf "$$build"
	cmake -S "$$src" -B "$$build" -G Ninja \
	    -DCMAKE_SYSTEM_NAME=Windows \
	    -DCMAKE_C_COMPILER=$(MINGW)-gcc \
	    -DCMAKE_CXX_COMPILER=$(MINGW)-g++ \
	    -DCMAKE_INSTALL_PREFIX="$(SYSROOT_ABS)" \
	    -DBUILD_SHARED_LIBS=OFF \
	    -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_TOOLS=OFF \
	    -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_DOCS=OFF \
	    -DCMAKE_BUILD_TYPE=Release
	ninja -C "$$build" -j$(JOBS)
	ninja -C "$$build" install
	echo "[make] expat: 完了"

# ── inih ────────────────────────────────────────────────────────────────────
$(INIH_STAMP): | win64-check
	src="$(WORK_ABS)/inih-$(INIH_VERSION)"
	mkdir -p "$(WORK_ABS)" "$(SYSROOT_ABS)/include" "$(SYSROOT_ABS)/lib/cmake/inih"
	echo "[make] inih $(INIH_VERSION) をビルド中..."
	if [ ! -d "$$src" ]; then
	    url="https://github.com/benhoyt/inih/archive/refs/tags/$(INIH_VERSION).tar.gz"
	    curl -fsSL "$$url" -o "$(WORK_ABS)/inih.tar.gz" || { echo "ERROR: inih のダウンロードに失敗しました: $$url" >&2; exit 1; }
	    tar xzf "$(WORK_ABS)/inih.tar.gz" -C "$(WORK_ABS)"
	    rm -f "$(WORK_ABS)/inih.tar.gz"
	fi
	cp "$$src/ini.h" "$(SYSROOT_ABS)/include/"
	# liblcf の Findinih.cmake 用 cmake 設定
	printf '%s\n' \
	    'if(NOT TARGET inih::inih)' \
	    '  add_library(inih::inih STATIC IMPORTED)' \
	    '  set_target_properties(inih::inih PROPERTIES' \
	    '    IMPORTED_LOCATION "$${CMAKE_CURRENT_LIST_DIR}/../../../lib/libinih.a"' \
	    '    INTERFACE_INCLUDE_DIRECTORIES "$${CMAKE_CURRENT_LIST_DIR}/../../../include"' \
	    '  )' \
	    'endif()' > "$(SYSROOT_ABS)/lib/cmake/inih/inihConfig.cmake"
	$(MINGW)-gcc -O2 -c "$$src/ini.c" -o "$(WORK_ABS)/ini.o"
	# スタンプ (libinih.a) は最後に生成する
	$(MINGW)-ar rcs "$(SYSROOT_ABS)/lib/libinih.a" "$(WORK_ABS)/ini.o"
	echo "[make] inih: 完了"

# ── ICU ─────────────────────────────────────────────────────────────────────
# クロスビルドにはホスト向けにビルドしたツール (pkgdata など) が必要なので、
# ホスト向けビルド → MinGW 向けビルド の順に行う。
$(ICU_STAMP): | win64-check
	src="$(WORK_ABS)/icu/source"
	host_build="$(WORK_ABS)/build-icu-host"
	win_build="$(WORK_ABS)/build-icu-win"
	mkdir -p "$(WORK_ABS)" "$(SYSROOT_ABS)/lib" "$(SYSROOT_ABS)/include"
	echo "[make] ICU $(ICU_VERSION) をビルド中（時間がかかります）..."
	if [ ! -d "$$src" ]; then
	    url="https://github.com/unicode-org/icu/releases/download/release-$(ICU_VERSION)/icu4c-$(ICU_VERSION)-sources.tgz"
	    tgz="$(WORK_ABS)/icu4c-$(ICU_VERSION)-sources.tgz"
	    # パイプで tar に渡すと 404 のときに原因が分かりにくいので、ファイルに落としてから展開する
	    curl -fsSL "$$url" -o "$$tgz" || { echo "ERROR: ICU のダウンロードに失敗しました: $$url" >&2; exit 1; }
	    tar xzf "$$tgz" -C "$(WORK_ABS)"
	    rm -f "$$tgz"
	    [ -d "$$src" ] || { echo "ERROR: ICU のソースが見つかりません: $$src" >&2; exit 1; }
	fi
	chmod +x "$$src/configure" "$$src/config.guess" "$$src/config.sub" "$$src/install-sh" 2>/dev/null || true

	# ホスト向けビルド (クロスビルドツール生成に必要)
	if [ ! -f "$$host_build/bin/pkgdata" ]; then
	    echo "[make] ICU: ホスト向けビルド中..."
	    mkdir -p "$$host_build"
	    ( cd "$$host_build" && "$$src/configure" \
	        --disable-shared --enable-static \
	        --disable-samples --disable-tests --disable-extras \
	        CXXFLAGS="-O2" )
	    $(MAKE) -C "$$host_build" -j$(JOBS)
	fi

	# MinGW 向けクロスビルド
	mkdir -p "$$win_build"
	build_triple=$$("$$src/config.guess")
	( cd "$$win_build" && "$$src/configure" \
	    --host=$(MINGW) --build="$$build_triple" \
	    --with-cross-build="$$host_build" \
	    --disable-shared --enable-static \
	    --disable-samples --disable-tests --disable-extras \
	    --prefix="$(SYSROOT_ABS)" \
	    CC=$(MINGW)-gcc CXX=$(MINGW)-g++ \
	    AR=$(MINGW)-ar RANLIB=$(MINGW)-ranlib \
	    CXXFLAGS="-O2" )

	# ライブラリとデータだけビルドする (ツール類は不要)。
	# pkgdata が ../lib/sicudt.a を生成するとき win_build/lib/ が存在しないと ar が失敗するので
	# 事前に作成する。また common → i18n → data の依存順を守るため逐次実行する。
	mkdir -p "$$win_build/lib"
	$(MAKE) -C "$$win_build/common" -j$(JOBS)
	$(MAKE) -C "$$win_build/i18n"   -j$(JOBS)
	$(MAKE) -C "$$win_build/data"   -j$(JOBS)

	cp "$$win_build/lib/libsicuin.a" "$(SYSROOT_ABS)/lib/libicui18n.a"
	cp "$$win_build/lib/sicudt.a"    "$(SYSROOT_ABS)/lib/libicudata.a"
	cp -r "$$src/common/unicode"     "$(SYSROOT_ABS)/include/"
	cp -r "$$src/i18n/unicode/."     "$(SYSROOT_ABS)/include/unicode/"
	# スタンプ (libicuuc.a) は最後に生成する
	cp "$$win_build/lib/libsicuuc.a" "$(SYSROOT_ABS)/lib/libicuuc.a"
	echo "[make] ICU: 完了"

# ── rm2kscript.exe ──────────────────────────────────────────────────────────
win64: submodules win64-check win64-deps
	echo "[make] rm2kscript.exe をビルド中..."
	rm -rf "$(WIN_BUILD)"
	cmake -S . -B "$(WIN_BUILD)" -G Ninja \
	    -DCMAKE_SYSTEM_NAME=Windows \
	    -DCMAKE_CXX_COMPILER=$(MINGW)-g++ \
	    -DCMAKE_BUILD_TYPE=Release \
	    -DLIBLCF_WITH_ICU=ON \
	    "-DCMAKE_PREFIX_PATH=$(SYSROOT_ABS)" \
	    "-DCMAKE_FIND_ROOT_PATH=$(SYSROOT_ABS)" \
	    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY \
	    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY \
	    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=BOTH \
	    "-DICU_ROOT=$(SYSROOT_ABS)" \
	    "-DICU_INCLUDE_DIR=$(SYSROOT_ABS)/include" \
	    "-DICU_i18n_LIBRARY=$(SYSROOT_ABS)/lib/libicui18n.a" \
	    "-DICU_uc_LIBRARY=$(SYSROOT_ABS)/lib/libicuuc.a" \
	    "-DICU_data_LIBRARY=$(SYSROOT_ABS)/lib/libicudata.a"
	cmake --build "$(WIN_BUILD)" --target rm2kscript -j$(JOBS)
	mkdir -p "$(DIST_DIR)"
	cp "$(WIN_BUILD)/rm2kscript.exe" "$(DIST_DIR)/"
	echo "[make] 完了: $(DIST_DIR)/rm2kscript.exe ($$(du -sh "$(DIST_DIR)/rm2kscript.exe" | cut -f1))"

# ────────────────────────────────────────────────────────────────────────────
# クリーン
# ────────────────────────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR) $(DEBUG_DIR) $(WIN_BUILD) $(DIST_DIR)

distclean: clean
	rm -rf $(WIN_SYSROOT) $(WORK_DIR)
