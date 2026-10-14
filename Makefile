# rm2kscript トップレベル Makefile
#
# ターゲット一覧:
#   make              — Release ビルド (build/)
#   make debug        — Debug ビルド   (build-debug/)
#   make test         — ユニットテスト (ctest)
#   make install      — システムインストール (PREFIX=/usr/local)
#   make win64        — Windows 向けクロスビルド (dist/rm2kscript.exe)
#   make clean        — ビルドディレクトリを削除
#   make distclean    — ビルド + 依存キャッシュ (win-sysroot/, build-deps/) も削除

BUILD_DIR   := build
DEBUG_DIR   := build-debug
PREFIX      ?= /usr/local
JOBS        ?= $(shell nproc 2>/dev/null || echo 4)
CMAKE_FLAGS ?=

# ゲームディレクトリを指定するとラウンドトリップテストも走る
# 例: make test GAME_DIR=/path/to/RPGGame
ifdef GAME_DIR
CMAKE_FLAGS += -DGAME_DIR=$(GAME_DIR)
endif

.PHONY: all release debug test install win64 clean distclean submodules

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
# Windows クロスビルド
# ────────────────────────────────────────────────────────────────────────────

win64: submodules
	bash scripts/build-win64.sh

# ────────────────────────────────────────────────────────────────────────────
# クリーン
# ────────────────────────────────────────────────────────────────────────────

clean:
	rm -rf $(BUILD_DIR) $(DEBUG_DIR) build-win dist

distclean: clean
	rm -rf win-sysroot build-deps
