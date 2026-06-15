#pragma once

/// @file MaterialLibrary.h
/// @brief NS::Scene::MaterialLibrary — .mat (JSON) からマテリアルを読み込みキャッシュする
///
/// @details .mat は vs / ps の HLSL パス・texture パス・baseColor・blend を持つ JSON
/// ファイル。`Load` は shader / texture をパスで dedupe しながら `NS::Graphics::Material` を
/// 生成して所有する。Material 内蔵 ConstantBuffer は `MeshRendererComponent::Draw` が流す
/// `FrameCB` に合わせて (size / slot) 構築するため、 本ライブラリは Scene 層に置く
/// 生成物はライブラリが所有するので、 参照する MeshRenderer より後に破棄すること
/// 依存: NS::Graphics::Material / Shader / Texture, NS::Core::FileSystem, NS::Math

#include "Framework/Graphics/Material.h"
#include "Framework/Math/Math.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace NS::Graphics
{
    class Shader;
    class Texture;
} // namespace NS::Graphics

namespace NS::Scene
{
    /// .mat (JSON) の解析結果。 GPU 非依存なので deviceless でテストできる
    struct MaterialFileDesc
    {
        std::filesystem::path vertexShader;
        std::filesystem::path pixelShader;
        std::vector<std::filesystem::path> textures;
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        NS::Graphics::BlendMode blend = NS::Graphics::BlendMode::Opaque;
    };

    /// JSON 文字列を MaterialFileDesc へ解析する。 成功で true、 失敗時は outError に理由を入れる
    /// @pre jsonText は .mat の中身 (UTF-8)。 vs / ps は必須、 他は欠落時に既定値
    /// @post 成功時 out の各フィールドが設定される
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    /// 読み込んだ Material とその基準色。 baseColor は MeshRenderer 側に適用するため別で返す
    struct LoadedMaterial
    {
        NS::Graphics::Material* material = nullptr; ///< MaterialLibrary 所有、 ライブラリ寿命中のみ有効
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
    };

    /// .mat を読み込み Material を生成・キャッシュする。 shader / texture はパスで dedupe する
    class MaterialLibrary
    {
    public:
        /// baseDir は .mat 内の相対パス (shader / texture) を解決する基準 (通常は exe ディレクトリ)
        explicit MaterialLibrary(std::filesystem::path baseDir) noexcept;
        ~MaterialLibrary();

        MaterialLibrary(const MaterialLibrary&) = delete;
        MaterialLibrary& operator=(const MaterialLibrary&) = delete;
        MaterialLibrary(MaterialLibrary&&) = delete;
        MaterialLibrary& operator=(MaterialLibrary&&) = delete;

        /// matPath の .mat を読み込み Material を返す。 相対パスは構築時の baseDir 基準で解決する
        /// 既に読込済なら cache を返す。 読込/解析失敗時は material=nullptr の LoadedMaterial を返す
        [[nodiscard]] LoadedMaterial Load(const std::filesystem::path& matPath);

        /// 全キャッシュ (material / shader / texture) を解放する。 Renderer 破棄より前に呼ぶ
        void Clear() noexcept;

    private:
        [[nodiscard]] NS::Graphics::Shader* GetOrLoadShader(const std::filesystem::path& path);
        [[nodiscard]] NS::Graphics::Texture* GetOrLoadTexture(const std::filesystem::path& path);

        struct MaterialRecord
        {
            std::unique_ptr<NS::Graphics::Material> material;
            NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        };

        std::filesystem::path m_baseDir;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Shader>> m_shaders;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Texture>> m_textures;
        std::map<std::filesystem::path, MaterialRecord> m_materials;
    };
} // namespace NS::Scene
