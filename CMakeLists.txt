cmake_minimum_required(VERSION 3.20)
project(ExchangeCore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable folder grouping for Visual Studio
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

include(FetchContent)

# Fetch FTXUI
message(STATUS "Fetching FTXUI...")
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG v5.0.0
)
FetchContent_MakeAvailable(ftxui)

# Fetch GoogleTest
message(STATUS "Fetching GoogleTest...")
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.14.0
)
FetchContent_MakeAvailable(googletest)

# Define Core Library Target
file(GLOB_RECURSE CORE_HEADERS "include/Core/*.hpp" "include/API/*.hpp" "include/Benchmarks/*.hpp")
file(GLOB_RECURSE CORE_SOURCES "src/Core/*.cpp" "src/API/*.cpp" "src/Benchmarks/*.cpp")

add_library(ExchangeCoreLib STATIC ${CORE_HEADERS} ${CORE_SOURCES})
target_include_directories(ExchangeCoreLib PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Compiler-specific warnings and options
if(MSVC)
    target_compile_options(ExchangeCoreLib PRIVATE /W4 /WX /utf-8 /wd4267 /wd4244)
else()
    target_compile_options(ExchangeCoreLib PRIVATE -Wall -Wextra -Werror -Wno-unused-parameter)
endif()

# Define TUI & App Target
file(GLOB_RECURSE TUI_HEADERS "src/TUI/*.hpp" "src/TUI/*.h")
file(GLOB_RECURSE TUI_SOURCES "src/TUI/*.cpp")
file(GLOB APP_SOURCES "app/*.cpp")

add_executable(ExchangeCoreApp ${TUI_HEADERS} ${TUI_SOURCES} ${APP_SOURCES})
target_include_directories(ExchangeCoreApp PRIVATE 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/TUI
)
target_link_libraries(ExchangeCoreApp PRIVATE 
    ExchangeCoreLib
    ftxui::screen
    ftxui::dom
    ftxui::component
)

# Define Benchmarks Target
file(GLOB BENCHMARK_SOURCES "benchmarks/*.cpp" "benchmarks/*.hpp")
add_executable(ExchangeCoreBenchmarks ${BENCHMARK_SOURCES})
target_include_directories(ExchangeCoreBenchmarks PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ExchangeCoreBenchmarks PRIVATE ExchangeCoreLib)

# Define Tests Target
file(GLOB TEST_SOURCES "tests/*.cpp" "tests/*.hpp")
add_executable(ExchangeCoreTests ${TEST_SOURCES})
target_include_directories(ExchangeCoreTests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(ExchangeCoreTests PRIVATE 
    ExchangeCoreLib
    gtest
    gtest_main
)

# Enable testing
enable_testing()
add_test(NAME CoreTests COMMAND ExchangeCoreTests)
