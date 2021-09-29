cmake_minimum_required(VERSION 3.16)
project(CAOSextended)


file(GLOB subDirectories RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} *)
foreach(dir ${subDirectories})
    if(IS_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/${dir}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/CMakeLists.txt")
            add_subdirectory(${dir})
        endif()
    endif()
endforeach(dir)

add_executable(my_test macro_check/test.c)

