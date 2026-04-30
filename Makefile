# Legacy Makefile — use only if you have the old fxSDK (pre-CMake).
# For the modern fxSDK, run:  fxsdk build cg

NAME     := Bomberman2P
SOURCES  := src/main.c
LIBS     := -lgint-cg -lc
TOOLCHAIN:= sh-elf

include $(shell fxsdk path)/Makefile.cfg
