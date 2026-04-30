cmake_minimum_required(VERSION 3.15)
project(Bomberman2P)

include(GenerateG3A)

add_executable(bomberman2p src/main.c)

target_compile_options(bomberman2p PRIVATE
    -Os
    -Wall
    -Wextra
)

target_link_libraries(bomberman2p Gint::Gint)

GenerateG3A(bomberman2p
    OUTPUT  "Bomberman2P.g3a"
    NAME    "Bomberman2P"
    ICON    "${PROJECT_SOURCE_DIR}/icon.png"
)
