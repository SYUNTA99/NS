-- NSlib を使う側のリンクの設定。生成の時に、使う側が links { "NSlib" } と書くだけで済むよう補う
local bundled = { "directxtk_simplemath", "jolt", "effekseer", "imgui" }

premake.override(premake.project, "bake", function(base, prj)
    base(prj)

    for cfg in premake.project.eachconfig(prj) do
        if cfg.kind == "StaticLib" and table.contains(cfg.links or {}, "NSlib") then
            -- ビルドの順だけを付け、NSlib の中身を自分の .lib へ写さない。写すと実行ファイルの WHOLEARCHIVE で重複定義になる
            local remaining = {}
            local dependencies = table.shallowcopy(cfg.dependson or {})
            for _, name in ipairs(cfg.links or {}) do
                if name == "NSlib" then
                    table.insert(dependencies, name)
                else
                    table.insert(remaining, name)
                end
            end
            cfg.links = remaining
            cfg.dependson = dependencies
        elseif (cfg.kind == "ConsoleApp" or cfg.kind == "WindowedApp") and table.contains(cfg.links or {}, "NSlib") then
            -- 自己登録の翻訳単位はどこからも参照されないので、リンカに捨てさせない
            local options = table.shallowcopy(cfg.linkoptions or {})
            table.insert(options, "/WHOLEARCHIVE:NSlib.lib")
            cfg.linkoptions = options
            local libraries = table.shallowcopy(cfg.links)
            for _, name in ipairs(bundled) do
                if name ~= "imgui" or cfg.buildcfg ~= "GameRelease" then
                    if not table.contains(libraries, name) then
                        table.insert(libraries, name)
                    end
                end
            end
            cfg.links = libraries
        end
    end
end)

require "vstudio"
premake.override(premake.vstudio.vc2010, "precompiledHeader", function(base, cfg, condition)
    local prjcfg, filecfg = premake.config.normalize(cfg)
    if filecfg and prjcfg.project.name == "NSlib" then
        local layer = filecfg.abspath:match("/Source/NSlib/([^/]+)/")
        if filecfg.abspath:match("/ScreenGrab11.cpp$") then
            layer = "Graphics"
        end
        if layer then
            local mode = "Use"
            if path.getname(filecfg.abspath) == layer .. "Pch.cpp" then
                mode = "Create"
            end
            local vc = premake.vstudio.vc2010
            local header = "NSlib/" .. layer .. "/" .. layer .. "Pch.h"
            vc.element("PrecompiledHeader", condition, mode)
            vc.element("PrecompiledHeaderFile", condition, header)
            vc.element("PrecompiledHeaderOutputFile", condition, "$(IntDir)" .. layer .. ".pch")
            vc.element("ForcedIncludeFiles", condition, header)
            return
        end
    end
    base(cfg, condition)
end)
