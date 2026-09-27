"""溜めの組 (charge.・release.・slam. の層) の定義が読むテクスチャを描き、同じフォルダの Texture/ に置く

色は白で、形はアルファに入れる (色はエフェクトの側で乗せる)。同じ入力から同じ画素を描く
ファイル名は charge_ で始め、impact の組の素材と道を重ねない。efkbuild.py は 2 つの組が同じ道を違う中身で持つと止まる
描く物は DRAWERS に「拡張子を除いたファイル名: Image を返す関数」で並べる
"""
import sys
from pathlib import Path

DRAWERS = {}


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
