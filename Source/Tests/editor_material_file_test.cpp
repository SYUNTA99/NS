#include "Editor/LevelEditorController.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/Windows/Filesystem.h"
#include "TestLogCapture.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <system_error>

namespace
{
    // 試しごとに作り、壊す時に中身ごと片付ける一時のフォルダ。リポジトリの .mat は書き換えない
    class TemporaryDirectory
    {
    public:
        TemporaryDirectory()
            : m_path(std::filesystem::temp_directory_path() /
                     ("ns_material_file_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
        {
            std::filesystem::create_directories(m_path);
        }

        ~TemporaryDirectory()
        {
            std::error_code error;
            std::filesystem::remove_all(m_path, error);
        }

        TemporaryDirectory(const TemporaryDirectory&) = delete;
        TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

        [[nodiscard]] std::string File(const char* name) const { return (m_path / name).generic_string(); }
        [[nodiscard]] std::string Path() const { return m_path.generic_string(); }

    private:
        std::filesystem::path m_path;
    };

    void SetBlend(NS::Material& material, const std::string& blend)
    {
        const NS::FieldDesc* field = NS::FindField(material.GetReflection(), "混ぜ方");
        ASSERT_NE(field, nullptr);
        field->set(&material, &blend);
    }

    bool WriteText(const std::string& path, const std::string& text)
    {
        return NS::OS::FileSystem::WriteAllBytes(path, std::as_bytes(std::span<const char>{text.data(), text.size()}));
    }
} // namespace

// 確定で書くと、.mat に今の値が入る
TEST(EditorMaterialFile, WritingPutsTheCurrentValuesInTheFile)
{
    const TemporaryDirectory directory;
    const std::string path = directory.File("stone.mat");
    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Material* material = assets.LoadMaterial("Assets/Materials/stone.mat");
    SetBlend(*material, "Additive");

    ASSERT_TRUE(NS::Editor::WriteMaterialFile(assets, *material, path));
    const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(path);
    ASSERT_TRUE(text.has_value());
    NS::MaterialFileDesc written{};
    std::string error;
    ASSERT_TRUE(NS::ParseMaterialJson(*text, written, error)) << error;
    EXPECT_EQ(written.blend, NS::Gfx::BlendMode::Additive);
    EXPECT_EQ(written.pixelShader, "Shaders/floor.ps.hlsl");
}

// 置き換えられなかった時は前の .mat が残り、一時のファイルも残らない
// 別の道具が .mat を開いたままだと置き換えられない。std::ifstream は消すことを許さずに開く
TEST(EditorMaterialFile, FailedWriteKeepsThePreviousFile)
{
    const TemporaryDirectory directory;
    const std::string path = directory.File("stone.mat");
    const std::string previous = "{\n  \"vs\": \"a\",\n  \"ps\": \"b\"\n}\n";
    ASSERT_TRUE(WriteText(path, previous));

    NS::AssetManager assets{NS::OS::FileSystem::ContentRoot()};
    NS::Material* material = assets.LoadMaterial("Assets/Materials/stone.mat");
    SetBlend(*material, "Additive");
    {
        const std::ifstream held{std::filesystem::path{path}};
        ASSERT_TRUE(held.is_open());
        const TestLogCapture log;
        EXPECT_FALSE(NS::Editor::WriteMaterialFile(assets, *material, path));
        EXPECT_EQ(log.ErrorCount(), 1);
    }

    EXPECT_EQ(NS::OS::FileSystem::ReadAllText(path), previous);
    EXPECT_EQ(NS::OS::FileSystem::ListFiles(directory.Path()).size(), 1u);
}
