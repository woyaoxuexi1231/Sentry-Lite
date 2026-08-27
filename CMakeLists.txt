cmake_minimum_required(VERSION 3.20)
project(Sentry-Lite VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(Sentry-Lite WIN32
    src/app/main.cpp
    src/app/core/config.cpp
    src/app/collectors/cpu_usage.cpp
    src/app/collectors/mem_usage.cpp
    src/app/collectors/gpu_usage.cpp
    src/app/collectors/net_speed.cpp
    src/app/history/recorder.cpp
    src/app/history/store.cpp
    src/app/history/retention.cpp
    src/app/ui/theme.cpp
    src/app/ui/d2d_render.cpp
    src/app/ui/float_bar.cpp
    src/app/core/shm_client.cpp
    resources/app.rc
)

target_include_directories(Sentry-Lite PRIVATE src src/app)

target_compile_definitions(Sentry-Lite PRIVATE
    UNICODE _UNICODE
    NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(Sentry-Lite PRIVATE /O2 /W4 /permissive- /utf-8 /GS /Zi)
target_link_options(Sentry-Lite PRIVATE /OPT:REF /OPT:ICF /DEBUG /SUBSYSTEM:WINDOWS)

# 静态 CRT：绿色单文件，无运行时依赖
set_property(TARGET Sentry-Lite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

target_link_libraries(Sentry-Lite PRIVATE
    d2d1.lib dwrite.lib dxgi.lib
    pdh.lib iphlpapi.lib wbemuuid.lib
    dwmapi.lib shcore.lib gdi32.lib user32.lib shell32.lib
    ole32.lib advapi32.lib
)

# 每显示器 v2 DPI 感知清单
target_sources(Sentry-Lite PRIVATE app.manifest)

# 资源编译器在 resources/ 目录解析 app.ico
set_source_files_properties(resources/app.rc PROPERTIES
    VS_RESOURCE_GENERATOR "ResourceCompile"
    VS_SETTINGS "WorkingDirectory=${CMAKE_SOURCE_DIR}/resources"
)
