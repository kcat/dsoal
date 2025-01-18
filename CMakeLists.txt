cmake_minimum_required(VERSION 3.14)

project(dsoal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(fmt-11.1.1)

include(CheckCXXCompilerFlag)

set(DSOAL_CPPDEFS )
set(DSOAL_CFLAGS )
set(DSOAL_LINK_FLAGS )

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P MATCHES "8")
        set(DEF_FILE msvc64.def)
    else()
        set(DEF_FILE msvc.def)
    endif()

    set(DSOAL_CPPDEFS ${DSOAL_CPPDEFS} _CRT_SECURE_NO_WARNINGS NOMINMAX)

    check_cxx_compiler_flag(/permissive- HAVE_PERMISSIVE_SWITCH)
    if(HAVE_PERMISSIVE_SWITCH)
        set(DSOAL_CFLAGS ${DSOAL_CFLAGS} $<$<COMPILE_LANGUAGE:CXX>:/permissive->)
    endif()
    set(DSOAL_CFLAGS ${DSOAL_CFLAGS} /W4 /w14640 /wd4065 /wd4127 /wd4268 /wd4324 /wd5030 /wd5051
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>)
else()
    set(DEF_FILE mingw.def)

    set(DSOAL_CFLAGS ${DSOAL_CFLAGS} -Winline -Wunused -Wall -Wextra -Wshadow -Wconversion
        -Wcast-align -Wpedantic
        $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast -Wnon-virtual-dtor -Woverloaded-virtual>)

    set(DSOAL_LINK_FLAGS ${DSOAL_LINK_FLAGS} -static-libgcc -static-libstdc++)
    set(DSOAL_LINK_FLAGS ${DSOAL_LINK_FLAGS} "-Wl,--push-state,-Bstatic,-lstdc++,-lwinpthread,--pop-state")
endif(MSVC)

if(MINGW)
    set(CMAKE_RC_COMPILER_INIT windres)
    set(CMAKE_RC_COMPILE_OBJECT
        "<CMAKE_RC_COMPILER> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
endif(MINGW)
enable_language(RC)

set(SRC_OBJS
    src/AL/al.h
    src/AL/alc.h
    src/AL/alext.h
    src/AL/efx.h
    src/buffer.cpp
    src/buffer.h
    src/capture.cpp
    src/capture.h
    src/comhelpers.h
    src/comptr.h
    src/dsoal_global.h
    src/dsoal.cpp
    src/dsoal.h
    src/dsoundoal.cpp
    src/dsoundoal.h
    src/eax.cpp
    src/eax.h
    src/enumerate.h
    src/expected.h
    src/factory.cpp
    src/factory.h
    src/fullduplex.cpp
    src/fullduplex.h
    src/guidprinter.h
    src/logging.cpp
    src/logging.h
    src/primarybuffer.cpp
    src/primarybuffer.h
    src/propset.cpp
    src/propset.h
    src/vmanager.h)

add_library(dsound SHARED ${SRC_OBJS} version.rc ${DEF_FILE})
set_target_properties(dsound PROPERTIES PREFIX "")
target_link_libraries(dsound PRIVATE dsoal::fmt ${DSOAL_LINK_FLAGS})
target_compile_definitions(dsound PRIVATE DSOAL_LIBRARY ${DSOAL_CPPDEFS})
target_compile_options(dsound PRIVATE ${DSOAL_CFLAGS})

set(NEED_ANALYZE_SOURCE_FILES "")
foreach(obj ${SRC_OBJS})
    list(APPEND NEED_ANALYZE_SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/${obj}")
endforeach()
set(CLANG_TIDY_EXECUTABLE "clang-tidy")
if(DEFINED ENV{CLANG_TIDY_EXECUTABLE})
    set(CLANG_TIDY_EXECUTABLE $ENV{CLANG_TIDY_EXECUTABLE})
endif()
add_custom_target(clang-tidy-check ${CLANG_TIDY_EXECUTABLE} -format-style=file
    -p ${CMAKE_BINARY_DIR}/compile_commands.json ${NEED_ANALYZE_SOURCE_FILES}
    DEPENDS ${NEED_ANALYZE_SOURCE_FILES} ${CMAKE_BINARY_DIR}/compile_commands.json)
