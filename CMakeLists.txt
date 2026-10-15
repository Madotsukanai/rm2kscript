cmake_minimum_required(VERSION 3.16)
project(rm2kscript VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: ソースは BOM なし UTF-8 (日本語のコメント・文字列を含む)。指定しないと既定のコードページで
# 解釈され、C4819 警告や文字化け (日本語ロケールではコメント末尾の 0x5C による次行の巡き込み) の原因になる
if(MSVC)
    add_compile_options(/utf-8)
endif()

# liblcf: 既定では git submodule (third_party/liblcf) をソースから組み込む。
# システムにインストール済みの liblcf を使う場合: cmake -DRM2K_USE_SYSTEM_LIBLCF=ON
# (submodule が未取得の場合も find_package にフォールバックする)
option(RM2K_USE_SYSTEM_LIBLCF "Use an installed liblcf instead of third_party/liblcf" OFF)
set(RM2K_LIBLCF_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/liblcf")

if(NOT RM2K_USE_SYSTEM_LIBLCF AND EXISTS "${RM2K_LIBLCF_DIR}/CMakeLists.txt")
    message(STATUS "liblcf: using submodule ${RM2K_LIBLCF_DIR}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(LIBLCF_ENABLE_TOOLS OFF CACHE BOOL "" FORCE)
    set(LIBLCF_ENABLE_TESTS OFF CACHE BOOL "" FORCE)
    set(LIBLCF_UPDATE_MIMEDB OFF CACHE BOOL "" FORCE)
    add_subdirectory("${RM2K_LIBLCF_DIR}" third_party/liblcf EXCLUDE_FROM_ALL)
else()
    if(NOT RM2K_USE_SYSTEM_LIBLCF)
        message(WARNING "third_party/liblcf is empty. Run: git submodule update --init --recursive "
                        "(falling back to an installed liblcf)")
    endif()
    find_package(liblcf REQUIRED)
endif()

# ASan/UBSan ビルド (GCC/Clang のみ): cmake -DRM2K_SANITIZE=ON
option(RM2K_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
if(RM2K_SANITIZE AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
    add_link_options(-fsanitize=address,undefined)
endif()

# ──────────────────────────────────────────────────────────────────────────────
# 共有ライブラリ (main を除いたコア): テストから再利用するため分離
# ──────────────────────────────────────────────────────────────────────────────

add_library(rm2kscript_core STATIC
    src/lexer.cpp
    src/decoder.cpp
    src/encoder.cpp
    src/commands.cpp
)

target_include_directories(rm2kscript_core PUBLIC include)

# ターゲット名: liblcf インストール版は "liblcf::liblcf"、
# ソースツリー組み込み (add_subdirectory) 版は "lcf"
if(TARGET liblcf::liblcf)
    target_link_libraries(rm2kscript_core PUBLIC liblcf::liblcf)
else()
    target_link_libraries(rm2kscript_core PUBLIC lcf)
endif()

# MSVC は -Wall/-Wextra をサポートしないため分岐
if(MSVC)
    target_compile_options(rm2kscript_core PRIVATE /W3)
else()
    target_compile_options(rm2kscript_core PRIVATE -Wall -Wextra)
endif()

# ──────────────────────────────────────────────────────────────────────────────
# メイン実行ファイル
# ──────────────────────────────────────────────────────────────────────────────

add_executable(r2s src/main.cpp)
target_link_libraries(r2s PRIVATE rm2kscript_core)
if(MSVC)
    target_compile_options(r2s PRIVATE /W3)
else()
    target_compile_options(r2s PRIVATE -Wall -Wextra)
endif()

# Windows スタティックリンク設定
if(MSVC)
    # MSVC: /MT でランタイムをスタティックリンク
    set_property(TARGET r2s PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET rm2kscript_core PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(WIN32)
    # MinGW: gcc/stdc++/winpthread をスタティックリンク
    target_link_options(r2s PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

install(TARGETS r2s DESTINATION bin)

# ──────────────────────────────────────────────────────────────────────────────
# テスト
# ──────────────────────────────────────────────────────────────────────────────

enable_testing()

add_executable(test_roundtrip tests/test_roundtrip.cpp)
target_link_libraries(test_roundtrip PRIVATE rm2kscript_core)

add_executable(test_game_roundtrip tests/test_game_roundtrip.cpp)
target_link_libraries(test_game_roundtrip PRIVATE rm2kscript_core)

add_test(NAME roundtrip COMMAND test_roundtrip)

# ゲームファイルが指定されていれば全マップテストを追加
if(DEFINED GAME_DIR AND EXISTS "${GAME_DIR}")
    add_test(NAME game_roundtrip COMMAND test_game_roundtrip "${GAME_DIR}")
    # 名前テーブルあり (switch["名前"] 形式) の経路も検証する
    if(EXISTS "${GAME_DIR}/RPG_RT.ldb")
        add_test(NAME game_roundtrip_names
                 COMMAND test_game_roundtrip "${GAME_DIR}" -n "${GAME_DIR}/RPG_RT.ldb")
    endif()
endif()
