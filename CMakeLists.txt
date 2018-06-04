cmake_minimum_required(VERSION 3.0.2)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckCCompilerFlag)
include(CheckIncludeFile)

project(DSOAL C)


IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
ENDIF()

set(DSOAL_DEFS "")
set(DSOAL_INC "")
set(DSOAL_FLAGS "")
set(DSOAL_LIBS "")

set(DSOAL_OBJS buffer.c
               capture.c
               dsound8.c
               dsound_main.c
               duplex.c
               primary.c
               propset.c)

find_package(OpenAL REQUIRED)
set(DSOAL_INC ${DSOAL_INC} ${DSOAL_BINARY_DIR} ${OPENAL_INCLUDE_DIR})

if(NOT MSVC)
    set(DSOAL_FLAGS ${DSOAL_FLAGS} -Winline -Wall)
    check_c_compiler_flag(-Wextra HAVE_W_EXTRA)
    if(HAVE_W_EXTRA)
        set(DSOAL_FLAGS ${DSOAL_FLAGS} -Wextra)
    endif()
endif()
if(NOT WIN32)
    check_c_compiler_flag(-fvisibility=hidden HAVE_FVISIBILITY_HIDDEN)
    if(HAVE_FVISIBILITY_HIDDEN)
        set(DSOAL_FLAGS ${DSOAL_FLAGS} -fvisibility=hidden)
    endif()
else()
    if(MINGW)
        set(CMAKE_RC_COMPILER_INIT windres)
        enable_language(RC)
        set(CMAKE_RC_COMPILE_OBJECT
            "<CMAKE_RC_COMPILER> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
    endif(MINGW)

    set(DSOAL_OBJS ${DSOAL_OBJS} debug.c version.rc)

    set(DSOAL_DEFS ${DSOAL_DEFS} _WIN32 WINVER=0x0600 DEBUG_INFO)

    set(DSOAL_LIBS ${DSOAL_LIBS} ${OPENAL_LIBRARY} dxguid uuid winmm ole32 dxerr8
        "${DSOAL_SOURCE_DIR}/dsound-mingw.def")
endif()
set(DSOAL_DEFS ${DSOAL_DEFS} COBJMACROS)

add_library(dsound SHARED ${DSOAL_OBJS})
if(WIN32)
    set_target_properties(dsound PROPERTIES PREFIX "")
endif()
target_compile_definitions(dsound PRIVATE ${DSOAL_DEFS})
target_include_directories(dsound PRIVATE ${DSOAL_INC})
target_compile_options(dsound PRIVATE ${DSOAL_FLAGS})
target_link_libraries(dsound PRIVATE ${DSOAL_LIBS})
