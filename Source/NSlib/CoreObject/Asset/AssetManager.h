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
    //! params の壊れた値は名前を WARN に出して 1 つずつ捨て、ほかの値は読む
    [[nodiscard]] bool ParseMaterialJson(std::string_view jsonText, MaterialFileDesc& out, std::string& outError);

    //! @brief MaterialFileDesc を .mat の JSON 文字列にする。ParseMaterialJson で読み直すと同じ値になる
    //! @details テクスチャが k_MaterialTextureSlots 枚より多い時、基本の色が数でない時、
    //! ParseMaterialJson が読めない材質の値がある時は nullopt
    //! 鍵の並びと書き方は今の .mat と同じで、値を変えずに書くと同じ文字列になる。params は空なら書かない
    [[nodiscard]] std::optional<std::string> MakeMaterialJson(const MaterialFileDesc& file);

    //! @brief 道の glTF を読んで形にする。外部のバッファも同じ読み方で、本体と同じフォルダから読む
    //! @details 本体か外部のバッファを読めない時と、中身が壊れている時はログを出して空を返す
    [[nodiscard]] MeshGeometry LoadGltfMesh(std::string_view path);
    //! 読み方と失敗の扱いは LoadGltfMesh と同じ。失敗した時は IsValid() が false
    [[nodiscard]] SkinnedMeshData LoadGltfSkinnedMesh(std::string_view path);
    //! 読み方と失敗の扱いは LoadGltfMesh と同じ。失敗した時は IsValid() が false
    [[nodiscard]] AnimSource LoadGltfAnimationSource(std::string_view path);

    //! @brief アプリ寿命で素材を重複なく持つ。素材の種類ごとに表を 1 本持つ
    //! @details メッシュ・マテリアル・骨付きのモデル・動きのクリップは、欄の名前を正規化した鍵で持つ。
    //! 読めなかった名前も中身の空の姿で覚え、Clear まで読み直さないので、読めない時のログは名前ごとに 1 回出る
    class AssetManager : public NonCopyable
    {
    public:
        //! baseDir は shader / texture の相対 path を解決する基準で、通常は ContentRoot
        explicit AssetManager(std::string baseDir) noexcept;
        ~AssetManager();

        //! @brief 素材を読む AssetManager を返す
        //! @details AssetManagerScope が差した物を返す。差していない間は、アプリ寿命の別の AssetManager を返す
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
        //! @details nullptr を返さない。空の名前と読めない名前は、描く形も当たりも無い中身の空のメッシュを返し、
        //! 何も描かない。読めない名前は ERROR を出す。空の名前は「選んでいない」で何も出さない
        //! 組み込みの形もファイルも、作る時に描く形と当たりを作る。D3DDevice の無い場では描く形を作らない
        //! マテリアルの欄が空の時に描くマテリアルも作る時に決める (Mesh::UseUnsetMaterial)。
        //! 組み込みの形と空のメッシュは灰色の既定 (default.mat)、ファイルのモデルはピンク (missing.mat) で ERROR を出す
        [[nodiscard]] Mesh* LoadMesh(std::string_view name);

        //! @brief 欄の名前のメッシュの当たりを返す。名前の決まりは LoadMesh と同じ
        //! @details 装置が無くても作れる。空の名前と読めない名前は nullptr で、当たりを作らない
        [[nodiscard]] const Phys::MeshShape* LoadMeshShape(std::string_view name);

        //! @brief 欄の名前の骨付きのモデルを返す。名前は ContentRoot からの glTF の道筋
        //! @details nullptr を返さない。空の名前と読めない名前は、その名前を持つ中身の空のモデルを返す
        //! 空の名前は「使わない」で何も出さない。読めない名前は ERROR を出す
        [[nodiscard]] SkinnedModel* LoadSkinnedModel(std::string_view name);

        //! @brief 欄の名前のクリップを返す。名前は ContentRoot からの glTF の道筋か「道筋#動きの名前」
        //! @details # が無ければファイルの中の先頭の 1 本。nullptr を返さない
        //! 空の名前・読めないファイル・無い動きの名前は、その名前を持つ中身の空のクリップを返す
        //! 空の名前は「使わない」で警告しない。読めない名前は最初の 1 回だけ警告し、Clear まで同じクリップを返す
        [[nodiscard]] AnimClip* LoadAnimClip(std::string_view name);

        //! コードで作る組み込みの形 cube / sphere / capsule / wedge45 / wedge30 / wedge22 / wedge15 / shadowQuad の名前
        [[nodiscard]] static std::span<const std::string_view> BuiltinMeshNames() noexcept;
        //! @brief 欄の名前のマテリアルを返す。名前は ContentRoot からの .mat の道筋
        //! @details nullptr を返さない。読めない名前は中身の空のマテリアルを返してピンク (missing.mat) で描き、ERROR を出す
        //! 空の名前は「選んでいない」で、中身の空のマテリアルを返す。何で描くかはメッシュが決める (Mesh::UseUnsetMaterial)
        [[nodiscard]] Material* LoadMaterial(std::string_view name);

        //! @brief material が .mat から読んだマテリアルなら、その値を .mat の JSON 文字列にする
        //! @details 読めなかった名前と空の名前のマテリアルは書く先が無いので nullopt
        //! 値が書けない時も nullopt (MakeMaterialJson と同じ決まり)
        [[nodiscard]] std::optional<std::string> MakeMaterialFileJson(const Material& material) const;
        //! @brief material の今の値で、シェーダーとテクスチャを引き直して中の D3DMaterial を作り直す
        //! @details 同じマテリアルのまま中身を替えるので、欄が指すポインタはそのまま使える
        //! 作り直せなかった時と中身の空のマテリアルは false を返し、前の D3DMaterial で描き続ける
        [[nodiscard]] bool RebuildMaterial(Material& material);

        //! path キーのシェーダーを、同じインスタンスのまま読み直す。成功で true、未キャッシュか失敗で false
        //! 読み直せたら、材質の値の並びを取り直すために全部のマテリアルを作り直す
        [[nodiscard]] bool Reload(std::string_view path);

        //! 読んだ全部のシェーダーを、同じインスタンスのまま各自の道で読み直す。成功本数を返す。HLSL 編集の即時反映に使う
        //! 読み直した後、材質の値の並びを取り直すために全部のマテリアルを作り直す
        std::size_t ReloadAllShaders();

        //! 全キャッシュを解放する。D3DDevice 破棄より前に呼ぶ
        void Clear() noexcept;

    private:
        // 表に無い key のメッシュを作る。組み込みの表か glTF から頂点を持ってきて、描く形と当たりを一度に作る
        [[nodiscard]] std::unique_ptr<Mesh> CreateMesh(const std::string& key);
        // 表に無い key のマテリアルを .mat から作る。読めなければピンクで描く中身の空のマテリアル
        [[nodiscard]] std::unique_ptr<Material> ReadMaterial(const std::string& key);
        // 読めなかった key のマテリアル。missing.mat 自身が読めない時は代わりも無い
        [[nodiscard]] std::unique_ptr<Material> MakeUnreadableMaterial(const std::string& key);
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
        // 読み直したシェーダーは MaterialParams の並びが変わり得るので、全部のマテリアルの b2 と値を組み直す
        void RebuildAllMaterials();

        // 表に無い key の骨付きのモデルを skinned glTF から作る。読めなければ中身の空のモデル
        [[nodiscard]] std::unique_ptr<SkinnedModel> CreateSkinnedModel(const std::string& key);
        // name のクリップを作る。読めなければ中身の空のクリップにする
        [[nodiscard]] std::unique_ptr<AnimClip> CreateAnimClip(std::string_view name);
        // アニメーションの glTF を path キーで 1 回だけ読む。読めなかった path も覚え、Clear まで読み直さない
        [[nodiscard]] const AnimSource* GetOrLoadAnimationSource(std::string_view path);

        std::string m_baseDir; // 相対 path 解決の基準ディレクトリ
        std::map<std::string, std::unique_ptr<Shader>> m_shaders;
        std::map<std::string, std::unique_ptr<Texture>> m_textures;
        std::map<std::string, std::unique_ptr<Mesh>> m_meshes;
        std::map<std::string, std::unique_ptr<Material>> m_materials;
        std::map<std::string, std::unique_ptr<SkinnedModel>> m_skinnedModels;
        std::map<std::string, std::unique_ptr<AnimSource>>  m_animationSources; // アニメの glTF。空の値は読み込み失敗の記録
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
