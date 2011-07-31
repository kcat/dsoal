cmake_minimum_required(VERSION 2.6)

SET(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckCCompilerFlag)
include(CheckIncludeFile)

project(DSOAL C)

if(NOT WIN32)
    find_path(WINE_DIR include/wine/library.h
              PATHS /usr /usr/local)
    if(NOT WINE_DIR)
        message(FATAL_ERROR "Could not find Wine installation directory")
    endif()
    message(STATUS "Found Wine installation directory - ${WINE_DIR}")
    include_directories("${WINE_DIR}/include/wine" "${WINE_DIR}/include/wine/windows")
    set(CMAKE_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}" "${WINE_DIR}/include/wine" "${WINE_DIR}/include/wine/windows")
endif()

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

check_include_file(dsound.h HAVE_DSOUND_H)
if(NOT HAVE_DSOUND_H)
    message(FATAL_ERROR "Could not find dsound.h")
endif()

if(WIN32)
    if(NOT WINDRES)
        if(HOST)
            set(WINDRES "${HOST}-windres")
        else()
            set(WINDRES "windres")
        endif()
    endif()

    add_definitions(-D_WIN32 -DDEBUG_INFO)

    set(DSOAL_OBJS ${DSOAL_OBJS} debug.c)

    add_library(${LIBNAME} SHARED ${DSOAL_OBJS})
    add_custom_command(TARGET ${LIBNAME} PRE_BUILD
                       COMMAND "${WINDRES}" -o version.o -i "${DSOAL_SOURCE_DIR}/version.rc"
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}"
                       COMMENT "Generating version.o" VERBATIM)
    target_link_libraries(${LIBNAME} ${OPENAL_LIBRARY} dxguid uuid winmm ole32 dxerr8)
    set_target_properties(${LIBNAME} PROPERTIES PREFIX ""
                                     LINK_FLAGS "version.o ${DSOAL_SOURCE_DIR}/dsound-mingw.def")
else()
    add_definitions(-D__WINESRC__)

    add_library(${LIBNAME} STATIC ${DSOAL_OBJS})
    set_target_properties(${LIBNAME} PROPERTIES PREFIX "lib" SUFFIX ".a"
                                     COMPILE_FLAGS -fPIC)

    add_custom_command(TARGET ${LIBNAME} POST_BUILD
                       COMMAND wrc --nostdinc -D__WINESRC__ "-I${DSOAL_BINARY_DIR}" "-I${WINE_DIR}/include/wine" "-I${WINE_DIR}/include/wine/windows" -foversion.res ${DSOAL_SOURCE_DIR}/version.rc
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}"
                       COMMENT "Generating version.res" VERBATIM)
    add_custom_command(TARGET ${LIBNAME} POST_BUILD
                       COMMAND widl -D__WINESRC__ "-I${DSOAL_BINARY_DIR}" "-I${WINE_DIR}/include/wine" "-I${WINE_DIR}/include/wine/windows" -r -o "${LIBNAME}_classes_r.res" "${DSOAL_SOURCE_DIR}/${LIBNAME}_classes.idl"
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}"
                       COMMENT "Generating ${LIBNAME}_classes_r.res" VERBATIM)
    add_custom_command(TARGET ${LIBNAME} POST_BUILD
                       COMMAND winebuild -w --def -o lib${LIBNAME}.def --export ${DSOAL_SOURCE_DIR}/${LIBNAME}.spec
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}"
                       COMMENT "Generating lib${LIBNAME}.def" VERBATIM)
    add_custom_command(TARGET ${LIBNAME} POST_BUILD
                       COMMAND winegcc -shared "${DSOAL_SOURCE_DIR}/${LIBNAME}.spec" "${LIBNAME}_classes_r.res" version.res -o "${DSOAL_BINARY_DIR}/${LIBNAME}.dll.so" "lib${LIBNAME}.a" -lopenal -ldxguid -luuid -lwinmm -lole32 -ldxerr8
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}"
                       COMMENT "Linking Wine shared library ${LIBNAME}.dll.so" VERBATIM)
    add_custom_command(TARGET ${LIBNAME} POST_BUILD
                       COMMAND winegcc -shared "${DSOAL_SOURCE_DIR}/${LIBNAME}.spec" "${LIBNAME}_classes_r.res" version.res -o "${DSOAL_BINARY_DIR}/${LIBNAME}.dll.fake" "lib${LIBNAME}.a" -lopenal -ldxguid -luuid -lwinmm -lole32 -ldxerr8
                       WORKING_DIRECTORY "${DSOAL_BINARY_DIR}"
                       COMMENT "Creating ${LIBNAME}.dll.fake" VERBATIM)

    install(FILES "${DSOAL_BINARY_DIR}/${LIBNAME}.dll.so"
            DESTINATION "${WINE_DIR}/lib${LIB_SUFFIX}/wine")
    install(FILES "${DSOAL_BINARY_DIR}/lib${LIBNAME}.def"
            DESTINATION "${WINE_DIR}/lib${LIB_SUFFIX}/wine")
    install(FILES "${DSOAL_BINARY_DIR}/${LIBNAME}.dll.fake"
            DESTINATION "${WINE_DIR}/lib${LIB_SUFFIX}/wine/fakedlls"
            RENAME ${LIBNAME}.dll)
endif()
