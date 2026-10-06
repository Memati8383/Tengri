cmake_minimum_required(VERSION 3.16)
project(TENGRI LANGUAGES CXX RC)

# Bunu belirtmeden aşağıdaki değişken 3.16 tabanında sessizce yok sayılır ve derleme
# dinamik CRT'ye düşer; build.bat'in kullandığı /MT ile ayrışır.
if(POLICY CMP0091)
    cmake_policy(SET CMP0091 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
if(NOT EXISTS ${IMGUI_DIR}/imgui.h)
    message(FATAL_ERROR "Arayuz kutuphanesi bulunamadi. Depoyu submodule ile klonladiysan: git submodule update --init --recursive")
endif()

# Surum basligi src\brand.hpp'den uretilir; boylece .rc dosyasindaki surum sabitleriyle
# ayri bir kopyasi kalmaz. rc uzerinde TENGRI_HAS_VERSION_H tanimli olmadan include edilmez.
set(VERSION_HEADER ${CMAKE_CURRENT_BINARY_DIR}/generated/version.h)

file(GENERATE
     OUTPUT  ${VERSION_HEADER}
     CONTENT "#error \"bu dosya uretilmemis; build betigini calistir\""
)

add_custom_command(
    OUTPUT  ${VERSION_HEADER}
    COMMAND powershell -NoProfile -ExecutionPolicy Bypass
            -File ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_version.ps1
            -Brand ${CMAKE_CURRENT_SOURCE_DIR}/src/brand.hpp
            -Out   ${VERSION_HEADER}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/brand.hpp
            ${CMAKE_CURRENT_SOURCE_DIR}/tools/make_version.ps1
    COMMENT "Surum basligi uretiliyor (kaynak: src/brand.hpp)"
    VERBATIM
)

add_executable(TENGRI WIN32
    src/main.cpp
    src/app.cpp
    src/gui/theme.cpp
    src/gui/fx.cpp
    src/gui/icons.cpp
    src/gui/brand_icons.cpp
    src/gui/widgets.cpp
    src/core/license.cpp
    src/core/sysinfo.cpp
    src/core/cleaner.cpp
    src/core/tweaks.cpp
    src/core/network.cpp
    src/core/lang.cpp
    src/core/sysinfo_detail.cpp
    src/core/ram.cpp
    src/core/regpack.cpp
    src/tray.cpp
    res/tengri.rc
    ${VERSION_HEADER}
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_win32.cpp
    ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
)

# Uygulama manifesti res\tengri.rc içinde RT_MANIFEST olarak zaten var. Visual Studio
# üreticisi bir tane de kendi manifestini üretip aynı tür/ad/dil kimliğiyle ikinci bir
# kaynak ekliyor; cvtres "yinelenen kaynak" (CVT1100), bağlayıcı da LNK1123 veriyor.
# Üretimi bağlayıcıya söylemek duruyor: VS_GLOBAL_GenerateManifest bunu etkilemiyor
# (ölçüldü, tools\probe_manifest.ps1).
set_target_properties(TENGRI PROPERTIES LINK_FLAGS "/MANIFEST:NO")

target_include_directories(TENGRI PRIVATE src res ${IMGUI_DIR} ${IMGUI_DIR}/backends ${CMAKE_CURRENT_BINARY_DIR}/generated)
target_compile_definitions(TENGRI PRIVATE UNICODE _UNICODE IMGUI_DEFINE_MATH_OPERATORS TENGRI_HAS_VERSION_H)
target_compile_options(TENGRI PRIVATE /utf-8 /W4)
target_link_libraries(TENGRI PRIVATE d3d11 dxgi d3dcompiler dwmapi user32 gdi32 advapi32 shell32 iphlpapi comctl32 version)
