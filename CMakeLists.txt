cmake_minimum_required(VERSION 3.0.2)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CheckTypeSize)
include(CheckIncludeFile)
include(GNUInstallDirs)

project(DSOAL C)

set(VERSION 0.9)

IF(NOT CMAKE_BUILD_TYPE)
    SET(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING
        "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel."
        FORCE)
ENDIF()

set(DSOAL_DEFS "")
set(DSOAL_INC "")
set(DSOAL_FLAGS "")
set(DSOAL_LIBS "")

set(DSOAL_TEXT LICENSE
               README.md)

set(DSOAL_OBJS buffer.c
               capture.c
               chorus.c
               dsound8.c
               dsound_main.c
               dsound_private.h
               duplex.c
               eax.c
               eax4.c
               eax.h
               eax3.h
               eax4.h
               eax-presets.h
               primary.c
               propset.c
               reverb.c)

set(DSOAL_INC ${DSOAL_INC} ${DSOAL_BINARY_DIR} ${DSOAL_SOURCE_DIR}/include/AL)


check_type_size("long" SIZEOF_LONG)
set(DSOAL_DEFS ${DSOAL_DEFS} "SIZEOF_LONG=${SIZEOF_LONG}")

check_c_source_compiles("#include <intrin.h>
int main()
{
    unsigned long idx = 0;
    _BitScanForward64(&idx, 1);
    return idx;
}" HAVE_BITSCANFORWARD64_INTRINSIC)
if(HAVE_BITSCANFORWARD64_INTRINSIC)
    set(DSOAL_DEFS ${DSOAL_DEFS} HAVE_BITSCANFORWARD64_INTRINSIC)
else()
    check_c_source_compiles("#include <intrin.h>
    int main()
    {
        unsigned long idx = 0;
        _BitScanForward(&idx, 1);
        return idx;
    }" HAVE_BITSCANFORWARD_INTRINSIC)
    if(HAVE_BITSCANFORWARD_INTRINSIC)
        set(DSOAL_DEFS ${DSOAL_DEFS} HAVE_BITSCANFORWARD_INTRINSIC)
    endif()
endif()

# MSVC workaround for C99 inline
if(MSVC)
    check_c_source_compiles("inline void foo(void) { }
                             int main() {return 0;}" HAVE_INLINE)
    if(NOT HAVE_INLINE)
        check_c_source_compiles("__inline void foo(void) { }
                                 int main() {return 0;}" HAVE___INLINE)
        if(NOT HAVE___INLINE)
            message(FATAL_ERROR "No inline keyword found, please update your MSVC version!")
        endif()

        set(DSOAL_DEFS ${DSOAL_DEFS} inline=__inline)
        set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -Dinline=__inline")
    endif()
endif()

if(NOT MSVC)
    set(DSOAL_FLAGS ${DSOAL_FLAGS} -Winline -Wall)
    check_c_compiler_flag(-Wextra HAVE_W_EXTRA)
    if(HAVE_W_EXTRA)
        set(DSOAL_FLAGS ${DSOAL_FLAGS} -Wextra)
    endif()
    set(DSOAL_LIBS ${DSOAL_LIBS} -static-libgcc)
endif()
if(NOT WIN32)
    check_c_compiler_flag(-fvisibility=hidden HAVE_FVISIBILITY_HIDDEN)
    if(HAVE_FVISIBILITY_HIDDEN)
        set(DSOAL_FLAGS ${DSOAL_FLAGS} -fvisibility=hidden)
    endif()
else()
    if(MINGW)
        set(CMAKE_RC_COMPILER_INIT windres)
        set(CMAKE_RC_COMPILE_OBJECT
            "<CMAKE_RC_COMPILER> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
    endif(MINGW)
    enable_language(RC)

    set(DSOAL_OBJS ${DSOAL_OBJS} debug.c version.rc)

    set(DSOAL_DEFS ${DSOAL_DEFS} _WIN32 WINVER=0x0600 DEBUG_INFO)

    set(DSOAL_LIBS ${DSOAL_LIBS} winmm ole32)
endif()
set(DSOAL_DEFS ${DSOAL_DEFS} COBJMACROS)

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P MATCHES "8")
        add_library(dsound SHARED ${DSOAL_OBJS} msvc64.def)
    else()
        add_library(dsound SHARED ${DSOAL_OBJS} msvc.def)
    endif()
    set(DSOAL_DEFS ${DSOAL_DEFS} _CRT_SECURE_NO_WARNINGS)
    set(DSOAL_FLAGS ${DSOAL_FLAGS} /W3 /wd4090 /wd4113)
else()
    add_library(dsound SHARED ${DSOAL_OBJS} mingw.def)
endif(MSVC)

if(WIN32)
    set_target_properties(dsound PROPERTIES PREFIX "")
endif()
target_compile_definitions(dsound PRIVATE ${DSOAL_DEFS})
target_include_directories(dsound PRIVATE ${DSOAL_INC})
target_compile_options(dsound PRIVATE ${DSOAL_FLAGS})
target_link_libraries(dsound PRIVATE ${DSOAL_LIBS})

install(TARGETS dsound
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
if(EXISTS "${DSOAL_BINARY_DIR}/dsoal-aldrv.dll")
    install(FILES
        "${DSOAL_BINARY_DIR}/dsoal-aldrv.dll"
        DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

target_sources(dsound PRIVATE ${DSOAL_TEXT})
install(FILES ${DSOAL_TEXT} TYPE DATA)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_VENDOR "Chris Robinson")
set(CPACK_PACKAGE_VERSION ${VERSION})
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES TRUE)
set(CPACK_OUTPUT_FILE_PREFIX packages)

include(CPack)
