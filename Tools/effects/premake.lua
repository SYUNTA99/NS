-- エフェクトの絵を作る道のうち、実行側で読んで PNG に描く efkprobe.exe
-- premake5.lua が Tools/*/premake.lua を読むので、ここに置けば @build.cmd で一緒に建つ

-- files や includedirs の相対パスはこのファイルの場所が基準になる。本体の物は根から辿る
local root = _MAIN_SCRIPT_DIR
local here = path.getdirectory(_SCRIPT)

local effekseerIncludeDirsFromRoot = {}
for _, dir in ipairs(effekseerIncludeDirs) do
    table.insert(effekseerIncludeDirsFromRoot, root .. "/" .. dir)
end

group "_Tools"

--============================================================================
-- efkprobe.exe
--   .efkefc を Effekseer の実行側で読み、依存の欠け・節の木・フレームごとのインスタンス数を出し、
--   画面外の描画先へ 1 フレームずつ描いて PNG に書く。Manager と Renderer の組み方は EffectScene と同じ
--   実行側はゲームと同じ effekseer の lib を繋ぐ。別に組むと、定義と例外の設定がゲームと食い違いうる
--   出荷 (GameRelease) では建てない
--============================================================================
project "EffectProbe"
    kind "ConsoleApp"
    location (root .. "/build/EffectProbe")
    targetname "efkprobe"

    targetdir (root .. "/" .. bindir .. "/%{prj.name}")
    objdir (root .. "/" .. objdir_base .. "/%{prj.name}")

    files {
        here .. "/efkprobe/**.cpp"
    }

    includedirs(effekseerIncludeDirsFromRoot)

    links {
        "effekseer",
        "d3d11",
        "dxgi",
        "d3dcompiler"
    }

    filter "configurations:GameRelease"
        kind "None"
    filter {}

    applyCommonBuildOptions()

group ""
