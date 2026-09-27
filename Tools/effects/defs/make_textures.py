"""絵に使うテクスチャを描く。色は白で、形はアルファに入れる (色はエフェクトの側で乗せる)

flash.png  128x128  白い閃光の丸。中心の芯は硬く、外へ柔らかく落ちる
spark.png   32x128  火花の筋。縦長で、端へ向かって細く消える
"""
import math
import sys
from pathlib import Path

from PIL import Image


def smoothstep(edge0, edge1, x):
    t = min(max((x - edge0) / (edge1 - edge0), 0.0), 1.0)
    return t * t * (3.0 - 2.0 * t)


def flash(size=128):
    image = Image.new("RGBA", (size, size))
    pixels = image.load()
    half = (size - 1) / 2.0
    for y in range(size):
        for x in range(size):
            r = math.hypot(x - half, y - half) / half  # 0 が中心、1 が縁
            core = 1.0 - smoothstep(0.0, 0.35, r)      # 硬い芯
            glow = (1.0 - min(r, 1.0)) ** 2.2          # 柔らかい光のにじみ
            a = min(1.0, core + 0.8 * glow)
            pixels[x, y] = (255, 255, 255, int(round(a * 255)))
    return image


def spark(width=32, height=128):
    image = Image.new("RGBA", (width, height))
    pixels = image.load()
    cx = (width - 1) / 2.0
    cy = (height - 1) / 2.0
    for y in range(height):
        for x in range(width):
            across = abs(x - cx) / cx                  # 幅方向 0..1
            along = abs(y - cy) / cy                   # 長さ方向 0..1
            width_at = 1.0 - along ** 1.5              # 端ほど細い
            a = (1.0 - smoothstep(0.0, max(width_at, 1e-3), across)) * (1.0 - along ** 3)
            pixels[x, y] = (255, 255, 255, int(round(max(a, 0.0) * 255)))
    return image


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "Texture"
    out.mkdir(parents=True, exist_ok=True)
    flash().save(out / "flash.png")
    spark().save(out / "spark.png")
    print("描いた:", out / "flash.png", out / "spark.png")


if __name__ == "__main__":
    main()
