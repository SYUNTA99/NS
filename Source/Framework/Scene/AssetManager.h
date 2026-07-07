#pragma once

/// @file AssetManager.h
/// @brief NS::Scene::AssetManager — アプリ寿命の単一アセットキャッシュ
///
/// @details path 鍵の leaf である Mesh / Texture / Shader を dedupe 所有し、 手続き生成の組み込みを名前鍵で配り、
/// Reload で reload-in-place する。 利用側は全て raw ポインタで参照し、 所有は本クラス単一
/// 内部 GPU リソースを握るため、 参照する Renderer より先に Clear / 破棄すること
/// 型別のロード処理を独立メソッドに分け、 本体は「キャッシュの容れ物 + Reload の窓口」に徹する
/// 依存: NS::Graphics::Shader / Texture / Mesh / StaticMesh, NS::Core::FileSystem

#include "Framework/Graphics/Animation.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/Skeleton.h"
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
} // namespace NS::Graphics

namespace NS::Scene
{
    /// .mat の JSON 解析結果。 GPU 非依存なので deviceless でテストできる
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
        /// AssetManager 所有、 キャッシュ寿命中のみ有効
        NS::Graphics::Material* material = nullptr;
        NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
    };

    /// 読み込んだ skinned model。 mesh は AssetManager 所有の参照、 skeleton / clips は呼出側へ複製を渡す
    /// 再生状態とクリップ合成のリターゲットはインスタンス側で持つため、 共有テンプレートを複製で配る
    struct LoadedSkinnedModel
    {
        /// AssetManager 所有、 キャッシュ寿命中のみ有効
        NS::Graphics::SkeletalMesh* mesh = nullptr;
        /// 複製で呼出側が所有する
        NS::Graphics::Skeleton skeleton;
        /// 複製で呼出側が所有する
        std::vector<NS::Graphics::AnimationClip> clips;
        /// bind ポーズ頂点の境界。 配置スケール計算に使う
        NS::Math::Vector3 boundsMin{};
        NS::Math::Vector3 boundsMax{};
        bool valid = false;
    };

    /// アプリ寿命でアセットを dedupe 所有する単一キャッシュ。 leaf は path 鍵、 組み込みは名前鍵
    /// Material 内蔵 CB は MeshRenderer が流す FrameCB に合わせるため本クラスは Scene 層に置く
    class AssetManager
    {
    public:
        /// baseDir は将来 file mesh 等の相対 path を解決する基準で、 通常は ContentRoot
        explicit AssetManager(std::filesystem::path baseDir) noexcept;
        ~AssetManager();

        AssetManager(const AssetManager&) = delete;
        AssetManager& operator=(const AssetManager&) = delete;
        AssetManager(AssetManager&&) = delete;
        AssetManager& operator=(AssetManager&&) = delete;

        /// path 鍵で Shader を dedupe して返す。 同一 path は同一インスタンス。 失敗時も非 null で fallback を返す
        [[nodiscard]] NS::Graphics::Shader* GetOrLoadShader(const std::filesystem::path& path);
        /// path 鍵で Texture を dedupe して返す。 同一 path は同一インスタンス
        [[nodiscard]] NS::Graphics::Texture* GetOrLoadTexture(const std::filesystem::path& path);
        /// path 鍵で Mesh を dedupe して返す。 読込 / GPU 生成に失敗した path も負キャッシュし、 以後は再読込せず
        /// 即 nullptr を返す。 修正した file の再試行は Clear() でキャッシュを解いてから
        [[nodiscard]] NS::Graphics::Mesh* GetOrLoadMesh(const std::filesystem::path& path);
        /// 現在キャッシュしている mesh エントリ数。 読込失敗を負キャッシュした path も 1 件として数える
        [[nodiscard]] std::size_t MeshCacheSize() const noexcept;

        /// skinned glTF を読み SkeletalMesh を path 鍵で dedupe 所有して返す。 再生状態とクリップ合成は
        /// インスタンス側が持つため skeleton / clips は複製で返す。 失敗時は valid=false。 相対 path は baseDir 基準
        [[nodiscard]] LoadedSkinnedModel GetOrLoadSkinnedModel(const std::filesystem::path& path);

        /// 手続き生成の組み込み cube / wedge45 / wedge30 / wedge22 / wedge15 / shadowQuad を一括登録する
        /// device 確立後・最初の利用前に 1 度だけ呼ぶ。 既登録名は上書きしない
        void RegisterBuiltins();
        /// 名前鍵で組み込み StaticMesh を引く。 未登録は nullptr
        [[nodiscard]] NS::Graphics::StaticMesh* Builtin(std::string_view name) const noexcept;

        /// matPath の .mat を読み込み composite Material を組んで返す。 shader / texture は内部 leaf を借りて dedupe
        /// 既読なら cache を返す。 読込 / 解析失敗時は material=nullptr の LoadedMaterial を返す
        /// 相対 path は構築時の baseDir 基準で解決する
        [[nodiscard]] LoadedMaterial LoadMaterial(const std::filesystem::path& matPath);

        /// 共有 material である player / water / shadow を組み込み shader + texture から一括組み立てする
        /// device + RegisterBuiltins 後・最初の利用前に 1 度呼ぶ。 既登録名は上書きしない
        void RegisterSharedMaterials();
        /// 名前鍵で共有 material を引く。 鍵は "player" / "water" / "shadow"。 未登録は nullptr
        [[nodiscard]] NS::Graphics::Material* SharedMaterial(std::string_view name) const noexcept;

        /// path 鍵 leaf を引き reload-in-place する。 現状 Shader のみ。 成功で true、 未キャッシュ / 失敗で false
        [[nodiscard]] bool Reload(const std::filesystem::path& path);

        /// キャッシュ済み全 Shader を reload-in-place する。 reload 成功本数を返す。 HLSL 編集の即時反映トリガ用
        std::size_t ReloadAllShaders();

        /// 全キャッシュを解放する。 Renderer 破棄より前に呼ぶ
        void Clear() noexcept;

    private:
        struct MaterialRecord
        {
            std::unique_ptr<NS::Graphics::Material> material;
            NS::Math::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        };

        struct SkinnedModelRecord
        {
            std::unique_ptr<NS::Graphics::SkeletalMesh> mesh;
            NS::Graphics::Skeleton skeleton;
            std::vector<NS::Graphics::AnimationClip> clips;
            NS::Math::Vector3 boundsMin{};
            NS::Math::Vector3 boundsMax{};
        };

        std::filesystem::path m_baseDir;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Shader>> m_shaders;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Texture>> m_textures;
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Mesh>> m_meshes;
        std::map<std::string, std::unique_ptr<NS::Graphics::StaticMesh>> m_builtins;      // path 無し、 leaf と別容器
        std::map<std::filesystem::path, MaterialRecord> m_materials;                      // .mat composite
        std::map<std::string, std::unique_ptr<NS::Graphics::Material>> m_sharedMaterials; // 手続き共有 material
        std::map<std::filesystem::path, SkinnedModelRecord>
            m_skinnedModels; // skinned glTF、 mesh 所有 + skeleton/clips テンプレ
    };
} // namespace NS::Scene
