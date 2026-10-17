#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/IUse/IUseDrawAdjust.h"
#include "NSlib/CoreObject/Reflection/AssetField.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/Gfx/DrawItem.h"

#include <string>
#include <string_view>
#include <vector>

namespace NS
{
    namespace Gfx
    {
        class D3DBuffer;
    }
} // namespace NS

namespace NS
{

    //! @brief メッシュとマテリアルを描画する部品
    //! @details 描画用の姿勢変更は根の Transform と衝突形状に影響せず、保存されない
    class Model : public Object, public IRenderable
    {
    public:
        //! @brief メッシュとマテリアルの欄は、作る時に既定の名前の素材を AssetManager::Get() から引く
        //! @param[in] drawAdjust 本体を描く時の調整を返す持ち主。nullptr なら調整なしで描く
        explicit Model(SceneActor* owner, const IUseDrawAdjust* drawAdjust = nullptr);

        //! マテリアルを共有したまま配置物ごとに変える個体色。lighting とは別系統
        void SetBaseColor(const Color& color) noexcept { m_baseColor = color; }

        //! @brief 欄を替えずに、描くメッシュを替える。保存する名前は欄のメッシュのまま
        //! @details nullptr で欄のメッシュに戻す。SceneActor::ReloadAssets も欄のメッシュに戻す
        void SetMesh(Mesh* mesh) noexcept { m_shownMesh = mesh; }
        //! 描くメッシュ。SetMesh で替えていればその物、無ければ欄のメッシュ
        [[nodiscard]] const Mesh* GetMesh() const noexcept;

        //! 描くメッシュの名前。組み込みの形の名前か、Content の下のパス。空・読めない時は何も描かない
        [[nodiscard]] const std::string& MeshRef() const noexcept { return AssetFieldName(m_mesh); }
        //! 名前のメッシュをその場で AssetManager::Get() から引き、欄に入れる
        void SetMeshRef(std::string_view ref);

        //! 欄のマテリアルの名前。.mat のパス。読めない時はピンクで描き、空の時は DrawnMaterial を描く
        [[nodiscard]] const std::string& MaterialRef() const noexcept { return AssetFieldName(m_material); }
        //! 名前のマテリアルをその場で AssetManager::Get() から引き、欄に入れる
        void SetMaterialRef(std::string_view ref);

        //! @brief 描くマテリアル。欄が空でなければ欄の物、空なら描くメッシュで選んだ物 (AssetManager::LoadUnsetMaterial)
        //! @details 選び直すのは ChooseDrawnMaterial を呼んだ時だけ。保存はしない
        [[nodiscard]] const Material* DrawnMaterial() const noexcept { return m_drawnMaterial; }
        //! @brief 今の欄と描くメッシュで、描くマテリアルを選び直す
        //! @details 作る時・名前の欄を書いた時・SceneActor::ReloadAssets の最後に呼ぶ。マテリアルの無いファイルのモデルは ERROR を出す
        void ChooseDrawnMaterial();

        //! 個体ごとの頂点シェーダーの定数を設定する。同じ SceneActor の Animation がプレイの始まりに設定する
        //! cpuData があれば、描画側が毎回バッファーへ転送してから設定する
        void SetPerObjectVsConstant(const Gfx::D3DBuffer* cb,
                                    const void* cpuData,
                                    std::size_t cpuDataSize,
                                    uint slot) noexcept;

        //! skinned など mesh 固定の LocalBounds では現在ポーズを包めない時に、同じ object の部品が毎フレーム
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

        //! @brief 形の原点を中心に、局所の回転より先に掛ける倍率を設定する。補間しない
        //! @details 差したメッシュの大きさを合わせる倍率で、根の倍率と描く倍率とは別に掛かる
        //! 描く倍率の中心と描く形の箱は、この倍率を掛けた形から測る
        //! @return 全成分が有限の正値の場合 true、それ以外の場合は変更せず false
        [[nodiscard]] bool SetLocalScale(const Vector3& scale) noexcept;
        //! 局所の倍率を返す。書かれていなければ (1, 1, 1)
        [[nodiscard]] const Vector3& LocalScale() const noexcept { return m_localScale; }

        //! @brief 本体を描く時の調整を返す
        //! @details 持ち主が返す今の値。持ち主が調整を返さなければ、ずれ 0・倍率 1・震えなし
        [[nodiscard]] DrawAdjust DrawAdjustment() const noexcept;
        //! @brief 描く倍率を補間せずに、持ち主の今の倍率へ切り替える
        //! @details 固定更新の途中で倍率を飛ばす持ち主が、倍率を書いた後に呼ぶ
        void SnapDrawScale() noexcept;

        //! @brief 局所回転と描画倍率を補間した描画姿勢を返す。世界座標のずれは補間後に足す
        //! @details 描画倍率は形の下端中央を中心に世界の軸で掛ける。境界がなければ根を中心にする
        //! @param[in] alpha 固定更新間の補間割合 0〜1
        [[nodiscard]] Matrix DrawWorldMatrix(float alpha) const noexcept;
        //! DrawWorldMatrix の描く時だけのずれを offset に置き換えた描画姿勢を返す
        [[nodiscard]] Matrix DrawWorldMatrixWithOffset(float alpha, const Vector3& offset) const noexcept;

        //! 持ち主が出ていて、描く形と欄のマテリアルの描く D3DMaterial が揃っている場合 true
        [[nodiscard]] bool CanDraw() const noexcept;
        //! @brief 欄のマテリアルと個体色で、world に描く DrawItem を作る
        //! @details 混ぜ方はマテリアルの物。CanDraw が true の時だけ呼ぶ
        [[nodiscard]] Gfx::DrawItem MakeDrawItem(const Gfx::RenderContext& context, const Matrix& world) const noexcept;
        //! 描く形の局所の境界。差された境界を先に、無ければメッシュの境界。どちらも無ければ nullptr
        [[nodiscard]] const AABB* DrawnLocalBounds() const noexcept;

        //! DrawWorldMatrix(context.alpha) を詰めた DrawItem を out に積む。持ち主が出ていなければ何も積まない
        void Collect(const Gfx::RenderContext& context, std::vector<Gfx::DrawItem>& out) override;

        //! D3DMaterial の BlendMode から bucket を返し、Opaque 以外は Transparent。D3DMaterial 不在は Opaque
        [[nodiscard]] RenderBucket Bucket() const noexcept override;
        //! Owner の世界行列の平行移動成分で半透明ソート用の中心
        [[nodiscard]] Vector3 SortCenter() const noexcept override;
        //! D3DMaterial の renderPriority で距離同値時のタイブレークに使う
        [[nodiscard]] int SortPriority() const noexcept override;

        //! @brief 上書き境界を優先し、メッシュの境界を世界座標で包む
        //! @details 描画倍率は前後の大きい方で包み、描画用のずれを加える。メッシュか持ち主がなければ原点の半幅 1
        [[nodiscard]] AABB WorldBounds() const noexcept override;

        //! OwningScene に自身を IRenderable として登録する。持ち主かシーンがなければ何もしない
        void Register();
        //! Owner の OwningScene から self を解除する。無効ポインタを残さないよう Scene 破棄前に呼ぶ
        void Unregister();
        //! 固定更新の先頭で補間用の姿勢を控える。時間停止中も前後の値を揃える
        void Snapshot() noexcept;

        NS_REFLECT_BEGIN(Model, Object)
        NS_FIELD(m_baseColor, "基本色")
        NS_FIELD(m_mesh, "メッシュ")
        NS_FIELD(m_material, "マテリアル")
        NS_REFLECT_END()

    private:
        // 持ち主が出ていて、描く形と material の描く D3DMaterial が揃っている場合 true
        [[nodiscard]] bool CanDraw(const Material* material) const noexcept;
        // CanDraw が true の時だけ呼ぶ
        [[nodiscard]] Gfx::DrawItem MakeDrawItem(const Gfx::RenderContext& context,
                                                     const Material* material,
                                                     const Matrix& world) const noexcept;

        // 素材は AssetManager が持つ。欄は nullptr にならない
        Mesh* m_mesh = nullptr;              // 描くメッシュ
        Material* m_material = nullptr;
        Mesh* m_shownMesh = nullptr;         // 欄を替えずに描くメッシュ。保存しない
        Material* m_drawnMaterial = nullptr; // ChooseDrawnMaterial が選ぶ。保存しない
        Color m_baseColor{1.0f, 1.0f, 1.0f};              // 個体色、lighting と別系統。透明度は使わず rgb だけを描く

        // 追加 VS 定数。cpuData があれば描画ごとに転送する
        const Gfx::D3DBuffer* m_perObjectVsCb = nullptr;
        const void* m_perObjectVsData = nullptr;
        std::size_t m_perObjectVsSize = 0;
        uint m_perObjectVsSlot = 1;

        // Animation が現在ポーズから与える局所境界
        AABB m_localBoundsOverride{};
        bool m_hasLocalBoundsOverride = false;

        // 描く時だけの局所の回転。同居する部品が毎フレーム書き直すので保存しない
        Quaternion m_localRotation = Quaternion::Identity;
        Quaternion m_previousLocalRotation = Quaternion::Identity; // 前のフレームの値。補間の始点

        // 差したメッシュに合わせる局所の倍率。メッシュを差す部品が書くので保存しない
        Vector3 m_localScale{1.0f, 1.0f, 1.0f};

        // 補間の始点。固定更新の頭と SnapDrawScale で持ち主の倍率を控える
        Vector3 m_previousDrawScale{1.0f, 1.0f, 1.0f};

        SceneActor* m_owner = nullptr;                  // 持ち主 (非所有)
        const IUseDrawAdjust* m_drawAdjust = nullptr; // 本体を描く時の調整を返す持ち主 (非所有)。無ければ調整なし
    };
} // namespace NS
