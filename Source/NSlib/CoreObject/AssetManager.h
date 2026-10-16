#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Graphics/Animation.h"
#include "NSlib/Graphics/Material.h"
#include "NSlib/Graphics/Skeleton.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class Shader;
    }
    namespace Gfx
    {
        class Texture;
    }
    namespace Gfx
    {
        class Mesh;
    }
    namespace Gfx
    {
        class StaticMesh;
    }
    namespace Gfx
    {
        struct AnimationSource;
    }
} // namespace NS

namespace NS
{
    namespace Phys
    {
        struct MeshShape;
    }
} // namespace NS

namespace NS
{
    class AssetManager;

    //! アセット相対パスを ContentRoot 配下の絶対パスへ正規化する
    //! ディレクトリトラバーサル (..) で ContentRoot 外へ出る不正なパスは弾き nullopt を返す
    [[nodiscard]] std::optional<std::string> ResolveContentPath(const std::string& relative);

    //! 参照文字列からメッシュを解決する。組み込み名を先に引き、外れたら ContentRoot 相対の glTF パスとして読む
    //! 空文字・登録前の組み込み名・トラバーサル・読込失敗は nullptr。組み込み名はパスとして読まない
    [[nodiscard]] Gfx::Mesh* ResolveMeshFromRef(AssetManager& assets, const std::string& meshRef);

    //! .mat の JSON 解析結果。GPU 非依存なので device 無しでテストできる
    struct MaterialFileDesc
    {
        std::string vertexShader;
        std::string pixelShader;
        std::vector<std::string> textures;
        Vector3 baseColor{1.0f, 1.0f, 1.0f};
        Gfx::BlendMode blend = Gfx::BlendMode::Opaque;
    };

    //! JSON 文字列を MaterialFileDesc へ解析する。成功で true、失敗時は outError に理由を入れる
    //! vs / ps は必須、他は欠落時に既定値
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    //! 読み込んだ Material とその基準色。baseColor は Model 側に適用するため別で返す
    struct LoadedMaterial
    {
        //! AssetManager 所有、キャッシュ寿命中のみ有効
        Gfx::Material* material = nullptr;
        Vector3 baseColor{1.0f, 1.0f, 1.0f};
    };

    //! 読み込んだ skinned model。mesh / skeleton / clips 全て AssetManager 所有でキャッシュ寿命中のみ有効
    //! 再生状態はインスタンス側が持つが、skeleton / clips のデータは不変なので複数インスタンスで共有する
    struct LoadedSkinnedModel
    {
        Gfx::SkeletalMesh* mesh = nullptr;
        const Gfx::Skeleton* skeleton = nullptr;
        const std::vector<Gfx::AnimationClip>* clips = nullptr;
        bool valid = false;
    };

    //! アプリ寿命でアセットを重複なく所有する単一キャッシュ。ファイル由来は path キー、組み込みは名前キー
    class AssetManager : public NonCopyable
    {
    public:
        //! baseDir は shader / texture の相対 path を解決する基準で、通常は ContentRoot
        explicit AssetManager(std::string baseDir) noexcept;
        ~AssetManager();

        //! @brief 素材を読む AssetManager を返す
        //! @details AssetManagerScope が差した物を返す。差していない間は、組み込みも共有の material も
        //! 登録していない AssetManager を返すので、組み込みの名前は引けない
        [[nodiscard]] static AssetManager& Get() noexcept;

        //! path ごとに Shader を共有する。失敗時も代替シェーダーを返す
        [[nodiscard]] Gfx::Shader* GetOrLoadShader(std::string_view path);
        //! path キーで Texture を共有して返す。同一 path は同一インスタンス
        [[nodiscard]] Gfx::Texture* GetOrLoadTexture(std::string_view path);
        //! path キーで Mesh を共有して返す。読込 / GPU 生成に失敗した path も負キャッシュし、以後は再読込せず
        //! 即 nullptr を返す。修正した file の再試行は Clear() でキャッシュを解いてから
        [[nodiscard]] Gfx::Mesh* GetOrLoadMesh(std::string_view path);

        //! 資産ごとの衝突形状を共有する。GPU がなくても作成でき、同じファイルは重複して読まない
        //! 空・領域外の参照・読込失敗は nullptr。失敗をキャッシュし、参照は Clear まで有効
        [[nodiscard]] const Phys::MeshShape* GetOrLoadMeshShape(const std::string& meshRef);

        //! skinned glTF を読み SkeletalMesh を path キーで重複なく所有して返す。skeleton / clips は
        //! キャッシュ record への参照で返し、再生状態だけをインスタンス側が持つ。失敗時は valid=false
        [[nodiscard]] LoadedSkinnedModel GetOrLoadSkinnedModel(std::string_view path);

        //! アニメーション専用 glTF を path キーで重複なく所有して返す。読込失敗の path は負キャッシュし
        //! 以後は再読込せず nullptr を返す。修正した file の再試行は Clear() でキャッシュを解いてから
        [[nodiscard]] const Gfx::AnimationSource* GetOrLoadAnimationSource(std::string_view path);

        //! 骨名で結合したクリップをパスの組ごとに共有する。結合対象がなければ空の一覧
        //! クリップ読込失敗は負キャッシュ、モデル読込失敗は再試行可能な nullptr
        [[nodiscard]] const std::vector<Gfx::AnimationClip>* GetOrLoadBoundClips(std::string_view clipPath,
                                                                                     std::string_view modelPath);

        //! 手続き生成の組み込み cube / sphere / wedge45 / wedge30 / wedge22 / wedge15 / shadowQuad を一括登録する
        //! device 確立後・最初の利用前に 1 度だけ呼ぶ。既登録名は上書きしない
        void RegisterBuiltins();
        //! 名前キーで組み込み StaticMesh を引く。未登録は nullptr
        //! RegisterBuiltins を通っていない AssetManager では全部 nullptr になる
        //! 同じ名前の当たりは RegisterBuiltins 無しでも GetOrLoadMeshShape が返す
        [[nodiscard]] Gfx::StaticMesh* Builtin(std::string_view name) const noexcept;
        //! name が RegisterBuiltins の登録する組み込みの形の名前なら true。登録の前でも答える
        [[nodiscard]] static bool IsBuiltinName(std::string_view name) noexcept;

        //! 寸法ごとに共有するカプセルメッシュ。半長 0 で球になり、参照は Clear まで有効
        //! 非有限の寸法・GPU なし・生成失敗は nullptr。生成失敗は次回に再試行する
        [[nodiscard]] Gfx::StaticMesh* GetOrMakeCapsuleMesh(float radius, float halfHeight);

        //! matPath の .mat を読み込み composite Material を組んで返す。shader / texture は内部 leaf を借りて共有
        //! 既読なら cache を返す。読込 / 解析失敗時は material=nullptr の LoadedMaterial を返す
        //! 相対 path は構築時の baseDir 基準で解決する
        [[nodiscard]] LoadedMaterial LoadMaterial(std::string_view matPath);

        //! 共有 material である player / water / shadow を組み込み shader + texture から一括組み立てする
        //! device + RegisterBuiltins 後・最初の利用前に 1 度呼ぶ。既登録名は上書きしない
        void RegisterSharedMaterials();
        //! 名前キーで共有 material を引く。キーは "player" / "water" / "shadow"。未登録は nullptr
        [[nodiscard]] Gfx::Material* SharedMaterial(std::string_view name) const noexcept;
        //! name が RegisterSharedMaterials の登録する共有 material の名前なら true。登録の前でも答える
        [[nodiscard]] static bool IsSharedMaterialName(std::string_view name) noexcept;

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
            std::unique_ptr<Gfx::Material> material;
            Vector3 baseColor{1.0f, 1.0f, 1.0f};
        };

        // ファイルのメッシュ 1 件。描画と当たりを一度の読み込みで作る。失敗時は両方空で記録する
        struct MeshRecord
        {
            std::unique_ptr<Gfx::Mesh> mesh;            // GPU での生成に失敗したら空
            std::unique_ptr<Phys::MeshShape> collision; // Jolt の形は当たりを頼まれた時に作る
        };

        // 正規化した path で記録を引き、無ければ glTF を読んで作る
        [[nodiscard]] MeshRecord& LoadMeshRecord(std::string_view path);

        struct SkinnedModelRecord
        {
            std::unique_ptr<Gfx::SkeletalMesh> mesh;
            Gfx::Skeleton skeleton;
            std::vector<Gfx::AnimationClip> clips;
        };

        std::string m_baseDir; // 相対 path 解決の基準ディレクトリ
        std::map<std::string, std::unique_ptr<Gfx::Shader>> m_shaders;
        std::map<std::string, std::unique_ptr<Gfx::Texture>> m_textures;
        std::map<std::string, MeshRecord> m_meshes;                                      // path キーの file mesh
        std::map<std::string, std::unique_ptr<Phys::MeshShape>> m_builtinCollisions; // 組み込み名キーの当たり
        std::map<std::string, std::unique_ptr<Gfx::StaticMesh>> m_builtins;          // path 無し、leaf と別容器
        std::map<std::pair<float, float>, std::unique_ptr<Gfx::StaticMesh>>
            m_capsules;                                                              // 半径と半分の高さの組がキー
        std::map<std::string, MaterialRecord> m_materials;                           // .mat composite
        std::map<std::string, std::unique_ptr<Gfx::Material>> m_sharedMaterials; // 手続き共有マテリアル
        std::map<std::string, SkinnedModelRecord>
            m_skinnedModels; // skinned glTF。record は挿入後に書き換えず、配った参照を安定させる
        std::map<std::string, std::unique_ptr<Gfx::AnimationSource>>
            m_animationSources; // アニメ専用 glTF。空の値は読み込み失敗の記録
        std::map<std::pair<std::string, std::string>, std::unique_ptr<std::vector<Gfx::AnimationClip>>>
            m_boundClips; // clip と model の path の組で引く結合済みクリップ。空の値は読み込み失敗の記録
    };

    //! @brief 生きている間だけ AssetManager::Get() が assets を返すようにし、壊れる時に前の物へ戻す
    //! @details Application が自分の AssetManager を差す。自分で AssetManager を作る試しも、試しの間だけ差す
    class AssetManagerScope : public NonCopyable
    {
    public:
        explicit AssetManagerScope(AssetManager& assets) noexcept;
        ~AssetManagerScope() noexcept;

    private:
        AssetManager* m_previous = nullptr;
    };
} // namespace NS
