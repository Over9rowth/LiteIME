cmake_minimum_required(VERSION 3.20)
project(LiteIME VERSION 0.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MINGW)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -municode)
    add_link_options(-static-libgcc -static-libstdc++ -municode)
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)

option(LITEIME_TSF_ONLY "Build only the TSF COM DLL (used for 32-bit WOW64 build)" OFF)

# 1. Core Bilingual Input Engine + CLI + Host + Tests (64-bit / host build)
if(NOT LITEIME_TSF_ONLY)
    add_library(liteime_engine STATIC
        src/engine/pinyin_table.cpp
        src/engine/binary_dict.cpp
        src/engine/bigram_model.cpp
        src/engine/user_dict.cpp
        src/engine/symbol_catalog.cpp
        src/engine/bilingual_engine.cpp
        src/engine/sentence_decoder.cpp
        src/engine/candidate_ranker.cpp
    )
    target_include_directories(liteime_engine PUBLIC src)

    # 2. Dictionary Compiler CLI
    add_executable(dict_compiler
        src/engine/dict_compiler_main.cpp
    )
    target_link_libraries(dict_compiler PRIVATE liteime_engine)
endif()

# 3. Genuine Windows TSF In-Process COM DLL (64-bit LiteIME64.dll / 32-bit LiteIME32.dll)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(LITEIME_TSF_TARGET LiteIME32)
else()
    set(LITEIME_TSF_TARGET LiteIME64)
endif()

add_library(${LITEIME_TSF_TARGET} SHARED
    src/tsf/guids.cpp
    src/tsf/dll_main.cpp
    src/tsf/class_factory.cpp
    src/tsf/register.cpp
    src/tsf/pipe_client.cpp
    src/tsf/display_attribute.cpp
    src/tsf/inproc_candidate_window.cpp
    src/tsf/composition.cpp
    src/tsf/text_service.cpp
    src/tsf/LiteIME.def
    src/tsf/liteime_tsf.rc
)
set_target_properties(${LITEIME_TSF_TARGET} PROPERTIES
    PREFIX ""
    OUTPUT_NAME "${LITEIME_TSF_TARGET}"
    SUFFIX ".dll"
)
target_include_directories(${LITEIME_TSF_TARGET} PRIVATE src)
target_link_libraries(${LITEIME_TSF_TARGET} PRIVATE
    ole32
    oleaut32
    uuid
    user32
    gdi32
    advapi32
)
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    target_link_options(${LITEIME_TSF_TARGET} PRIVATE -static -Wl,--enable-stdcall-fixup -Wl,--kill-at)
else()
    target_link_options(${LITEIME_TSF_TARGET} PRIVATE -static -Wl,--enable-stdcall-fixup)
endif()

if(NOT LITEIME_TSF_ONLY)
    # 4. Native Direct2D Host + Candidate Window + Status Bar + Tray
    add_executable(LiteIMEHost WIN32
        src/host/main.cpp
        src/host/pipe_server.cpp
        src/host/candidate_window.cpp
        src/host/status_window.cpp
        src/host/tray_icon.cpp
        src/host/quick_input_dialog.cpp
        src/host/custom_kaomoji_dialog.cpp
        src/host/settings_dialog.cpp
        src/host/liteime_host.rc
    )
    target_include_directories(LiteIMEHost PRIVATE src)
    target_link_libraries(LiteIMEHost PRIVATE
        liteime_engine
        d2d1
        dwrite
        user32
        gdi32
        shell32
        advapi32
        ole32
        comctl32
        dwmapi
    )
    target_link_options(LiteIMEHost PRIVATE -static -mwindows)

    # 5. Automated Unit Tests
    enable_testing()
    add_executable(ime_tests
        tests/engine_tests.cpp
    )
    target_include_directories(ime_tests PRIVATE src)
    target_link_libraries(ime_tests PRIVATE liteime_engine)
    add_test(NAME EngineTests COMMAND ime_tests)

    add_executable(ui_window_smoke WIN32
        tests/ui_window_smoke.cpp
        src/host/candidate_window.cpp
        src/host/status_window.cpp
        src/host/settings_dialog.cpp
    )
    target_include_directories(ui_window_smoke PRIVATE src)
    target_link_libraries(ui_window_smoke PRIVATE liteime_engine d2d1 dwrite user32 gdi32 ole32 comctl32)
    add_test(NAME UiWindowSmoke COMMAND ui_window_smoke)
endif()
