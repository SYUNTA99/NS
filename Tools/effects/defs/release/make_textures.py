"""放しの弾けと突進の尾が読むテクスチャを同じフォルダのTextureへ描く。

色は白、形はアルファ。charge_の画像名は素材の相対参照を保つために維持する。
"""
import sys
from pathlib import Path

import numpy as np
from PIL import Image


def smoothstep(edge0, edge1, x):
    t = np.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def grid(width, height):
    """画素の中心の座標を -1..1 で返す (x は右、y は下)"""
    xs = (np.arange(width) + 0.5) / width * 2.0 - 1.0
    ys = (np.arange(height) + 0.5) / height * 2.0 - 1.0
    return np.meshgrid(xs, ys)


def white_with_alpha(alpha):
    alpha = np.clip(alpha, 0.0, 1.0)
    height, width = alpha.shape
    rgba = np.zeros((height, width, 4), dtype=np.uint8)
    rgba[..., 0:3] = 255
    rgba[..., 3] = np.round(alpha * 255.0).astype(np.uint8)
    return Image.fromarray(rgba, "RGBA")


def ring(size=256):
    # 半径 0.88 の細い輪。外は硬く切り、内側へ少しにじむ
    x, y = grid(size, size)
    r = np.hypot(x, y)
    band = np.exp(-((r - 0.88) / 0.035) ** 2)
    inner = 0.25 * np.exp(-((r - 0.8) / 0.08) ** 2)
    outside = 1.0 - smoothstep(0.95, 1.0, r)
    return white_with_alpha((band + inner) * outside)


def streak(width=32, height=128):
    # 上 (y = -1) が頭で太く、下へ細って消える筋
    x, y = grid(width, height)
    t = (y + 1.0) / 2.0  # 0 が頭、1 が尾
    half_width = 0.9 * (1.0 - t) + 0.05
    across = np.abs(x) / half_width
    alpha = (1.0 - smoothstep(0.3, 1.0, across)) * (1.0 - t) ** 0.7
    return white_with_alpha(alpha)


def glow(size=128):
    x, y = grid(size, size)
    r = np.hypot(x, y)
    return white_with_alpha((1.0 - smoothstep(0.0, 1.0, r)) ** 1.8)


def halo(size=128):
    # 中が抜けた柔らかい円。半径 0.55 が最も明るく、中心 (半径 0.3 まで) と外へ薄れる。玉と玉に付いた層を白く塗らない
    x, y = grid(size, size)
    r = np.hypot(x, y)
    inner = smoothstep(0.2, 0.55, r)
    outer = (1.0 - smoothstep(0.55, 1.0, r)) ** 1.5
    return white_with_alpha(inner * outer)


def trail(size=64):
    # 横 (x) は中心が明るく端で消え、縦 (y) は一様。リボンの幅の向きに横を当てる
    x, _ = grid(size, size)
    return white_with_alpha(np.exp(-(x / 0.45) ** 2) * (1.0 - smoothstep(0.9, 1.0, np.abs(x))))


DRAWERS = {
    "charge_ring": ring,
    "charge_streak": streak,
    "charge_glow": glow,
    "charge_halo": halo,
    "charge_trail": trail,
}


def main():
    out = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "Texture"
    if not DRAWERS:
        print("描く物が無い")
        return
    out.mkdir(parents=True, exist_ok=True)
    for name, draw in DRAWERS.items():
        draw().save(out / f"{name}.png")
        print("描いた:", out / f"{name}.png")


if __name__ == "__main__":
    main()
