# CMakeList.txt : CMake project for level-editor, include source and define
# project specific logic here.
#

find_package(SDL3 REQUIRED)
find_package(SDL3_image REQUIRED)
find_package(glm REQUIRED)
# Add source to this project's executable.
add_executable (level-editor "level-editor.cpp" "level-editor.h"   )

if (CMAKE_VERSION VERSION_GREATER 3.12)
  set_property(TARGET level-editor PROPERTY CXX_STANDARD 20)
endif()


# TODO: Add tests and install targets if needed.
target_link_libraries(level-editor SDL3::SDL3 SDL3_image::SDL3_image glm::glm)
