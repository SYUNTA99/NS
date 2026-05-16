--============================================================================
-- export-compile-commands.lua
-- Premakeモジュール: compile_commands.json生成（MSVC対応版）
--
-- 元: https://github.com/tarruda/premake-export-compile-commands
-- 変更:
--   - コンパイラを clang++ に変更（clangd互換）
--   - getIncludeDirs のバグ修正（result -> flags）
--   - MSVCシステムインクルードパス対応（INCLUDE環境変数）
--============================================================================

local p = premake

p.modules.export_compile_commands = {}
local m = p.modules.export_compile_commands

local workspace = p.workspace
local project = p.project

-- MSVCシステムインクルードパスを取得
-- vcvarsall.bat実行後のINCLUDE環境変数からパスを抽出
function m.getMsvcSystemIncludes()
    local includeEnv = os.getenv("INCLUDE")
    if not includeEnv then
        return {}
    end

    local flags = {}
    for dir in includeEnv:gmatch("[^;]+") do
        if dir ~= "" then
            table.insert(flags, '-isystem')
            table.insert(flags, p.quoted(dir))
        end
    end
    return flags
end

-- インクルードディレクトリをフラグに変換
function m.getIncludeDirs(cfg)
    local flags = {}
    for _, dir in ipairs(cfg.includedirs) do
        table.insert(flags, '-I' .. p.quoted(dir))
    end
    for _, dir in ipairs(cfg.sysincludedirs or {}) do
        table.insert(flags, '-isystem')
        table.insert(flags, p.quoted(dir))
    end
    return flags
end

-- 定義マクロをフラグに変換
function m.getDefines(cfg)
    local flags = {}
    for _, def in ipairs(cfg.defines) do
        table.insert(flags, '-D' .. def)
    end
    return flags
end

-- C++標準をフラグに変換
function m.getCppStandard(cfg)
    local dialect = cfg.cppdialect
    if dialect == "C++20" then
        return { "-std=c++20" }
    elseif dialect == "C++17" then
        return { "-std=c++17" }
    elseif dialect == "C++14" then
        return { "-std=c++14" }
    elseif dialect == "C++11" then
        return { "-std=c++11" }
    end
    return {}
end

-- 共通フラグを取得
function m.getCommonFlags(cfg)
    local flags = {}

    -- C++標準
    flags = table.join(flags, m.getCppStandard(cfg))

    -- 定義
    flags = table.join(flags, m.getDefines(cfg))

    -- ユーザーインクルードパス
    flags = table.join(flags, m.getIncludeDirs(cfg))

    -- MSVCシステムインクルードパス
    flags = table.join(flags, m.getMsvcSystemIncludes())

    return flags
end

-- オブジェクトファイルパスを取得
function m.getObjectPath(prj, cfg, node)
    return path.join(cfg.objdir, path.appendExtension(node.objname, '.o'))
end

-- ファイル固有のフラグを取得
function m.getFileFlags(prj, cfg, node)
    return table.join(m.getCommonFlags(cfg), {
        '-o', m.getObjectPath(prj, cfg, node),
        '-c', node.abspath
    })
end

-- コンパイルコマンドを生成
function m.generateCompileCommand(prj, cfg, node)
    return {
        directory = prj.location,
        file = node.abspath,
        command = 'clang++ ' .. table.concat(m.getFileFlags(prj, cfg, node), ' ')
    }
end

-- C++ファイルかどうか判定
function m.includeFile(prj, node, depth)
    return path.iscppfile(node.abspath)
end

-- プロジェクトの全コンパイルコマンドを取得
function m.getProjectCommands(prj, cfg)
    local tr = project.getsourcetree(prj)
    local cmds = {}
    p.tree.traverse(tr, {
        onleaf = function(node, depth)
            if not m.includeFile(prj, node, depth) then
                return
            end
            table.insert(cmds, m.generateCompileCommand(prj, cfg, node))
        end
    })
    return cmds
end

-- メイン実行関数
local function execute()
    for wks in p.global.eachWorkspace() do
        local cfgCmds = {}
        for prj in workspace.eachproject(wks) do
            for cfg in project.eachconfig(prj) do
                local cfgKey = string.format('%s', cfg.shortname)
                if not cfgCmds[cfgKey] then
                    cfgCmds[cfgKey] = {}
                end
                cfgCmds[cfgKey] = table.join(cfgCmds[cfgKey], m.getProjectCommands(prj, cfg))
            end
        end
        for cfgKey, cmds in pairs(cfgCmds) do
            local outfile = 'premake/compile_commands.json'
            p.generate(wks, outfile, function(wks)
                p.w('[')
                for i = 1, #cmds do
                    local item = cmds[i]
                    local command = string.format([[
  {
    "directory": "%s",
    "file": "%s",
    "command": "%s"
  }]],
                    item.directory:gsub('\\', '/'),
                    item.file:gsub('\\', '/'),
                    item.command:gsub('\\', '\\\\'):gsub('"', '\\"'))
                    if i > 1 then
                        p.w(',')
                    end
                    p.w(command)
                end
                p.w(']')
            end)
        end
    end
end

newaction {
    trigger = 'export-compile-commands',
    description = 'Export compiler commands in JSON Compilation Database Format',
    execute = execute
}

return m
