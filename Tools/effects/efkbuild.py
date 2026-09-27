"""エフェクトの定義 (.efkproj の XML) から .efkefc を作り、読めるかを確かめ、フレームごとの PNG を出す

入口は Tools/@build_effects.cmd で、efkprobe を組んでからこれを呼ぶ。置き方と定義の書き方は同じ場所の README.md

使い方:
  python Tools/effects/efkbuild.py [名前 ...] [--defs 定義のフォルダ] [--out 出力のフォルダ] [--frames N] [--no-probe]
                                   [--install] [--install-dir 置き場]

定義は 定義のフォルダ/*.efkproj と、組のフォルダ 定義のフォルダ/<組>/*.efkproj に置く
素材のフォルダは、定義のフォルダと組のフォルダの直下の Texture/
定義のフォルダの直下のそれ以外のフォルダが組のフォルダ (_ と . で始まる名前は見ない)
組ごとに分けると、2 つの作業が同じフォルダのファイルを書き合わない
どの組の素材も 出力/ と置き場の同じ並びへ重ねて写す。同じ道の素材を 2 か所が違う中身で持つと、どの本も作らずに失敗

編集ソフト (Effekseer 1.80.7) の場所は環境変数 NS_EFFEKSEER_TOOL で受ける。Effekseer.exe と bin/ が並ぶフォルダを渡す
道具の場所を読むのはこのファイルだけで、efkxml には組む時と走らせる時に bin の場所を渡す

名前を省くと、定義のフォルダと組のフォルダの *.efkproj を全部作る。名前は組をまたいで重ねない
1 本ごとに次を順に行い、どこかで失敗したらその本は失敗
  1. 定義から注釈を外した写しを 出力/<名前>.efkproj に書く。編集ソフトの読み込みは注釈で落ちる
     素材のフォルダ、例えば Texture/ を 出力/ の同じ場所へ写す
     編集ソフトは素材の道を書き出し先から見た相対に書き換えるので、.efkefc の隣に同じ並びで置く
  2. 編集ソフト (Effekseer.exe -cui) で 出力/<名前>.efkefc を書き出す
  3. 書き出した .efkefc の塊 (INFO・EDIT・BIN_) に、この機械の利用者のフォルダや道が混ざっていないかを見る
  4. efkxml check: 定義の葉が全部読まれたか、.efkefc の編集用の中身が定義と一字一句同じか
  5. efkprobe: 実行側の Effect::Create が通るか、節の木と寿命、フレームごとのインスタンス数と PNG を 出力/frames/<名前>/
  6. PNG を 1 枚に並べた一覧を 出力/frames/<名前>_sheet.png
  7. --install を渡した時だけ、全部通った後に .efkefc と素材のフォルダを Assets/Effects/ へ同じ並びで写す
     --install-dir を渡すと Assets/Effects/ の代わりにそこへ写す
     出力のフォルダは作業場で、注釈を外した写しと PNG を含む。ゲームへ渡すのは .efkefc と素材だけ
"""
import argparse
import os
import re
import shutil
import struct
import subprocess
import sys
import time
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
TOOL_ENVIRONMENT = "NS_EFFEKSEER_TOOL"
# efkprobe は @build_effects.cmd が Debug で組む。組み方は Tools/effects/premake.lua の EffectProbe
EFKPROBE = REPO / "build" / "bin" / "Debug-windows-x86_64" / "EffectProbe" / "efkprobe.exe"
INSTALL_DESTINATION = REPO / "Assets" / "Effects"

COMMENT = re.compile(r"\s*<!--.*?-->", re.DOTALL)
# ドライブから始まる道。C:/ と C:\ の形
ABSOLUTE_PATH = re.compile(r"[A-Za-z]:[\\/]")
# 絶対の道を探す塊。INFO は依存の道の一覧、EDIT は定義の文字。BIN_ は浮動小数の並びが偶然 "x:/" に読めうるので、
# 利用者の名前だけを探す。BIN_ が持つ依存の道は INFO にも載る
TEXT_CHUNKS = ("INFO", "EDIT")
# 定義が読む素材の置き場の名前。今の定義が読む素材はテクスチャだけ
ASSET_FOLDERS = ("Texture",)


def run(arguments):
    completed = subprocess.run([str(a) for a in arguments], capture_output=True)
    text = completed.stdout.decode("utf-8", "replace") + completed.stderr.decode("utf-8", "replace")
    return completed.returncode, text


def find_tool():
    """NS_EFFEKSEER_TOOL から編集ソフトのフォルダを引く。使えなければ理由の文を返す"""
    value = os.environ.get(TOOL_ENVIRONMENT, "")
    if value == "":
        return None, (f"環境変数 {TOOL_ENVIRONMENT} が無い。Effekseer 1.80.7 の Tool フォルダ "
                      "(Effekseer.exe と bin/ が並ぶ所) を渡す。置き方は Tools/effects/README.md")
    tool = Path(value)
    if not (tool / "Effekseer.exe").is_file():
        return None, f"{TOOL_ENVIRONMENT} のフォルダに Effekseer.exe が無い: {tool}"
    if not (tool / "bin" / "EffekseerCore.dll").is_file():
        return None, f"{TOOL_ENVIRONMENT} のフォルダに bin/EffekseerCore.dll が無い: {tool}"
    return tool, None


def build_efkxml(tool, out):
    """efkxml を組み、実行ファイルの道を返す。組めなければ None"""
    artifacts = out / "efkxml"
    code, text = run([
        "dotnet", "build", HERE / "efkxml" / "efkxml.csproj", "-c", "Release", "-nologo", "-v:q",
        "--artifacts-path", artifacts, f"-p:EffekseerToolBin={(tool / 'bin').as_posix()}",
    ])
    exe = artifacts / "bin" / "efkxml" / "release" / "efkxml.exe"
    if code != 0 or not exe.is_file():
        print(f"失敗: efkxml を組めなかった\n{text}")
        return None
    return exe


def group_folders(defs):
    """組のフォルダを名前の順に返す"""
    return sorted(child for child in defs.iterdir()
                  if child.is_dir() and child.name not in ASSET_FOLDERS and child.name[0] not in "_.")


def find_definitions(defs):
    """定義の名前から道を引く表を返す。同じ名前が 2 か所にあれば、表の代わりに理由の文を返す"""
    found = {}
    for folder in [defs] + group_folders(defs):
        for definition in sorted(folder.glob("*.efkproj")):
            if definition.stem in found:
                return None, f"定義の名前 {definition.stem} が 2 か所にある: {found[definition.stem]} と {definition}"
            found[definition.stem] = definition
    return found, None


def collect_assets(defs):
    """素材のフォルダの中のファイルを、写す先の相対の道から元の道を引く表で返す

    同じ相対の道を 2 か所が違う中身で持てば、表の代わりに理由の文を返す。同じ中身なら 1 つに重ねる
    """
    asset_folders = []
    for folder in [defs] + group_folders(defs):
        for child in sorted(folder.iterdir()):
            if child.is_dir() and child.name in ASSET_FOLDERS:
                asset_folders.append((folder, child))
    assets = {}
    for base, folder in asset_folders:
        for source in sorted(p for p in folder.rglob("*") if p.is_file()):
            relative = source.relative_to(base)
            if relative in assets and assets[relative].read_bytes() != source.read_bytes():
                return None, f"素材の道 {relative.as_posix()} を 2 か所が違う中身で持つ: {assets[relative]} と {source}"
            assets[relative] = source
    return assets, None


def copy_assets(assets, destination):
    for relative, source in assets.items():
        target = destination / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)


def stage(definition, assets, out):
    copy_assets(assets, out)
    staged = out / definition.name
    text = definition.read_text(encoding="utf-8")
    staged.write_text(COMMENT.sub("", text), encoding="utf-8", newline="\n")
    return staged


def compile_effect(tool, staged, target):
    started = time.time()
    code, text = run([tool / "Effekseer.exe", "-cui", "-in", staged.as_posix(), "-o", target.as_posix()])
    # 編集ソフトは開けなくても終了コード 0 を返す。書き出した物の時刻で成否を見る
    if not target.exists() or target.stat().st_mtime < started - 1:
        return False, text
    return True, text


def efkefc_chunks(path):
    """.efkefc の塊を (名前, 中身) で返す。EDIT は zlib を解いた中身にする"""
    data = path.read_bytes()
    chunks = []
    position = 8
    while position + 8 <= len(data):
        tag = data[position:position + 4].decode("ascii", "replace")
        size = struct.unpack("<I", data[position + 4:position + 8])[0]
        body = data[position + 8:position + 8 + size]
        if tag == "EDIT":
            body = zlib.decompress(body)
        chunks.append((tag, body))
        position += 8 + size
    return chunks


def find_personal_traces(path):
    """書き出した .efkefc の中の、この機械の利用者の名前と絶対の道を探す。見つけた物の説明を並べて返す"""
    home = Path.home()
    needles = {home.name, home.as_posix(), str(home)}
    found = []
    for tag, body in efkefc_chunks(path):
        for encoding in ("utf-8", "utf-16-le"):
            text = body.decode(encoding, "ignore")
            for needle in needles:
                if needle.lower() in text.lower():
                    found.append(f"{tag} の塊 ({encoding}) に利用者の名前か道: {needle}")
            if tag not in TEXT_CHUNKS:
                continue
            for match in ABSOLUTE_PATH.finditer(text):
                found.append(f"{tag} の塊 ({encoding}) に絶対の道: {text[match.start():match.start() + 40]!r}")
    return found


def contact_sheet(frames_dir, sheet_path, columns=8, cell=160):
    from PIL import Image, ImageDraw

    frames = sorted(frames_dir.glob("frame_*.png"))
    if not frames:
        return None
    rows = (len(frames) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell, rows * (cell + 16)), (0, 0, 0))
    draw = ImageDraw.Draw(sheet)
    for index, frame in enumerate(frames):
        image = Image.open(frame).convert("RGB").resize((cell, cell), Image.LANCZOS)
        x = (index % columns) * cell
        y = (index // columns) * (cell + 16)
        sheet.paste(image, (x, y + 16))
        draw.text((x + 4, y + 2), frame.stem.replace("frame_", "f"), fill=(220, 220, 220))
    sheet.save(sheet_path)
    return sheet_path


def build_one(name, tool, efkxml, definitions, assets, out, frames, probe):
    print(f"==== {name}")
    definition = definitions.get(name)
    if definition is None:
        print(f"失敗: 定義 {name}.efkproj が定義のフォルダにも組のフォルダにも無い")
        return False
    staged = stage(definition, assets, out)
    target = out / f"{name}.efkefc"
    ok, text = compile_effect(tool, staged, target)
    if not ok:
        print(f"失敗: 編集ソフトが .efkefc を書き出さなかった (注釈・StartFrame・EndFrame・IsLoop を見る)\n{text}")
        return False
    print(f"書き出した: {target}")

    traces = find_personal_traces(target)
    if traces:
        for trace in traces:
            print(f"失敗: {target.name} の {trace}")
        return False
    print("塊 INFO・EDIT・BIN_ に利用者の名前と絶対の道は無い")

    code, text = run([efkxml, "--tool-bin", tool / "bin", "check", staged, target])
    print(text.rstrip())
    if code != 0:
        return False

    if not probe:
        return True
    if not EFKPROBE.exists():
        print(f"失敗: efkprobe が無い ({EFKPROBE})。Tools/@build_effects.cmd で組む")
        return False
    frames_dir = out / "frames" / name
    code, text = run([EFKPROBE, target, frames_dir, "--frames", str(frames)])
    print(text.rstrip())
    if code != 0:
        return False
    sheet = contact_sheet(frames_dir, out / "frames" / f"{name}_sheet.png")
    if sheet is not None:
        print(f"一覧: {sheet}")
    return True


def install(names, assets, out, destination):
    """通った本だけを写す。1 本でも落ちたら main が先に返るので、ここへは全部通った時だけ来る"""
    destination.mkdir(parents=True, exist_ok=True)
    copy_assets(assets, destination)
    for name in names:
        shutil.copy2(out / f"{name}.efkefc", destination / f"{name}.efkefc")
        print(f"写した: {destination / (name + '.efkefc')}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("names", nargs="*")
    parser.add_argument("--defs", default=str(HERE / "defs"))
    parser.add_argument("--out", default=str(REPO / "build" / "effects"))
    parser.add_argument("--frames", type=int, default=32)
    parser.add_argument("--no-probe", action="store_true")
    parser.add_argument("--install", action="store_true")
    # 試し用の絵 (test_defs/) は Source/Tests/data/effects/ へ置く。既定の Assets/Effects/ は出荷の絵だけ
    parser.add_argument("--install-dir", default=str(INSTALL_DESTINATION))
    args = parser.parse_args()

    tool, problem = find_tool()
    if tool is None:
        print(f"失敗: {problem}")
        return 1

    defs = Path(args.defs).resolve()
    out = Path(args.out).resolve()
    out.mkdir(parents=True, exist_ok=True)
    efkxml = build_efkxml(tool, out)
    if efkxml is None:
        return 1

    definitions, problem = find_definitions(defs)
    if definitions is None:
        print(f"失敗: {problem}")
        return 1
    assets, problem = collect_assets(defs)
    if assets is None:
        print(f"失敗: {problem}")
        return 1
    names = args.names or sorted(definitions)
    if not names:
        print(f"失敗: 定義のフォルダと組のフォルダに .efkproj が 1 本も無い: {defs}")
        return 1
    failed = [n for n in names
              if not build_one(n, tool, efkxml, definitions, assets, out, args.frames, not args.no_probe)]
    print(f"==== 結果: {len(names) - len(failed)} / {len(names)} 本が通った")
    if failed:
        print("通らなかった: " + ", ".join(failed))
        return 1
    if args.install:
        install(names, assets, out, Path(args.install_dir).resolve())
    return 0


if __name__ == "__main__":
    sys.exit(main())
