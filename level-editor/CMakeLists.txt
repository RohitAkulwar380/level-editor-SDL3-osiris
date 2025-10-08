# Specify the minimum version of CMake required
cmake_minimum_required(VERSION 3.10)

# Define the project name and set C++ standard
project(level_editor VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# --- Find SDL3 ---
# This attempts to locate the SDL3 development files required by the project.
# NOTE: CMAKE_PREFIX_PATH is commonly used to hint where libraries like SDL3 are installed.
# We explicitly set a variable that can be overridden, often needed when the SDK is not installed in a standard system path.
# You will still need to define the value of SDL3_INSTALL_DIR when running CMake if SDL3 isn't found automatically.
if (NOT SDL3_INSTALL_DIR)
    set(SDL3_INSTALL_DIR "" CACHE PATH "Path to the root directory of your SDL3 installation (e.g., D:\\SDL3-devel-3.2.20-VC\\SDL3-3.2.20)")
endif()

if (SDL3_INSTALL_DIR)
    list(APPEND CMAKE_PREFIX_PATH ${SDL3_INSTALL_DIR})
endif()

find_package(SDL3 REQUIRED)

# --- Create Executable ---
# Add the executable target, pointing to the source file
add_executable(level_editor level-editor.cpp)

# --- Link Libraries and Include Directories ---
# Link the SDL3 library to the executable. 
# SDL3::SDL3 is the official target name provided by the FindSDL3.cmake module.
target_link_libraries(level_editor PRIVATE SDL3::SDL3)
