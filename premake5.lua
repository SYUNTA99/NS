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

    -- Windows共通定義（全プロジェクト共通）
    defines {
        "_WIN32_WINNT=0x0A00",
        "WIN32_LEAN_AND_MEAN",
        "NOMINMAX",
        "UNICODE",
        "_UNICODE"
    }

    --------------------------------------------------------------------------
    -- 構成別設定 
    --   - ns.lib 列の最適化を構成デフォルトとし、Game.exe 固有調整は
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

-- 共通 build options (全 ns_* / Game / Tests project で使用)
local function applyCommonBuildOptions()
    warnings "Extra"
    -- flags { "FatalWarnings" }  -- T1.0.8 build 安定後に有効化
    buildoptions { "/utf-8", "/permissive-", "/FS" }
    linkoptions { "/ignore:4006" }
end

--============================================================================
-- DirectXTK SimpleMath サブセット (StaticLib)
--   `Vector3::Zero` / `Matrix::Identity` 等の静的定数 TU を提供する。
--   pch.h が Windows.h まで巻き込むため、本体に汚染を持ち込まないように
--   隔離した独立プロジェクトとしてビルドする。
--============================================================================
project "directxtk_simplemath"
    kind "StaticLib"
    location "build/directxtk_simplemath"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/third_party/DirectXTK/Src/SimpleMath.cpp"
    }

    includedirs {
        "Source/third_party/DirectXTK/Inc",
        "Source/third_party/DirectXTK/Src"   -- SimpleMath.cpp 内の "pch.h" 解決用
    }

    warnings "Off"
    buildoptions { "/utf-8", "/FS" }

--============================================================================
-- ns_core モジュール (StaticLib)
--   Logger / Math / StringUtils / Clock / FileSystem
--============================================================================
project "ns_core"
    kind "StaticLib"
    location "build/ns_core"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ns/core/**.h",
        "Source/ns/core/**.cpp"
    }

    -- ns::core::Math は SimpleMath の using-alias、Logger は spdlog/magic_enum を使用
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

    applyCommonBuildOptions()

--============================================================================
-- ns_platform モジュール (StaticLib)
--   Window / Input / Keyboard / Mouse / Gamepad
--============================================================================
project "ns_platform"
    kind "StaticLib"
    location "build/ns_platform"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ns/platform/**.h",
        "Source/ns/platform/**.cpp"
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

    links { "ns_core" }

    -- XInput リンク
    filter "system:windows"
        links { "Xinput" }
    filter {}

    applyCommonBuildOptions()

--============================================================================
-- ns_graphics モジュール (StaticLib)
--   Renderer / RenderTarget / CommonStates / Buffer / Texture / Shader /
--   Mesh / Camera / Material
--============================================================================
project "ns_graphics"
    kind "StaticLib"
    location "build/ns_graphics"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ns/graphics/**.h",
        "Source/ns/graphics/**.cpp"
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
        "ns_core",
        "ns_platform",
        -- D3D11 system libs
        "d3d11",
        "dxgi",
        "dxguid",
        "d3dcompiler"
    }

    applyCommonBuildOptions()

--============================================================================
-- ns_physics モジュール (StaticLib)
--   Capsule / SweptAABB / CharacterController / Ray / Plane
--   Mario 系プラットフォーマー特化 Custom AABB 物理、graphics 非依存
--============================================================================
project "ns_physics"
    kind "StaticLib"
    location "build/ns_physics"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ns/physics/**.h",
        "Source/ns/physics/**.cpp"
    }

    -- ns::core::Vector3 / BoundingBox / Ray (SimpleMath) を使う
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

    links { "ns_core" }

    applyCommonBuildOptions()

--============================================================================
-- ns_scene モジュール (StaticLib)
--   GameObject / Component / Transform / IRenderable / RenderContext +
--   components/* (MeshComponent / CharacterMovement / Camera / 他)
--   UE5 風 OOP の合成主体。engine = Library として 7 層目に配置。
--============================================================================
project "ns_scene"
    kind "StaticLib"
    location "build/ns_scene"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ns/scene/**.h",
        "Source/ns/scene/**.cpp"
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
        "ns_core",
        "ns_platform",
        "ns_graphics",
        "ns_physics"
    }

    applyCommonBuildOptions()

--============================================================================
-- ns_app モジュール (StaticLib)
--   Application / Scene / WinMain
--============================================================================
project "ns_app"
    kind "StaticLib"
    location "build/ns_app"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/ns/app/**.h",
        "Source/ns/app/**.cpp"
    }

    -- ns::core::Logger を include するため spdlog / magic_enum の参照が必要
    includedirs {
        "Source/third_party/spdlog/include",
        "Source/third_party/magic_enum/include",
    }

    defines {
        "SPDLOG_HEADER_ONLY",
        "SPDLOG_WCHAR_TO_UTF8_SUPPORT",
        "SPDLOG_NO_EXCEPTIONS"
    }

    links {
        "ns_core",
        "ns_platform",
        "ns_graphics"
    }

    applyCommonBuildOptions()

--============================================================================
-- Game 実行ファイル (WindowedApp)
--   CubeScene + CreateApplication / CreateInitialScene
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

    -- CubeScene 経由で ns/core/math.h → SimpleMath.h、Material::SetParams で
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
        "ns_core",
        "ns_platform",
        "ns_graphics",
        "ns_physics",
        "ns_scene",
        "ns_app"
    }

    -- HLSL / Texture は exe 隣の Shaders/ Assets/ にコピーし、FileSystem::GetExeDirectory()
    -- 相対で実行時に読込む。premake トークン {COPYDIR}/{MKDIR} はクロスシェル安全。
    postbuildcommands {
        '{MKDIR} "%{cfg.buildtarget.directory}/Shaders"',
        '{COPYDIR} "%{wks.location}/../Source/Shaders" "%{cfg.buildtarget.directory}/Shaders"',
        '{MKDIR} "%{cfg.buildtarget.directory}/Assets"',
        '{COPYDIR} "%{wks.location}/../Assets" "%{cfg.buildtarget.directory}/Assets"',
    }

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
--   GoogleTest ベース、ns_* リンクして個別モジュールをテスト
--============================================================================
project "Tests"
    kind "ConsoleApp"
    location "build/Tests"

    targetdir (bindir .. "/%{prj.name}")
    objdir (objdir_base .. "/%{prj.name}")

    files {
        "Source/Tests/**.h",
        "Source/Tests/**.cpp"
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
        "ns_core",
        "ns_platform",
        "ns_graphics",
        "ns_physics",
        "ns_scene",
        "ns_app"
    }

    debugdir "."
    disablewarnings { "4244", "4834" }  -- テスト用: 暗黙変換、[[nodiscard]]無視

    applyCommonBuildOptions()
