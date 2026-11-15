cmake_minimum_required(VERSION 3.20)
project(LiteIME VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MINGW)
    add_compile_options(-Wall -Wextra -Wno-unused-parameter -municode)
    add_link_options(-static-libgcc -static-libstdc++ -municode)
endif()

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)

# 1. Core Bilingual Input Engine (portable C++20)
add_library(liteime_engine STATIC
    src/engine/pinyin_table.cpp
    src/engine/binary_dict.cpp
    src/engine/user_dict.cpp
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

# 3. Genuine Windows TSF In-Process COM DLL
add_library(LiteIME64 SHARED
    src/tsf/guids.cpp
    src/tsf/dll_main.cpp
    src/tsf/class_factory.cpp
    src/tsf/register.cpp
    src/tsf/pipe_client.cpp
    src/tsf/display_attribute.cpp
    src/tsf/composition.cpp
    src/tsf/text_service.cpp
    src/tsf/LiteIME.def
)
set_target_properties(LiteIME64 PROPERTIES
    PREFIX ""
    OUTPUT_NAME "LiteIME64"
    SUFFIX ".dll"
)
target_include_directories(LiteIME64 PRIVATE src)
target_link_libraries(LiteIME64 PRIVATE
    ole32
    oleaut32
    uuid
    user32
    advapi32
)
target_link_options(LiteIME64 PRIVATE -static -Wl,--enable-stdcall-fixup)

# 4. Native Direct2D Host + Candidate Window + Status Bar + Tray
add_executable(LiteIMEHost WIN32
    src/host/main.cpp
    src/host/pipe_server.cpp
    src/host/candidate_window.cpp
    src/host/status_window.cpp
    src/host/tray_icon.cpp
    src/host/quick_input_dialog.cpp
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
)
target_include_directories(ui_window_smoke PRIVATE src)
target_link_libraries(ui_window_smoke PRIVATE liteime_engine d2d1 dwrite user32 gdi32 ole32)
add_test(NAME UiWindowSmoke COMMAND ui_window_smoke)
