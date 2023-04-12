cmake_minimum_required(VERSION 3.14)

project(dsoal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P MATCHES "8")
        set(DEF_FILE msvc64.def)
    else()
        set(DEF_FILE msvc.def)
    endif()
else()
    set(DEF_FILE mingw.def)
endif(MSVC)

if(MINGW)
    set(CMAKE_RC_COMPILER_INIT windres)
    set(CMAKE_RC_COMPILE_OBJECT
        "<CMAKE_RC_COMPILER> -O coff <DEFINES> -i <SOURCE> -o <OBJECT>")
endif(MINGW)
enable_language(RC)

add_library(dsound SHARED
  src/dsoal_global.h
  src/dsoal.cpp
  src/dsoal.h
  version.rc
  ${DEF_FILE}
)
set_target_properties(dsound PROPERTIES PREFIX "")
target_compile_definitions(dsound PRIVATE DSOAL_LIBRARY)
