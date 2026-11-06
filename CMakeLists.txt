cmake_minimum_required(VERSION 3.25)
project(VTabsExplorer VERSION 1.0.0 LANGUAGES CXX RC)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "VTabsExplorer va compilato a 64 bit (x64).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Proiezioni C++/WinRT per la mod, generate dai .winmd del runtime WinUI 3 di Esplora file
# (tools\Generate-Projections.ps1, chiamato da build.ps1).
set(XPX_CPPWINRT_DIR "${CMAKE_BINARY_DIR}/generated/cppwinrt")

add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)

# VTabsExplorerMod.dll: la mod delle schede verticali, iniettata in Esplora file (mai nel processo della
# barra delle applicazioni durante lo sviluppo: vedi src/ModShared.h).
add_library(VTabsExplorerMod SHARED
    src/mod/dllmain.cpp
    src/mod/CaptionButtons.cpp
    src/mod/DevDiagnostics.cpp
    src/mod/Discovery.cpp
    src/mod/Glass.cpp
    src/mod/Interop.cpp
    src/mod/MenuArray.cpp
    src/mod/ModCore.cpp
    src/mod/ModLog.cpp
    src/mod/ModSettings.cpp
    src/mod/ModernMenu.cpp
    src/mod/NavBar.cpp
    src/mod/NavGroups.cpp
    src/mod/NavItems.cpp
    src/mod/NavOrder.cpp
    src/mod/TabColors.cpp
    src/mod/TextInput.cpp
    src/mod/TreeDump.cpp
    src/mod/UnifiedHeader.cpp
    src/mod/VerticalTabs.cpp
    src/mod/WindowLayout.cpp
    src/mod/VTabsExplorerMod.def)
target_include_directories(VTabsExplorerMod PRIVATE "${XPX_CPPWINRT_DIR}" src/mod)
target_precompile_headers(VTabsExplorerMod PRIVATE src/mod/pch.h)
target_compile_options(VTabsExplorerMod PRIVATE /bigobj)
target_link_libraries(VTabsExplorerMod PRIVATE windowsapp comctl32 ole32 oleaut32 user32 shell32 shlwapi propsys gdi32 advapi32 uuid dwmapi uxtheme d2d1 dwrite)

# VTabsExplorer.exe: icona nella tray che ricorda le finestre di Esplora file e le riapre.
add_executable(VTabsExplorer WIN32
    src/main.cpp
    src/BackToTop.cpp
    src/Common.cpp
    src/Explorer.cpp
    src/Hotkey.cpp
    src/Injector.cpp
    src/ModManager.cpp
    src/Session.cpp
    src/Settings.cpp
    src/TabColorFiles.cpp
    src/TrayIcon.cpp
    src/Veil.cpp
    src/Worker.cpp
    src/VTabsExplorer.rc
    src/VTabsExplorer.manifest)
target_link_libraries(VTabsExplorer PRIVATE user32 shell32 shlwapi gdi32 advapi32 ole32 oleaut32 uuid crypt32 gdiplus comctl32 uxtheme)
add_dependencies(VTabsExplorer VTabsExplorerMod)
