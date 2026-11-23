cmake_minimum_required(VERSION 3.25)
project(InkPages LANGUAGES CXX RC)

# InkPages: the pen-first writing area for Win32 apps – Windows ink through the Win32 ink presenter
# (IInkDesktopHost) in DirectComposition, finger/touchpad pan and zoom with DirectManipulation, comctl32 v6 controls.
# Everything from the Windows SDK and Visual Studio: C++/WinRT headers ship with the SDK, the unit test framework with
# Visual Studio – nothing is downloaded or restored.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: an app is a single exe (no VC++ runtime DLLs next to it).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(INKPAGES_COMPILE_OPTIONS /W4 /WX /permissive- /EHsc /utf-8)
set(INKPAGES_DEFINITIONS UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)

# The library (linked statically into the apps).
# Modules (src/<module>, public headers include/inkpages[/model]); dependencies point downwards only:
#   toolbar -> surface -> text, tools -> ink -> render -> model -> (none);  base (Log) is used by all.
file(GLOB INKPAGES_SOURCES CONFIGURE_DEPENDS
    src/base/*.cpp src/model/*.cpp src/render/*.cpp src/ink/*.cpp src/tools/*.cpp src/surface/*.cpp src/toolbar/*.cpp src/text/*.cpp)
add_library(InkPages STATIC ${INKPAGES_SOURCES})
set(INKPAGES_TOOLBAR_ATLAS "${CMAKE_CURRENT_SOURCE_DIR}/assets/toolbar-icons.png")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${INKPAGES_TOOLBAR_ATLAS}")
file(READ "${INKPAGES_TOOLBAR_ATLAS}" INKPAGES_TOOLBAR_ATLAS_HEX HEX)
string(REGEX REPLACE "(..)" "0x\\1,\n" INKPAGES_TOOLBAR_ATLAS_BYTES "${INKPAGES_TOOLBAR_ATLAS_HEX}")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/toolbar-atlas.inc" CONTENT "${INKPAGES_TOOLBAR_ATLAS_BYTES}")
set_property(SOURCE src/toolbar/Icons.cpp APPEND PROPERTY INCLUDE_DIRECTORIES "${CMAKE_CURRENT_BINARY_DIR}/generated")
target_include_directories(InkPages PUBLIC include PRIVATE src)
target_compile_options(InkPages PRIVATE ${INKPAGES_COMPILE_OPTIONS})
target_compile_definitions(InkPages PUBLIC ${INKPAGES_DEFINITIONS})
target_link_libraries(InkPages PUBLIC windowsapp d3d11 dxgi d2d1 dcomp comctl32 imm32 uiautomationcore ninput windowscodecs coremessaging)

# The demo and development app (UI tests in Windows Sandbox run against it).
add_executable(InkPagesDemo WIN32 demo/main.cpp demo/DemoPictures.cpp demo/app.rc)
target_compile_options(InkPagesDemo PRIVATE ${INKPAGES_COMPILE_OPTIONS})
target_link_options(InkPagesDemo PRIVATE /MANIFEST:NO) # the manifest is a resource (app.manifest)
target_link_libraries(InkPagesDemo PRIVATE InkPages comctl32 dwrite)


# Test tool (sandbox UI tests): the pointer as the screen draws it, from DXGI Desktop Duplication.
add_executable(cursorprobe tools/cursorprobe.cpp)
target_compile_options(cursorprobe PRIVATE ${INKPAGES_COMPILE_OPTIONS})
target_compile_definitions(cursorprobe PRIVATE ${INKPAGES_DEFINITIONS})
target_link_libraries(cursorprobe PRIVATE d3d11 dxgi)

# Unit tests: GoogleTest (fetched once at configure time, pinned release; only for the tests, never in an app),
# run by CTest. (Microsoft's CppUnitTest found no tests in a CMake-built DLL under vstest.console – 2026-11-19.)
include(FetchContent)
FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()
add_executable(InkPagesTests tests/LogTests.cpp tests/GeometryTests.cpp tests/ToolTests.cpp tests/ToolGeometryTests.cpp tests/ToolbarModelTests.cpp tests/StrokeKeysTests.cpp tests/PathDataTests.cpp tests/PageMapTests.cpp tests/TapGestureTests.cpp tests/TextTests.cpp tests/RichEditBoxTests.cpp tests/StrokeEditTests.cpp tests/InkHostTests.cpp tests/ScrollTests.cpp tests/ViewerTests.cpp)
target_compile_options(InkPagesTests PRIVATE ${INKPAGES_COMPILE_OPTIONS})
target_link_libraries(InkPagesTests PRIVATE InkPages GTest::gtest_main)
target_include_directories(InkPagesTests PRIVATE src) # pure units of the modules (e.g. toolbar/ToolbarModel)
include(GoogleTest)
gtest_discover_tests(InkPagesTests)

# Test tool: what native UI Automation (as Narrator) sees in a window.
add_executable(uiaprobe tools/uiaprobe.cpp)
target_compile_options(uiaprobe PRIVATE ${INKPAGES_COMPILE_OPTIONS})
target_compile_definitions(uiaprobe PRIVATE ${INKPAGES_DEFINITIONS})
target_link_libraries(uiaprobe PRIVATE windowsapp oleaut32 uiautomationcore dwmapi)

# Standalone build: the demo and the test tools get their own output folders (the sandbox UI tests copy a folder).
if(PROJECT_IS_TOP_LEVEL)
    set_target_properties(InkPagesDemo PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/demo/$<CONFIG>")
    set_target_properties(cursorprobe uiaprobe PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools/$<CONFIG>")
endif()
