#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Anteprima dei costumi di Clawd: legge le tabelle di claude_buddy/costumes.h e le disegna come
farebbe il firmware (stessi ancoraggi e stesso ordine), cosi' si vede il risultato senza la scheda.

  pip install pillow
  python tools/preview_costumes.py [uscita.png]
"""
import os
import re
import sys
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, "..", "claude_buddy", "costumes.h")
W = 466
CX, CY = W // 2, W // 2
U = 22
TOP = CY - 90          # BODY_TOP del firmware
BODY_H = 6 * U
EYE = (20, 14, 14)
SCALE = 2


def rgb565(r, g, b):
    return (r, g, b)


def parse(path):
    text = open(path, encoding="utf-8").read()
    tables = {}
    for m in re.finditer(r"static const Shape (\w+)\[\] = \{(.*?)\n\};", text, re.S):
        shapes = []
        for line in m.group(2).splitlines():
            line = line.split("//")[0].strip()
            mm = re.match(r"S_(RECT|RRECT|TRI|CIRC)\((AN_\w+(?:\s*\|\s*AN_\w+)*),\s*(.*)\),?$", line)
            if not mm:
                continue
            col = re.search(r"RGB565\((\d+),\s*(\d+),\s*(\d+)\)\s*$", mm.group(3))
            nums = [int(v) for v in re.findall(r"-?\d+", mm.group(3)[:col.start()])]
            shapes.append((mm.group(1), mm.group(2), nums, tuple(int(x) for x in col.groups())))
        tables[m.group(1)] = shapes
    bodies = re.findall(r"\{ (\w+), COS_N\(\w+\), (\w+), COS_N\(\w+\), RGB565\((\d+), (\d+), (\d+)\)", text)
    return tables, [None] + bodies  # indice 0 = nessun costume


def draw_shapes(d, shapes, cx, top, bottom, left, right, ayl, ayr, busy):
    for kind, an, n, col in shapes:
        held = "AN_HELD" in an
        if held and busy:
            continue
        an = an.replace("| AN_HELD", "").replace("AN_HELD |", "").strip()
        ox, oy = {"AN_TOP": (cx, top), "AN_BOTTOM": (cx, bottom), "AN_LEFT": (left, top), "AN_RIGHT": (right, top),
                  "AN_ARML": (left, ayl), "AN_ARMR": (right, ayr)}[an]
        S = SCALE
        if kind == "RECT":
            x, y, w, h = n
            d.rectangle([(ox + x) * S, (oy + y) * S, (ox + x + w) * S - 1, (oy + y + h) * S - 1], fill=col)
        elif kind == "RRECT":
            x, y, w, h, r = n
            d.rounded_rectangle([(ox + x) * S, (oy + y) * S, (ox + x + w) * S - 1, (oy + y + h) * S - 1], radius=r * S, fill=col)
        elif kind == "TRI":
            x1, y1, x2, y2, x3, y3 = n
            d.polygon([((ox + x1) * S, (oy + y1) * S), ((ox + x2) * S, (oy + y2) * S), ((ox + x3) * S, (oy + y3) * S)], fill=col)
        else:
            x, y, r = n
            d.ellipse([(ox + x - r) * S, (oy + y - r) * S, (ox + x + r) * S, (oy + y + r) * S], fill=col)


def clawd(tables, bodies, idx, arm=0.0, busy=False, dy=0):
    img = Image.new("RGB", (W * SCALE, W * SCALE), (0, 0, 0))
    d = ImageDraw.Draw(img)
    S = SCALE
    color = (215, 119, 87)
    back = front = []
    if idx > 0:
        b, f, r, g, bl = bodies[idx]
        back, front, color = tables[b], tables[f], (int(r), int(g), int(bl))
    top = TOP + dy
    bottom = top + BODY_H
    left, right = CX - 5 * U, CX + 5 * U
    d.ellipse([(CX - 5 * U) * S, (TOP + 8 * U + 8 - 9) * S, (CX + 5 * U) * S, (TOP + 8 * U + 8 + 9) * S], fill=(38, 38, 44))
    ayl = ayr = top + 2 * U - int(arm * 50)
    draw_shapes(d, back, CX, top, bottom, left, right, ayl, ayr, busy)
    for lx in (-4, -2, 1, 3):
        d.rectangle([(CX + lx * U) * S, (bottom - 1) * S, (CX + lx * U + U) * S, (bottom + 2 * U) * S], fill=color)
    d.rectangle([left * S, top * S, right * S, bottom * S], fill=color)
    d.rectangle([(left - 34) * S, ayl * S, left * S, (ayl + 40) * S], fill=color)
    d.rectangle([right * S, ayr * S, (right + 34) * S, (ayr + 40) * S], fill=color)
    ecy = top + int(BODY_H * 0.42)
    for side in (-1, 1):
        ex = CX + side * 3 * U
        d.rectangle([(ex - 10) * S, (ecy - U) * S, (ex + 10) * S, (ecy + U) * S], fill=EYE)
    draw_shapes(d, front, CX, top, bottom, left, right, ayl, ayr, busy)
    return img


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "costumes_preview.png")
    tables, bodies = parse(HEADER)
    n = len(bodies)
    # righe: a riposo / braccia alzate (oggetti in mano che seguono) / mani impegnate (oggetti nascosti)
    states = [(0.0, False), (0.9, False), (0.5, True)]
    sheet = Image.new("RGB", (W * SCALE * n, W * SCALE * len(states)), (0, 0, 0))
    for r, (arm, busy) in enumerate(states):
        for i in range(n):
            sheet.paste(clawd(tables, bodies, i, arm, busy, dy=30 if arm > 0.5 and not busy else 0), (i * W * SCALE, r * W * SCALE))
    sheet = sheet.resize((sheet.width // 3, sheet.height // 3), Image.LANCZOS)
    sheet.save(out)
    print(out, sheet.size)


if __name__ == "__main__":
    main()
