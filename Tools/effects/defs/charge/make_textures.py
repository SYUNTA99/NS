"""溜めの組 (charge.・release.・slam. の層) の定義が読むテクスチャを描き、同じフォルダの Texture/ に置く

色は白で、形はアルファに入れる (色はエフェクトの側で乗せる)。同じ入力から同じ画素を描く
ファイル名は charge_ で始め、impact の組の素材と道を重ねない。efkbuild.py は 2 つの組が同じ道を違う中身で持つと止まる
描く物は DRAWERS に「拡張子を除いたファイル名: Image を返す関数」で並べる

charge_shell    256x256  縁が明るい円。丸まりの殻 (charge.curl) と溜まる光の殻 (charge.gather)
charge_ring     256x256  細い輪。足元の空気の輪 (charge.curl) と放しの輪 (release.burst)
charge_arc      256x256  向かい合う 2 本の弧 (1 本 120 度)。頭が太く明るく、尾が細く消える。回転の弧 (charge.spin)
charge_dot       64x64   柔らかい点。溜まる光の点 (charge.gather)
charge_flash_h  256x64   横長の閃き。芯の横線と柔らかいにじみ (charge.full)
charge_pillar_v  64x256  縦の光の柱。根元 (下) が太く明るく、先 (上) へ細る (charge.full)
charge_streak    32x128  先細りの筋。放しの弾けの筋 (release.burst)
charge_glow     128x128  柔らかい円。放しの丸屋根の光と、はじけの後のもや (release.burst)
charge_halo     128x128  中が抜けた柔らかい円。放しのはじけの光 (release.burst)
charge_aura     128x128  charge_halo の上の ±45 度を抜いた形。玉を包む光 (charge.gather)
charge_aura_rim 128x128  charge_aura と同じく中が抜けて上を抜いた形で、最も濃い所を半径 0.82 へ外へずらした物。
                         玉を包む光の下に敷く濃い青の塗り (charge.gather)
charge_trail     64x64   横ぼかしの帯。横は中心が明るく、縦は一様 (slam.trail のリボン)
charge_smoke    128x128  CC0 の煙の塊 (Effekseer 1.80.7 の Sample/01_AndrewFM01/Texture/smoke_tex.png) の明るさを
                         透明度にした物。削る粉 (charge.grind)。
                         元は環境変数 NS_EFFEKSEER_TOOL の 1 つ上の Sample から読む
"""
import os
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


def shell(size=256):
    # 縁 (半径 0.92) に明るい帯、内側は薄い膜。玉を包む球の縁が光って見える形
    x, y = grid(size, size)
    r = np.hypot(x, y)
    rim = np.exp(-((r - 0.9) / 0.06) ** 2)
    membrane = 0.18 * (1.0 - smoothstep(0.7, 0.92, r))
    outside = 1.0 - smoothstep(0.96, 1.0, r)
    return white_with_alpha((rim + membrane) * outside)


def ring(size=256):
    # 半径 0.88 の細い輪。外は硬く切り、内側へ少しにじむ
    x, y = grid(size, size)
    r = np.hypot(x, y)
    band = np.exp(-((r - 0.88) / 0.035) ** 2)
    inner = 0.25 * np.exp(-((r - 0.8) / 0.08) ** 2)
    outside = 1.0 - smoothstep(0.95, 1.0, r)
    return white_with_alpha((band + inner) * outside)


def arc(size=256):
    # 半径 0.88 の円周の上に 120 度の弧を 2 本、向かい合わせに置く。頭 (角度の大きい側) が太く明るい
    x, y = grid(size, size)
    r = np.hypot(x, y)
    angle = np.degrees(np.arctan2(-y, x)) % 360.0
    alpha = np.zeros_like(r)
    for start in (0.0, 180.0):
        t = ((angle - start) % 360.0) / 120.0  # 0 が尾、1 が頭
        inside = (t >= 0.0) & (t <= 1.0)
        thickness = 0.03 + 0.07 * np.clip(t, 0.0, 1.0)
        strength = np.clip(t, 0.0, 1.0) ** 1.5 * (1.0 - smoothstep(0.93, 1.0, t))
        band = np.exp(-((r - 0.88) / thickness) ** 2)
        alpha = np.maximum(alpha, np.where(inside, band * strength, 0.0))
    return white_with_alpha(alpha * 1.2)


def dot(size=64):
    x, y = grid(size, size)
    r = np.hypot(x, y)
    return white_with_alpha(np.exp(-(r / 0.35) ** 2) * (1.0 - smoothstep(0.9, 1.0, r)))


def flash_h(width=256, height=64):
    # 横に長い閃き。中心の横線 (芯) と、その上下の柔らかいにじみ。左右の端は細って消える
    x, y = grid(width, height)
    along = 1.0 - np.abs(x) ** 1.6
    core = np.exp(-(y / 0.08) ** 2)
    glow = 0.45 * np.exp(-(y / 0.45) ** 2)
    center = 0.6 * np.exp(-((x / 0.12) ** 2 + (y / 0.6) ** 2))
    return white_with_alpha((core + glow) * np.clip(along, 0.0, 1.0) + center)


def pillar_v(width=64, height=256):
    # 下 (y = 1) が根元で太く明るく、上 (y = -1) へ細って消える柱。根元を玉の中心に置き、玉の上へ立てる
    x, y = grid(width, height)
    t = (y + 1.0) / 2.0  # 0 が先、1 が根元
    half_width = 0.15 + 0.85 * t ** 1.5
    across = np.abs(x) / half_width
    core = np.exp(-(across / 0.25) ** 2)
    glow = 0.4 * np.exp(-(across / 0.8) ** 2)
    along = t ** 0.6 * (1.0 - smoothstep(0.93, 1.0, t))
    return white_with_alpha((core + glow) * along)


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


def aura(size=128):
    # halo と同じ中が抜けた柔らかい円から、上 (y が負) の中心の角度 ±45 度を抜き、±80 度で元の明るさへ戻す。
    # 後ろからのカメラで玉のすぐ上に見える床の狙いの矢印に、玉を包む光を重ねない
    x, y = grid(size, size)
    r = np.hypot(x, y)
    inner = smoothstep(0.2, 0.55, r)
    outer = (1.0 - smoothstep(0.55, 1.0, r)) ** 1.5
    from_up = np.degrees(np.arctan2(np.abs(x), -y))
    return white_with_alpha(inner * outer * smoothstep(45.0, 80.0, from_up))


def aura_rim(size=128):
    # aura の最も明るい半径 0.55 より外の 0.82 を最も濃くする。加算の白い光のすぐ外側に濃い青を置き、明るい背景でも
    # 光の輪郭を背景より暗い色で読ませる。上の抜き方は aura と同じで、床の狙いの矢印を青で塗らない
    x, y = grid(size, size)
    r = np.hypot(x, y)
    inner = smoothstep(0.5, 0.82, r)
    outer = 1.0 - smoothstep(0.82, 1.0, r)
    from_up = np.degrees(np.arctan2(np.abs(x), -y))
    return white_with_alpha(inner * outer * smoothstep(45.0, 80.0, from_up))


def trail(size=64):
    # 横 (x) は中心が明るく端で消え、縦 (y) は一様。リボンの幅の向きに横を当てる
    x, _ = grid(size, size)
    return white_with_alpha(np.exp(-(x / 0.45) ** 2) * (1.0 - smoothstep(0.9, 1.0, np.abs(x))))


def smoke(size=128):
    tool = os.environ.get("NS_EFFEKSEER_TOOL", "")
    if tool == "":
        raise SystemExit("失敗: 環境変数 NS_EFFEKSEER_TOOL が無いので、CC0 の煙の元 smoke_tex.png を読めない")
    source = Path(tool).parent / "Sample" / "01_AndrewFM01" / "Texture" / "smoke_tex.png"
    if not source.is_file():
        raise SystemExit(f"失敗: CC0 の煙の元が無い: {source}")
    image = Image.open(source).convert("RGBA").resize((size, size), Image.LANCZOS)
    pixels = np.asarray(image).astype(np.float32) / 255.0
    luminance = pixels[..., 0] * 0.299 + pixels[..., 1] * 0.587 + pixels[..., 2] * 0.114
    return white_with_alpha(luminance * pixels[..., 3])


DRAWERS = {
    "charge_shell": shell,
    "charge_ring": ring,
    "charge_arc": arc,
    "charge_dot": dot,
    "charge_flash_h": flash_h,
    "charge_pillar_v": pillar_v,
    "charge_streak": streak,
    "charge_glow": glow,
    "charge_halo": halo,
    "charge_aura": aura,
    "charge_aura_rim": aura_rim,
    "charge_trail": trail,
    "charge_smoke": smoke,
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
