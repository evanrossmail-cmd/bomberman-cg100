# CMakeLists.txt for Bomberman2P
# Build with: fxsdk build-cg

cmake_minimum_required(VERSION 3.15)
project(Bomberman2P)

include(GenerateG1A)
include(GenerateG3A)
include(Fxconv)
find_package(Gint 2.9 REQUIRED)

set(SOURCES
  src/main.c
)

set(ASSETS)

fxconv_declare_assets(${ASSETS} WITH_METADATA)

add_executable(bomberman2p ${SOURCES} ${ASSETS})

target_compile_options(bomberman2p PRIVATE -Wall -Os)
target_link_libraries(bomberman2p Gint::Gint)

if("${FXSDK_PLATFORM_LONG}" STREQUAL fxCG50)
  generate_g3a(TARGET bomberman2p OUTPUT "Bomberman2P.g3a"
    NAME "Bomberman2P" ICONS icon.png icon-sel.png)
endif()
