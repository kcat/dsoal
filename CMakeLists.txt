cmake_minimum_required(VERSION 3.14)

project(dsoal LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(LINKER_FLAGS )

if(MSVC)
    if(CMAKE_SIZEOF_VOID_P MATCHES "8")
        set(DEF_FILE msvc64.def)
    else()
        set(DEF_FILE msvc.def)
    endif()
else()
    set(DEF_FILE mingw.def)

    set(LINKER_FLAGS ${LINKER_FLAGS} -static-libgcc -static-libstdc++)
    set(LINKER_FLAGS ${LINKER_FLAGS} "-Wl,--push-state,-Bstatic,-lwinpthread,--pop-state")
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
  src/logging.cpp
  src/logging.h
  version.rc
  ${DEF_FILE}
)
set_target_properties(dsound PROPERTIES PREFIX "")
target_link_libraries(dsound PRIVATE ${LINKER_FLAGS})
target_compile_definitions(dsound PRIVATE DSOAL_LIBRARY)
