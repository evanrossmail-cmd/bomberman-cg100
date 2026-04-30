"""
Generates icon.png — a 48x48 Bomberman-themed icon for the .g3a add-in.
Run once:  python3 make_icon.py
"""
import struct, zlib

W, H = 48, 48

# Palette (RGB)
BG   = (30, 30, 30)
RED  = (200, 40,  40)
BLUE = (40,  80, 200)
YEL  = (240, 200,  0)
WHT  = (255, 255, 255)
BLK  = (0,   0,   0)
GRN  = (30, 100,  30)

def px(x, y):
    """Return (r,g,b) for pixel (x,y)."""
    # Grass background
    c = GN = GRN
    # --- P1 (red, left side) ---
    if 4 <= x <= 20 and 10 <= y <= 38:
        c = RED
    if 5 <= x <= 8  and 14 <= y <= 18: c = WHT   # eye L
    if 16 <= x <= 19 and 14 <= y <= 18: c = WHT   # eye R
    if 6 <= x <= 7  and 15 <= y <= 17: c = BLK
    if 17 <= x <= 18 and 15 <= y <= 17: c = BLK
    if 7 <= x <= 17  and 33 <= y <= 35: c = BLK   # mouth
    # --- P2 (blue, right side) ---
    if 28 <= x <= 44 and 10 <= y <= 38:
        c = BLUE
    if 29 <= x <= 32  and 14 <= y <= 18: c = WHT
    if 40 <= x <= 43  and 14 <= y <= 18: c = WHT
    if 30 <= x <= 31  and 15 <= y <= 17: c = BLK
    if 41 <= x <= 42  and 15 <= y <= 17: c = BLK
    if 31 <= x <= 41  and 33 <= y <= 35: c = BLK
    # --- Bomb (centre) ---
    bx, by, br = 24, 28, 6
    if (x-bx)**2 + (y-by)**2 <= br*br:
        c = BLK
    # Fuse
    if 23 <= x <= 25 and 20 <= y <= 24: c = YEL
    return c

def write_png(path):
    def chunk(name, data):
        c = struct.pack('>I', len(data)) + name + data
        c += struct.pack('>I', zlib.crc32(name + data) & 0xFFFFFFFF)
        return c

    raw = b''
    for y in range(H):
        raw += b'\x00'                      # filter type: None
        for x in range(W):
            r, g, b = px(x, y)
            raw += bytes([r, g, b])

    sig   = b'\x89PNG\r\n\x1a\n'
    ihdr  = chunk(b'IHDR', struct.pack('>IIBBBBB', W, H, 8, 2, 0, 0, 0))
    idat  = chunk(b'IDAT', zlib.compress(raw))
    iend  = chunk(b'IEND', b'')

    with open(path, 'wb') as f:
        f.write(sig + ihdr + idat + iend)

    print(f"Written {path}  ({W}x{H} RGB PNG)")

write_png('icon.png')
