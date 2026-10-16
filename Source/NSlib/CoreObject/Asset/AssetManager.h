#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/Core/Animation/AnimClip.h"
#include "NSlib/Core/Animation/Skeleton.h"
#include "NSlib/Core/Geometry/GltfParser.h"

#include <functional>
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
    namespace Phys
    {
        struct MeshShape;
    }
    namespace Gfx
    {
        struct BuiltinShaders;
    }
} // namespace NS

namespace NS
{
    class AnimClip;
    class AssetManager;
    class Shader;
    class SkinnedModel;
    class Texture;

    //! アセット相対パスを ContentRoot 配下の絶対パスへ正規化する
    //! ディレクトリトラバーサル (..) で ContentRoot 外へ出る不正なパスは弾き nullopt を返す
    [[nodiscard]] std::optional<std::string> ResolveContentPath(const std::string& relative);

    //! JSON 文字列を MaterialFileDesc へ解析する。成功で true、失敗時は outError に理由を入れる
    //! vs / ps は必須、他は欠落時に既定値。綴りの合わない混ぜ方は Opaque。GPU を使わないので装置無しで試せる
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    //! @brief MaterialFileDesc を .mat の JSON 文字列にする。ParseMaterialJson で読み直すと同じ値になる
    //! @details テクスチャが k_MaterialTextureSlots 枚より多い時と、基本の色が数でない時は nullopt
    //! 鍵の並びと書き方は今の .mat と同じで、値を変えずに書くと同じ文字列になる
    [[nodiscard]] std::optional<std::string> MakeMaterialJson(const MaterialFileDesc& file);

    //! @brief 道の glTF を読んで形にする。外部のバッファも同じ読み方で、本体と同じフォルダから読む
    //! @details 本体か外部のバッファを読めない時と、中身が壊れている時はログを出して空を返す
    [[nodiscard]] MeshGeometry LoadGltfMesh(std::string_view path);
    //! 読み方と失敗の扱いは LoadGltfMesh と同じ。失敗した時は IsValid() が false
    [[nodiscard]] SkinnedMeshData LoadGltfSkinnedMesh(std::string_view path);
    //! 読み方と失敗の扱いは LoadGltfMesh と同じ。失敗した時は IsValid() が false
    [[nodiscard]] AnimSource LoadGltfAnimationSource(std::string_view path);

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

        //! 正規化した path ごとに 1 つを作って共有する。読めない時は代わりの中身になる
        [[nodiscard]] Shader* LoadShader(std::string_view path);
        //! @brief 描く段が使う組み込みのシェーダーを、baseDir の Shaders から LoadShader で引く
        //! @details 欄は全部この AssetManager の Shader の中身を指し、nullptr にならない。何度引いても同じ物を返す
        //! ReloadAllShaders が読み直す。Clear と破棄で捨てるので、その後は渡した先で描かない
        [[nodiscard]] Gfx::BuiltinShaders LoadBuiltinShaders();
        //! 正規化した path ごとに 1 つを作って共有する。読めない時は代わりのピンクの画像になる
        [[nodiscard]] Texture* LoadTexture(std::string_view path);
        //! @brief 欄の名前のメッシュを返す。名前は組み込みの形の名前か、ContentRoot からのファイルの道筋
        //! @details nullptr を返さない。空の名前と読めない名前は、その名前を道に持ち描く形の無いメッシュを返し、
        //! 組み込みの cube で描く。読めない名前は警告する
        //! 組み込みの形の名前はファイルとして読まないので、RegisterBuiltins の前は描く形が無い
        [[nodiscard]] Mesh* LoadMesh(std::string_view name);

        //! @brief 欄の名前のメッシュの当たりを返す。名前の決まりと代わりの cube は LoadMesh と同じ
        //! @details 装置が無くても作れる。同じファイルは描く物と同じ記録から 1 回だけ読む
        [[nodiscard]] const Phys::MeshShape* LoadMeshShape(std::string_view name);

        //! @brief 欄の名前の骨付きのモデルを返す。名前は ContentRoot からの glTF の道筋
        //! @details nullptr を返さない。空の名前と読めない名前は、その名前を持つ中身の空のモデルを返す
        //! 空の名前は「使わない」で警告しない。読めない名前は報告する
        //! 読めなかったファイルは覚えないので、直した後に読み直すと読める
        [[nodiscard]] SkinnedModel* LoadSkinnedModel(std::string_view name);

        //! @brief 欄の名前のクリップを返す。名前は ContentRoot からの glTF の道筋か「道筋#動きの名前」
        //! @details # が無ければファイルの中の先頭の 1 本。nullptr を返さない
        //! 空の名前・読めないファイル・無い動きの名前は、その名前を持つ中身の空のクリップを返す
        //! 空の名前は「使わない」で警告しない。読めない名前は最初の 1 回だけ警告し、Clear まで同じクリップを返す
        [[nodiscard]] AnimClip* LoadAnimClip(std::string_view name);

        //! 手続き生成の組み込み cube / sphere / capsule / wedge45 / wedge30 / wedge22 / wedge15 / shadowQuad を一括登録する
        //! device 確立後・最初の利用前に 1 度だけ呼ぶ。既登録名は上書きしない
        //! 前に LoadMesh や LoadMeshShape が返した組み込みの形にも、同じメッシュのまま描く形を入れる
        void RegisterBuiltins();
        //! 登録前も取得できる組み込みの形の名前。名前の一覧はアプリ寿命で保持する
        [[nodiscard]] static std::span<const std::string_view> BuiltinMeshNames() noexcept;
        //! @brief 欄の名前のマテリアルを返す。名前は共有の material の名前か、ContentRoot からの .mat の道筋
        //! @details nullptr を返さない。空の名前と読めない名前は、その名前を持つ中身の空のマテリアルを返し、
        //! 共有の player で描く。読めない名前は警告する
        //! 共有の名前はファイルとして読まないので、RegisterSharedMaterials の前は中身の空のマテリアルを返す
        //! 読めなかった .mat は覚えないので、直した後に引き直すと読める
        [[nodiscard]] Material* LoadMaterial(std::string_view name);

        //! 共有 material である player / water / shadow を組み込み shader + texture から一括組み立てする
        //! device + RegisterBuiltins 後・最初の利用前に 1 度呼ぶ。既登録名は上書きしない
        //! 前に LoadMaterial が返した中身の空のマテリアルは、これより後は player で描く
        void RegisterSharedMaterials();
        //! 登録前も取得できる共有の材質の名前。名前の一覧はアプリ寿命で保持する
        [[nodiscard]] static std::span<const std::string_view> SharedMaterialNames() noexcept;

        //! @brief material が .mat から読んだマテリアルなら、その値を .mat の JSON 文字列にする
        //! @details 共有のマテリアルと読めなかった名前のマテリアルは書く先が無いので nullopt
        //! 値が書けない時も nullopt (MakeMaterialJson と同じ決まり)
        [[nodiscard]] std::optional<std::string> MakeMaterialFileJson(const Material& material) const;
        //! @brief material の今の値で、シェーダーとテクスチャを引き直して中の D3DMaterial を作り直す
        //! @details 同じマテリアルのまま中身を替えるので、欄が指すポインタはそのまま使える
        //! 作り直せなかった時と中身の空のマテリアルは false を返し、前の D3DMaterial で描き続ける
        [[nodiscard]] bool RebuildMaterial(Material& material);

        //! path キーのシェーダーを、同じインスタンスのまま読み直す。成功で true、未キャッシュか失敗で false
        [[nodiscard]] bool Reload(std::string_view path);

        //! 読んだ全部のシェーダーを、同じインスタンスのまま各自の道で読み直す。成功本数を返す。HLSL 編集の即時反映に使う
        std::size_t ReloadAllShaders();

        //! 全キャッシュを解放する。Renderer 破棄より前に呼ぶ
        void Clear() noexcept;

    private:
        // 正規化した path でメッシュを引き、無ければ glTF を読んで描く形と当たりを一度に作る
        // 読めなかった道も中身の空のメッシュで覚える。name はメッシュが持つ道
        [[nodiscard]] Mesh& LoadMeshFile(std::string_view path, std::string_view name);
        // 組み込みの形のメッシュ。無ければ当たりだけで作り、描く形は RegisterBuiltins が入れる
        [[nodiscard]] Mesh& BuiltinMesh(std::string_view name);
        // ファイルでも組み込みでもない名前 (空・ContentRoot の外) のメッシュ。名前ごとに 1 つ
        [[nodiscard]] Mesh& MissingMesh(std::string_view name);
        // 本当の場所の .mat を読んで覚える。読めなければ nullptr。name はマテリアルが持つ道
        [[nodiscard]] Material* LoadMaterialFile(const std::string& path, std::string_view name);
        // 読めなかった名前のマテリアル。名前ごとに 1 つ
        [[nodiscard]] Material& MissingMaterial(std::string_view name);
        struct MaterialSources
        {
            Shader* vertexShader = nullptr;
            Shader* pixelShader = nullptr;
            std::vector<Texture*> textures; // 並びの番号がテクスチャの枠の番号
        };

        // file の道を baseDir から引いてシェーダーとテクスチャを読み、マテリアルを作る
        [[nodiscard]] std::unique_ptr<Material> CreateMaterial(std::string_view name, MaterialFileDesc file);
        // file の道を baseDir から引いてシェーダーとテクスチャを読む
        [[nodiscard]] MaterialSources LoadMaterialSources(const MaterialFileDesc& file);
        [[nodiscard]] Material* FindSharedMaterial(std::string_view name) const noexcept;

        // 本当の場所の skinned glTF を読んで記録する。読めなければ nullptr。name はモデルとメッシュが持つ道
        [[nodiscard]] SkinnedModel* LoadSkinnedModelFile(std::string_view path, std::string_view name);
        // 読めなかった名前のモデル。名前ごとに 1 つ
        [[nodiscard]] SkinnedModel& MissingSkinnedModel(std::string_view name);
        // name のクリップを作る。読めなければ中身の空のクリップにする
        [[nodiscard]] std::unique_ptr<AnimClip> CreateAnimClip(std::string_view name);
        // アニメーションの glTF を path キーで 1 回だけ読む。読めなかった path も覚え、Clear まで読み直さない
        [[nodiscard]] const AnimSource* GetOrLoadAnimationSource(std::string_view path);

        std::string m_baseDir; // 相対 path 解決の基準ディレクトリ
        std::map<std::string, std::unique_ptr<Shader>> m_shaders;
        std::map<std::string, std::unique_ptr<Texture>> m_textures;
        std::map<std::string, std::unique_ptr<Mesh>> m_meshes; // path キーの glTF のメッシュ
        // 組み込みの形の名前キー。当たりは装置の前でも作り、描く形は RegisterBuiltins が足す
        std::map<std::string, std::unique_ptr<Mesh>> m_builtins;
        std::map<std::string, std::unique_ptr<Mesh>> m_missingMeshes; // 名前キー。ファイルでも組み込みでもない名前
        std::map<std::string, std::unique_ptr<Material>> m_materials;        // path キーの .mat
        std::map<std::string, std::unique_ptr<Material>> m_sharedMaterials;  // コードで組む共有のマテリアル
        std::map<std::string, std::unique_ptr<Material>> m_missingMaterials; // 名前キー。読めなかった名前
        std::map<std::string, std::unique_ptr<SkinnedModel>> m_skinnedModels; // path キーの skinned glTF
        std::map<std::string, std::unique_ptr<SkinnedModel>> m_missingSkinnedModels; // 名前キー。読めなかった名前
        std::map<std::string, std::unique_ptr<AnimSource>>
            m_animationSources; // アニメの glTF。空の値は読み込み失敗の記録
        // 欄の字面キー。読めなかった名前の中身の空のクリップも覚える
        std::map<std::string, std::unique_ptr<AnimClip>, std::less<>> m_animClips;
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
