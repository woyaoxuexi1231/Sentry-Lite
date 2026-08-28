cmake_minimum_required(VERSION 3.20)
project(Sentry-Lite VERSION 1.3.1 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# ---- �?EXE 主程序（含温度采集，�?PawnIO 时自动降级不检测）----
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
    src/app/history/query.cpp
    src/app/temp/temp_cpu_lhm.cpp
    src/app/temp/temp_gpu_nvml.cpp
    src/app/ui/dashboard.cpp
    resources/app.rc
)

target_include_directories(Sentry-Lite PRIVATE
    src src/app
    third_party/webview2/include
)

target_compile_definitions(Sentry-Lite PRIVATE
    UNICODE _UNICODE
    NOMINMAX WIN32_LEAN_AND_MEAN
    WINVER=0x0A00 _WIN32_WINNT=0x0A00
    _CRT_SECURE_NO_WARNINGS
)

target_compile_options(Sentry-Lite PRIVATE /O2 /W4 /permissive- /utf-8 /GS /Zi)
target_link_options(Sentry-Lite PRIVATE
    /OPT:REF /OPT:ICF /DEBUG /SUBSYSTEM:WINDOWS
    "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'"
)

set_property(TARGET Sentry-Lite PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded")

target_link_libraries(Sentry-Lite PRIVATE
    "${CMAKE_SOURCE_DIR}/third_party/webview2/lib/x64/WebView2LoaderStatic.lib"
    pdh.lib iphlpapi.lib
    shcore.lib gdi32.lib user32.lib shell32.lib
    ole32.lib oleaut32.lib advapi32.lib shlwapi.lib
)

target_sources(Sentry-Lite PRIVATE app.manifest)

set_source_files_properties(resources/app.rc PROPERTIES
    VS_RESOURCE_GENERATOR "ResourceCompile"
    VS_SETTINGS "WorkingDirectory=${CMAKE_SOURCE_DIR}/resources"
)

# 构建后把 web 资源目录复制�?exe 旁，�?WebView2 虚拟主机加载
add_custom_command(TARGET Sentry-Lite POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/src/app/web" "$<TARGET_FILE_DIR:Sentry-Lite>/web"
    VERBATIM
)

find_program(DOTNET_EXECUTABLE dotnet)
if(DOTNET_EXECUTABLE)
    add_custom_command(TARGET Sentry-Lite POST_BUILD
        COMMAND ${DOTNET_EXECUTABLE} publish "${CMAKE_SOURCE_DIR}/tools/lhm-bridge/Sentry-Lite-lhm.csproj"
                -c $<IF:$<CONFIG:Debug>,Debug,Release> -r win-x64 --self-contained false
                -o "$<TARGET_FILE_DIR:Sentry-Lite>"
        COMMENT "Publishing Sentry-Lite-lhm (LibreHardwareMonitorLib 0.9.6)"
        VERBATIM
    )
endif()