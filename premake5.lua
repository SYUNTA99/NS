--============================================================================
-- premake5.lua
-- NS プロジェクト構成
--============================================================================

-- compile_commands.json生成モジュール
require "premake/modules/export-compile-commands/export-compile-commands"

--============================================================================
-- ワークスペース
--============================================================================
workspace "NS"
    configurations { "Debug", "Development", "GameDebug", "GameRelease" }
    platforms { "x64" }
    location "build"

    language "C++"
    cppdialect "C++20"
    characterset "Unicode"

    -- 全プロジェクト共通 include root = "Source/"
    includedirs { "Source" }

    -- Windows 共通定義 (全プロジェクト共通)
    --   ThirdParty (spdlog 等) からの transitive <windows.h> でも NOMINMAX が確実に効くよう
    --   コマンドライン /D で global 伝搬させる (header 経由では取りこぼし発生)
    defines {
        "_WIN32_WINNT=0x0A00",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "UNICODE",
        "_UNICODE"
    }

    -- Profile build opt-in: 環境変数 NS_ENABLE_PROFILING=1 で有効化。
    -- NS_SCOPED_TIMER が clock.h で何もしない実装から ScopedTimer 展開に切替わる。
    -- 通常 build では未定義 = profiling マクロは ((void)0) で 0 overhead。
    -- Tools\@build_profile.cmd 経由で 1 cmd 実行可能。
    if os.getenv("NS_ENABLE_PROFILING") == "1" then
        defines { "NS_ENABLE_PROFILING" }
        print("[premake5] NS_ENABLE_PROFILING enabled — profile build")
    end

    --------------------------------------------------------------------------
    -- 構成別設定
    --   - Runtime 層列の最適化を構成デフォルトとし、Game.exe 固有調整は
    --     Game プロジェクト側で filter override する
    --------------------------------------------------------------------------
    filter "configurations:Debug"
        defines {
            "NS_BUILD_DEBUG",
            "NS_EDITOR_ENABLED=1",
            "NS_LOG_LEVEL=0",          -- Trace+
            "NS_ENABLE_ASSERT=1",
            "_ITERATOR_DEBUG_LEVEL=2", -- フルチェック
            "DEBUG", "_DEBUG"
        }
        symbols "On"
        optimize "Off"
        runtime "Debug"

    filter "configurations:Development"
        defines {
            "NS_BUILD_DEV",
            "NS_EDITOR_ENABLED=1",
            "NS_LOG_LEVEL=1",          -- Debug+
            "NS_ENABLE_ASSERT=1",
            "_ITERATOR_DEBUG_LEVEL=0",
            "NDEBUG"
        }
        symbols "On"
        optimize "On"
        runtime "Release"

    filter "configurations:GameDebug"
        -- ns.lib は -O2、Game.exe は -O0（Game プロジェクト側で上書き）
        defines {
            "NS_BUILD_GAMEDEBUG",
            "NS_EDITOR_ENABLED=1",
            "NS_LOG_LEVEL=1",
            "NS_ENABLE_ASSERT=1",
            "_ITERATOR_DEBUG_LEVEL=1", -- 境界チェックのみ
            "NDEBUG"
        }
        symbols "On"
        optimize "On"
        runtime "Release"

    filter "configurations:GameRelease"
        defines {
            "NS_BUILD_RELEASE",
            "NS_EDITOR_ENABLED=0",
            "NS_LOG_LEVEL=4",          -- Error/Fatal のみ
            "NS_ENABLE_ASSERT=0",
            "_ITERATOR_DEBUG_LEVEL=0",
            "NDEBUG", "NS_SHIPPING"
        }
        symbols "Off"
        optimize "Full"
        runtime "Release"
        flags { "LinkTimeOptimization", "MultiProcessorCompile" }

    filter "platforms:x64"
        architecture "x64"

    filter {}

--============================================================================
-- 共通変数
--============================================================================
outputdir = "%{cfg.buildcfg}-%{cfg.system}-%{cfg.architecture}"
bindir = "build/bin/" .. outputdir
objdir_base = "build/obj/" .. outputdir

-- 共通 build options (全 Runtime 層 / Game / Tests project で使用)
local function applyCommonBuildOptions()
    warnings "Extra"
    -- flags { "FatalWarnings" }  -- build 安定後に有効化
    -- 実行時型情報は使わない方針。 /GR- で切り、 dynamic_cast / 多態 typeid の使用 (C4541) は error で弾く
    rtti "Off"
    fatalwarnings { "4541" }
    buildoptions { "/utf-8", "/permissive-", "/FS" }
    linkoptions { "/ignore:4006" }
end

-- Runtime 層共通定義。 各層の <Layer>Pch.h を /FI で全 .cpp へ強制 include し、
-- CommonStl.h (windows.h + 定番 stdlib) と層固有の重いヘッダを PCH で償却する。
-- forceincludes はパスを project 相対へ rebase するが、 層により .cpp の深さが異なり
-- 相対 /FI が破綻するため、 include root (Source) から一意に解決できる論理名を渡す。
local function applyRuntimeLayerDefaults(layerName)
    local pchLogical = "Runtime/" .. layerName .. "/" .. layerName .. "Pch.h"
    pchheader(pchLogical)
    pchsource("Source/Runtime/" .. layerName .. "/" .. layerName .. "Pch.cpp")
    buildoptions { "/FI\"" .. pchLogical .. "\"" }
end

--============================================================================
-- DirectXTK 必要サブセット (StaticLib)
--   NS が使う 4 機能: SimpleMath / CommonStates / DDSTextureLoader /
--   WICTextureLoader。 Effects / SpriteBatch / GeometricPrimitive 等は精
--   コンパイル済 HLSL (`.inc`) を要求するため exclude。
--   pch.h が Windows.h まで巻き込むため、 本体に汚染を持ち込まないように
--   隔離した独立プロジェクトとしてビルドする。
--   project 名は consumer の links { } 互換のため `directxtk_simplemath` を維持。
--   Solution Explorer では _ThirdParty フォルダへ畳み、自作の層と混ざらないようにする。
--============================================================================
group "_ThirdParty"

project "directxtk_simplemath"
    kind "StaticLib"
    location "build/directxtk_simplemath"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        -- PCH (DirectXTK 標準パターン、 各 .cpp が冒頭で `#include "pch.h"`)
        "Source/ThirdParty/DirectXTK/Src/pch.cpp",
        "Source/ThirdParty/DirectXTK/Src/pch.h",
        -- NS が利用する最小サブセット
        "Source/ThirdParty/DirectXTK/Src/SimpleMath.cpp",
        "Source/ThirdParty/DirectXTK/Src/CommonStates.cpp",
        "Source/ThirdParty/DirectXTK/Src/DDSTextureLoader.cpp",
        "Source/ThirdParty/DirectXTK/Src/WICTextureLoader.cpp",
        -- 上記 .cpp が依存する内部ヘッダ
        "Source/ThirdParty/DirectXTK/Src/DDS.h",
        "Source/ThirdParty/DirectXTK/Src/LoaderHelpers.h",
        "Source/ThirdParty/DirectXTK/Src/PlatformHelpers.h",
    }

    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/DirectXTK/Src"   -- "pch.h" 解決用
    }

    -- DirectXTK 標準の pch.h を PCH 化 (各 .cpp が冒頭で `#include "pch.h"` 済)
    pchheader "pch.h"
    pchsource "Source/ThirdParty/DirectXTK/Src/pch.cpp"

    warnings "Off"
    buildoptions { "/utf-8", "/FS" }

--============================================================================
-- Runtime 層 (Solution Folder)
--   8 層 (Core / Platform / Physics / Graphics / Audio / Object / UI / App) を
--   Visual Studio Solution Explorer 上で 1 つのフォルダにまとめる。
--   ルート直下は Game / directxtk_simplemath、 Tests / 3rd party は別 group。
--============================================================================
group "Runtime"

--============================================================================
-- Core 層 (StaticLib)
--   Logger / Math / StringUtils / Clock / Filesystem
--============================================================================
project "Core"
    kind "StaticLib"
    location "build/Core"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/Core/**.h",
        "Source/Runtime/Core/**.cpp"
    }

    -- NS::Core::Math は SimpleMath の using-alias、Logger は spdlog/magic_enum を使用
    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    -- Math.h の型は SimpleMath の using-alias なので、静的定数 TU をここでリンクへ伝播させる
    links { "directxtk_simplemath" }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    applyRuntimeLayerDefaults("Core")
    applyCommonBuildOptions()

--============================================================================
-- Platform 層 (StaticLib)
--   Window / Input / Keyboard / Mouse / Gamepad
--============================================================================
project "Platform"
    kind "StaticLib"
    location "build/Platform"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/Platform/**.h",
        "Source/Runtime/Platform/**.cpp"
    }

    -- WindowDesc 等が NS::Core::Size2D (Math.h 経由で SimpleMath) を保持するため DirectXTK が必要
    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links { "Core" }

    -- XInput リンク
    filter "system:windows"
        links { "Xinput" }
    filter {}

    applyRuntimeLayerDefaults("Platform")
    applyCommonBuildOptions()

--============================================================================
-- Graphics 層 (StaticLib)
--   Renderer / CommonStates / Buffer / Texture / Shader /
--   Mesh / Camera / Material
--============================================================================
project "Graphics"
    kind "StaticLib"
    location "build/Graphics"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/Graphics/**.h",
        "Source/Runtime/Graphics/**.cpp"
    }

    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/DirectXTex/DirectXTex",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
        "Source/ThirdParty/cgltf"
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        -- D3D11 system libs
        "d3d11",
        "dxgi",
        "dxguid",
        "d3dcompiler"
    }

    -- Graphics は GraphicsPch.h で D3D11 / SimpleMath の cold parse も償却する
    applyRuntimeLayerDefaults("Graphics")
    applyCommonBuildOptions()

--============================================================================
-- Physics 層 (StaticLib)
--   Capsule / SweptAABB / CharacterController / Ray / Plane
--   Mario 系プラットフォーマー特化 Custom AABB 物理、graphics 非依存
--============================================================================
project "Physics"
    kind "StaticLib"
    location "build/Physics"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/Physics/**.h",
        "Source/Runtime/Physics/**.cpp"
    }

    -- NS::Core::Vector3 / BoundingBox / Ray (SimpleMath) を使う
    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links { "Core" }

    applyRuntimeLayerDefaults("Physics")
    applyCommonBuildOptions()

--============================================================================
-- Audio 層 (StaticLib、 placeholder)
--   現状は空フォルダ + Audio.h placeholder のみ。 将来 XAudio2 + DirectXTK::Audio で
--   BGM/SE を実装予定。
--============================================================================
project "Audio"
    kind "StaticLib"
    location "build/Audio"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/Audio/**.h",
        "Source/Runtime/Audio/**.cpp"
    }

    includedirs {
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links { "Core" }

    applyRuntimeLayerDefaults("Audio")
    applyCommonBuildOptions()

--============================================================================
-- Object 層 (StaticLib)
--   GameObject / Component / Transform / IRenderable / RenderContext +
--   各種 Component (MeshRendererComponent / CharacterMovement / Camera / 他)
--   UE5 風 OOP の合成主体。 Runtime Library として 7 層目に配置。
--============================================================================
project "Object"
    kind "StaticLib"
    location "build/Object"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/Object/**.h",
        "Source/Runtime/Object/**.cpp"
    }

    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        "Graphics",
        "Physics",
        "Audio"
    }

    applyRuntimeLayerDefaults("Object")
    applyCommonBuildOptions()

--============================================================================
-- UI 層 (StaticLib、 Runtime 8 層目)
--   ImGui ラッパ (GameRelease では非ビルド、 Debug / Development / GameDebug でのみビルド)。
--   ImGui 型はヘッダから露出させず detail/ 配下にのみ取り込む (header pollution rule)。
--============================================================================
project "UI"
    kind "StaticLib"
    location "build/UI"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/UI/**.h",
        "Source/Runtime/UI/**.cpp"
    }

    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        "Graphics"
    }

    -- editor は Debug / Development / GameDebug にのみ存在するため、imgui を取り込み + link する。
    filter "configurations:Debug or Development or GameDebug"
        includedirs {
            "Source/ThirdParty/imgui",
            "Source/ThirdParty/imgui/backends",
        }
        links { "imgui" }
    filter {}

    -- ゲーム UI (Widget / UISystem) は出荷対象なので全構成でビルドする。
    -- ImGui まわり (ImGuiContext / Panel) は NS_EDITOR_ENABLED ガードで GameRelease では空になる

    applyRuntimeLayerDefaults("UI")
    applyCommonBuildOptions()

--============================================================================
-- App 層 (StaticLib)
--   Application / WinMain (基盤は Object 層に同居)
--   DD7: フォルダ・ namespace ・ premake project 全て短縮命名 `App` で統一
--============================================================================
project "App"
    kind "StaticLib"
    location "build/App"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Runtime/App/**.h",
        "Source/Runtime/App/**.cpp"
    }

    -- WindowDesc 等が NS::Core::Size2D (Math.h 経由で SimpleMath) を保持するため DirectXTK が必要
    -- Logger 経由で spdlog / magic_enum も参照
    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Object"
    }

    applyRuntimeLayerDefaults("App")
    applyCommonBuildOptions()

--============================================================================
-- Solution Folder を解除し、 Game 実行ファイルをルート直下に戻す。
--============================================================================
group ""

--============================================================================
-- Game (StaticLib) — ゲーム本体 (content / logic)
--   Player / Block / LevelPlayScene / Level / Theme 等。
--   editor を一切知らない (依存の向きは Editor → Game の一方向)。 出荷を含む全構成でビルド。
--   合成 Layer ::Game もここに置き、 editor から Game::Get() で参照できるようにする。
--============================================================================
project "Game"
    kind "StaticLib"
    location "build/Game"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Game/**.h",
        "Source/Game/**.cpp"
    }
    -- GameMain.cpp (CreateApplication = 合成ルート) は exe 側。 Game からは除外する
    removefiles { "Source/Game/GameMain.cpp" }

    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Object",
        "App",
        "UI",
        "directxtk_simplemath"
    }

    -- Game / Editor 共通の安定 Runtime API を全 .cpp へ /FI 強制 include する。
    -- GamePch.cpp は Source/Game/**.cpp の glob で既に拾われる。 GamePch.h は
    -- include root (Source) 経由で全 .cpp から一意に解決できる論理名で渡す。
    pchheader "Game/GamePch.h"
    pchsource "Source/Game/GamePch.cpp"
    buildoptions { "/FI\"Game/GamePch.h\"" }

    applyCommonBuildOptions()

--============================================================================
-- Editor (StaticLib) — エディタモジュール (NS::Editor)
--   Editor / LevelEditorController / EditorCamera / EditorMode /
--   GizmoEditor / CategoryPalette / LevelFileBrowser / LevelFilePaths。
--   Game + UI(ImGui) に依存。 GameRelease では kind None で出荷から物理排除し、
--   「ゲーム本体は editor を知らない」をリンカで強制する。
--============================================================================
project "Editor"
    kind "StaticLib"
    location "build/Editor"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Editor/**.h",
        "Source/Editor/**.cpp",
        -- Game 側 PCH を共有するため pchsource 用に取り込む (Editor は Game に依存済)
        "Source/Game/GamePch.cpp"
    }

    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
        "Source/ThirdParty/imgui",
        "Source/ThirdParty/imgui/backends",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Game",
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Object",
        "App",
        "UI",
        "imgui",
        "directxtk_simplemath"
    }

    -- GameRelease では editor を丸ごとビルドしない (出荷から物理排除)
    filter "configurations:GameRelease"
        kind "None"
    filter {}

    -- Game と同じ Game 側 PCH を共有する (GamePch.cpp は files に追加済)
    pchheader "Game/GamePch.h"
    pchsource "Source/Game/GamePch.cpp"
    buildoptions { "/FI\"Game/GamePch.h\"" }

    applyCommonBuildOptions()

--============================================================================
-- Solution Folder を解除し、 Game 実行ファイルをルート直下に戻す。
--============================================================================
group ""

--============================================================================
-- Game 実行ファイル (WindowedApp) — 薄い合成ルート
--   GameMain.cpp (CreateApplication) のみ。 Game を常時、 Editor を editor 構成のみリンクし、
--   出荷 (GameRelease) には editor / imgui を一切積まない (ゲーム UI の UI 層は積む)。
--============================================================================
project "GameApp"
    kind "WindowedApp"
    location "build/GameApp"
    targetname "Game"

    targetdir (bindir)        -- exe は build/bin/<Config>/ 直下
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Game/GameMain.cpp",
        -- Game 側 PCH を共有するため pchsource 用に取り込む
        "Source/Game/GamePch.cpp"
    }

    includedirs {
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Game",
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Object",
        "App",
        "UI",
        "directxtk_simplemath"
    }

    -- Object の component 自己登録はどこからも参照されない TU の静的初期化に載っているため、
    -- リンカの未参照 obj 除去で無言に欠け得る。Object.lib は全 obj を強制で取り込んで防ぐ
    -- Game 層の配置物 Component も同じ理由で落ちる。Source/Game/Level/ に足した Component は
    -- 他のコードから型を参照されない限り Game.lib の中で未参照のまま残り、
    -- 対策が無いと登録ごと捨てられてエディタのコンポーネント追加一覧に出ない
    linkoptions { "/WHOLEARCHIVE:Object.lib", "/WHOLEARCHIVE:Game.lib" }

    -- 出荷 (GameRelease) のみ exe 隣へ Shaders/ Assets/ をコピーする (exe 相対で読込む配布レイアウト)
    -- 開発構成は FileSystem::ContentRoot() がリポ直下を直接読むためコピーしない (ビルド毎のコピーを排除)
    filter "configurations:GameRelease"
        postbuildcommands {
            '{MKDIR} "%{cfg.buildtarget.directory}/Shaders"',
            '{COPYDIR} "%{wks.location}/../Shaders" "%{cfg.buildtarget.directory}/Shaders"',
            '{MKDIR} "%{cfg.buildtarget.directory}/Assets"',
            '{COPYDIR} "%{wks.location}/../Assets" "%{cfg.buildtarget.directory}/Assets"',
        }
    filter {}

    -- editor 構成のみ Editor モジュール (+imgui) をリンクする。 GameRelease では積まない
    filter "configurations:Debug or Development or GameDebug"
        links { "Editor", "imgui" }
    filter {}

    -- GameDebug: Game.exe のみ -O0 + symbols フル
    filter "configurations:GameDebug"
        optimize "Off"
    filter {}

    -- Game と同じ Game 側 PCH を共有する (GamePch.cpp は files に追加済)
    pchheader "Game/GamePch.h"
    pchsource "Source/Game/GamePch.cpp"
    buildoptions { "/FI\"Game/GamePch.h\"" }

    applyCommonBuildOptions()

--============================================================================
-- テスト関連（ソリューションフォルダで非表示）
--============================================================================
group "_Tests"

--============================================================================
-- Dear ImGui (docking branch v1.92.6、 GameRelease 以外で build)
--   NS::UI 内部実装で使用、 GameRelease のみ link しない
--============================================================================
project "imgui"
    kind "StaticLib"
    location "build/imgui"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ThirdParty/imgui/imgui.h",
        "Source/ThirdParty/imgui/imgui.cpp",
        "Source/ThirdParty/imgui/imgui_internal.h",
        "Source/ThirdParty/imgui/imconfig.h",
        "Source/ThirdParty/imgui/imgui_draw.cpp",
        "Source/ThirdParty/imgui/imgui_widgets.cpp",
        "Source/ThirdParty/imgui/imgui_tables.cpp",
        "Source/ThirdParty/imgui/imgui_demo.cpp",
        "Source/ThirdParty/imgui/backends/imgui_impl_win32.h",
        "Source/ThirdParty/imgui/backends/imgui_impl_win32.cpp",
        "Source/ThirdParty/imgui/backends/imgui_impl_dx11.h",
        "Source/ThirdParty/imgui/backends/imgui_impl_dx11.cpp",
        "Source/ThirdParty/imgui/imstb_textedit.h",
        "Source/ThirdParty/imgui/imstb_truetype.h",
        "Source/ThirdParty/imgui/imstb_rectpack.h",
    }

    includedirs {
        "Source/ThirdParty/imgui",
        "Source/ThirdParty/imgui/backends",
        -- imconfig.h が NS の Assert / Logger を取り込むので spdlog / magic_enum が要る
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    warnings "Off"
    buildoptions { "/utf-8", "/FS" }

    -- GameRelease では build しない (kind を None にして空 project 化)
    filter "configurations:GameRelease"
        kind "None"
    filter {}

--============================================================================
-- Google Test ライブラリ（Source/ThirdParty/ source drop）
--============================================================================
project "googletest"
    kind "StaticLib"
    location "build/googletest"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ThirdParty/googletest/googletest/src/gtest-all.cc",
        "Source/ThirdParty/googletest/googlemock/src/gmock-all.cc"
    }

    includedirs {
        "Source/ThirdParty/googletest/googletest/include",
        "Source/ThirdParty/googletest/googletest",
        "Source/ThirdParty/googletest/googlemock/include",
        "Source/ThirdParty/googletest/googlemock"
    }

    -- Google Testの警告を無視
    warnings "Off"
    -- Tests 側 (/GR-) と実行時型情報の有無を揃え、 header と lib で経路が食い違わないようにする
    rtti "Off"
    buildoptions { "/utf-8", "/FS" }

--============================================================================
-- Tests 実行ファイル (ConsoleApp)
--   GoogleTest ベース、 Runtime 各層をリンクして個別モジュールをテスト
--============================================================================
project "Tests"
    kind "ConsoleApp"
    location "build/Tests"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Tests/**.h",
        "Source/Tests/**.cpp",
        -- Game 側 GameObject 派生 (Player) は Application 依存を持たないので
        -- Tests から直接コンパイルしてリンクする。Game.cpp は Application や
        -- Window への依存があるので除外し、unit test で扱える範囲だけ取り込む。
        -- Source/Game/Player/ 配下とは別物。GameObject 派生の Player 本体
        "Source/Game/Player.cpp",
        "Source/Editor/EditorCamera.cpp",
        -- LevelEditorController は EnterPlay / EnterEdit / 値型 PlayMode の配線テストで参照する。
        -- Setup は Application::Get() を要求するため test では呼ばないが、 ctor / EnterPlay /
        -- EnterEdit / 値メンバ accessor の symbol が要るので .cpp を Tests に取り込む。
        "Source/Editor/LevelEditorController.cpp",
        -- Level 配下と Undo Command は Application 非依存の純粋ロジックなので
        -- Tests project から直接 compile する。
        "Source/Game/Level/**.cpp",
        -- Entity / Player 配下は状態の自己登録 (NS_STATE) が無名 namespace の静的初期化に載る。
        -- Tests が自分でコンパイルした obj はリンカが必ず取り込むので、ここへ足せば /WHOLEARCHIVE は要らない
        "Source/Game/Entity/**.cpp",
        "Source/Game/Player/**.cpp",
        "Source/Editor/Undo/**.cpp",
        -- editor のうち Application 非依存なものだけ取り込む (Editor は Application 依存のため除外)
        "Source/Editor/EditorObjects.cpp",
        "Source/Editor/EditorMode.cpp",
        "Source/Editor/InspectorReflection.cpp",
        "Source/Editor/GizmoEditor.cpp",
        "Source/Editor/GridMath.cpp",
        "Source/Editor/CategoryPalette.cpp",
        "Source/Editor/PaletteTemplates.cpp",
        "Source/Editor/LevelFileBrowser.cpp",
        "Source/Editor/LevelFilePaths.cpp",
        "Source/Editor/PlayControls.cpp",
        "Source/Editor/Theme/**.cpp",
        -- Editor / Game の各 .cpp は GamePch の /FI 前提で Runtime include を持たない。
        -- 同じソースを直接コンパイルする Tests でも同一 prelude を与えるため GamePch を共有する
        "Source/Game/GamePch.cpp"
    }

    includedirs {
        "Source/ThirdParty/googletest/googletest/include",
        "Source/ThirdParty/googletest/googlemock/include",
        "Source/ThirdParty/DirectXTK/Inc",
        "Source/ThirdParty/spdlog/include",
        "Source/ThirdParty/magic_enum/include",
    }

    defines {
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    -- Game / Editor と同じ GamePch を共有し、 strip 済ソースへ /FI で prelude を与える
    pchheader "Game/GamePch.h"
    pchsource "Source/Game/GamePch.cpp"
    buildoptions { "/FI\"Game/GamePch.h\"" }

    links {
        "googletest",
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Object",
        "App"
    }

    -- Game.exe と同じ理由で Object の自己登録 TU をリンカ除去から守る
    -- Game 層は Source/Game/Level/**.cpp を直接コンパイルしていて Game.lib を link しないため、
    -- Game.lib 側の指定は要らない。ここで守れているのは Tests が自分でコンパイルした obj だから
    linkoptions { "/WHOLEARCHIVE:Object.lib" }

    -- Debug / Development / GameDebug の Tests は editor / ImGui を呼ぶため UI + imgui を link する。
    -- GameRelease では UI 層が非ビルドのため link / include しない。
    filter "configurations:Debug or Development or GameDebug"
        links { "UI", "imgui" }
        includedirs {
            "Source/ThirdParty/imgui",
            "Source/ThirdParty/imgui/backends",
        }
    filter {}

    -- 出荷 (GameRelease) ではテストをビルドしない。テストは Debug/Development/GameDebug の関心事で
    -- shipping 構成の成果物ではない (UI 非ビルドと SimpleMath link 漏れの両方をここで回避)
    filter "configurations:GameRelease"
        kind "None"
    filter {}

    -- Skybox / Texture 等のテストは Shaders / Assets を実行時に exe 隣ディレクトリから
    -- 読み込むため、 Game.exe と同じ postbuild で出力先にコピーしておく。
    postbuildcommands {
        '{MKDIR} "%{cfg.buildtarget.directory}/Shaders"',
        '{COPYDIR} "%{wks.location}/../Shaders" "%{cfg.buildtarget.directory}/Shaders"',
        '{MKDIR} "%{cfg.buildtarget.directory}/Assets"',
        '{COPYDIR} "%{wks.location}/../Assets" "%{cfg.buildtarget.directory}/Assets"',
    }

    debugdir "."
    disablewarnings { "4244", "4834" }  -- テスト用: 暗黙変換、[[nodiscard]]無視

    applyCommonBuildOptions()
