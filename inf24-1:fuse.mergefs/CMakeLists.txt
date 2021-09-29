project(24-1)

set(CMAKE_CXX_STANDARD 17)
add_compile_options(${FUSE_CFLAGS_OTHER})
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize=leak -g")
find_package(PkgConfig REQUIRED)
pkg_check_modules(FUSE REQUIRED fuse3)
include_directories(${FUSE_INCLUDE_DIRS}) # -I/usr/include/fuse3

add_executable(24-1 24-1.cpp)

target_link_libraries(24-1 ${FUSE_LIBRARIES})  # -lfuse3 -lpthread
