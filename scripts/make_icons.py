"""VoiceTune: erzeugt die 16x16-Menue-Icons (PNG, RGBA) fuer das TS3-Plugin-Menue nach assets/icons/.
Gezeichnet auf 64x64 und per Box-Filter auf 16x16 verkleinert (Antialiasing). Keine Abhaengigkeiten ausser der Standardbibliothek.
Aufruf: python scripts/make_icons.py
"""
import math
import os
import struct
import zlib

BIG, SMALL = 64, 16
ACCENT = (61, 139, 255)
WHITE = (240, 244, 250)


class Canvas:
    def __init__(self):
        self.px = [[(0, 0, 0, 0)] * BIG for _ in range(BIG)]

    def shape(self, inside, color):
        for y in range(BIG):
            for x in range(BIG):
                if inside(x + 0.5, y + 0.5):
                    self.px[y][x] = (*color, 255)

    def circle(self, cx, cy, r, color):
        self.shape(lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r, color)

    def ellipse(self, cx, cy, rx, ry, color):
        self.shape(lambda x, y: ((x - cx) / rx) ** 2 + ((y - cy) / ry) ** 2 <= 1, color)

    def rect(self, x0, y0, x1, y1, color):
        self.shape(lambda x, y: x0 <= x <= x1 and y0 <= y <= y1, color)

    def round_rect(self, x0, y0, x1, y1, r, color):
        def inside(x, y):
            if not (x0 <= x <= x1 and y0 <= y <= y1):
                return False
            cx = min(max(x, x0 + r), x1 - r)
            cy = min(max(y, y0 + r), y1 - r)
            return (x - cx) ** 2 + (y - cy) ** 2 <= r * r
        self.shape(inside, color)

    def ring(self, cx, cy, r_out, r_in, color):
        self.shape(lambda x, y: r_in ** 2 <= (x - cx) ** 2 + (y - cy) ** 2 <= r_out ** 2, color)

    def triangle(self, p0, p1, p2, color):
        def sign(a, b, c):
            return (a[0] - c[0]) * (b[1] - c[1]) - (b[0] - c[0]) * (a[1] - c[1])

        def inside(x, y):
            p = (x, y)
            d1, d2, d3 = sign(p, p0, p1), sign(p, p1, p2), sign(p, p2, p0)
            neg = d1 < 0 or d2 < 0 or d3 < 0
            pos = d1 > 0 or d2 > 0 or d3 > 0
            return not (neg and pos)
        self.shape(inside, color)

    def downsample(self):
        k = BIG // SMALL
        rows = []
        for y in range(SMALL):
            row = bytearray()
            for x in range(SMALL):
                r = g = b = a = 0.0
                for dy in range(k):
                    for dx in range(k):
                        pr, pg, pb, pa = self.px[y * k + dy][x * k + dx]
                        r += pr * pa; g += pg * pa; b += pb * pa; a += pa
                if a == 0:
                    row += bytes((0, 0, 0, 0))
                else:
                    row += bytes((round(r / a), round(g / a), round(b / a), round(a / (k * k))))
            rows.append(bytes(row))
        return rows


def write_png(path, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", SMALL, SMALL, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def plugin_icon():  # VoiceTune-Menue-Icon: Equalizer-Balken auf violetter Kachel (bewusst NICHT das App-Icon)
    c = Canvas()
    c.round_rect(2, 2, 62, 62, 14, (122, 88, 255))
    for x, h in ((10, 22), (22, 40), (34, 50), (46, 30)):
        c.round_rect(x, 32 - h // 2, x + 8, 32 + h // 2, 4, WHITE)
    return c


def toggle_icon():  # Ein/Aus-Symbol
    c = Canvas()
    c.ring(32, 35, 24, 17, ACCENT)
    c.px = [[(0, 0, 0, 0) if (24 <= x <= 40 and y <= 30) else p for x, p in enumerate(row)] for y, row in enumerate(c.px)]  # Luecke oben im Ring
    c.round_rect(29, 6, 35, 34, 3, ACCENT)  # senkrechter Strich
    return c


def settings_icon():  # Schieberegler
    c = Canvas()
    for y, kx in ((14, 22), (32, 42), (50, 28)):
        c.round_rect(6, y - 3, 58, y + 3, 3, (110, 118, 135))
        c.round_rect(6, y - 3, kx, y + 3, 3, ACCENT)
        c.circle(kx, y, 9, WHITE)
    return c


if __name__ == "__main__":
    out = os.path.join(os.path.dirname(__file__), "..", "assets", "icons")
    os.makedirs(out, exist_ok=True)
    for name, make in (("plugin", plugin_icon), ("toggle", toggle_icon), ("settings", settings_icon)):
        write_png(os.path.join(out, name + ".png"), make().downsample())
        print("geschrieben:", os.path.join(out, name + ".png"))
