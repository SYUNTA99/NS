#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Animation.h"
#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Skeleton.h"
#include "Runtime/Math/Math.h"

#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS::Graphics
{
    class Shader;
    class Texture;
    class Mesh;
    class StaticMesh;
    struct AnimationSource;
} // namespace NS::Graphics

namespace NS::Object
{
    class AssetManager;

    /// アセット相対パスを ContentRoot 配下の絶対パスへ正規化する
    /// ディレクトリトラバーサル (`..`) で ContentRoot 外へ出る不正なパスは弾き nullopt を返す
    [[nodiscard]] std::optional<std::filesystem::path> ResolveContentPath(const std::string& relative);

    /// 参照文字列からメッシュを解決する。 組み込み名を先に引き、 外れたら ContentRoot 相対の glTF パスとして読む
    /// 空文字・トラバーサル・読込失敗は nullptr
    [[nodiscard]] NS::Graphics::Mesh* ResolveMeshFromRef(AssetManager& assets, const std::string& meshRef);

    /// .mat の JSON 解析結果。 GPU 非依存なので device 無しでテストできる
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

    /// 読み込んだ skinned model。 mesh / skeleton / clips 全て AssetManager 所有でキャッシュ寿命中のみ有効
    /// 再生状態はインスタンス側が持つが、 skeleton / clips のデータは不変なので複数インスタンスで共有する
    struct LoadedSkinnedModel
    {
        NS::Graphics::SkeletalMesh* mesh = nullptr;                      // AssetManager 所有
        const NS::Graphics::Skeleton* skeleton = nullptr;                // AssetManager 所有
        const std::vector<NS::Graphics::AnimationClip>* clips = nullptr; // AssetManager 所有
        /// bind ポーズ頂点の境界。 配置スケール計算に使う
        NS::Math::Vector3 boundsMin{};
        NS::Math::Vector3 boundsMax{};
        bool valid = false;
    };

    /// アプリ寿命でアセットを重複なく所有する単一キャッシュ。 leaf は path 鍵、 組み込みは名前鍵
    /// Material 内蔵 CB は MeshRenderer が流す FrameCB に合わせるため本クラスは Scene 層に置く
    class AssetManager : public NS::Core::NonCopyable
    {
    public:
        /// baseDir は将来 file mesh 等の相対 path を解決する基準で、 通常は ContentRoot
        explicit AssetManager(std::filesystem::path baseDir) noexcept;
        ~AssetManager();

        /// path 鍵で Shader を共有して返す。 同一 path は同一インスタンス。 失敗時も非 null で fallback を返す
        [[nodiscard]] NS::Graphics::Shader* GetOrLoadShader(const std::filesystem::path& path);
        /// path 鍵で Texture を共有して返す。 同一 path は同一インスタンス
        [[nodiscard]] NS::Graphics::Texture* GetOrLoadTexture(const std::filesystem::path& path);
        /// path 鍵で Mesh を共有して返す。 読込 / GPU 生成に失敗した path も負キャッシュし、 以後は再読込せず
        /// 即 nullptr を返す。 修正した file の再試行は Clear() でキャッシュを解いてから
        [[nodiscard]] NS::Graphics::Mesh* GetOrLoadMesh(const std::filesystem::path& path);
        /// 現在キャッシュしている mesh エントリ数。 読込失敗を負キャッシュした path も 1 件として数える
        [[nodiscard]] std::size_t MeshCacheSize() const noexcept;

        /// skinned glTF を読み SkeletalMesh を path 鍵で重複なく所有して返す。 skeleton / clips は
        /// キャッシュ record への参照で返し、 再生状態だけをインスタンス側が持つ。 失敗時は valid=false
        /// 相対 path は baseDir 基準
        [[nodiscard]] LoadedSkinnedModel GetOrLoadSkinnedModel(const std::filesystem::path& path);

        /// アニメーション専用 glTF を path 鍵で重複なく所有して返す。 読込失敗の path は負キャッシュし
        /// 以後は再読込せず nullptr を返す。 修正した file の再試行は Clear() でキャッシュを解いてから
        [[nodiscard]] const NS::Graphics::AnimationSource* GetOrLoadAnimationSource(const std::filesystem::path& path);

        /// clipPath のアニメーションを modelPath の骨格へ骨名で結合した結果を両 path の組で重複なく所有して
        /// 返す。 クリップ側の読込失敗は負キャッシュして nullptr、 model 側の失敗はキャッシュせず nullptr を
        /// 返し後で再試行できる。 結合できるトラックが 1 本も無ければ空の一覧を非 null で返す
        /// 同じ組で呼ぶ全インスタンスが結果を共有する
        [[nodiscard]] const std::vector<NS::Graphics::AnimationClip>* GetOrLoadBoundClips(
            const std::filesystem::path& clipPath, const std::filesystem::path& modelPath);

        /// 手続き生成の組み込み cube / wedge45 / wedge30 / wedge22 / wedge15 / shadowQuad を一括登録する
        /// device 確立後・最初の利用前に 1 度だけ呼ぶ。 既登録名は上書きしない
        void RegisterBuiltins();
        /// 名前鍵で組み込み StaticMesh を引く。 未登録は nullptr
        [[nodiscard]] NS::Graphics::StaticMesh* Builtin(std::string_view name) const noexcept;

        /// matPath の .mat を読み込み composite Material を組んで返す。 shader / texture は内部 leaf を借りて共有
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

        std::filesystem::path m_baseDir; // 相対 path 解決の基準ディレクトリ
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Shader>> m_shaders; // path 鍵の Shader キャッシュ
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Texture>>
            m_textures; // path 鍵の Texture キャッシュ
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::Mesh>>
            m_meshes; // path 鍵の Mesh キャッシュ、 null は負キャッシュ
        std::map<std::string, std::unique_ptr<NS::Graphics::StaticMesh>> m_builtins;      // path 無し、 leaf と別容器
        std::map<std::filesystem::path, MaterialRecord> m_materials;                      // .mat composite
        std::map<std::string, std::unique_ptr<NS::Graphics::Material>> m_sharedMaterials; // 手続き共有 material
        std::map<std::filesystem::path, SkinnedModelRecord>
            m_skinnedModels; // skinned glTF。 record は挿入後に書き換えず、 配った参照を安定させる
        std::map<std::filesystem::path, std::unique_ptr<NS::Graphics::AnimationSource>>
            m_animationSources; // アニメ専用 glTF、 null は負キャッシュ
        std::map<std::pair<std::filesystem::path, std::filesystem::path>,
                 std::unique_ptr<std::vector<NS::Graphics::AnimationClip>>>
            m_boundClips; // clip と model の path 組が鍵の結合済クリップ、 null は負キャッシュ
    };
} // namespace NS::Object
