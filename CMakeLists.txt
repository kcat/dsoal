cmake_minimum_required(VERSION 2.6)

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckCCompilerFlag)
include(CheckIncludeFile)

project(DSOAL C)

set(LIBNAME dsound)

IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
ENDIF()

set(DSOAL_OBJS buffer.c
               capture.c
               dsound8.c
               dsound_main.c
               duplex.c
               primary.c
               propset.c)

find_package(OpenAL REQUIRED)
include_directories("${DSOAL_BINARY_DIR}" ${OPENAL_INCLUDE_DIR})

if(NOT MSVC)
    add_definitions(-Winline -Wall)
    check_c_compiler_flag(-Wextra HAVE_W_EXTRA)
    if(HAVE_W_EXTRA)
        add_definitions(-Wextra)
    endif()
    if(WIN32)
        check_c_compiler_flag(-Wformat=0 HAVE_W_FORMAT_ZERO)
        if(HAVE_W_FORMAT_ZERO)
            add_definitions(-Wformat=0)
        endif()
    endif()
endif()

if(WIN32)
    check_include_file(dsound.h HAVE_DSOUND_H)
    if(NOT HAVE_DSOUND_H)
        message(FATAL_ERROR "Could not find dsound.h")
    endif()

    add_definitions(-D_WIN32 -DDEBUG_INFO)

    set(DSOAL_OBJS ${DSOAL_OBJS} debug.c)

    add_library(${LIBNAME} SHARED ${DSOAL_OBJS})
    target_link_libraries(${LIBNAME} ${OPENAL_LIBRARY} dxguid uuid winmm ole32 dxerr8)
    set_target_properties(${LIBNAME} PROPERTIES PREFIX ""
                                                LINK_FLAGS "${DSOAL_SOURCE_DIR}/dsound-mingw.def")
else()
    find_path(WINE_INCLUDE_DIR library.h
              PATHS
              /usr/include/wine
              /usr/local/include/wine)
    if(NOT WINE_INCLUDE_DIR)
        message(FATAL_ERROR "Could not find Wine header files")
    endif()
    message(STATUS "Found Wine header files - ${WINE_INCLUDE_DIR}")
    add_definitions(-D__WINESRC__ "-I${WINE_INCLUDE_DIR}" "-I${WINE_INCLUDE_DIR}/windows")
    set(CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}" "${WINE_INCLUDE_DIR}" "${WINE_INCLUDE_DIR}/windows")

    check_include_file(dsound.h HAVE_DSOUND_H)
    if(NOT HAVE_DSOUND_H)
        message(FATAL_ERROR "Could not find dsound.h")
    endif()

    add_custom_command(OUTPUT version.res
                       COMMAND wrc --nostdinc -D__WINESRC__ "-I${DSOAL_BINARY_DIR}" "-I${WINE_INCLUDE_DIR}" "-I${WINE_INCLUDE_DIR}/windows" -foversion.res ${DSOAL_SOURCE_DIR}/version.rc
                       DEPENDS "${DSOAL_SOURCE_DIR}/version.rc"
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}/CMakeFiles/dsoal.dir"
                       COMMENT "Generating version.res..." VERBATIM)

    add_library(dsoal STATIC ${DSOAL_OBJS})
    set_target_properties(dsoal PROPERTIES PREFIX "lib" SUFFIX ".a"
                                COMPILE_FLAGS -fPIC)

    set(OBJECT_FILES)
    foreach(SRC ${DSOAL_OBJS})
        string(REGEX REPLACE ".c" ".o" OBJ "${SRC}")
        set(OBJECT_FILES "${OBJECT_FILES}" "${OBJ}")
    endforeach()

    add_custom_command(OUTPUT "${DSOAL_BINARY_DIR}/dsound.dll.so"
                       COMMAND winegcc -shared "${DSOAL_SOURCE_DIR}/dsound.spec" version.res -o "${DSOAL_BINARY_DIR}/dsound.dll.so" "${DSOAL_BINARY_DIR}/libdsoal.a" -lopenal -ldxguid -luuid -lwinmm -lole32 -ldxerr8
                       DEPENDS version.res "${DSOAL_SOURCE_DIR}/dsound.spec" "${DSOAL_BINARY_DIR}/libdsoal.a"
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}/CMakeFiles/dsoal.dir"
                       COMMENT "Linking C shared library dsound.dll.so" VERBATIM)

    add_custom_command(OUTPUT "${DSOAL_BINARY_DIR}/dsound.dll.fake"
                       COMMAND winegcc -shared "${DSOAL_SOURCE_DIR}/dsound.spec" version.res -o "${DSOAL_BINARY_DIR}/dsound.dll.fake" "${DSOAL_BINARY_DIR}/libdsoal.a" -lopenal -ldxguid -luuid -lwinmm -lole32 -ldxerr8
                       DEPENDS version.res "${DSOAL_SOURCE_DIR}/dsound.spec" "${DSOAL_BINARY_DIR}/libdsoal.a"
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}/CMakeFiles/dsoal.dir"
                       COMMENT "Linking C shared library dsound.dll.fake" VERBATIM)

    add_custom_target(dsound ALL DEPENDS "${DSOAL_BINARY_DIR}/dsound.dll.so" "${DSOAL_BINARY_DIR}/dsound.dll.fake")
endif()
