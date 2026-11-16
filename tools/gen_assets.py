#!/usr/bin/env python3
"""Generates placeholder art for Ordinary Day.

  * game/assets/vox/*.vox        - MagicaVoxel models (1 voxel = 1/16 m), editable in MagicaVoxel
  * game/assets/sprites/*.png    - 4-frame 16x32 pixel-art characters (idle, step, pass, step)
  * game/assets/icons/*.png      - 16x16 item icons

Pure standard library. Re-run after tweaking; it overwrites the files.
"""

import math
import os
import random
import struct
import zlib

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "game", "assets")


def hexc(h):
    h = h.lstrip("#")
    return (int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16), 255)


def glow(c):
    """Always-on emissive (screens, signs)."""
    c = hexc(c) if isinstance(c, str) else c
    return (c[0], c[1], c[2], 96)


def glow_night(c):
    """Emissive at night only (lamps, lit windows)."""
    c = hexc(c) if isinstance(c, str) else c
    return (c[0], c[1], c[2], 200)


def shade(c, f):
    return (min(255, int(c[0] * f)), min(255, int(c[1] * f)), min(255, int(c[2] * f)), c[3])


# ============================================================== .vox writing


class Model:
    """x = right, y = depth (0 = front, facing the camera), z = up."""

    def __init__(self, w, d, h):
        self.w, self.d, self.h = w, d, h
        self.v = {}

    def set(self, x, y, z, c):
        if 0 <= x < self.w and 0 <= y < self.d and 0 <= z < self.h:
            if c is None:
                self.v.pop((x, y, z), None)
            else:
                self.v[(x, y, z)] = hexc(c) if isinstance(c, str) else c

    def box(self, x0, y0, z0, x1, y1, z1, c):
        for x in range(x0, x1):
            for y in range(y0, y1):
                for z in range(z0, z1):
                    self.set(x, y, z, c)

    def save(self, name):
        colors = []
        index = {}
        voxels = []
        for (x, y, z), c in self.v.items():
            if c not in index:
                if len(colors) >= 255:
                    c = min(colors, key=lambda o: sum(abs(a - b) for a, b in zip(o, c)))
                else:
                    colors.append(c)
                    index[c] = len(colors)
            voxels.append((x, y, z, index[c]))
        size = struct.pack("<iii", self.w, self.d, self.h)
        xyzi = struct.pack("<i", len(voxels)) + b"".join(struct.pack("<BBBB", *v) for v in voxels)
        pal = b"".join(struct.pack("<BBBB", *c) for c in colors) + b"\0\0\0\xff" * (256 - len(colors))

        def chunk(cid, content, children=b""):
            return cid + struct.pack("<ii", len(content), len(children)) + content + children

        body = chunk(b"SIZE", size) + chunk(b"XYZI", xyzi) + chunk(b"RGBA", pal)
        data = b"VOX " + struct.pack("<i", 150) + chunk(b"MAIN", b"", body)
        path = os.path.join(ROOT, "vox", name + ".vox")
        with open(path, "wb") as f:
            f.write(data)


def jitter(c, rnd, amount=0.06):
    base = hexc(c) if isinstance(c, str) else c
    return shade(base, 1.0 - amount + rnd.random() * amount * 2)


def make_models():
    R = random.Random(7)
    wood = "#8d6e4f"
    wood_d = "#6b5038"

    m = Model(32, 16, 14)  # bed
    m.box(0, 0, 0, 32, 16, 4, wood_d)
    m.box(1, 1, 4, 31, 15, 7, "#eeeeee")
    m.box(1, 0, 6, 22, 16, 9, "#3f5f9a")
    for x in range(1, 22, 3):
        m.box(x, 0, 8, x + 1, 16, 9, "#4a6bab")
    m.box(23, 2, 7, 29, 14, 9, "#f5f5f5")
    m.box(30, 0, 0, 32, 16, 14, wood)
    m.box(0, 0, 0, 2, 16, 9, wood)
    m.save("bed")

    m = Model(22, 11, 23)  # desk with PC
    m.box(0, 0, 11, 22, 11, 12, wood)
    for x in (0, 20):
        m.box(x, 1, 0, x + 2, 10, 11, wood_d)
    m.box(5, 4, 12, 15, 6, 13, "#222222")
    m.box(6, 5, 13, 14, 6, 22, "#1b1b1b")
    m.box(7, 4, 14, 13, 5, 21, glow("#5aa0ff"))
    for z in range(15, 20, 2):
        m.box(8, 4, z, 12, 5, z + 1, glow("#a8d0ff"))
    m.box(4, 1, 12, 16, 3, 13, "#2b2b2b")
    m.box(17, 2, 12, 21, 10, 21, "#262626")
    m.set(18, 1, 19, glow("#ff3d6e"))
    m.set(19, 1, 19, glow("#3dfff0"))
    m.box(1, 3, 12, 3, 5, 15, "#e0e0e0")
    m.save("desk_pc")

    m = Model(16, 10, 34)  # wardrobe
    m.box(0, 0, 0, 16, 10, 34, wood)
    m.box(8, 0, 1, 8 + 1, 1, 33, wood_d)
    m.box(6, 0, 16, 7, 1, 19, "#d4b06a")
    m.box(9, 0, 16, 10, 1, 19, "#d4b06a")
    m.box(0, 0, 33, 16, 10, 34, wood_d)
    m.save("wardrobe")

    m = Model(10, 1, 13)  # poster
    m.box(0, 0, 0, 10, 1, 13, "#1d1d3a")
    m.box(1, 0, 6, 9, 1, 11, "#ff5e5b")
    m.box(3, 0, 2, 7, 1, 5, "#ffd166")
    m.save("poster")

    m = Model(10, 10, 30)  # fridge
    m.box(0, 0, 0, 10, 10, 30, "#efefef")
    m.box(0, 0, 18, 10, 1, 19, "#c9c9c9")
    m.box(8, 0, 20, 9, 1, 26, "#9a9a9a")
    m.box(8, 0, 8, 9, 1, 16, "#9a9a9a")
    m.set(3, 0, 24, "#ff6f61")
    m.set(5, 0, 22, "#4fc3f7")
    m.save("fridge")

    m = Model(16, 10, 14)  # kitchen counter
    m.box(0, 0, 0, 16, 10, 13, "#d9cdb4")
    m.box(0, 0, 13, 16, 10, 14, "#6d5a4a")
    m.box(7, 0, 1, 8, 1, 12, "#b8ab92")
    m.box(9, 3, 13, 14, 8, 14, "#9aa3a8")
    m.box(13, 7, 14, 14, 8, 18, "#9aa3a8")
    m.save("counter")

    m = Model(10, 10, 14)  # stove
    m.box(0, 0, 0, 10, 10, 13, "#f0f0f0")
    m.box(0, 0, 13, 10, 10, 14, "#262626")
    m.box(1, 0, 2, 9, 1, 9, "#3a3a3a")
    m.box(2, 3, 14, 4, 5, 15, "#555555")
    m.box(6, 5, 14, 8, 7, 15, "#555555")
    m.box(5, 4, 14, 9, 8, 18, "#c0392b")  # a pot of soup
    m.save("stove")

    m = Model(18, 12, 12)  # table
    m.box(0, 0, 11, 18, 12, 12, wood)
    for x, y in ((1, 1), (16, 1), (1, 10), (16, 10)):
        m.box(x, y, 0, x + 1, y + 1, 11, wood_d)
    m.box(3, 2, 12, 15, 10, 12 + 0, "#ffffff")
    m.box(7, 5, 12, 10, 8, 13, "#e8e8e8")
    m.save("table")

    m = Model(7, 7, 15)  # chair
    m.box(0, 0, 6, 7, 7, 7, wood)
    for x, y in ((0, 0), (6, 0), (0, 6), (6, 6)):
        m.box(x, y, 0, x + 1, y + 1, 6, wood_d)
    m.box(0, 6, 7, 7, 7, 15, wood)
    m.save("chair")

    m = Model(36, 16, 48)  # staircase (rises to the left)
    for i in range(12):
        x0 = 36 - (i + 1) * 3
        m.box(x0, 0, 0, x0 + 3, 16, (i + 1) * 4, jitter("#9e9a92", R, 0.03))
        m.box(x0, 0, (i + 1) * 4 - 1, x0 + 3, 16, (i + 1) * 4, "#7f7a72")
    for i in range(0, 36, 2):
        top = (12 - i // 3) * 4 + 12
        m.box(i, 0, top - 12 + 4, i + 1, 1, min(48, top), "#3c3c3c")
    m.save("stairs")

    m = Model(14, 3, 26)  # mailboxes
    for x in range(0, 14, 7):
        for z in range(14, 26, 4):
            m.box(x, 0, z, x + 6, 3, z + 3, "#b07d3a")
            m.box(x + 1, 0, z + 1, x + 5, 1, z + 2, "#2d2d2d")
    m.save("mailboxes")

    m = Model(26, 3, 15)  # bike
    for cx in (5, 20):
        for a in range(0, 360, 12):
            x = int(cx + 4.5 * math.cos(math.radians(a)))
            z = int(5 + 4.5 * math.sin(math.radians(a)))
            m.set(x, 1, z, "#222222")
    for t in range(0, 15):
        m.set(5 + t, 1, 5 + t // 3, "#2e86de")
    m.box(12, 1, 5, 13, 2, 11, "#2e86de")
    m.box(10, 1, 11, 15, 2, 12, "#111111")
    m.box(18, 1, 10, 22, 2, 11, "#2e86de")
    m.box(20, 1, 11, 21, 2, 14, "#555555")
    m.save("bike")

    m = Model(14, 13, 15)  # armchair (floral)
    m.box(0, 0, 0, 14, 13, 7, "#8e3b46")
    m.box(0, 10, 7, 14, 13, 15, "#8e3b46")
    m.box(0, 0, 7, 2, 13, 10, "#7a2f3a")
    m.box(12, 0, 7, 14, 13, 10, "#7a2f3a")
    for _ in range(18):
        m.set(R.randrange(14), 0 if R.random() < 0.5 else 10, R.randrange(2, 14), "#f2c46d")
    m.save("armchair")

    m = Model(14, 10, 18)  # old CRT tv on a cabinet
    m.box(0, 0, 0, 14, 10, 8, wood_d)
    m.box(1, 1, 8, 13, 10, 18, "#3a3a3a")
    m.box(2, 0, 9, 11, 1, 17, glow("#7fa39d"))
    m.box(11, 0, 11, 12, 1, 13, "#c0c0c0")
    m.save("tv_old")

    m = Model(20, 8, 28)  # PRL wall unit
    m.box(0, 0, 0, 20, 8, 28, "#7b4a2b")
    m.box(1, 0, 14, 19, 1, 25, "#bfe0e8")
    for x in range(2, 18, 3):
        m.box(x, 1, 16, x + 2, 3, 20, R.choice(["#d84315", "#f9a825", "#5e35b1", "#ffffff"]))
    m.box(10, 0, 1, 11, 1, 13, "#5c3720")
    m.save("cabinet")

    m = Model(8, 8, 20)  # plant
    m.box(1, 1, 0, 7, 7, 6, "#b5651d")
    for _ in range(140):
        x, y, z = R.randrange(8), R.randrange(8), R.randrange(6, 20)
        if (x - 3.5) ** 2 + (y - 3.5) ** 2 < (5 - (z - 6) * 0.15) ** 2:
            m.set(x, y, z, jitter("#3f8f3f", R, 0.15))
    m.save("plant")

    m = Model(8, 4, 7)  # Filemon the cat
    m.box(0, 0, 0, 6, 4, 4, "#e0e0e0")
    m.box(5, 0, 3, 8, 4, 6, "#e8e8e8")
    m.set(5, 0, 6, "#e8e8e8")
    m.set(7, 0, 6, "#e8e8e8")
    m.set(7, 0, 4, "#222222")
    m.box(0, 1, 3, 1, 2, 6, "#9e9e9e")
    m.save("cat")

    m = Model(30, 13, 14)  # sofa
    m.box(0, 0, 0, 30, 13, 6, "#2f8f8a")
    m.box(0, 9, 6, 30, 13, 14, "#2a7f7a")
    m.box(0, 0, 6, 3, 13, 10, "#25706c")
    m.box(27, 0, 6, 30, 13, 10, "#25706c")
    m.box(5, 6, 6, 11, 9, 11, "#f2b134")
    m.box(19, 6, 6, 25, 9, 11, "#e76f51")
    m.save("sofa")

    m = Model(6, 6, 16)  # speaker
    m.box(0, 0, 0, 6, 6, 16, "#1e1e1e")
    m.box(1, 0, 3, 5, 1, 7, "#444444")
    m.box(2, 0, 10, 4, 1, 12, "#444444")
    m.save("speaker")

    m = Model(16, 6, 32)  # bookshelf
    m.box(0, 0, 0, 16, 6, 32, wood_d)
    m.box(1, 0, 1, 15, 5, 31, None)
    for z0 in (1, 9, 17, 25):
        m.box(1, 0, z0 - 1, 15, 6, z0, wood_d)
        x = 1
        while x < 15:
            w = R.choice([1, 1, 2])
            h = R.randrange(4, 7)
            m.box(x, 1, z0, min(15, x + w), 5, z0 + h, R.choice(["#c62828", "#283593", "#2e7d32", "#f9a825", "#6d4c41", "#ad1457", "#00838f"]))
            x += w
    m.save("bookshelf")

    m = Model(14, 10, 14)  # moving boxes
    m.box(0, 0, 0, 9, 9, 7, "#c8a46e")
    m.box(1, 1, 7, 8, 8, 13, "#b8935e")
    m.box(9, 2, 0, 14, 8, 5, "#c8a46e")
    m.box(0, 0, 3, 9, 1, 4, "#d9c7a5")
    m.save("boxes")

    m = Model(30, 8, 28)  # shop shelf
    m.box(0, 4, 0, 30, 8, 28, "#9ea4a8")
    for z in (1, 8, 15, 22):
        m.box(0, 0, z - 1, 30, 8, z, "#c4c9cc")
        x = 0
        while x < 30:
            w = R.choice([2, 2, 3])
            m.box(x, 1, z, min(30, x + w), 5, z + R.randrange(3, 6), R.choice(["#e53935", "#fdd835", "#43a047", "#1e88e5", "#fb8c00", "#8e24aa", "#ffffff"]))
            x += w + 1
    m.save("shop_shelf")

    m = Model(14, 10, 32)  # drinks fridge
    m.box(0, 0, 0, 14, 10, 32, "#d32f2f")
    m.box(1, 0, 2, 13, 1, 29, glow("#cfe8f3"))
    for z in (3, 10, 17, 24):
        for x in range(2, 12, 2):
            m.box(x, 1, z, x + 1, 3, z + 5, R.choice(["#f57c00", "#2e7d32", "#c62828", "#283593"]))
    m.save("drinks_fridge")

    m = Model(18, 9, 18)  # shop counter
    m.box(0, 0, 0, 18, 9, 14, "#2e7d32")
    m.box(0, 0, 14, 18, 9, 15, "#e0e0e0")
    m.box(11, 3, 15, 16, 7, 18, "#424242")
    m.box(2, 2, 15, 6, 6, 17, "#ffeb3b")
    m.save("shop_counter")

    m = Model(28, 10, 20)  # treadmill
    m.box(0, 1, 0, 26, 9, 3, "#2b2b2b")
    m.box(1, 2, 3, 25, 8, 4, "#444444")
    m.box(24, 1, 3, 26, 2, 18, "#555555")
    m.box(24, 8, 3, 26, 9, 18, "#555555")
    m.box(22, 1, 17, 28, 9, 20, "#333333")
    m.box(24, 0, 18, 27, 1, 19, glow("#00e676"))
    m.save("treadmill")

    m = Model(26, 8, 18)  # bench press
    m.box(4, 2, 0, 20, 6, 6, "#333333")
    m.box(4, 2, 6, 20, 6, 8, "#b71c1c")
    m.box(2, 1, 0, 3, 2, 14, "#666666")
    m.box(2, 6, 0, 3, 7, 14, "#666666")
    m.box(0, 3, 13, 26, 5, 14, "#9e9e9e")
    for x in (0, 1, 24, 25):
        m.box(x, 1, 10, x + 1, 7, 17, "#212121")
    m.save("bench_press")

    m = Model(22, 6, 16)  # dumbbell rack
    m.box(0, 1, 0, 22, 5, 2, "#424242")
    for z in (6, 12):
        m.box(0, 1, z - 1, 22, 5, z, "#616161")
        for x in range(1, 21, 4):
            m.box(x, 1, z, x + 3, 5, z + 3, "#1b1b1b")
    for x in (0, 21):
        m.box(x, 1, 0, x + 1, 5, 16, "#616161")
    m.save("weight_rack")

    m = Model(64, 1, 22)  # chalkboard
    m.box(0, 0, 0, 64, 1, 22, "#5d4037")
    m.box(1, 0, 1, 63, 1, 21, "#2e5e45")
    for _ in range(90):
        x = R.randrange(4, 60)
        z = R.choice([5, 6, 10, 11, 15, 16])
        m.box(x, 0, z, x + R.randrange(1, 4), 1, z + 1, "#e8efe9")
    m.save("board")

    m = Model(30, 8, 13)  # lecture desk with bench
    m.box(0, 4, 11, 30, 8, 12, "#c9a46b")
    m.box(0, 4, 0, 1, 8, 11, "#555555")
    m.box(29, 4, 0, 30, 8, 11, "#555555")
    m.box(0, 0, 6, 30, 3, 7, "#a07a48")
    m.box(1, 0, 0, 2, 3, 6, "#555555")
    m.box(28, 0, 0, 29, 3, 6, "#555555")
    m.save("lecture_desk")

    m = Model(14, 10, 30)  # vending machine
    m.box(0, 0, 0, 14, 10, 30, "#c62828")
    m.box(1, 0, 8, 10, 1, 28, glow("#3b5866"))
    for z in range(10, 27, 4):
        for x in range(2, 10, 2):
            m.set(x, 1, z, R.choice(["#ffeb3b", "#4caf50", "#ff9800", "#03a9f4"]))
    m.box(11, 0, 18, 13, 1, 24, "#9e9e9e")
    m.box(2, 0, 2, 10, 1, 5, "#111111")
    m.save("vending")

    m = Model(28, 10, 16)  # cafe counter
    m.box(0, 0, 0, 28, 10, 14, "#6d4c41")
    m.box(0, 0, 14, 28, 10, 15, "#d7ccc8")
    m.box(2, 0, 15, 14, 6, 20, "#e3f2fd")
    for x in range(3, 13, 3):
        m.box(x, 1, 15, x + 2, 4, 17, R.choice(["#ffcc80", "#8d6e63", "#f48fb1"]))
    for x in range(0, 28, 4):
        m.box(x, 0, 1, x + 2, 1, 13, "#5d4037")
    m.save("cafe_counter")

    m = Model(10, 7, 9)  # espresso machine
    m.box(0, 0, 0, 10, 7, 9, "#b0bec5")
    m.box(1, 0, 4, 9, 1, 8, "#78909c")
    m.box(3, 0, 1, 7, 2, 3, "#37474f")
    m.save("coffee_machine")

    m = Model(44, 20, 72)  # tree
    m.box(20, 8, 0, 24, 12, 34, "#6d4c33")
    m.box(17, 9, 26, 20, 11, 30, "#6d4c33")
    for x in range(44):
        for y in range(20):
            for z in range(26, 72):
                dx = (x - 22) / 21.0
                dy = (y - 10) / 10.0
                dz = (z - 50) / 21.0
                n = math.sin(x * 0.7) * 0.06 + math.cos(z * 0.5 + y) * 0.06
                if dx * dx + dy * dy + dz * dz < 1.0 + n and R.random() < 0.97:
                    m.set(x, y, z, jitter(R.choice(["#4e7d3a", "#5a8a42", "#43702f"]), R, 0.08))
    m.save("tree")

    m = Model(14, 3, 74)  # street lamp
    m.box(6, 1, 0, 8, 3, 70, "#37474f")
    m.box(4, 0, 0, 10, 3, 3, "#263238")
    m.box(2, 0, 68, 12, 3, 71, "#37474f")
    m.box(1, 0, 66, 5, 3, 68, glow_night("#ffe9a8"))
    m.save("lamp")

    m = Model(26, 8, 13)  # park bench
    for z in (6, 7):
        m.box(0, 0, z, 26, 6, z + 1, "#8d6e4f" if z == 6 else "#7d5e3f")
    m.box(0, 6, 8, 26, 7, 13, "#8d6e4f")
    for x in (2, 23):
        m.box(x, 0, 0, x + 1, 7, 6, "#2b2b2b")
    m.save("bench")

    m = Model(7, 7, 11)  # bin
    m.box(0, 0, 0, 7, 7, 10, "#546e7a")
    m.box(0, 0, 10, 7, 7, 11, "#37474f")
    m.save("trash")

    m = Model(56, 20, 44)  # bus shelter
    m.box(0, 0, 40, 56, 20, 43, "#455a64")
    m.box(1, 18, 0, 55, 19, 40, (186, 222, 246, 255))
    m.box(0, 17, 0, 2, 20, 40, "#37474f")
    m.box(54, 17, 0, 56, 20, 40, "#37474f")
    m.box(8, 12, 7, 40, 17, 8, "#78909c")
    m.box(8, 15, 0, 9, 16, 7, "#37474f")
    m.box(39, 15, 0, 40, 16, 7, "#37474f")
    m.box(44, 10, 0, 45, 11, 50, "#9e9e9e")
    m.box(41, 9, 44, 50, 12, 52, "#fdd835")
    m.box(43, 8, 46, 48, 9, 50, "#1b5e20")
    m.save("bus_shelter")


def make_extra_models():
    R = random.Random(11)
    m = Model(42, 16, 56)  # staircase for 3.5 m floors (rises to the left)
    for i in range(14):
        x0 = 42 - (i + 1) * 3
        m.box(x0, 0, 0, x0 + 3, 16, (i + 1) * 4, jitter("#a39e95", R, 0.03))
        m.box(x0, 0, (i + 1) * 4 - 1, x0 + 3, 16, (i + 1) * 4, "#827d74")
    for i in range(0, 42, 2):
        top = (14 - i // 3) * 4 + 12
        m.box(i, 0, top - 12 + 4, i + 1, 1, min(56, top), "#3c3c3c")
    m.save("stairs_tall")

    L, D, H = 160, 40, 48  # city bus, front at +x
    m = Model(L, D, H)
    red, cream, dark = "#b71c1c", "#f1e6c8", "#263238"
    glass = glow_night("#7fa2b8")
    for x in range(L):
        for y in range(D):
            for z in range(8, H):
                edge = x in (0, L - 1) or y in (0, D - 1) or z in (8, H - 1)
                if not edge:
                    continue
                c = red if z < 22 else cream
                if z == 22:
                    c = dark
                if 24 <= z < 40 and 3 < x < L - 4 and (y == 0 or y == D - 1):
                    c = dark if x % 22 < 2 else glass
                if x == L - 1 and 20 <= z < 42 and 3 < y < D - 4:
                    c = glass
                if x == L - 1 and 42 <= z < 46 and 8 < y < D - 8:
                    c = glow("#ff9800")
                for dx in (L - 26, 30):
                    if dx <= x < dx + 12 and y == 0 and 9 <= z < 40:
                        c = dark if x in (dx, dx + 11) or z == 39 else glass
                m.set(x, y, z, c)
    for cx in (24, L - 30):
        for y in (2, D - 3):
            for a in range(0, 360, 8):
                for r in range(0, 9):
                    x = int(cx + r * math.cos(math.radians(a)))
                    z = int(8 + r * math.sin(math.radians(a)))
                    m.set(x, y, max(0, z), "#1a1a1a" if r > 3 else "#9e9e9e")
    m.box(L - 1, 4, 10, L, 10, 14, glow_night("#fffbe6"))
    m.box(L - 1, D - 10, 10, L, D - 4, 14, glow_night("#fffbe6"))
    m.box(0, 4, 12, 1, 9, 16, glow_night("#ff1744"))
    m.box(0, D - 9, 12, 1, D - 4, 16, glow_night("#ff1744"))
    m.save("bus")


# ============================================================== PNG writing


def write_png(path, w, h, px):
    raw = b"".join(b"\0" + b"".join(struct.pack("BBBB", *px[y * w + x]) for x in range(w)) for y in range(h))

    def chunk(t, d):
        c = struct.pack(">I", len(d)) + t + d
        return c + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(data)


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [(0, 0, 0, 0)] * (w * h)

    def set(self, x, y, c):
        if 0 <= x < self.w and 0 <= y < self.h and c is not None:
            self.px[y * self.w + x] = hexc(c) if isinstance(c, str) else c

    def get(self, x, y):
        if 0 <= x < self.w and 0 <= y < self.h:
            return self.px[y * self.w + x]
        return (0, 0, 0, 0)

    def rect(self, x0, y0, x1, y1, c):
        for y in range(y0, y1):
            for x in range(x0, x1):
                self.set(x, y, c)

    def outline(self, color, x0=0, x1=None):
        x1 = self.w if x1 is None else x1
        col = hexc(color)
        add = []
        for y in range(self.h):
            for x in range(x0, x1):
                if self.get(x, y)[3] == 0:
                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                        nx = x + dx
                        if x0 <= nx < x1 and self.get(nx, y + dy)[3] > 0 and self.get(nx, y + dy) != col:
                            add.append((x, y))
                            break
        for x, y in add:
            self.set(x, y, col)


CHARACTERS = {
    "player": dict(skin="#f1c9a5", hair="#3b2a20", hair_style="messy", top="#4a5a4f", top2="#3d4b41", legs="#3b4b6b", shoes="#e8e8e8", extra=["headphones"]),
    "mama": dict(skin="#efc3a0", hair="#8a3b2a", hair_style="perm", top="#8e4a5a", top2="#7a3d4c", legs="#5d5d6b", shoes="#a1887f", extra=[]),
    "tata": dict(skin="#e8b994", hair="#6e6e6e", hair_style="bald", top="#5d6d7e", top2="#4e5c6b", legs="#6d5240", shoes="#3e2723", extra=["mustache"]),
    "kuba": dict(skin="#e3b48c", hair="#e2c26e", hair_style="short", top="#ff8a3d", top2="#e57428", legs="#2a2a2a", shoes="#ffffff", extra=["broad"]),
    "ola": dict(skin="#f3cfb3", hair="#2b2b3a", hair_style="long", top="#e0b040", top2="#c99a30", legs="#4a6a8a", shoes="#7b3f2f", extra=["bangs"]),
    "halina": dict(skin="#efcfb8", hair="#d0d0d0", hair_style="bun", top="#7e57c2", top2="#6a45ad", legs="#7e57c2", shoes="#5d4037", extra=["glasses", "dress"]),
    "marta": dict(skin="#f5d0b5", hair="#c4622d", hair_style="ponytail", top="#26807a", top2="#1f6b66", legs="#2f2f3f", shoes="#111111", extra=["glasses"]),
    "nowak": dict(skin="#e6b896", hair="#9e9e9e", hair_style="short", top="#7a5c3e", top2="#664c33", legs="#4a4a4a", shoes="#2b2b2b", extra=["beard"]),
    "szef": dict(skin="#d9a27f", hair="#2b2b2b", hair_style="bald", top="#f5f5f5", top2="#222222", legs="#3a3a3a", shoes="#1b1b1b", extra=["beard", "apron"]),
}


def draw_person(cv, ox, frame, p):
    bob = -1 if frame == 2 else 0
    skin, hair = p["skin"], p["hair"]
    top, top2 = p["top"], p["top2"]
    ex = p["extra"]
    broad = "broad" in ex
    tx0, tx1 = (4, 12) if broad else (5, 11)
    # legs (drawn first, torso overlaps)
    legs = {0: ((6, 8), (8, 10)), 1: ((4, 6), (9, 11)), 2: ((6, 8), (8, 10)), 3: ((5, 7), (9, 11)), 4: ((6, 8), (8, 10)), 5: ((5, 7), (9, 11))}[frame]
    if "dress" in ex:
        cv.rect(ox + tx0, 21 + bob, ox + tx1, 27, top)
        for i, (a, b) in enumerate(legs):
            cv.rect(ox + a, 27, ox + b, 30, skin)
            cv.rect(ox + a, 30, ox + b, 32, p["shoes"])
    else:
        for i, (a, b) in enumerate(legs):
            shade_leg = p["legs"] if i == 1 else shade(hexc(p["legs"]), 0.85)
            if frame == 5 and i == 1:
                # climbing: front knee up on the next step
                cv.rect(ox + a, 21, ox + b + 1, 25, shade_leg)
                cv.rect(ox + a + 1, 25, ox + b + 1, 28, shade_leg)
                cv.rect(ox + a + 1, 28, ox + b + 2, 30, p["shoes"])
                continue
            cv.rect(ox + a, 21 + bob, ox + b, 30, shade_leg)
            cv.rect(ox + a, 30, ox + b + (1 if i == 1 else 0), 32, p["shoes"])
    # torso
    cv.rect(ox + tx0, 13 + bob, ox + tx1, 22 + bob, top)
    if "apron" in ex:
        cv.rect(ox + tx0 + 1, 15 + bob, ox + tx1 - 1, 24 + bob, top2)
    # arm swinging opposite to legs
    if frame == 4:
        # reaching forward: using something, talking, handing over
        cv.rect(ox + 7, 14, ox + 9, 17, top2)
        cv.rect(ox + 9, 15, ox + 13, 17, top2)
        cv.rect(ox + 13, 15, ox + 14, 17, skin)
    else:
        arm_dx = {0: 0, 1: 1, 2: 0, 3: -1, 5: 1}[frame]
        cv.rect(ox + 7 + arm_dx, 14 + bob, ox + 9 + arm_dx, 21 + bob, top2)
        cv.rect(ox + 7 + arm_dx, 21 + bob, ox + 9 + arm_dx, 22 + bob, skin)
    # neck + head
    cv.rect(ox + 7, 12 + bob, ox + 9, 13 + bob, skin)
    cv.rect(ox + 5, 4 + bob, ox + 11, 12 + bob, skin)
    cv.set(ox + 11, 8 + bob, skin)  # nose
    cv.set(ox + 9, 7 + bob, "#2b1d14")  # eye
    cv.set(ox + 9, 10 + bob, shade(hexc(skin), 0.8))  # mouth
    style = p["hair_style"]
    if style == "messy":
        cv.rect(ox + 5, 2 + bob, ox + 11, 5 + bob, hair)
        cv.rect(ox + 4, 3 + bob, ox + 7, 9 + bob, hair)
        for x in (5, 7, 9):
            cv.set(ox + x, 1 + bob, hair)
    elif style == "perm":
        cv.rect(ox + 4, 2 + bob, ox + 12, 6 + bob, hair)
        cv.rect(ox + 4, 3 + bob, ox + 7, 11 + bob, hair)
        for x in range(4, 12, 2):
            cv.set(ox + x, 1 + bob, hair)
    elif style == "bald":
        cv.rect(ox + 5, 6 + bob, ox + 7, 10 + bob, hair)
        cv.rect(ox + 6, 3 + bob, ox + 10, 4 + bob, shade(hexc(skin), 1.05))
    elif style == "short":
        cv.rect(ox + 5, 3 + bob, ox + 11, 5 + bob, hair)
        cv.rect(ox + 5, 3 + bob, ox + 7, 8 + bob, hair)
    elif style == "long":
        cv.rect(ox + 5, 2 + bob, ox + 11, 5 + bob, hair)
        cv.rect(ox + 3, 3 + bob, ox + 7, 17 + bob, hair)
    elif style == "bun":
        cv.rect(ox + 5, 3 + bob, ox + 11, 5 + bob, hair)
        cv.rect(ox + 4, 4 + bob, ox + 7, 9 + bob, hair)
        cv.rect(ox + 3, 1 + bob, ox + 6, 4 + bob, hair)
    elif style == "ponytail":
        cv.rect(ox + 5, 3 + bob, ox + 11, 5 + bob, hair)
        cv.rect(ox + 4, 4 + bob, ox + 7, 8 + bob, hair)
        cv.rect(ox + 2, 5 + bob + (1 if frame in (1, 3) else 0), ox + 4, 13 + bob, hair)
    if "bangs" in ex:
        cv.rect(ox + 7, 5 + bob, ox + 11, 6 + bob, hair)
    if "glasses" in ex:
        cv.rect(ox + 8, 7 + bob, ox + 11, 8 + bob, "#3a3a3a")
        cv.set(ox + 9, 7 + bob, "#cfe3ff")
    if "mustache" in ex:
        cv.rect(ox + 8, 9 + bob, ox + 12, 10 + bob, hair)
    if "beard" in ex:
        cv.rect(ox + 7, 9 + bob, ox + 11, 12 + bob, hair)
    if "headphones" in ex:
        cv.rect(ox + 6, 1 + bob, ox + 10, 2 + bob, "#d32f2f")
        cv.rect(ox + 5, 6 + bob, ox + 7, 9 + bob, "#d32f2f")


def make_sprites():
    for name, p in CHARACTERS.items():
        frames = 6
        cv = Canvas(16 * frames, 32)
        for f in range(frames):
            draw_person(cv, f * 16, f, p)
        for f in range(frames):
            cv.outline("#1a1a24", f * 16, f * 16 + 16)
        write_png(os.path.join(ROOT, "sprites", name + ".png"), 16 * frames, 32, cv.px)


def make_icons():
    def icon(name, draw):
        cv = Canvas(16, 16)
        draw(cv)
        cv.outline("#1a1a24")
        write_png(os.path.join(ROOT, "icons", name + ".png"), 16, 16, cv.px)

    icon("phone", lambda c: (c.rect(5, 1, 11, 15, "#263238"), c.rect(6, 3, 10, 12, "#64b5f6"), c.set(8, 13, "#90a4ae")))

    def keys(c):
        for a in range(0, 360, 30):
            c.set(int(5 + 3 * math.cos(math.radians(a))), int(5 + 3 * math.sin(math.radians(a))), "#c9a227")
        c.rect(7, 7, 13, 9, "#d4af37")
        c.rect(11, 9, 12, 11, "#d4af37")
        c.rect(9, 9, 10, 10, "#d4af37")

    icon("keys", keys)

    def headphones(c):
        for a in range(180, 361, 15):
            c.set(int(8 + 5 * math.cos(math.radians(a))), int(8 + 5 * math.sin(math.radians(a))), "#d32f2f")
        c.rect(2, 8, 5, 13, "#b71c1c")
        c.rect(11, 8, 14, 13, "#b71c1c")

    icon("headphones", headphones)
    icon("student_id", lambda c: (c.rect(1, 4, 15, 13, "#1e88e5"), c.rect(2, 5, 6, 11, "#e3f2fd"), c.rect(7, 6, 14, 7, "#bbdefb"), c.rect(7, 9, 12, 10, "#bbdefb")))
    icon("wallet", lambda c: (c.rect(2, 4, 14, 13, "#6d4c41"), c.rect(9, 7, 15, 10, "#8d6e63"), c.set(12, 8, "#ffd54f")))
    icon("gym_pass", lambda c: (c.rect(1, 4, 15, 12, "#ff7043"), c.rect(3, 7, 13, 9, "#263238"), c.rect(5, 6, 6, 10, "#263238"), c.rect(10, 6, 11, 10, "#263238")))

    def umbrella(c):
        for y in range(3, 8):
            c.rect(8 - (y - 1), y, 8 + (y - 1), y + 1, "#5e35b1")
        c.rect(8, 8, 9, 14, "#3e2723")
        c.rect(6, 13, 8, 14, "#3e2723")

    icon("umbrella", umbrella)
    icon("notes", lambda c: (c.rect(3, 1, 13, 15, "#fff176"), [c.rect(4, y, 12, y + 1, "#9e9d24") for y in (4, 7, 10, 13)]))
    icon("laptop", lambda c: (c.rect(3, 3, 13, 10, "#90a4ae"), c.rect(4, 4, 12, 9, "#26c6da"), c.rect(1, 10, 15, 12, "#78909c")))
    icon("book", lambda c: (c.rect(3, 2, 13, 14, "#43a047"), c.rect(4, 2, 5, 14, "#2e7d32"), c.rect(7, 5, 11, 6, "#c8e6c9")))

    def cake(c):
        for y in range(5, 12):
            c.rect(2 + (11 - y), y, 14, y + 1, "#ffe082")
        c.rect(2, 11, 14, 13, "#c58b4b")
        c.rect(9, 4, 14, 6, "#fff8e1")

    icon("cake", cake)
    icon("cat_food", lambda c: (c.rect(3, 3, 13, 14, "#8bc34a"), c.rect(3, 3, 13, 5, "#bdbdbd"), c.rect(6, 7, 10, 11, "#ffffff"), c.set(7, 8, "#222222"), c.set(9, 8, "#222222")))
    icon("ola_number", lambda c: (c.rect(2, 3, 14, 13, "#f48fb1"), c.rect(4, 6, 12, 7, "#880e4f"), c.rect(4, 9, 10, 10, "#880e4f"), c.set(12, 10, "#c2185b")))


if __name__ == "__main__":
    for sub in ("vox", "sprites", "icons"):
        os.makedirs(os.path.join(ROOT, sub), exist_ok=True)
    make_models()
    make_extra_models()
    make_sprites()
    make_icons()
    print("assets written to", os.path.normpath(ROOT))
