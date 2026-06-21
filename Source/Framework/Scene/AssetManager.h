#pragma once

/// @file AssetManager.h
/// @brief NS::Scene::AssetManager — アプリ寿命の単一アセットキャッシュ
///
/// @details path 鍵 leaf (Mesh / Texture / Shader) を dedupe 所有し、 手続き生成 builtin を名前鍵で配り、
/// Reload で reload-in-place する。 利用側は全て raw ポインタで参照する (所有は本クラス単一)
/// 内部 GPU リソースを握るため、 参照する Renderer より先に Clear / 破棄すること
/// 型別のロード処理を独立メソッドに分け、 本体は「キャッシュの容れ物 + Reload の窓口」に徹する
/// 依存: NS::Graphics::Shader / Texture / Mesh / StaticMesh, NS::Core::FileSystem

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
    class Mesh;
    class StaticMesh;
    class TextureArray;
    struct TextureArrayDesc;
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
    /// vs / ps は必須、 他は欠落時に既定値
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    /// 読み込んだ Material とその基準色。 baseColor は MeshRenderer 側に適用するため別で返す
    struct LoadedMaterial
    {
        NS::Graphics::Material* material = nullptr; ///< AssetManager 所有、 キャッシュ寿命中のみ有効
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
    };

    /// アプリ寿命でアセットを dedupe 所有する単一キャッシュ。 leaf は path 鍵、 builtin は名前鍵
    /// Material 内蔵 CB は MeshRenderer が流す FrameCB に合わせるため本クラスは Scene 層に置く
    class AssetManager
    {
    public:
        /// baseDir は将来 file mesh 等の相対 path を解決する基準 (通常は ContentRoot)
        explicit AssetManager(std::filesystem::path baseDir) noexcept;
        ~AssetManager();

        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
        AssetManager(AssetManager&&) = delete;
        AssetManager& operator=(AssetManager&&) = delete;

        /// path 鍵で Shader を dedupe して返す。 同一 path は同一インスタンス。 失敗時も非 null (fallback)
        [[nodiscard]] NS::Graphics::Shader* GetOrLoadShader(const std::filesystem::path& path);
        /// path 鍵で Texture を dedupe して返す。 同一 path は同一インスタンス
        [[nodiscard]] NS::Graphics::Texture* GetOrLoadTexture(const std::filesystem::path& path);
        /// path 鍵で Mesh を dedupe して返す。 file mesh のロード対応前は未対応 path で nullptr
        [[nodiscard]] NS::Graphics::Mesh* GetOrLoadMesh(const std::filesystem::path& path);

        /// 手続き生成 builtin を一括登録する (cube / wedge45 / wedge30 / wedge22 / wedge15 / pole / shadowQuad)
        /// device 確立後・最初の利用前に 1 度だけ呼ぶ。 既登録名は上書きしない
        void RegisterBuiltins();
        /// 名前鍵で builtin StaticMesh を引く。 未登録は nullptr
        [[nodiscard]] NS::Graphics::StaticMesh* Builtin(std::string_view name) const noexcept;

        /// matPath の .mat を読み込み composite Material を組んで返す。 shader / texture は内部 leaf を借りて dedupe
        /// 既読なら cache を返す。 読込 / 解析失敗時は material=nullptr の LoadedMaterial を返す
        /// 相対 path は構築時の baseDir 基準で解決する
        [[nodiscard]] LoadedMaterial LoadMaterial(const std::filesystem::path& matPath);

        /// 共有 material (player / block / water / shadow) を builtin shader + texture から一括組み立てする
        /// device + RegisterBuiltins 後・最初の利用前に 1 度呼ぶ。 既登録名は上書きしない
        void RegisterSharedMaterials();
        /// 名前鍵で共有 material を引く ("player" / "block" / "water" / "shadow")。 未登録は nullptr
        [[nodiscard]] NS::Graphics::Material* SharedMaterial(std::string_view name) const noexcept;

        /// 名前鍵で Texture2DArray を保持する。 初回は desc から生成して所有、 2 回目以降は name で cache hit (desc
        /// は無視) slice path リストから組む theme atlas のような app 寿命で 1 本の array 向け。 失敗時も非 null
        /// (fallback)
        [[nodiscard]] NS::Graphics::TextureArray* GetOrCreateTextureArray(std::string_view name,
                                                                          const NS::Graphics::TextureArrayDesc& desc);
        /// 名前鍵で TextureArray を引く (描画パスの毎フレーム取得用)。 未登録は nullptr
        [[nodiscard]] NS::Graphics::TextureArray* TextureArrayByName(std::string_view name) const noexcept;

        /// path 鍵 leaf を引き reload-in-place する (現状 Shader のみ)。 成功で true、 未キャッシュ / 失敗で false
        [[nodiscard]] bool Reload(const std::filesystem::path& path);

        /// キャッシュ済み全 Shader を reload-in-place する。 reload 成功本数を返す (HLSL 編集の即時反映トリガ用)
        std::size_t ReloadAllShaders();

        /// 全キャッシュを解放する。 Renderer 破棄より前に呼ぶ
        void Clear() noexcept;

    private:
        struct MaterialRecord
        {
            std::unique_ptr<NS::Graphics::Material> material;
            NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        };

        std::filesystem::path m_baseDir;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Shader>> m_shaders;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Texture>> m_textures;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Mesh>> m_meshes;
        std::map<std::string, std::unique_ptr<NS::Graphics::StaticMesh>> m_builtins;        // path 無し、 leaf と別容器
        std::map<std::filesystem::path, MaterialRecord> m_materials;                        // .mat composite
        std::map<std::string, std::unique_ptr<NS::Graphics::Material>> m_sharedMaterials;   // 手続き共有 material
        std::map<std::string, std::unique_ptr<NS::Graphics::TextureArray>> m_textureArrays; // 名前鍵 Texture2DArray
    };
} // namespace NS::Scene
