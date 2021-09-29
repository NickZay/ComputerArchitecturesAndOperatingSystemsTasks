project(25-1)

set(CMAKE_C_FLAGS "-std=gnu11")
set(CMAKE_C_STANDARD 11)

find_package(PkgConfig REQUIRED)
pkg_search_module(OPENSSL REQUIRED openssl)

add_executable(25-1 25-1.c)

target_include_directories(25-1 PUBLIC ${OPENSSL_INCLUDE_DIRS})
target_link_libraries(25-1 ${OPENSSL_LIBRARIES})
