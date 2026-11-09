#include "NSlib/Gfx/D3DDevice.h"
#include "NSlib/Gfx/D3DShader.h"
#include "NSlib/Core/Windows/Window.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

namespace
{
    // テストごとに一意の一時フォルダ。抜ける時に中身ごと消す
    class TempShaderDirectory
    {
    public:
        TempShaderDirectory()
            : m_root(std::filesystem::temp_directory_path() /
                     ("ns_shader_include_" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(m_root / "Base");
            std::filesystem::create_directories(m_root / "Hooks");
        }
        ~TempShaderDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_root, error);
        }
        TempShaderDirectory(const TempShaderDirectory&) = delete;
        TempShaderDirectory& operator=(const TempShaderDirectory&) = delete;
        TempShaderDirectory(TempShaderDirectory&&) = delete;
        TempShaderDirectory& operator=(TempShaderDirectory&&) = delete;

        void Write(const std::string& relative, const std::string& text) const
        {
            std::ofstream file(m_root / relative, std::ios::binary);
            file << text;
        }
        [[nodiscard]] std::string Path(const std::string& relative) const { return (m_root / relative).generic_string(); }

    private:
        std::filesystem::path m_root;
    };
} // namespace

// include は渡したフォルダを先に探し、無ければ include したファイルのフォルダから探す。どちらにも無ければ ERROR で断る
TEST(D3DShader, IncludeSearchesTheGivenDirectoriesFirst)
{
    const TempShaderDirectory directory;
    directory.Write("Base/main.ps.hlsl",
                    "#include \"Hook.hlsli\"\n#include \"Shared.hlsli\"\n"
                    "float4 PSMain() : SV_Target { return float4(HookValue(), SharedValue(), 0.0, 1.0); }\n");
    // Base の口のファイルは壊れている。Hooks の物を先に読めばコンパイルが通る
    directory.Write("Base/Hook.hlsli", "this is not hlsl\n");
    directory.Write("Base/Shared.hlsli", "float SharedValue() { return 0.5; }\n");
    directory.Write("Hooks/Hook.hlsli", "float HookValue() { return 1.0; }\n");
    const std::string mainPath = directory.Path("Base/main.ps.hlsl");

    const std::vector<std::string> hooks{directory.Path("Hooks")};
    EXPECT_NE(NS::Gfx::CompileShaderFile(mainPath, "PSMain", "ps_5_0", hooks), nullptr);

    const TestLogCapture log;
    EXPECT_EQ(NS::Gfx::CompileShaderFile(mainPath, "PSMain", "ps_5_0", {}), nullptr);
    EXPECT_EQ(log.ErrorCount(), 1);
    directory.Write("Base/main.ps.hlsl",
                    "#include \"Missing.hlsli\"\nfloat4 PSMain() : SV_Target { return float4(1.0, 1.0, 1.0, 1.0); }\n");
    EXPECT_EQ(NS::Gfx::CompileShaderFile(mainPath, "PSMain", "ps_5_0", hooks), nullptr);
    EXPECT_EQ(log.ErrorCount(), 2);
}

// 読み直しの道のファイル名からステージを決められない時は、作る時と同じく ERROR を出して断る
TEST(D3DShader, ReloadWithoutStageInFileNameLogsAnError)
{
    NS::OS::Window window(NS::OS::WindowDesc{.title = "シェーダーのステージの試し", .size = {16, 16}, .visible = false});
    NS::Gfx::D3DDevice device(NS::Gfx::D3DDeviceDesc{}, window);
    ASSERT_TRUE(device.IsValid());
    // 無いファイルなので代わりのピンクになる。ステージはファイル名から決まる
    const std::unique_ptr<NS::Gfx::D3DShader> shader = NS::Gfx::D3DShader::Create("ns_missing_stage_test.ps.hlsl");
    ASSERT_TRUE(shader->IsUsingFallback());

    const TestLogCapture log;
    EXPECT_FALSE(shader->Reload("ns_missing_stage_test.hlsl"));
    EXPECT_EQ(log.ErrorCount(), 1);
}
