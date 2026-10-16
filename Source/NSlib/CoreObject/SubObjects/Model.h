#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/SubObject.h"
#include "NSlib/Graphics/DrawItem.h"

#include <string>
#include <utility>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class Mesh;
    }
    namespace Gfx
    {
        class Material;
    }
    namespace Gfx
    {
        class Buffer;
    }
} // namespace NS

namespace NS
{

    // 震えの定数の 16 バイト整列を値で抱えるため、想定どおりのパディングが入る。C4324 を黙らせる
#pragma warning(push)
#pragma warning(disable : 4324)
    //! @brief メッシュとマテリアルを描画する部品
    //! @details 描画用の姿勢変更は根の Transform と衝突形状に影響せず、保存されない
    class Model : public SubObject, public IRenderable
    {
    public:
        //! Mesh / Material は非所有の生ポインタ。空で作り、後から SetMesh / SetMaterial で入れる
        //! 寿命は AssetManager 等の所有側が保証する
        Model() noexcept = default;

        //! Material を共有したまま配置物ごとに変える個体色。lighting とは別系統
        void SetBaseColor(const Vector3& color) noexcept { m_baseColor = color; }

        //! nullptr で描画しない。マテリアルは非所有で、ブレンド設定を即反映する
        void SetMaterial(Gfx::Material* material) noexcept { m_material = material; }
        //! 現在の Material で非所有。未設定なら nullptr
        [[nodiscard]] Gfx::Material* GetMaterial() const noexcept { return m_material; }

        //! nullptr で描画しない。メッシュは非所有
        void SetMesh(Gfx::Mesh* mesh) noexcept { m_mesh = mesh; }
        //! build 時に解決された実体 Mesh を返す。未解決なら nullptr
        [[nodiscard]] const Gfx::Mesh* GetMesh() const noexcept { return m_mesh; }

        //! 描くメッシュの参照。builtin 名または ContentRoot 配下の相対パス。空 / 解決不可なら構築側が既定にする
        [[nodiscard]] const std::string& MeshRef() const noexcept { return m_meshRef; }
        void SetMeshRef(std::string ref) noexcept { m_meshRef = std::move(ref); }

        //! 描画 material の参照。player / water / shadow といった共有 material 名または .mat 相対パス
        //! 空・解決不可は ResolveAssets が既定 material にする
        [[nodiscard]] const std::string& MaterialRef() const noexcept { return m_materialRef; }
        void SetMaterialRef(std::string ref) noexcept { m_materialRef = std::move(ref); }

        //! 個体ごとの頂点シェーダーの定数を設定する。同じ SceneActor の Animation がプレイの始まりに設定する
        //! cpuData があれば、描画側が毎回バッファーへ転送してから設定する
        void SetPerObjectVsConstant(const Gfx::Buffer* cb,
                                    const void* cpuData,
                                    std::size_t cpuDataSize,
                                    uint slot) noexcept;

        //! skinned など mesh 固定の LocalBounds では現在ポーズを包めない時に、同じ object の SubObject が毎フレーム
        //! 現在ポーズの局所境界を差す。差された間は WorldBounds がこれを世界変換して使う
        void SetLocalBoundsOverride(const AABB& localBounds) noexcept
        {
            m_localBoundsOverride = localBounds;
            m_hasLocalBoundsOverride = true;
        }

        //! 根の行列の前に掛ける描画用の局所回転。固定更新間を補間する
        void SetLocalRotation(const Quaternion& rotation) noexcept { m_localRotation = rotation; }
        //! 補間せずに局所回転を切り替える
        void SnapLocalRotation(const Quaternion& rotation) noexcept
        {
            m_localRotation = rotation;
            m_previousLocalRotation = rotation;
        }
        //! 今のフレームの局所の回転を返す。書かれていなければ単位回転
        [[nodiscard]] const Quaternion& LocalRotation() const noexcept { return m_localRotation; }

        //! @brief 形の下端中央を中心に、世界軸の描画倍率を設定する。固定更新間を補間する
        //! @details 境界がなければ根を中心にする。補間しない変更は SnapDrawScale を使う
        //! @return 全成分が有限の正値の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetDrawScale(const Vector3& scale) noexcept;
        //! 補間せずに描画倍率を切り替える。受け付ける値は SetDrawScale と同じ
        [[nodiscard]] bool SnapDrawScale(const Vector3& scale) noexcept;
        //! 今のフレームの描く時だけの倍率を返す。書かれていなければ (1, 1, 1)
        [[nodiscard]] const Vector3& DrawScale() const noexcept { return m_drawScale; }

        //! @brief 世界座標のずれ m を描画姿勢へ足す。補間しない
        //! @return 全成分が有限の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetDrawOffset(const Vector3& offset) noexcept;
        //! 描く時だけのずれを返す。書かれていなければ (0, 0, 0)
        [[nodiscard]] const Vector3& DrawOffset() const noexcept { return m_drawOffset; }

        //! @brief 頂点シェーダーへ渡す震えを設定する。補間しない
        //! @return 全項目が有限の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetTremor(const Gfx::TremorCB& tremor) noexcept;
        //! 描く時だけの震えを返す。書かれていなければ振れ幅 0
        [[nodiscard]] const Gfx::TremorCB& Tremor() const noexcept { return m_tremor; }

        //! @brief 根から ±spread m の位置に残像を描く。ゼロなら描かない
        //! @details 補間せず、描画用のずれは残像に加えない
        //! @return 全成分が有限の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetGhostSpread(const Vector3& spread) noexcept;
        //! 描く時だけの残像の離れを返す。書かれていなければ (0, 0, 0)
        [[nodiscard]] const Vector3& GhostSpread() const noexcept { return m_ghostSpread; }
        //! @brief 残像 1 つの世界行列を返す
        //! @details DrawWorldMatrix の描く時だけのずれを、side × 離れ に置き換えた行列
        //! @param[in] alpha 前の固定フレームから今の固定フレームまでの補間の割合 0..1
        //! @param[in] side 離れに掛ける向き。+1 か −1
        [[nodiscard]] Matrix GhostWorldMatrix(float alpha, float side) const noexcept;

        //! @brief 今の描く世界行列 DrawWorldMatrix(1) を、明けの写しの姿として控える
        //! @details 控えた姿は根が動いても動かない。写しを描くのは不透明度が正の間だけ
        void CaptureAfterimage() noexcept;
        //! @brief 明けの写しの不透明度を書く
        //! @param[in] opacity 0..1。0 で写しを描かない
        //! @return 有限で書いた場合 true、それ以外の場合は false
        [[nodiscard]] bool SetAfterimageOpacity(float opacity) noexcept;
        //! 明けの写しの不透明度を返す。書かれていなければ 0
        [[nodiscard]] float AfterimageOpacity() const noexcept { return m_afterimageOpacity; }
        //! 控えた明けの写しの世界行列を返す。控えていなければ単位行列
        [[nodiscard]] const Matrix& AfterimageWorldMatrix() const noexcept { return m_afterimageWorld; }

        //! @brief 局所回転と描画倍率を補間した描画姿勢を返す。世界座標のずれは補間後に足す
        //! @param[in] alpha 固定更新間の補間割合 0〜1
        [[nodiscard]] Matrix DrawWorldMatrix(float alpha) const noexcept;

        //! DrawWorldMatrix(context.alpha) を詰めた DrawItem を out に積む。持ち主が出ていなければ何も積まない
        void Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out) override;

        //! Material の BlendMode から bucket を返し、Opaque 以外は Transparent。Material 不在は Opaque
        [[nodiscard]] RenderBucket Bucket() const noexcept override;
        //! Owner の世界行列の平行移動成分で半透明ソート用の中心
        [[nodiscard]] Vector3 SortCenter() const noexcept override;
        //! Material の renderPriority で距離同値時のタイブレークに使う
        [[nodiscard]] int SortPriority() const noexcept override;

        //! @brief 上書き境界を優先し、メッシュの境界を世界座標で包む
        //! @details 描画倍率は前後の大きい方で包み、描画用のずれを加える。メッシュか持ち主がなければ原点の半幅 1
        [[nodiscard]] AABB WorldBounds() const noexcept override;

        //! OwningScene に自身と残像を IRenderable として登録する。持ち主かシーンがなければ何もしない
        void Register();
        //! Owner の OwningScene から self と残像を解除する。無効ポインタを残さないよう Scene 破棄前に呼ぶ
        void Unregister();
        //! 固定更新の先頭で補間用の姿勢を控える。時間停止中も前後の値を揃える
        void Snapshot() noexcept;

        //! meshRef / matRef の参照文字列から実体の Mesh / Material を引き当てる
        //! 共有 material 名を先に引き、外れたら .mat 相対パスとして読む。解決不可は cube と既定 material にする
        //! 残像には共有の water の material を当てる
        void ResolveAssets(AssetManager& assets) override;

        NS_REFLECT_BEGIN(Model, SubObject)
        NS_REFLECT_FIELD(m_baseColor, "基本色")
        NS_REFLECT_FIELD(m_meshRef, "メッシュ")
        NS_REFLECT_FIELD(m_materialRef, "マテリアル")
        NS_REFLECT_END()

    private:
        // 残像の写しを半透明の並びで描く。不透明の並びで描くと、後に描く空と物が写しを上書きする
        class Ghosts final : public IRenderable
        {
        public:
            explicit Ghosts(Model& model) noexcept : m_model(model) {}
            void Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out) override;
            [[nodiscard]] AABB WorldBounds() const noexcept override;
            [[nodiscard]] RenderBucket Bucket() const noexcept override { return RenderBucket::Transparent; }
            [[nodiscard]] Vector3 SortCenter() const noexcept override { return m_model.SortCenter(); }

        private:
            Model& m_model;
        };

        [[nodiscard]] Matrix DrawWorldMatrixWithOffset(float alpha, const Vector3& offset) const noexcept;
        [[nodiscard]] Gfx::DrawItem MakeDrawItem(const Gfx::RenderContext& context,
                                                     Gfx::Material* material,
                                                     const Matrix& world) const noexcept;

        // 描く形の局所の境界。差された境界を先に、無ければ mesh の境界。どちらも無ければ nullptr
        [[nodiscard]] const AABB* DrawnLocalBounds() const noexcept;

        Gfx::Mesh* m_mesh = nullptr;           // 描画する Mesh (非所有)
        Gfx::Material* m_material = nullptr;   // 描画に使う Material (非所有)
        Vector3 m_baseColor{1.0f, 1.0f, 1.0f}; // 個体色、lighting と別系統
        // 保存・編集される参照文字列。build 時に解決して m_mesh / m_material へ実体を当てる
        std::string m_meshRef{};
        std::string m_materialRef{};

        // 追加 VS 定数。cpuData があれば描画ごとに転送する
        const Gfx::Buffer* m_perObjectVsCb = nullptr;
        const void* m_perObjectVsData = nullptr;
        std::size_t m_perObjectVsSize = 0;
        uint m_perObjectVsSlot = 1;

        // Animation が現在ポーズから与える局所境界
        AABB m_localBoundsOverride{};
        bool m_hasLocalBoundsOverride = false;

        // 描く時だけの局所の回転。同居する SubObject が毎フレーム書き直すので保存しない
        Quaternion m_localRotation = Quaternion::Identity;
        Quaternion m_previousLocalRotation = Quaternion::Identity; // 前のフレームの値。補間の始点

        // 描く時だけの世界の軸の倍率。他の SubObject が書き直すので保存しない
        Vector3 m_drawScale{1.0f, 1.0f, 1.0f};
        Vector3 m_previousDrawScale{1.0f, 1.0f, 1.0f}; // 前のフレームの値。補間の始点

        // 描く時だけの世界のずれ。他の SubObject が書き直すので保存しない。補間しない
        Vector3 m_drawOffset{0.0f, 0.0f, 0.0f};
        // 描く時だけの震え。他の SubObject が書き直すので保存しない。補間しない
        Gfx::TremorCB m_tremor{};
        // 描く時だけの残像の離れ。他の SubObject が書き直すので保存しない。補間しない
        Vector3 m_ghostSpread{0.0f, 0.0f, 0.0f};
        // 明けの写しの控えた姿と不透明度。他の SubObject が書き直すので保存しない
        Matrix m_afterimageWorld = Matrix::Identity;
        float m_afterimageOpacity = 0.0f;
        Gfx::Material* m_ghostMaterial = nullptr; // 残像の共有 water material (非所有)
        Ghosts m_ghosts{*this};                       // 残像を描く物。Scene へは self と並べて登録する
    };
#pragma warning(pop)
} // namespace NS
