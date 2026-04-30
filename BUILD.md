# Building Bomberman2P.g3a

## Prerequisites

Install the fxSDK + gint toolchain (Linux/WSL/macOS):

```
git clone https://gitea.planet-casio.com/Lephenixnoir/fxsdk
cd fxsdk && cmake -B build && cmake --build build && cmake --install build
```

The installer also sets up the `sh-elf-gcc` cross-compiler. Follow the full
guide on Planet Casio: https://www.planet-casio.com/Fr/forums/topic17512-1

## Icon

Create `icon.png` — a **48 × 48** pixel PNG with at most 3 colours
(Casio add-in icon format). A plain black image works fine for testing:

```
convert -size 48x48 xc:black icon.png   # requires ImageMagick
```

## Build

```
fxsdk build cg          # CMake workflow (modern fxSDK)
# — or —
make                    # legacy Makefile workflow
```

Output: `build-cg/Bomberman2P.g3a`  (or `Bomberman2P.g3a` with make)

## Install on calculator

1. Connect the fx-CG100 via USB and select "USB Flash Drive" on the calc.
2. Copy `Bomberman2P.g3a` to the root of the calculator's storage.
3. The add-in appears in the main menu — press it to launch.

## Controls

| Action | Player 1      | Player 2  |
|--------|---------------|-----------|
| Move   | Arrow keys    | 8 / 2 / 4 / 6 |
| Bomb   | EXE           | 5         |

## Adjusting difficulty

Open `src/main.c` and change these constants near the top:

| Constant    | Default | Effect                              |
|-------------|---------|-------------------------------------|
| BOMB_FUSE   | 90      | Ticks until bomb explodes (lower = faster) |
| FIRE_LIFE   | 22      | How long fire stays on screen       |
| MOVE_DELAY  | 7       | Ticks between auto-repeat steps     |
