cmake_minimum_required(VERSION 3.20)

project(diskscangrok VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Dependencies ──────────────────────────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v6.1.9
  GIT_SHALLOW    TRUE
)
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ftxui)

if(WIN32)
  FetchContent_Declare(
    sqlite3
    GIT_REPOSITORY https://github.com/azadkuh/sqlite-amalgamation.git
    GIT_TAG        3.38.2
    GIT_SHALLOW    TRUE
  )
  FetchContent_GetProperties(sqlite3)
  if(NOT sqlite3_POPULATED)
    FetchContent_Populate(sqlite3)
    add_library(sqlite3 STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC ${sqlite3_SOURCE_DIR})
    if(MSVC)
      target_compile_options(sqlite3 PRIVATE /W0)
    endif()
  endif()
endif()

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
  src/main.cpp
  src/model/FileNode.cpp
  src/scanner/Scanner.cpp
  src/scanner/MftReader.cpp
  src/scanner/MftFlatEntry.cpp
  src/search/FileIndex.cpp
  src/search/Search.cpp
  src/ui/App.cpp
  src/ui/TreeView.cpp
  src/ui/SearchView.cpp
  src/ui/StatusBar.cpp
  src/util/WindowsPath.cpp
  src/util/FileOperations.cpp
)

add_executable(diskscangrok ${SOURCES})

target_include_directories(diskscangrok PRIVATE src)

target_link_libraries(diskscangrok PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
)

if(WIN32)
  target_sources(diskscangrok PRIVATE src/scanner/MftCache.cpp)
  target_link_libraries(diskscangrok PRIVATE sqlite3)
endif()

# ── Compiler / linker flags ───────────────────────────────────────────────────
if(MSVC)
  target_compile_options(diskscangrok PRIVATE /EHsc /W4 /permissive-)
  target_link_options(diskscangrok PRIVATE $<$<CONFIG:Release>:/OPT:REF /OPT:ICF>)
  # For a *truly* static no-redist exe (single file portable without VC++ redist):
  #   - Use the MinGW cross-compile toolchain from Linux, or
  #   - Manually experiment with /MT + cleaning _deps/ftxui-build after setting
  #     CMAKE_MSVC_RUNTIME_LIBRARY early (current FetchContent + FTXUI does not
  #     easily produce mixed-runtime free link).
  # Default build produces a working exe + small set of system + VC runtime DLLs.
else()
  target_compile_definitions(diskscangrok PRIVATE PLATFORM_POSIX)
  target_link_libraries(diskscangrok PRIVATE pthread)
  if(MINGW OR CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_options(diskscangrok PRIVATE -static-libgcc -static-libstdc++ -static)
  endif()
endif()

if(WIN32)
  target_compile_definitions(diskscangrok PRIVATE PLATFORM_WINDOWS)
else()
  target_compile_definitions(diskscangrok PRIVATE PLATFORM_POSIX)
endif()

# ── Build-type flags ──────────────────────────────────────────────────────────
if(NOT MSVC)
  target_compile_options(diskscangrok PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ── Tests ────────────────────────────────────────────────────────────────────
include(CTest)
if(BUILD_TESTING)
  add_executable(windows_path_test
    tests/WindowsPathTest.cpp
    src/util/WindowsPath.cpp
  )
  target_include_directories(windows_path_test PRIVATE src)
  add_test(NAME windows_path_test COMMAND windows_path_test)

  add_executable(search_test
    tests/SearchTest.cpp
    src/model/FileNode.cpp
    src/search/FileIndex.cpp
    src/search/Search.cpp
  )
  target_include_directories(search_test PRIVATE src)
  add_test(NAME search_test COMMAND search_test)

  add_executable(scanner_smoke_test
    tests/ScannerSmokeTest.cpp
    src/model/FileNode.cpp
    src/scanner/Scanner.cpp
    src/scanner/MftReader.cpp
    src/scanner/MftFlatEntry.cpp
    src/util/WindowsPath.cpp
  )
  target_include_directories(scanner_smoke_test PRIVATE src)
  if(WIN32)
    target_sources(scanner_smoke_test PRIVATE src/scanner/MftCache.cpp)
    target_compile_definitions(scanner_smoke_test PRIVATE PLATFORM_WINDOWS)
    target_link_libraries(scanner_smoke_test PRIVATE sqlite3)
  else()
    target_compile_definitions(scanner_smoke_test PRIVATE PLATFORM_POSIX)
    target_link_libraries(scanner_smoke_test PRIVATE pthread)
  endif()
  add_test(NAME scanner_smoke_test COMMAND scanner_smoke_test)
  set_tests_properties(scanner_smoke_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
  )
endif()
