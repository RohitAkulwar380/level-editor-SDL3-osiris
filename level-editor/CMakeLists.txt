cmake_minimum_required(VERSION 3.10)

project(level_editor CXX)

# Optional: Add a custom hint variable for the install location
if(SDL3_INSTALL_DIR)
    set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} ${SDL3_INSTALL_DIR})
endif()

# Find SDL3
find_package(SDL3 REQUIRED)

# Find SDL3_image (CRUCIAL ADDITION)


add_executable(level_editor level-editor.cpp)

# Link SDL3 and SDL3_image (CRUCIAL ADDITION)
target_link_libraries(level_editor PRIVATE 
    SDL3::SDL3 

)
