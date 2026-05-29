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
    --   third_party (spdlog 等) からの transitive <windows.h> でも NOMINMAX が確実に効くよう
    --   コマンドライン /D で global 伝搬させる (header 経由では取りこぼし発生)
    defines {
        "_WIN32_WINNT=0x0A00",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "UNICODE",
        "_UNICODE"
    }

    -- Profile build opt-in: 環境変数 NS_ENABLE_PROFILING=1 で有効化。
    -- NS_SCOPED_TIMER が clock.h で no-op から ScopedTimer 展開に切替わる。
    -- 通常 build では未定義 = profiling マクロは ((void)0) で 0 overhead。
    -- tools\@build_profile.cmd 経由で 1 cmd 実行可能。
    if os.getenv("NS_ENABLE_PROFILING") == "1" then
        defines { "NS_ENABLE_PROFILING" }
        print("[premake5] NS_ENABLE_PROFILING enabled — profile build")
    end

    --------------------------------------------------------------------------
    -- 構成別設定
    --   - Framework 層列の最適化を構成デフォルトとし、Game.exe 固有調整は
    --     Game プロジェクト側で filter override する
    --------------------------------------------------------------------------
    filter "configurations:Debug"
        defines {
            "NS_BUILD_DEBUG",
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

-- 共通 build options (全 Framework 層 / Game / Tests project で使用)
local function applyCommonBuildOptions()
    warnings "Extra"
    -- flags { "FatalWarnings" }  -- build 安定後に有効化
    buildoptions { "/utf-8", "/permissive-", "/FS" }
    linkoptions { "/ignore:4006" }
end

-- Framework 層共通定義 ( 時点では空、 PCH は 2.5b-0 で再導入予定)
local function applyFrameworkLayerDefaults(layerName)
    -- placeholder: layer 名引数は 2.5b-0 で PCH 再導入時に利用
    _ = layerName
end

--============================================================================
-- DirectXTK 必要サブセット (StaticLib)
--   NS が使う 4 機能: SimpleMath / CommonStates / DDSTextureLoader /
--   WICTextureLoader。 Effects / SpriteBatch / GeometricPrimitive 等は精
--   コンパイル済 HLSL (`.inc`) を要求するため exclude。
--   pch.h が Windows.h まで巻き込むため、 本体に汚染を持ち込まないように
--   隔離した独立プロジェクトとしてビルドする。
--   project 名は consumer の links { } 互換のため `directxtk_simplemath` を維持。
--============================================================================
project "directxtk_simplemath"
    kind "StaticLib"
    location "build/directxtk_simplemath"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        -- PCH (DirectXTK 標準パターン、 各 .cpp が冒頭で `#include "pch.h"`)
        "Source/third_party/DirectXTK/Src/pch.cpp",
        "Source/third_party/DirectXTK/Src/pch.h",
        -- NS が利用する最小サブセット
        "Source/third_party/DirectXTK/Src/SimpleMath.cpp",
        "Source/third_party/DirectXTK/Src/CommonStates.cpp",
        "Source/third_party/DirectXTK/Src/DDSTextureLoader.cpp",
        "Source/third_party/DirectXTK/Src/WICTextureLoader.cpp",
        -- 上記 .cpp が依存する内部ヘッダ
        "Source/third_party/DirectXTK/Src/DDS.h",
        "Source/third_party/DirectXTK/Src/LoaderHelpers.h",
        "Source/third_party/DirectXTK/Src/PlatformHelpers.h",
    }

    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/DirectXTK/Src"   -- "pch.h" 解決用
    }

    -- DirectXTK 標準の pch.h を PCH 化 (各 .cpp が冒頭で `#include "pch.h"` 済)
    pchheader "pch.h"
    pchsource "Source/third_party/DirectXTK/Src/pch.cpp"

    warnings "Off"
    buildoptions { "/utf-8", "/FS" }

--============================================================================
-- Framework 層 (Solution Folder)
--   8 層 (Core / Platform / Physics / Graphics / Audio / Scene / UI / App) を
--   Visual Studio Solution Explorer 上で 1 つのフォルダにまとめる。
--   ルート直下は Game / directxtk_simplemath、 Tests / 3rd party は別 group。
--============================================================================
group "Framework"

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
        "Source/Framework/Core/**.h",
        "Source/Framework/Core/**.cpp"
    }

    -- NS::Core::Math は SimpleMath の using-alias、Logger は spdlog/magic_enum を使用
    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    -- SimpleMath の静的定数 TU をリンク伝播させる (Math モジュール用)
    links { "directxtk_simplemath" }

    defines {
        "SPDLOG_HEADER_ONLY",             -- header-only モード
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    applyFrameworkLayerDefaults("Core")
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
        "Source/Framework/Platform/**.h",
        "Source/Framework/Platform/**.cpp"
    }

    -- WindowDesc 等が NS::Core::Size2D (Math.h 経由で SimpleMath) を保持するため DirectXTK が必要
    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links { "Core" }

    -- XInput リンク
    filter "system:windows"
        links { "Xinput" }
    filter {}

    applyFrameworkLayerDefaults("Platform")
    applyCommonBuildOptions()

--============================================================================
-- Graphics 層 (StaticLib)
--   Renderer / RenderTarget / CommonStates / Buffer / Texture / Shader /
--   Mesh / Camera / Material
--============================================================================
project "Graphics"
    kind "StaticLib"
    location "build/Graphics"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Framework/Graphics/**.h",
        "Source/Framework/Graphics/**.cpp"
    }

    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/DirectXTex/DirectXTex",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include"
    }

    defines {
        "SPDLOG_HEADER_ONLY",
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

    applyFrameworkLayerDefaults("Graphics")
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
        "Source/Framework/Physics/**.h",
        "Source/Framework/Physics/**.cpp"
    }

    -- NS::Core::Vector3 / BoundingBox / Ray (SimpleMath) を使う
    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links { "Core" }

    applyFrameworkLayerDefaults("Physics")
    applyCommonBuildOptions()

--============================================================================
-- Audio 層 (StaticLib、 placeholder)
--    で空フォルダ + Audio.h placeholder のみ。  以降に
--   XAudio2 + DirectXTK::Audio で BGM/SE 実装予定。
--============================================================================
project "Audio"
    kind "StaticLib"
    location "build/Audio"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Framework/Audio/**.h",
        "Source/Framework/Audio/**.cpp"
    }

    includedirs {
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links { "Core" }

    applyFrameworkLayerDefaults("Audio")
    applyCommonBuildOptions()

--============================================================================
-- Scene 層 (StaticLib)
--   GameObject / Component / Transform / IRenderable / RenderContext +
--   各種 Component (MeshComponent / CharacterMovement / Camera / 他)
--   UE5 風 OOP の合成主体。 Framework Library として 7 層目に配置。
--============================================================================
project "Scene"
    kind "StaticLib"
    location "build/Scene"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Framework/Scene/**.h",
        "Source/Framework/Scene/**.cpp"
    }

    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
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

    applyFrameworkLayerDefaults("Scene")
    applyCommonBuildOptions()

--============================================================================
-- UI 層 (StaticLib、 Framework 8 層目 —  で新設)
--   ImGui ラッパ (Debug / Development 構成のみ実機能、 GameDebug / GameRelease は stub)。
--   ImGui 型はヘッダから露出させず detail/ 配下にのみ取り込む (header pollution rule)。
--============================================================================
project "UI"
    kind "StaticLib"
    location "build/UI"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Framework/UI/**.h",
        "Source/Framework/UI/**.cpp"
    }

    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        "Graphics"
    }

    -- Debug / Development のみ imgui を取り込み + link する。
    -- GameDebug / GameRelease では preprocessor で stub に切替わるので link 不要。
    filter "configurations:Debug or Development"
        includedirs {
            "Source/third_party/imgui",
            "Source/third_party/imgui/backends",
        }
        links { "imgui" }
    filter {}

    applyFrameworkLayerDefaults("UI")
    applyCommonBuildOptions()

--============================================================================
-- App 層 (StaticLib)
--   Application / WinMain (SceneBase は Scene 層に昇格、 T1 2026-05-23)
--   DD7: フォルダ・ namespace ・ premake project 全て短縮命名 `App` で統一
--============================================================================
project "App"
    kind "StaticLib"
    location "build/App"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Framework/App/**.h",
        "Source/Framework/App/**.cpp"
    }

    -- WindowDesc 等が NS::Core::Size2D (Math.h 経由で SimpleMath) を保持するため DirectXTK が必要
    -- Logger 経由で spdlog / magic_enum も参照
    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Scene",
        "UI"
    }

    applyFrameworkLayerDefaults("App")
    applyCommonBuildOptions()

--============================================================================
-- Solution Folder を解除し、 Game 実行ファイルをルート直下に戻す。
--============================================================================
group ""

--============================================================================
-- Game 実行ファイル (WindowedApp)
--   MainScene + CreateApplication / CreateInitialWorld
--============================================================================
project "Game"
    kind "WindowedApp"
    location "build/Game"

    targetdir (bindir)        -- exe は build/bin/<Config>/ 直下
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Game/**.h",
        "Source/Game/**.cpp"
    }

    -- Framework/Core/Math.h → SimpleMath.h、Material::SetParams で
    -- DirectXMath.h が必要になる。spdlog/magic_enum は将来 Game 側でも使う想定で同居。
    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Scene",
        "UI",
        "App"
    }

    -- HLSL / Texture は exe 隣の Shaders/ Assets/ にコピーし、FileSystem::GetExeDirectory()
    -- 相対で実行時に読込む。premake トークン {COPYDIR}/{MKDIR} はクロスシェル安全。
    postbuildcommands {
        '{MKDIR} "%{cfg.buildtarget.directory}/Shaders"',
        '{COPYDIR} "%{wks.location}/../Source/Shaders" "%{cfg.buildtarget.directory}/Shaders"',
        '{MKDIR} "%{cfg.buildtarget.directory}/Assets"',
        '{COPYDIR} "%{wks.location}/../Assets" "%{cfg.buildtarget.directory}/Assets"',
    }

    -- Debug / Development では Editor UI (CategoryPalette 等) が直接 ImGui を呼ぶため
    -- include path のみ通す (実体 link は UI 経由)。 Shipping 構成では gate により stub。
    filter "configurations:Debug or Development"
        includedirs {
            "Source/third_party/imgui",
            "Source/third_party/imgui/backends",
        }
    filter {}

    -- GameDebug: Game.exe のみ -O0 + symbols フル
    filter "configurations:GameDebug"
        optimize "Off"

    filter {}

    applyCommonBuildOptions()

--============================================================================
-- テスト関連（ソリューションフォルダで非表示）
--============================================================================
group "_Tests"

--============================================================================
-- Dear ImGui (docking branch v1.92.6、 Debug/Development 構成のみ build)
--   NS::UI 内部実装で使用、 GameDebug/GameRelease では link しない ( A2-)
--============================================================================
project "imgui"
    kind "StaticLib"
    location "build/imgui"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/third_party/imgui/imgui.h",
        "Source/third_party/imgui/imgui.cpp",
        "Source/third_party/imgui/imgui_internal.h",
        "Source/third_party/imgui/imconfig.h",
        "Source/third_party/imgui/imgui_draw.cpp",
        "Source/third_party/imgui/imgui_widgets.cpp",
        "Source/third_party/imgui/imgui_tables.cpp",
        "Source/third_party/imgui/imgui_demo.cpp",
        "Source/third_party/imgui/backends/imgui_impl_win32.h",
        "Source/third_party/imgui/backends/imgui_impl_win32.cpp",
        "Source/third_party/imgui/backends/imgui_impl_dx11.h",
        "Source/third_party/imgui/backends/imgui_impl_dx11.cpp",
        "Source/third_party/imgui/imstb_textedit.h",
        "Source/third_party/imgui/imstb_truetype.h",
        "Source/third_party/imgui/imstb_rectpack.h",
    }

    includedirs {
        "Source/third_party/imgui",
        "Source/third_party/imgui/backends",
    }

    warnings "Off"
    buildoptions { "/utf-8", "/FS" }

    -- GameDebug / GameRelease では build しない (kind を None にして空 project 化)
    filter "configurations:GameDebug or GameRelease"
        kind "None"
    filter {}

--============================================================================
-- Google Test ライブラリ（Source/third_party/ source drop）
--============================================================================
project "googletest"
    kind "StaticLib"
    location "build/googletest"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/third_party/googletest/googletest/src/gtest-all.cc",
        "Source/third_party/googletest/googlemock/src/gmock-all.cc"
    }

    includedirs {
        "Source/third_party/googletest/googletest/include",
        "Source/third_party/googletest/googletest",
        "Source/third_party/googletest/googlemock/include",
        "Source/third_party/googletest/googlemock"
    }

    -- Google Testの警告を無視
    warnings "Off"
    buildoptions { "/utf-8", "/FS" }

--============================================================================
-- Tests 実行ファイル (ConsoleApp)
--   GoogleTest ベース、 Framework 各層をリンクして個別モジュールをテスト
--============================================================================
project "Tests"
    kind "ConsoleApp"
    location "build/Tests"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Tests/**.h",
        "Source/Tests/**.cpp",
        -- Game 側 GameObject 派生 (Player / Block) は Application 依存を持たないので
        -- Tests から直接コンパイルしてリンクする。Game.cpp は Application や
        -- Window への依存があるので除外し、unit test で扱える範囲だけ取り込む。
        "Source/Game/Player.cpp",
        "Source/Game/Block.cpp",
        "Source/Game/Blocks/**.cpp",
        "Source/Game/CameraRig.cpp",
        "Source/Game/EditorCameraRig.cpp",
        -- LevelEditorScene は EnterPlay / EnterEdit / 値型 PlayMode の配線テストで参照する。
        -- OnStart は Application::Get() を要求するため test では呼ばないが、 ctor / EnterPlay /
        -- EnterEdit / 値メンバ accessor の symbol が要るので .cpp を Tests に取り込む。
        "Source/Game/LevelEditorScene.cpp",
        -- Level data / ChunkIO / CRC32 / Undo Command / AutoTile は Application
        -- 非依存の純粋ロジックなので Tests project から直接 compile する。
        "Source/Game/Level/**.cpp",
        "Source/Game/Undo/**.cpp",
        "Source/Game/Editor/**.cpp",
        "Source/Game/Theme/**.cpp"
    }

    includedirs {
        "Source/third_party/googletest/googletest/include",
        "Source/third_party/googletest/googlemock/include",
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "googletest",
        "Core",
        "Platform",
        "Physics",
        "Graphics",
        "Audio",
        "Scene",
        "UI",
        "App"
    }

    -- Debug / Development の Tests は ImGui 機能を呼ぶため imgui を link する。
    -- GameDebug / GameRelease では UI 側が stub なので link 不要。
    filter "configurations:Debug or Development"
        links { "imgui" }
        includedirs {
            "Source/third_party/imgui",
            "Source/third_party/imgui/backends",
        }
    filter {}

    -- Skybox / Texture 等のテストは Shaders / Assets を実行時に exe 隣ディレクトリから
    -- 読み込むため、 Game.exe と同じ postbuild で出力先にコピーしておく。
    postbuildcommands {
        '{MKDIR} "%{cfg.buildtarget.directory}/Shaders"',
        '{COPYDIR} "%{wks.location}/../Source/Shaders" "%{cfg.buildtarget.directory}/Shaders"',
        '{MKDIR} "%{cfg.buildtarget.directory}/Assets"',
        '{COPYDIR} "%{wks.location}/../Assets" "%{cfg.buildtarget.directory}/Assets"',
    }

    debugdir "."
    disablewarnings { "4244", "4834" }  -- テスト用: 暗黙変換、[[nodiscard]]無視

    applyCommonBuildOptions()
