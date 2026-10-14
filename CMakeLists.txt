cmake_minimum_required(VERSION 3.16)
project(rm2kscript VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# liblcf
find_package(liblcf REQUIRED)

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

add_executable(rm2kscript src/main.cpp)
target_link_libraries(rm2kscript PRIVATE rm2kscript_core)

# Windows スタティックリンク設定
if(MSVC)
    # MSVC: /MT でランタイムをスタティックリンク
    set_property(TARGET rm2kscript PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set_property(TARGET rm2kscript_core PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif(WIN32)
    # MinGW: gcc/stdc++/winpthread をスタティックリンク
    target_link_options(rm2kscript PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

install(TARGETS rm2kscript DESTINATION bin)

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
endif()
