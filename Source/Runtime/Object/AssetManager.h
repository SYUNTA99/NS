#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Graphics/Animation.h"
#include "Runtime/Graphics/Material.h"
#include "Runtime/Graphics/Skeleton.h"

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

namespace NS::Physics
{
    struct MeshCollision;
}

namespace NS::Object
{
    class AssetManager;

    //! アセット相対パスを ContentRoot 配下の絶対パスへ正規化する
    //! ディレクトリトラバーサル (..) で ContentRoot 外へ出る不正なパスは弾き nullopt を返す
    [[nodiscard]] std::optional<std::string> ResolveContentPath(const std::string& relative);

    //! 参照文字列からメッシュを解決する。組み込み名を先に引き、外れたら ContentRoot 相対の glTF パスとして読む
    //! 空文字・トラバーサル・読込失敗は nullptr
    [[nodiscard]] NS::Graphics::Mesh* ResolveMeshFromRef(AssetManager& assets, const std::string& meshRef);

    //! .mat の JSON 解析結果。GPU 非依存なので device 無しでテストできる
    struct MaterialFileDesc
    {
        std::string vertexShader;
        std::string pixelShader;
        std::vector<std::string> textures;
        NS::Core::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        NS::Graphics::BlendMode blend = NS::Graphics::BlendMode::Opaque;
    };

    //! JSON 文字列を MaterialFileDesc へ解析する。成功で true、失敗時は outError に理由を入れる
    //! vs / ps は必須、他は欠落時に既定値
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    //! 読み込んだ Material とその基準色。baseColor は MeshRenderer 側に適用するため別で返す
    struct LoadedMaterial
    {
        //! AssetManager 所有、キャッシュ寿命中のみ有効
        NS::Graphics::Material* material = nullptr;
        NS::Core::Vector3 baseColor{1.0f, 1.0f, 1.0f};
    };

    //! 読み込んだ skinned model。mesh / skeleton / clips 全て AssetManager 所有でキャッシュ寿命中のみ有効
    //! 再生状態はインスタンス側が持つが、skeleton / clips のデータは不変なので複数インスタンスで共有する
    struct LoadedSkinnedModel
    {
        NS::Graphics::SkeletalMesh* mesh = nullptr;                      // AssetManager 所有
        const NS::Graphics::Skeleton* skeleton = nullptr;                // AssetManager 所有
        const std::vector<NS::Graphics::AnimationClip>* clips = nullptr; // AssetManager 所有
        bool valid = false;
    };

    //! アプリ寿命でアセットを重複なく所有する単一キャッシュ。ファイル由来は path キー、組み込みは名前キー
    class AssetManager : public NS::Core::NonCopyable
    {
    public:
        //! baseDir は shader / texture の相対 path を解決する基準で、通常は ContentRoot
        explicit AssetManager(std::string baseDir) noexcept;
        ~AssetManager();

        //! path キーで Shader を共有して返す。同一 path は同一インスタンス。失敗時も非 null で fallback を返す
        [[nodiscard]] NS::Graphics::Shader* GetOrLoadShader(std::string_view path);
        //! path キーで Texture を共有して返す。同一 path は同一インスタンス
        [[nodiscard]] NS::Graphics::Texture* GetOrLoadTexture(std::string_view path);
        //! path キーで Mesh を共有して返す。読込 / GPU 生成に失敗した path も負キャッシュし、以後は再読込せず
        //! 即 nullptr を返す。修正した file の再試行は Clear() でキャッシュを解いてから
        [[nodiscard]] NS::Graphics::Mesh* GetOrLoadMesh(std::string_view path);
        //! 現在キャッシュしている mesh エントリ数。読込失敗を負キャッシュした path も 1 件として数える
        [[nodiscard]] std::size_t MeshCacheSize() const noexcept;

        //! meshRef の形を当たりにして返す。参照の引き方は ResolveMeshFromRef と同じ
        //! 三角形は描画の index の並びのままで、法線 (v1 - v0) × (v2 - v0) が表面の外を向く
        //! Jolt の形は最初に頼まれた時に 1 度だけ作り、以後は同じ当たりを返す。歪みの無い配置物はその形を共有する
        //! 同じ file を指す参照には、区切り文字や . / .. の書き方が違っても同じ当たりを返す
        //! file は GetOrLoadMesh と同じ読込を通るので、glTF を読むのは描画と合わせて 1 回
        //! device が無くても当たりは作れる。読めなかった参照も負キャッシュする
        //! 空文字・トラバーサル・読込失敗は nullptr。返す当たりは AssetManager 所有で Clear() まで有効
        [[nodiscard]] const NS::Physics::MeshCollision* GetOrLoadMeshCollision(const std::string& meshRef);

        //! skinned glTF を読み SkeletalMesh を path キーで重複なく所有して返す。skeleton / clips は
        //! キャッシュ record への参照で返し、再生状態だけをインスタンス側が持つ。失敗時は valid=false
        [[nodiscard]] LoadedSkinnedModel GetOrLoadSkinnedModel(std::string_view path);

        //! アニメーション専用 glTF を path キーで重複なく所有して返す。読込失敗の path は負キャッシュし
        //! 以後は再読込せず nullptr を返す。修正した file の再試行は Clear() でキャッシュを解いてから
        [[nodiscard]] const NS::Graphics::AnimationSource* GetOrLoadAnimationSource(std::string_view path);

        //! clipPath のアニメーションを modelPath の骨格へ骨名で結合した結果を両 path の組で重複なく所有して
        //! 返す。クリップ側の読込失敗は負キャッシュして nullptr、model 側の失敗はキャッシュせず nullptr を
        //! 返し後で再試行できる。結合できるトラックが 1 本も無ければ空の一覧を非 null で返す
        //! 同じ組で呼ぶ全インスタンスが結果を共有する
        [[nodiscard]] const std::vector<NS::Graphics::AnimationClip>* GetOrLoadBoundClips(std::string_view clipPath,
                                                                                          std::string_view modelPath);

        //! 手続き生成の組み込み cube / sphere / wedge45 / wedge30 / wedge22 / wedge15 / shadowQuad を一括登録する
        //! device 確立後・最初の利用前に 1 度だけ呼ぶ。既登録名は上書きしない
        void RegisterBuiltins();
        //! 名前キーで組み込み StaticMesh を引く。未登録は nullptr
        //! RegisterBuiltins を通っていない AssetManager では全部 nullptr になる
        //! 同じ名前の当たりは RegisterBuiltins 無しでも GetOrLoadMeshCollision が返す
        [[nodiscard]] NS::Graphics::StaticMesh* Builtin(std::string_view name) const noexcept;

        //! matPath の .mat を読み込み composite Material を組んで返す。shader / texture は内部 leaf を借りて共有
        //! 既読なら cache を返す。読込 / 解析失敗時は material=nullptr の LoadedMaterial を返す
        //! 相対 path は構築時の baseDir 基準で解決する
        [[nodiscard]] LoadedMaterial LoadMaterial(std::string_view matPath);

        //! 共有 material である player / water / shadow を組み込み shader + texture から一括組み立てする
        //! device + RegisterBuiltins 後・最初の利用前に 1 度呼ぶ。既登録名は上書きしない
        void RegisterSharedMaterials();
        //! 名前キーで共有 material を引く。キーは "player" / "water" / "shadow"。未登録は nullptr
        [[nodiscard]] NS::Graphics::Material* SharedMaterial(std::string_view name) const noexcept;

        //! path キーのアセットを同じインスタンスのまま読み直す。現状 Shader のみ。成功で true、未キャッシュ /
        //! 失敗で false
        [[nodiscard]] bool Reload(std::string_view path);

        //! キャッシュ済み全 Shader を同じインスタンスのまま読み直す。成功本数を返す。HLSL 編集の即時反映に使う
        std::size_t ReloadAllShaders();

        //! 全キャッシュを解放する。Renderer 破棄より前に呼ぶ
        void Clear() noexcept;

    private:
        struct MaterialRecord
        {
            std::unique_ptr<NS::Graphics::Material> material;
            NS::Core::Vector3 baseColor{1.0f, 1.0f, 1.0f};
        };

        // file の mesh 1 件。描画と当たりを 1 回の読込から両方作る。読込に失敗した path も両方 null で残す
        struct MeshRecord
        {
            std::unique_ptr<NS::Graphics::Mesh> mesh;              // GPU 生成に失敗したら null
            std::unique_ptr<NS::Physics::MeshCollision> collision; // Jolt の形は当たりを頼まれた時に作る
        };

        // 正規化した path で記録を引き、無ければ glTF を読んで作る
        [[nodiscard]] MeshRecord& LoadMeshRecord(std::string_view path);

        struct SkinnedModelRecord
        {
            std::unique_ptr<NS::Graphics::SkeletalMesh> mesh;
            NS::Graphics::Skeleton skeleton;
            std::vector<NS::Graphics::AnimationClip> clips;
        };

        std::string m_baseDir;                                                    // 相対 path 解決の基準ディレクトリ
        std::map<std::string, std::unique_ptr<NS::Graphics::Shader>> m_shaders;   // path キーの Shader キャッシュ
        std::map<std::string, std::unique_ptr<NS::Graphics::Texture>> m_textures; // path キーの Texture キャッシュ
        std::map<std::string, MeshRecord> m_meshes;                               // path キーの file mesh
        std::map<std::string, std::unique_ptr<NS::Physics::MeshCollision>>
            m_builtinCollisions;                                                          // 組み込み名キーの当たり
        std::map<std::string, std::unique_ptr<NS::Graphics::StaticMesh>> m_builtins;      // path 無し、leaf と別容器
        std::map<std::string, MaterialRecord> m_materials;                                // .mat composite
        std::map<std::string, std::unique_ptr<NS::Graphics::Material>> m_sharedMaterials; // 手続き共有マテリアル
        std::map<std::string, SkinnedModelRecord>
            m_skinnedModels; // skinned glTF。record は挿入後に書き換えず、配った参照を安定させる
        std::map<std::string, std::unique_ptr<NS::Graphics::AnimationSource>>
            m_animationSources; // アニメ専用 glTF、null は負キャッシュ
        std::map<std::pair<std::string, std::string>, std::unique_ptr<std::vector<NS::Graphics::AnimationClip>>>
            m_boundClips; // clip と model の path 組がキーの結合済クリップ、null は負キャッシュ
    };
} // namespace NS::Object
