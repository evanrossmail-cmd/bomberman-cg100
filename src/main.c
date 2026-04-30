/* Bomberman 2P — Casio fx-CG100 add-in
 *
 * Player 1 : Arrow keys to move,  EXE  to drop bomb
 * Player 2 : 8/2/4/6 to move,      5   to drop bomb
 *
 * Build with fxSDK + gint:
 *   fxsdk build cg          (CMake workflow)
 *   - or -  make            (legacy Makefile workflow)
 */

#include <gint/display.h>
#include <gint/keyboard.h>
#include <gint/timer.h>
#include <gint/gint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* ── Screen / grid layout ─────────────────────────────────────── */
#define SW  396         /* screen width  (fx-CG50/CG100 with gint) */
#define SH  224         /* screen height */
#define GW  13          /* grid columns  */
#define GH  9           /* grid rows     */
#define TW  28          /* tile pixel width  */
#define TH  22          /* tile pixel height */
#define OX  ((SW - GW*TW) / 2)   /* grid x offset */
#define OY  4                     /* grid y offset */

/* ── Colours (RGB565 via gint's C_RGB macro: 0-31 per channel) ─ */
#define COL_GROUND   C_RGB(4, 16, 4)
#define COL_WALL_D   C_RGB(8,  8,  8)
#define COL_WALL_L   C_RGB(12, 12, 12)
#define COL_BRICK_D  C_RGB(18, 10, 4)
#define COL_BRICK_L  C_RGB(24, 15, 7)
#define COL_FIRE_O   C_RGB(31, 10, 0)
#define COL_FIRE_Y   C_RGB(31, 27, 0)
#define COL_BOMB     C_RGB(3,  3,  3)
#define COL_FUSE_A   C_RGB(31, 27, 0)
#define COL_FUSE_B   C_RGB(31, 16, 0)
#define COL_P1       C_RGB(28, 4,  4)
#define COL_P2       C_RGB(4,  10, 28)
#define COL_DEAD     C_RGB(10, 10, 10)
#define COL_HUD      C_RGB(2,  2,  2)

/* ── Tile types ───────────────────────────────────────────────── */
#define T_EMPTY  0
#define T_WALL   1   /* indestructible */
#define T_BRICK  2   /* destructible   */

/* ── Tuning constants ─────────────────────────────────────────── */
#define BOMB_FUSE   90   /* game ticks until explosion  */
#define FIRE_LIFE   22   /* game ticks fire persists    */
#define MOVE_DELAY   7   /* ticks between held-key steps */
#define MAX_BOMBS   10
#define MAX_FIRES   80

/* ── Data types ───────────────────────────────────────────────── */
typedef struct {
    int8_t  r, c;
    int16_t timer;
    int8_t  range;
    int8_t  owner;   /* 0 = P1, 1 = P2 */
} Bomb;

typedef struct {
    int8_t  r, c;
    int8_t  dur;
} Fire;

typedef struct {
    int8_t  r, c;
    bool    alive;
    int8_t  max_bombs;
    int8_t  blast;
    int8_t  move_cd;  /* cooldown for auto-repeat movement */
} Player;

/* ── Global state ─────────────────────────────────────────────── */
static uint8_t grid[GH][GW];
static Bomb    bombs[MAX_BOMBS];
static int     nbombs;
static Fire    fires[MAX_FIRES];
static int     nfires;
static Player  p1, p2;

/* ── Tick counter (set by timer callback at ~30 fps) ──────────── */
static volatile int g_tick;
static int on_tick(volatile int *v) { (*v)++; return TIMER_CONTINUE; }

/* ── Helpers ──────────────────────────────────────────────────── */
static inline int tx(int c) { return OX + c * TW; }
static inline int ty(int r) { return OY + r * TH; }

/* Minimal LCG so we don't need stdlib rand seeding quirks */
static uint32_t rng_state = 0xDEADBEEF;
static int rng2(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (rng_state >> 16) & 1;
}

/* ── Map generation ───────────────────────────────────────────── */
static bool spawn_safe(int r, int c)
{
    /* Keep the 2-tile L-corner around each player spawn clear */
    return ((r==1&&c==1)||(r==1&&c==2)||(r==2&&c==1) ||
            (r==GH-2&&c==GW-2)||(r==GH-2&&c==GW-3)||(r==GH-3&&c==GW-2));
}

static void make_map(void)
{
    for (int r = 0; r < GH; r++) {
        for (int c = 0; c < GW; c++) {
            if (r==0 || r==GH-1 || c==0 || c==GW-1)
                grid[r][c] = T_WALL;
            else if (r%2==0 && c%2==0)
                grid[r][c] = T_WALL;
            else if (spawn_safe(r, c))
                grid[r][c] = T_EMPTY;
            else
                grid[r][c] = rng2() ? T_BRICK : T_EMPTY;
        }
    }
}

/* ── Rendering ────────────────────────────────────────────────── */
static void draw_tile(int r, int c)
{
    int x = tx(c), y = ty(r);
    switch (grid[r][c]) {
    case T_EMPTY:
        drect(x, y, x+TW-1, y+TH-1, COL_GROUND);
        break;
    case T_WALL:
        drect(x,   y,   x+TW-1, y+TH-1, COL_WALL_D);
        drect(x+2, y+2, x+TW-3, y+TH-3, COL_WALL_L);
        break;
    case T_BRICK:
        drect(x,   y,   x+TW-1, y+TH-1, COL_BRICK_D);
        drect(x+3, y+3, x+TW-4, y+TH-4, COL_BRICK_L);
        /* mortar lines */
        drect(x,       y+TH/2-1, x+TW-1, y+TH/2,   COL_BRICK_D);
        drect(x+TW/2-1, y+1,      x+TW/2, y+TH/2-2, COL_BRICK_D);
        break;
    }
}

static void draw_fire(int r, int c)
{
    int x = tx(c), y = ty(r);
    drect(x,   y,   x+TW-1, y+TH-1, COL_FIRE_O);
    drect(x+4, y+4, x+TW-5, y+TH-5, COL_FIRE_Y);
}

static void draw_bomb(const Bomb *b)
{
    int x = tx(b->c), y = ty(b->r);
    drect(x, y, x+TW-1, y+TH-1, COL_GROUND);

    /* Oval body (overlapping rects) */
    drect(x+5, y+4,  x+TW-6, y+TH-5, COL_BOMB);
    drect(x+4, y+5,  x+TW-5, y+TH-6, COL_BOMB);
    drect(x+3, y+6,  x+TW-4, y+TH-7, COL_BOMB);

    /* Fuse — flicker rate increases near detonation */
    int rate  = (b->timer > 40) ? 8 : (b->timer > 20) ? 4 : 2;
    int fcol  = ((b->timer / rate) & 1) ? COL_FUSE_A : COL_FUSE_B;
    drect(x+TW/2-1, y+1, x+TW/2+1, y+5, fcol);
}

static void draw_player(const Player *p, int col)
{
    if (!p->alive) return;
    int x = tx(p->c), y = ty(p->r);

    drect(x+2,     y+2,     x+TW-3,  y+TH-3,  col);
    /* Eyes */
    drect(x+5,     y+5,     x+9,     y+9,     C_WHITE);
    drect(x+TW-10, y+5,     x+TW-6,  y+9,     C_WHITE);
    drect(x+6,     y+6,     x+8,     y+8,     C_BLACK);
    drect(x+TW-9,  y+6,     x+TW-7,  y+8,     C_BLACK);
    /* Mouth */
    drect(x+7,     y+TH-7,  x+TW-8,  y+TH-6,  C_BLACK);
}

static void render(void)
{
    dclear(C_BLACK);

    /* Grid */
    for (int r = 0; r < GH; r++)
        for (int c = 0; c < GW; c++)
            draw_tile(r, c);

    /* Fire overlay */
    for (int i = 0; i < nfires; i++)
        draw_fire(fires[i].r, fires[i].c);

    /* Bombs */
    for (int i = 0; i < nbombs; i++)
        draw_bomb(&bombs[i]);

    /* Players */
    draw_player(&p1, p1.alive ? COL_P1 : COL_DEAD);
    draw_player(&p2, p2.alive ? COL_P2 : COL_DEAD);

    /* HUD strip */
    int sy = OY + GH*TH + 3;
    drect(0, sy, SW-1, SH-1, COL_HUD);
    dtext(6,        sy+4, p1.alive ? COL_P1 : COL_DEAD, "P1  ALIVE");
    dtext(SW/2 + 6, sy+4, p2.alive ? COL_P2 : COL_DEAD, "P2  ALIVE");
    if (!p1.alive) dtext(6,        sy+4, COL_DEAD, "P1  DEAD ");
    if (!p2.alive) dtext(SW/2 + 6, sy+4, COL_DEAD, "P2  DEAD ");

    dupdate();
}

/* ── Bomb & fire logic ────────────────────────────────────────── */
static bool bomb_at(int r, int c)
{
    for (int i = 0; i < nbombs; i++)
        if (bombs[i].r == r && bombs[i].c == c) return true;
    return false;
}

static void add_fire(int r, int c)
{
    for (int i = 0; i < nfires; i++) {
        if (fires[i].r == r && fires[i].c == c) {
            fires[i].dur = FIRE_LIFE;   /* refresh existing */
            return;
        }
    }
    if (nfires >= MAX_FIRES) return;
    fires[nfires++] = (Fire){ (int8_t)r, (int8_t)c, FIRE_LIFE };
}

/* Forward declaration needed for chain reactions */
static void explode(int idx);

static void explode(int idx)
{
    Bomb b = bombs[idx];
    /* Remove bomb first to avoid double-explosion */
    bombs[idx] = bombs[--nbombs];

    add_fire(b.r, b.c);

    int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (int d = 0; d < 4; d++) {
        for (int dist = 1; dist <= b.range; dist++) {
            int er = b.r + dirs[d][0]*dist;
            int ec = b.c + dirs[d][1]*dist;
            if (er<0||er>=GH||ec<0||ec>=GW) break;
            if (grid[er][ec] == T_WALL) break;

            add_fire(er, ec);

            if (grid[er][ec] == T_BRICK) {
                grid[er][ec] = T_EMPTY;   /* destroy brick */
                break;
            }
            /* Chain-trigger any bomb in the blast path */
            for (int j = 0; j < nbombs; j++) {
                if (bombs[j].r == er && bombs[j].c == ec)
                    bombs[j].timer = 1;
            }
        }
    }
}

static void place_bomb(Player *p, int owner_id)
{
    if (bomb_at(p->r, p->c)) return;
    int active = 0;
    for (int i = 0; i < nbombs; i++)
        if (bombs[i].owner == owner_id) active++;
    if (active >= p->max_bombs || nbombs >= MAX_BOMBS) return;
    bombs[nbombs++] = (Bomb){ p->r, p->c, BOMB_FUSE, p->blast, (int8_t)owner_id };
}

/* ── Player movement ──────────────────────────────────────────── */
static bool can_enter(int r, int c)
{
    if (r<0||r>=GH||c<0||c>=GW) return false;
    if (grid[r][c] != T_EMPTY)  return false;
    if (bomb_at(r, c))          return false;
    return true;
}

static void try_move(Player *p, int dr, int dc)
{
    if (p->move_cd > 0) return;
    if (!can_enter(p->r + dr, p->c + dc)) return;
    p->r += (int8_t)dr;
    p->c += (int8_t)dc;
    p->move_cd = MOVE_DELAY;
}

/* ── One game tick ────────────────────────────────────────────── */
static void handle_input(void)
{
    if (p1.alive) {
        if (key_down(KEY_UP))    try_move(&p1, -1,  0);
        if (key_down(KEY_DOWN))  try_move(&p1,  1,  0);
        if (key_down(KEY_LEFT))  try_move(&p1,  0, -1);
        if (key_down(KEY_RIGHT)) try_move(&p1,  0,  1);
        if (key_down(KEY_EXE))   place_bomb(&p1, 0);
    }
    if (p2.alive) {
        if (key_down(KEY_8)) try_move(&p2, -1,  0);
        if (key_down(KEY_2)) try_move(&p2,  1,  0);
        if (key_down(KEY_4)) try_move(&p2,  0, -1);
        if (key_down(KEY_6)) try_move(&p2,  0,  1);
        if (key_down(KEY_5)) place_bomb(&p2, 1);
    }
    if (p1.move_cd > 0) p1.move_cd--;
    if (p2.move_cd > 0) p2.move_cd--;
}

static void update(void)
{
    /* Bombs */
    for (int i = nbombs - 1; i >= 0; i--) {
        if (--bombs[i].timer <= 0)
            explode(i);
    }
    /* Fire */
    for (int i = nfires - 1; i >= 0; i--) {
        if (--fires[i].dur <= 0)
            fires[i] = fires[--nfires];
    }
    /* Kill players standing in fire */
    for (int i = 0; i < nfires; i++) {
        if (p1.alive && p1.r==fires[i].r && p1.c==fires[i].c) p1.alive = false;
        if (p2.alive && p2.r==fires[i].r && p2.c==fires[i].c) p2.alive = false;
    }
}

/* ── UI helpers ───────────────────────────────────────────────── */
static void wait_exe(void)
{
    while (!key_down(KEY_EXE)) {}
    while ( key_down(KEY_EXE)) {}
}

static void centred_text(int y, int col, const char *s)
{
    int x = SW/2 - (int)(strlen(s) * 6) / 2;
    dtext(x, y, col, s);
}

static void title_screen(void)
{
    dclear(C_BLACK);
    centred_text(44,  COL_FIRE_O, "BOMBERMAN  2P");
    centred_text(72,  C_WHITE,    "P1 : Arrow keys + EXE");
    centred_text(88,  C_WHITE,    "P2 : 8/2/4/6  +  5");
    centred_text(130, COL_DEAD,   "Press EXE to start");
    dupdate();
    wait_exe();
}

static void result_screen(void)
{
    render();   /* show the final game frame underneath the banner */

    bool both = !p1.alive && !p2.alive;
    const char *msg = both       ? "   DRAW!"  :
                      !p2.alive  ? "P1 WINS!"  : "P2 WINS!";
    int col = both ? C_WHITE : (!p2.alive ? COL_P1 : COL_P2);

    int x = SW/2 - 44, y = SH/2 - 12;
    drect(x-4, y-4, x+92, y+28, C_BLACK);
    centred_text(y,    col,     msg);
    centred_text(y+14, COL_DEAD, "EXE to play again");
    dupdate();
    wait_exe();
}

/* ── Entry point ──────────────────────────────────────────────── */
int main(void)
{
    /* Set up a ~30 fps timer */
    int tid = timer_configure(TIMER_ANY, 33000 /*µs*/,
                              GINT_CALL(on_tick, &g_tick));
    timer_start(tid);

    while (true) {
        title_screen();

        /* Reset everything */
        make_map();
        nbombs = nfires = 0;
        p1 = (Player){ 1,      1,      true, 1, 2, 0 };
        p2 = (Player){ GH-2,   GW-2,   true, 1, 2, 0 };

        /* Game loop: wait for timer tick, then step */
        while (p1.alive && p2.alive) {
            while (!g_tick) {}
            g_tick = 0;
            handle_input();
            update();
            render();
        }

        result_screen();
    }

    timer_stop(tid);
    return 1;
}
