#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/Graphics/Animation.h"
#include "NSlib/Graphics/D3DMaterial.h"
#include "NSlib/Graphics/Skeleton.h"

#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class D3DShader;
    }
    namespace Gfx
    {
        class D3DTexture;
    }
    namespace Gfx
    {
        class D3DMesh;
    }
    namespace Gfx
    {
        struct AnimSource;
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
    class Mesh;
    class Shader;
    class Texture;

    //! アセット相対パスを ContentRoot 配下の絶対パスへ正規化する
    //! ディレクトリトラバーサル (..) で ContentRoot 外へ出る不正なパスは弾き nullopt を返す
    [[nodiscard]] std::optional<std::string> ResolveContentPath(const std::string& relative);

    //! JSON 文字列を MaterialFileDesc へ解析する。成功で true、失敗時は outError に理由を入れる
    //! vs / ps は必須、他は欠落時に既定値。綴りの合わない混ぜ方は Opaque。GPU を使わないので装置無しで試せる
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    //! @brief 読んだ骨付きのモデル 1 件。AssetManager が持ち、Clear まで有効
    //! @details 読んだ後は書き換えないので、何体のアクターでも同じ物を使う。再生の状態はアクターの側が持つ
    struct SkinnedModel
    {
        std::unique_ptr<Gfx::D3DSkeletalMesh> mesh;
        Gfx::Skeleton skeleton;
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

        //! path ごとに D3DShader を共有する。失敗時も代替シェーダーを返す
        [[nodiscard]] Gfx::D3DShader* GetOrLoadShader(std::string_view path);
        //! path キーで D3DTexture を共有して返す。同一 path は同一インスタンス
        [[nodiscard]] Gfx::D3DTexture* GetOrLoadTexture(std::string_view path);
        //! @brief 欄の名前のメッシュを返す。名前は組み込みの形の名前か、ContentRoot からのファイルの道筋
        //! @details 空の名前と読めない名前は組み込みの cube で代え、読めない名前は警告する
        //! 組み込みの形の名前はファイルとして読まないので、RegisterBuiltins の前は nullptr
        [[nodiscard]] Gfx::D3DMesh* LoadMesh(std::string_view name);
        //! LoadMesh と同じ名前の決まりで引き、読めなければ代えずに nullptr を返す。警告もしない
        [[nodiscard]] Gfx::D3DMesh* TryLoadMesh(std::string_view name);

        //! @brief 欄の名前のメッシュの当たりを返す。名前の決まりと代わりの cube は LoadMesh と同じ
        //! @details 装置が無くても作れる。同じファイルは描く物と同じ記録から 1 回だけ読む
        [[nodiscard]] const Phys::MeshShape* LoadMeshShape(std::string_view name);

        //! @brief 欄の名前の骨付きのモデルを返す。名前は ContentRoot からの glTF の道筋
        //! @details 空の名前は「使わない」で、警告せずに nullptr。読めない名前は報告して nullptr
        //! 読めなかったファイルは覚えないので、直した後に読み直すと読める
        [[nodiscard]] const SkinnedModel* LoadSkinnedModel(std::string_view name);

        //! @brief 欄の名前のクリップを返す。名前は ContentRoot からの glTF の道筋か「道筋#動きの名前」
        //! @details # が無ければファイルの中の先頭の 1 本。空の名前は「使わない」で、警告せずに nullptr
        //! 読めないファイル・無い動きの名前は警告して nullptr。トラックは骨を名前で持ち、どのモデルにも依らない
        [[nodiscard]] const Gfx::AnimClip* LoadAnimClip(std::string_view name);

        //! 手続き生成の組み込み cube / sphere / capsule / wedge45 / wedge30 / wedge22 / wedge15 / shadowQuad を一括登録する
        //! device 確立後・最初の利用前に 1 度だけ呼ぶ。既登録名は上書きしない
        void RegisterBuiltins();
        //! 名前キーで組み込みの形の描く物を引く。未登録は nullptr
        //! RegisterBuiltins を通っていない AssetManager では全部 nullptr になる
        //! 同じ名前の当たりは RegisterBuiltins 無しでも LoadMeshShape が返す
        [[nodiscard]] Gfx::D3DMesh* Builtin(std::string_view name) const noexcept;
        //! 登録前も取得できる組み込みの形の名前。名前の一覧はアプリ寿命で保持する
        [[nodiscard]] static std::span<const std::string_view> BuiltinMeshNames() noexcept;
        //! @brief 欄の名前のマテリアルを返す。名前は共有の material の名前か、ContentRoot からの .mat の道筋
        //! @details 空の名前と読めない名前は共有の player で代え、読めない名前は警告する
        //! 共有の名前はファイルとして読まないので、RegisterSharedMaterials の前は nullptr
        [[nodiscard]] Gfx::D3DMaterial* LoadMaterial(std::string_view name);
        //! LoadMaterial と同じ名前の決まりで引き、読めなければ代えずに nullptr を返す。警告もしない
        [[nodiscard]] Gfx::D3DMaterial* TryLoadMaterial(std::string_view name);
        //! TryLoadMaterial と同じ名前の決まりで、マテリアルそのものを返す。読めなければ nullptr
        [[nodiscard]] Material* TryLoadMaterialAsset(std::string_view name);
        //! name の .mat に書かれた基準色。共有の名前・空・まだ読んでいない名前は (1, 1, 1)
        [[nodiscard]] Vector3 MaterialBaseColor(std::string_view name) const;

        //! 共有 material である player / water / shadow を組み込み shader + texture から一括組み立てする
        //! device + RegisterBuiltins 後・最初の利用前に 1 度呼ぶ。既登録名は上書きしない
        void RegisterSharedMaterials();
        //! 名前キーで共有 material を引く。キーは "player" / "water" / "shadow"。未登録は nullptr
        [[nodiscard]] Gfx::D3DMaterial* SharedMaterial(std::string_view name) const noexcept;
        //! 登録前も取得できる共有の材質の名前。名前の一覧はアプリ寿命で保持する
        [[nodiscard]] static std::span<const std::string_view> SharedMaterialNames() noexcept;

        //! path キーのシェーダーを、同じインスタンスのまま読み直す。成功で true、未キャッシュか失敗で false
        [[nodiscard]] bool Reload(std::string_view path);

        //! 読んだ全部のシェーダーを、同じインスタンスのまま各自の道で読み直す。成功本数を返す。HLSL 編集の即時反映に使う
        std::size_t ReloadAllShaders();

        //! 全キャッシュを解放する。Renderer 破棄より前に呼ぶ
        void Clear() noexcept;

    private:
        // 正規化した path ごとに 1 つを作って共有する。読めない時は代わりの中身になる
        [[nodiscard]] Shader* LoadShader(std::string_view path);
        [[nodiscard]] Texture* LoadTexture(std::string_view path);
        // 正規化した path でメッシュを引き、無ければ glTF を読んで描く形と当たりを一度に作る
        // 読めなかった道も中身の空のメッシュで覚える
        [[nodiscard]] Mesh& LoadMeshFile(std::string_view path);
        // 本当の場所の .mat を読んで覚える。読めなければ nullptr
        [[nodiscard]] Material* LoadMaterialFile(const std::string& path);
        // file の道を baseDir から引いてシェーダーとテクスチャを読み、マテリアルを作る
        [[nodiscard]] std::unique_ptr<Material> CreateMaterial(MaterialFileDesc file);
        [[nodiscard]] Material* FindSharedMaterial(std::string_view name) const noexcept;

        // 本当の場所の skinned glTF を読んで記録する。読めなければ nullptr
        [[nodiscard]] const SkinnedModel* LoadSkinnedModelFile(std::string_view path);
        // アニメーションの glTF を path キーで 1 回だけ読む。読めなかった path も覚え、Clear まで読み直さない
        [[nodiscard]] const Gfx::AnimSource* GetOrLoadAnimationSource(std::string_view path);

        std::string m_baseDir; // 相対 path 解決の基準ディレクトリ
        std::map<std::string, std::unique_ptr<Shader>> m_shaders;
        std::map<std::string, std::unique_ptr<Texture>> m_textures;
        std::map<std::string, std::unique_ptr<Mesh>> m_meshes; // path キーの glTF のメッシュ
        // 組み込みの形の名前キー。当たりは装置の前でも作り、描く形は RegisterBuiltins が足す
        std::map<std::string, std::unique_ptr<Mesh>> m_builtins;
        std::map<std::string, std::unique_ptr<Material>> m_materials;       // path キーの .mat
        std::map<std::string, std::unique_ptr<Material>> m_sharedMaterials; // コードで組む共有のマテリアル
        std::map<std::string, SkinnedModel> m_skinnedModels; // skinned glTF。挿入後に書き換えず、配った参照を安定させる
        std::map<std::string, std::unique_ptr<Gfx::AnimSource>>
            m_animationSources; // アニメの glTF。空の値は読み込み失敗の記録
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
