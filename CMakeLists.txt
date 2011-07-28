cmake_minimum_required(VERSION 2.6)

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

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
               propset.c
               regsvr.c)

find_package(OpenAL)
include_directories("${DSOAL_BINARY_DIR}" ${OPENAL_INCLUDE_DIR})

if(WIN32)
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
        message(FATAL_ERROR "Could not find Wine header files" )
    endif()
    message(STATUS "Found Wine header files - ${WINE_INCLUDE_DIR}" )

    set(DSOAL_CPP_FLAGS -D__WINESRC__ "-I${DSOAL_BINARY_DIR}" "-I${OPENAL_INCLUDE_DIR}")
    set(DSOAL_C_FLAGS -g -O2 -Wall -pipe -fno-strict-aliasing -Wdeclaration-after-statement -Wstrict-prototypes -Wwrite-strings -Wtype-limits -Wpointer-arith)

    add_custom_command(OUTPUT version.res
                       COMMAND wrc --nostdinc ${DSOAL_CPP_FLAGS} "-I${WINE_INCLUDE_DIR}" "-I${WINE_INCLUDE_DIR}/windows" -foversion.res ${DSOAL_SOURCE_DIR}/version.rc
                       DEPENDS "${DSOAL_SOURCE_DIR}/version.rc"
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}/CMakeFiles/dsound.dir"
                       COMMENT "Generating version.res..." VERBATIM)

    set(OBJECT_FILES)
    foreach(SRC ${DSOAL_OBJS})
        string(REGEX REPLACE ".c" ".o" OBJ "${SRC}")
        add_custom_command(OUTPUT "${OBJ}"
                           COMMAND winegcc ${DSOAL_CPP_FLAGS} ${DSOAL_C_FLAGS} -o "${OBJ}" -c "${DSOAL_SOURCE_DIR}/${SRC}"
                           DEPENDS "${DSOAL_SOURCE_DIR}/${SRC}"
                           WORKING_DIRECTORY "${DSOAL_BINARY_DIR}/CMakeFiles/dsound.dir"
                           COMMENT "Building C object CMakeFiles/dsound.dir/${OBJ}" VERBATIM)
        set(OBJECT_FILES "${OBJECT_FILES}" "${OBJ}")
    endforeach()

    add_custom_command(OUTPUT "${DSOAL_BINARY_DIR}/dsound.dll.so"
                       COMMAND winegcc -shared "${DSOAL_SOURCE_DIR}/dsound.spec" ${OBJECT_FILES} version.res -o "${DSOAL_BINARY_DIR}/dsound.dll.so" -lopenal -ldxguid -luuid -lwinmm -lole32 -ldxerr8
                       DEPENDS ${OBJECT_FILES} version.res
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}/CMakeFiles/dsound.dir"
                       COMMENT "Linking C shared library dsound.dll.so" VERBATIM)

    add_custom_target(dsound ALL DEPENDS "${DSOAL_BINARY_DIR}/dsound.dll.so")
endif()
