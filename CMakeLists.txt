cmake_minimum_required(VERSION 3.15)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

project(tpl CXX C)
include(cmake/StandardProjectSettings.cmake)

include_directories(include)

add_library(tpl_project_options INTERFACE)
add_library(tpl_project_warnings INTERFACE)
add_library(tpl::project_options ALIAS tpl_project_options)
add_library(tpl::project_warnings ALIAS tpl_project_warnings)

include(cmake/CompilerWarnings.cmake)
set_project_warnings(tpl_project_warnings)

include(cmake/Sanitizers.cmake)
enable_sanitizers(tpl_project_options)

include(cmake/StaticAnalyzers.cmake)
include(cmake/Emscripten.cmake)

option(ENABLE_PCH "Enable Precompiled Headers" OFF)
if(ENABLE_PCH)
    target_precompile_headers(ui_project_options INTERFACE <vector> <string> <map> <utility> <unordered_map> <unordered_set> <list>)
endif(ENABLE_PCH)

add_library(tpl_core INTERFACE)
add_library(tpl::core ALIAS tpl_core)
target_include_directories(tpl_core INTERFACE include)

option(ENABLE_TESTING "Enable Test Builds" ON)

if(ENABLE_TESTING)
    enable_testing()
    message("Building Tests. Be sure to check out test/ directory")
    add_subdirectory(test)
endif(ENABLE_TESTING)

add_subdirectory(examples)

