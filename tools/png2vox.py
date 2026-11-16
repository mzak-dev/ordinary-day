#!/usr/bin/env python3
"""Turns hand-drawn pixel art into MagicaVoxel slabs: 1 art pixel = 1 voxel.

The front face of the model is the drawing, pixel for pixel. Nothing is
invented: the tool only copies pixels and extrudes them. Depth is then
sculpted by hand in MagicaVoxel.

  python tools/png2vox.py art/Commode.png game/assets/vox/hall_commode.vox --depth 8
  python tools/png2vox.py art/Background.png out/hall_wall.vox --rows 0:68 --depth 2
  python tools/png2vox.py art/Background.png out/hall_floor.vox --rows 68:108 --floor 70
  python tools/png2vox.py art/Commode.png --check          # style report only

Options
  --scale N     screen pixels per art pixel (default: auto-detect, 10 for the old files)
  --depth N     slab thickness in voxels (default 1)
  --rows A:B    use only art rows A..B-1 (top = 0)
  --floor D     lay the picture down as a floor D voxels deep (rows stretch front to back)
  --stack A:B,C:D,...  rebuild the picture from these row ranges, top to bottom
                (fits a hand-drawn wall to another room height without redrawing it)
  --width N     crop or repeat the columns to N art px
  --hole X0:X1:R0:R1   leave columns X0..X1-1, rows R0..R1-1 empty (window openings)
  --alpha T     pixels with alpha below T are empty (default 128)
  --check       print the style report and write nothing

Images wider than 192 art pixels are split into tiles name_0.vox, name_1.vox...
(.vox models are limited to 256 voxels per side). Pure standard library.
"""

import os
import struct
import sys
import zlib

TILE = 192


# ------------------------------------------------------------------ PNG reading


def read_png(path):
    """Returns (w, h, rows) with rows[y][x] = (r, g, b, a). 8-bit, non-interlaced."""
    data = open(path, "rb").read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG: " + path)
    at = 8
    idat = b""
    plte, trns = None, None
    while at < len(data):
        n, cid = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        at += 12 + n
        if cid == b"IHDR":
            w, h, bits, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if bits != 8 or interlace:
                raise ValueError("only 8-bit non-interlaced PNG is supported: " + path)
        elif cid == b"PLTE":
            plte = body
        elif cid == b"tRNS":
            trns = body
        elif cid == b"IDAT":
            idat += body
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    pos = 0
    for _ in range(h):
        f = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else (b if pb <= pc else c))) & 255
        prev = line
        out = []
        for x in range(w):
            px = line[x * bpp:(x + 1) * bpp]
            if ctype == 6:
                out.append(tuple(px))
            elif ctype == 2:
                out.append((px[0], px[1], px[2], 255))
            elif ctype == 0:
                out.append((px[0], px[0], px[0], 255))
            elif ctype == 4:
                out.append((px[0], px[0], px[0], px[1]))
            else:
                i = px[0]
                a = trns[i] if trns and i < len(trns) else 255
                out.append((plte[i * 3], plte[i * 3 + 1], plte[i * 3 + 2], a))
        rows.append(out)
    return w, h, rows


# ------------------------------------------------------------------ art grid


def detect_scale(w, h, rows):
    """Largest block size for which (almost) every block is a single colour."""
    for s in (20, 16, 10, 8, 5, 4, 2):
        if w % s or h % s:
            continue
        bad = total = 0
        for y in range(0, h, s):
            for x in range(0, w, s):
                total += 1
                c = rows[y][x]
                if rows[y + s - 1][x + s - 1] != c or rows[y + s // 2][x + s // 2] != c:
                    bad += 1
        if bad <= total * 0.02:
            return s
    return 1


def to_art(w, h, rows, scale):
    """Samples the centre of every block. Returns (aw, ah, art, off_grid_share)."""
    aw, ah = w // scale, h // scale
    art, off = [], 0
    for y in range(ah):
        line = []
        for x in range(aw):
            c = rows[y * scale + scale // 2][x * scale + scale // 2]
            if scale > 1 and (rows[y * scale][x * scale] != c or rows[y * scale + scale - 1][x * scale + scale - 1] != c):
                off += 1
            line.append(c)
        art.append(line)
    return aw, ah, art, off / float(max(1, aw * ah))


def report(name, aw, ah, art, scale, off, alpha_cut):
    solid = [c for line in art for c in line if c[3] >= alpha_cut]
    soft = sum(1 for line in art for c in line if 0 < c[3] < alpha_cut)
    colours = len(set(solid))
    black = sum(1 for c in solid if max(c[:3]) < 24)
    print("%s: %dx%d art px (scale %d), %d colours" % (name, aw, ah, scale, colours))
    notes = []
    if off > 0.02:
        notes.append("%.0f%% of pixels are off the %d px grid (soft layer: gradients, blur)" % (off * 100, scale))
    if soft:
        notes.append("%d semi-transparent pixels dropped (shadows become real light)" % soft)
    if solid and black > len(solid) * 0.15:
        notes.append("%.0f%% near-black pixels: outlines are reserved for picture frames" % (100.0 * black / len(solid)))
    if colours > 255:
        notes.append("more than 255 colours: nearest colours will be merged")
    for n in notes:
        print("  note: " + n)


# ------------------------------------------------------------------ .vox writing


def write_vox(path, w, d, h, voxels):
    """voxels: dict (x, y_depth, z_up) -> (r, g, b, a). y = 0 is the front."""
    colours, index, packed = [], {}, []
    for (x, y, z), c in voxels.items():
        if c not in index:
            if len(colours) >= 255:
                near = min(colours, key=lambda o: sum(abs(a - b) for a, b in zip(o, c)))
                index[c] = index[near]
            else:
                colours.append(c)
                index[c] = len(colours)
        packed.append(struct.pack("<BBBB", x, y, z, index[c]))

    def chunk(cid, content, children=b""):
        return cid + struct.pack("<ii", len(content), len(children)) + content + children

    pal = b"".join(struct.pack("<BBBB", *c) for c in colours) + b"\0\0\0\xff" * (256 - len(colours))
    body = (chunk(b"SIZE", struct.pack("<iii", w, d, h))
            + chunk(b"XYZI", struct.pack("<i", len(packed)) + b"".join(packed))
            + chunk(b"RGBA", pal))
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(b"VOX " + struct.pack("<i", 150) + chunk(b"MAIN", b"", body))


def slab(art, x0, x1, depth, alpha_cut):
    ah = len(art)
    v = {}
    for row in range(ah):
        for x in range(x0, x1):
            c = art[row][x]
            if c[3] < alpha_cut:
                continue
            for y in range(depth):
                v[(x - x0, y, ah - 1 - row)] = (c[0], c[1], c[2], 255)
    return x1 - x0, depth, ah, v


def floor(art, x0, x1, depth):
    """Top row of the picture is the far edge, bottom row the near edge."""
    ah = len(art)
    v = {}
    for y in range(depth):  # y = 0 is the front
        row = ah - 1 - (y * ah) // depth
        for x in range(x0, x1):
            c = art[row][x]
            v[(x - x0, y, 1)] = (c[0], c[1], c[2], 255)
            v[(x - x0, y, 0)] = (int(c[0] * 0.7), int(c[1] * 0.7), int(c[2] * 0.7), 255)
    return x1 - x0, depth, 2, v


def main(argv):
    args = [a for a in argv if not a.startswith("--")]
    opts = {}
    it = iter(argv)
    for a in it:
        if a.startswith("--"):
            opts[a] = True if a == "--check" else next(it)
    args = [a for a in args if a not in opts.values()]
    if not args:
        print(__doc__)
        return 1
    src = args[0]
    alpha_cut = int(opts.get("--alpha", 128))
    w, h, rows = read_png(src)
    scale = int(opts["--scale"]) if "--scale" in opts else detect_scale(w, h, rows)
    if scale == 1 and "--scale" not in opts and w % 10 == 0 and h % 10 == 0:
        scale = 10  # old files with a soft layer on top: still drawn on the 10 px grid
    aw, ah, art, off = to_art(w, h, rows, scale)
    if "--rows" in opts:
        a, b = opts["--rows"].split(":")
        art = art[int(a):int(b)]
        ah = len(art)
    if "--stack" in opts:
        rows_out = []
        for part in opts["--stack"].split(","):
            a, b = part.split(":")
            rows_out.extend(art[int(a):int(b)])
        art = rows_out
        ah = len(art)
    if "--width" in opts:
        n = int(opts["--width"])
        art = [[row[i % aw] for i in range(n)] for row in art]
        aw = n
    if "--hole" in opts:
        x0, x1, r0, r1 = (int(v) for v in opts["--hole"].split(":"))
        for r in range(max(0, r0), min(ah, r1)):
            for x in range(max(0, x0), min(aw, x1)):
                art[r][x] = (0, 0, 0, 0)
    report(os.path.basename(src), aw, ah, art, scale, off, alpha_cut)
    if "--check" in opts or len(args) < 2:
        return 0
    dst = args[1]
    tiles = [(x, min(aw, x + TILE)) for x in range(0, aw, TILE)] if aw > 256 else [(0, aw)]
    for i, (x0, x1) in enumerate(tiles):
        if "--floor" in opts:
            model = floor(art, x0, x1, int(opts["--floor"]))
        else:
            model = slab(art, x0, x1, int(opts.get("--depth", 1)), alpha_cut)
        path = dst if len(tiles) == 1 else dst.replace(".vox", "_%d.vox" % i)
        write_vox(path, *model)
        print("  wrote %s (%dx%dx%d)" % (path, model[0], model[1], model[2]))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
