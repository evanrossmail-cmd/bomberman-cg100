/* Bomberman 2P — Casio fx-CG50 add-in
 *
 * Player 1 : 8/2/4/6 to move,      5    to drop bomb
 * Player 2 : Arrow keys to move,  EXE   to drop bomb
 * MENU     : Exit at any time
 *
 * Build with fxSDK + gint:
 *   fxsdk build-cg
 */

#include <gint/display.h>
#include <gint/keyboard.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>

/* ── Screen / grid layout ─────────────────────────────────────── */
#define SW  396         /* screen width  (fx-CG50 with gint) */
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
#define BOMB_FUSE      90   /* game ticks until explosion  */
#define FIRE_LIFE      22   /* game ticks fire persists    */
#define MOVE_DELAY      7   /* ticks between held-key steps */
#define MAX_BOMBS      10
#define MAX_FIRES      80
/* ~2 minutes at ~30fps; sudden-death (draw) after this */
#define TIMEOUT_FRAMES 3600

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
static int     game_frames;   /* ticks elapsed this round (for HUD timer) */

/* ── Simple frame throttle (~30 fps via busy-wait) ───────────── */
static void frame_wait(void)
{
    volatile int d = 180000;
    while (d--) {}
}

/* ── Helpers ──────────────────────────────────────────────────── */
static inline int tx(int c) { return OX + c * TW; }
static inline int ty(int r) { return OY + r * TH; }

/* Minimal LCG — seeded from title-screen frame count each new game
   so every round has a different map layout.                        */
static uint32_t rng_state = 1;
static int rng2(void)
{
    rng_state = rng_state * 1664525u + 1013904223u;
    return (rng_state >> 16) & 1;
}

/* Draw a fixed-width decimal integer (leading zeros). */
static void draw_int(int x, int y, int col, int val, int digits)
{
    char buf[8];
    int i = digits;
    buf[i] = '\0';
    while (i-- > 0) {
        buf[i] = '0' + (val % 10);
        val /= 10;
    }
    dtext(x, y, col, buf);
}

/* ── Map generation ───────────────────────────────────────────── */
/* Returns true for cells that must stay empty around spawn corners.
   Permanent wall slots (even row AND even col) are handled first in
   make_map() so they are never overridden by this check.
   P1 spawns at (1,1), P2 at (GH-2, GW-2).
   We clear a plus-shaped 3-tile corridor in each corner so players
   cannot be trapped immediately.                                    */
static bool spawn_safe(int r, int c)
{
    /* P1 corner — row 1, cols 1-3  AND  col 1, rows 1-3 */
    if (r == 1       && c >= 1      && c <= 3)      return true;
    if (c == 1       && r >= 1      && r <= 3)      return true;
    /* P2 corner — row GH-2, cols GW-4..GW-2  AND  col GW-2, rows GH-4..GH-2 */
    if (r == GH-2    && c >= GW-4   && c <= GW-2)   return true;
    if (c == GW-2    && r >= GH-4   && r <= GH-2)   return true;
    return false;
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
        drect(x,        y+TH/2-1, x+TW-1, y+TH/2,   COL_BRICK_D);
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
    int rate = (b->timer > 40) ? 8 : (b->timer > 20) ? 4 : 2;
    int fcol = ((b->timer / rate) & 1) ? COL_FUSE_A : COL_FUSE_B;
    drect(x+TW/2-1, y+1, x+TW/2+1, y+5, fcol);
}

/* Draw the player sprite.  Dead players are rendered as a dim grey
   tombstone so their last position remains visible on the board.    */
static void draw_player(const Player *p, int col)
{
    int x = tx(p->c), y = ty(p->r);

    if (!p->alive) {
        /* Dim grey body with closed (X) eyes — clearly dead but locatable */
        drect(x+2,     y+2,     x+TW-3,  y+TH-3,  COL_DEAD);
        /* Left eye — small horizontal bar (closed) */
        drect(x+5,     y+7,     x+9,     y+8,     C_BLACK);
        /* Right eye */
        drect(x+TW-10, y+7,     x+TW-6,  y+8,     C_BLACK);
        return;
    }

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

    /* Players (dead players rendered as grey markers, not invisible) */
    draw_player(&p1, COL_P1);
    draw_player(&p2, COL_P2);

    /* HUD strip */
    int sy = OY + GH*TH + 3;
    drect(0, sy, SW-1, SH-1, COL_HUD);

    /* P1 status — left */
    dtext(6, sy+4,
          p1.alive ? COL_P1 : COL_DEAD,
          p1.alive ? "P1 ALIVE" : "P1  DEAD");

    /* Countdown timer — centred
       "M:SS" = 4 chars * 6px = 24px; start at SW/2 - 12            */
    int rem  = TIMEOUT_FRAMES - game_frames;
    if (rem < 0) rem = 0;
    int secs = rem / 30;
    int mm   = secs / 60;
    int ss   = secs % 60;
    /* Warn in orange when under 30 seconds remain */
    int tcol = (rem < 900) ? COL_FIRE_O : C_WHITE;
    int cx   = SW/2 - 12;
    draw_int(cx,    sy+4, tcol, mm, 1);
    dtext(cx+6,     sy+4, tcol, ":");
    draw_int(cx+12, sy+4, tcol, ss, 2);

    /* P2 status — right-aligned (8 chars * 6 = 48px from right edge) */
    dtext(SW - 54, sy+4,
          p2.alive ? COL_P2 : COL_DEAD,
          p2.alive ? "P2 ALIVE" : "P2  DEAD");

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
    /* Remove this bomb first (swap with last) to avoid double-explosion.
       The bomb now at slot idx is processed next iteration.           */
    bombs[idx] = bombs[--nbombs];

    add_fire(b.r, b.c);

    int dirs[4][2] = {{-1,0},{1,0},{0,-1},{0,1}};
    for (int d = 0; d < 4; d++) {
        for (int dist = 1; dist <= b.range; dist++) {
            int er = b.r + dirs[d][0]*dist;
            int ec = b.c + dirs[d][1]*dist;
            if (er<0||er>=GH||ec<0||ec>=GW) break;
            if (grid[er][ec] == T_WALL)     break;

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
/* Pass the OTHER player so we can block walking through them.      */
static bool can_enter(int r, int c, const Player *other)
{
    if (r<0||r>=GH||c<0||c>=GW) return false;
    if (grid[r][c] != T_EMPTY)  return false;
    if (bomb_at(r, c))          return false;
    /* Block walking through the other living player */
    if (other->alive && other->r == r && other->c == c) return false;
    return true;
}

static void try_move(Player *p, int dr, int dc, const Player *other)
{
    if (p->move_cd > 0) return;
    if (!can_enter(p->r + dr, p->c + dc, other)) return;
    p->r += (int8_t)dr;
    p->c += (int8_t)dc;
    p->move_cd = MOVE_DELAY;
}

/* ── Per-frame logic ──────────────────────────────────────────── */
static void handle_input(void)
{
    /* P1 — numpad (8/2/4/6 move, 5 bomb) */
    if (p1.alive) {
        if (keydown(KEY_8)) try_move(&p1, -1,  0, &p2);
        if (keydown(KEY_2)) try_move(&p1,  1,  0, &p2);
        if (keydown(KEY_4)) try_move(&p1,  0, -1, &p2);
        if (keydown(KEY_6)) try_move(&p1,  0,  1, &p2);
        if (keydown(KEY_5)) place_bomb(&p1, 0);
    }
    /* P2 — arrow keys + EXE bomb */
    if (p2.alive) {
        if (keydown(KEY_UP))    try_move(&p2, -1,  0, &p1);
        if (keydown(KEY_DOWN))  try_move(&p2,  1,  0, &p1);
        if (keydown(KEY_LEFT))  try_move(&p2,  0, -1, &p1);
        if (keydown(KEY_RIGHT)) try_move(&p2,  0,  1, &p1);
        if (keydown(KEY_EXE))   place_bomb(&p2, 1);
    }
    if (p1.move_cd > 0) p1.move_cd--;
    if (p2.move_cd > 0) p2.move_cd--;
}

static void update(void)
{
    /* Bombs — forward index loop.
       explode() swap-removes bombs[i], so after an explosion we must
       re-examine the same index (now holding a different bomb) rather
       than skipping it.                                               */
    int i = 0;
    while (i < nbombs) {
        if (--bombs[i].timer <= 0)
            explode(i);   /* slot i now holds the swapped-in bomb */
        else
            i++;
    }

    /* Fire — reverse loop is safe here (we only remove, never add) */
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
static void centred_text(int y, int col, const char *s)
{
    int x = SW/2 - (int)(strlen(s) * 6) / 2;
    dtext(x, y, col, s);
}

/* Wait for a confirm or exit key.
   KEY_EXE  — P2's natural confirm button.
   KEY_5    — P1's natural bomb/confirm button (same hand as numpad).
   KEY_MENU — exit the add-in.
   Returns true if MENU was pressed (caller should exit).            */
static bool wait_key(void)
{
    while (true) {
        if (keydown(KEY_MENU)) return true;
        if (keydown(KEY_EXE) || keydown(KEY_5)) {
            /* Debounce: wait for both keys to be released */
            while (keydown(KEY_EXE) || keydown(KEY_5)) {}
            return false;
        }
    }
}

/* Show the title / instructions screen.
   While waiting for input we count frames and use the count to seed
   the RNG, ensuring a different map layout each round.              */
static bool title_screen(void)
{
    dclear(C_BLACK);
    centred_text(44,  COL_FIRE_O, "BOMBERMAN  2P");
    centred_text(68,  C_WHITE,    "P1 : 8/2/4/6  +  5");
    centred_text(84,  C_WHITE,    "P2 : Arrows  + EXE");
    centred_text(112, COL_P1,     "EXE / 5 - start");
    centred_text(126, COL_DEAD,   "MENU    - quit");
    dupdate();

    uint32_t frame = 0;
    while (true) {
        frame++;
        frame_wait();
        if (keydown(KEY_MENU)) {
            rng_state = frame | 1u;
            return true;
        }
        if (keydown(KEY_EXE) || keydown(KEY_5)) {
            while (keydown(KEY_EXE) || keydown(KEY_5)) {}
            rng_state = frame | 1u;
            return false;
        }
    }
}

static bool result_screen(void)
{
    render();

    bool both = !p1.alive && !p2.alive;
    const char *msg = both      ? "   DRAW!"  :
                      !p2.alive ? "P1 WINS!"  : "P2 WINS!";
    int col = both ? C_WHITE : (!p2.alive ? COL_P1 : COL_P2);

    /* Box wide enough for the longest line of text */
    int bx1 = 120, bx2 = 276;
    int by1 = SH/2 - 16, by2 = SH/2 + 18;
    drect(bx1, by1, bx2, by2, C_BLACK);
    centred_text(SH/2 - 10, col,     msg);
    centred_text(SH/2 + 2,  COL_DEAD,"EXE/5-replay  MENU-quit");
    dupdate();
    return wait_key();
}

/* ── Entry point ──────────────────────────────────────────────── */
int main(void)
{
    while (true) {
        if (title_screen()) return 0;   /* MENU → exit */

        /* Reset everything for a fresh round */
        make_map();
        nbombs = nfires = game_frames = 0;
        p1 = (Player){ 1,    1,    true, 1, 2, 0 };
        p2 = (Player){ GH-2, GW-2, true, 1, 2, 0 };

        /* Game loop */
        while (p1.alive && p2.alive) {
            if (keydown(KEY_MENU)) return 0;
            handle_input();
            update();
            game_frames++;
            render();
            frame_wait();

            /* Timeout — end as a draw (sudden death) */
            if (game_frames >= TIMEOUT_FRAMES) {
                p1.alive = p2.alive = false;
                break;
            }
        }

        if (result_screen()) return 0;  /* MENU → exit */
    }
}
