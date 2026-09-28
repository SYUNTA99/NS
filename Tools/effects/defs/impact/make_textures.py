"""当たりの組 (impact.・launch.・rebound.・land. の層) の定義が読むテクスチャを描き、同じフォルダの Texture/ に置く

色は白で、形はアルファに入れる (色はエフェクトの側で乗せる)。同じ入力から同じ画素を描く
ファイル名は impact_ で始め、charge の組の素材と道を重ねない。efkbuild.py は 2 つの組が同じ道を違う中身で持つと止まる
描く物は DRAWERS に「拡張子を除いたファイル名: Image を返す関数」で並べる

impact_star   256x256  核の星形。長い 4 本と短い 4 本の先細りの放射と、真ん中の小さな丸
impact_core   128x128  核の芯。硬い丸で、縁だけ柔らかく落ちる
impact_line   256x32   光条。横に長い細い線で、両端へ細って消える
impact_spark   32x128  火花と弾かれ線の筋。縦長で、端へ細って消える
impact_band    64x32   輪の帯。縦の真ん中が濃く、上下へ柔らかく落ちる。横は一様 (輪の周に沿って巻く)
impact_glow   128x128  照りの丸。中心から縁へ滑らかに落ちる
impact_puff   128x128  土ぼこりの塊。丸い塊に固定の種の揺らぎを掛けた縁
impact_ribbon  64x64   飛び出しの尾と反動の尾の帯。横 (帯の幅の向き) は中心が濃く縁で消え、縦 (帯の長さの向き) は一様
impact_rays   512x512  核の放射の線。真ん中を空けた細い 8 本 (長い 4 本と短い 4 本) が、根元から先へ細って消える
"""
import math
import sys
from pathlib import Path

import numpy
from PIL import Image


def smoothstep(edge0, edge1, x):
    t = numpy.clip((x - edge0) / (edge1 - edge0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def to_image(alpha):
    """0〜1 のアルファの配列を、色が白の RGBA の絵にする"""
    a = numpy.clip(alpha, 0.0, 1.0)
    rgba = numpy.zeros(a.shape + (4,), dtype=numpy.uint8)
    rgba[..., 0:3] = 255
    rgba[..., 3] = numpy.round(a * 255.0).astype(numpy.uint8)
    return Image.fromarray(rgba, "RGBA")


def grid(width, height):
    """画素の中心を -1〜1 に置いた座標 (x は右、y は下)"""
    xs = (numpy.arange(width) + 0.5) / width * 2.0 - 1.0
    ys = (numpy.arange(height) + 0.5) / height * 2.0 - 1.0
    return numpy.meshgrid(xs, ys)


def star(size=256):
    x, y = grid(size, size)
    r = numpy.hypot(x, y)
    theta = numpy.arctan2(y, x)
    alpha = numpy.zeros_like(r)
    # 長い 4 本は上下左右、短い 4 本は斜め。どちらも根元が太く先へ細る
    for count, offset, length, width in ((4, 0.0, 0.98, 0.20), (4, math.pi / 4.0, 0.55, 0.16)):
        for i in range(count):
            angle = offset + i * (2.0 * math.pi / count)
            delta = numpy.angle(numpy.exp(1j * (theta - angle)))
            along = r / length
            half_width = width * numpy.clip(1.0 - along, 0.0, 1.0) ** 1.4
            across = numpy.abs(delta) * numpy.maximum(r, 1e-4)
            ray = (1.0 - smoothstep(0.0, numpy.maximum(half_width * 0.5, 1e-4), across))
            ray = ray * numpy.clip(1.0 - along, 0.0, 1.0) ** 0.6
            alpha = numpy.maximum(alpha, ray)
    disc = 1.0 - smoothstep(0.10, 0.26, r)
    return to_image(numpy.maximum(alpha, disc))


def rays(size=512):
    x, y = grid(size, size)
    r = numpy.hypot(x, y)
    theta = numpy.arctan2(y, x)
    alpha = numpy.zeros_like(r)
    # 根元の半幅は半径の 2.3% (縁の柔らかい所を除いて見える太さは半径の約 3%。中心近くの半径 1.54 m で 0.05 m、
    # 隠さない決まりの線の太さ 0.025〜0.06 m の中)。真ん中の 3 割は核の星形が受け持つので空け、面を塗り足さない
    # 短い 4 本は長い 4 本の 6.5 割。斜めにも線を置き、上下と左右だけの十字より広がりを埋める
    for count, offset, length in ((4, 0.0, 0.98), (4, math.pi / 4.0, 0.65)):
        for i in range(count):
            angle = offset + i * (2.0 * math.pi / count)
            delta = numpy.angle(numpy.exp(1j * (theta - angle)))
            along = numpy.clip(r / length, 0.0, 1.0)
            half_width = 0.019 * (1.0 - along) + 0.004
            across = numpy.abs(delta) * numpy.maximum(r, 1e-4)
            ray = 1.0 - smoothstep(half_width * 0.4, half_width, across)
            ray = ray * (1.0 - smoothstep(0.75, 1.0, r / length)) * smoothstep(0.22, 0.32, r)
            alpha = numpy.maximum(alpha, ray)
    return to_image(alpha)


def core(size=128):
    x, y = grid(size, size)
    r = numpy.hypot(x, y)
    return to_image(1.0 - smoothstep(0.55, 0.95, r))


def line(width=256, height=32):
    x, y = grid(width, height)
    # 縦の芯は細く、両端の 3 割で細って消える
    across = numpy.exp(-(y / 0.22) ** 2)
    along = 1.0 - smoothstep(0.7, 1.0, numpy.abs(x))
    return to_image(across * along)


def spark(width=32, height=128):
    x, y = grid(width, height)
    along = numpy.abs(y)
    half_width = numpy.clip(1.0 - along ** 1.5, 1e-3, 1.0)
    alpha = (1.0 - smoothstep(0.0, half_width, numpy.abs(x))) * (1.0 - along ** 3)
    return to_image(alpha)


def band(width=64, height=32):
    x, y = grid(width, height)
    return to_image(numpy.exp(-(y / 0.45) ** 2) * (1.0 - smoothstep(0.85, 1.0, numpy.abs(y))))


def glow(size=128):
    x, y = grid(size, size)
    r = numpy.hypot(x, y)
    return to_image(numpy.clip(1.0 - r, 0.0, 1.0) ** 1.8)



def ribbon(size=64):
    x, _ = grid(size, size)
    return to_image(numpy.exp(-(x / 0.5) ** 2) * (1.0 - smoothstep(0.85, 1.0, numpy.abs(x))))


def value_noise(size, cells, rng):
    """格子の乱数を滑らかに繋いだ揺らぎ。0〜1"""
    lattice = rng.random((cells + 1, cells + 1))
    coords = numpy.arange(size) / size * cells
    i = numpy.floor(coords).astype(int)
    f = coords - i
    f = f * f * (3.0 - 2.0 * f)
    iy, ix = numpy.meshgrid(i, i, indexing="ij")
    fy, fx = numpy.meshgrid(f, f, indexing="ij")
    a = lattice[iy, ix]
    b = lattice[iy, ix + 1]
    c = lattice[iy + 1, ix]
    d = lattice[iy + 1, ix + 1]
    return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy


def puff(size=128):
    rng = numpy.random.default_rng(20260927)
    x, y = grid(size, size)
    r = numpy.hypot(x, y)
    noise = 0.55 * value_noise(size, 4, rng) + 0.3 * value_noise(size, 8, rng) + 0.15 * value_noise(size, 16, rng)
    # 縁を揺らぎで崩し、中も揺らぎで濃淡を付ける
    edge = 1.0 - smoothstep(0.35, 0.95, r + (noise - 0.5) * 0.5)
    return to_image(edge * (0.55 + 0.45 * noise))


DRAWERS = {
    "impact_star": star,
    "impact_core": core,
    "impact_line": line,
    "impact_spark": spark,
    "impact_band": band,
    "impact_glow": glow,
    "impact_puff": puff,
    "impact_ribbon": ribbon,
    "impact_rays": rays,
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
