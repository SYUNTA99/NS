#pragma once

#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/Gfx/Render/DrawItem.h"

#include <vector>

namespace NS
{
    class Model;
} // namespace NS

namespace GL::Level
{
        //! @brief 体当たりの残像。止めの間の左右の写しと、明けに当たった所へ残す写しを描く
        //! @details 同じアクターの Model のメッシュ・マテリアル・姿勢を借り、本体のマテリアルを半透明で重ねる。
        //! Model は覚えず、描く物の管理クラスが登録の組から渡す。持ち主は InitDraw で自分の Model と一緒に入れる。
        //! 値は描く時だけの物で保存しない。書くのは TackleGhostDesc::WriteTo と、残像を止める持ち主
        class TackleGhost final : public NS::IRenderable
        {
        public:
            //! @brief 根から ±spread m の位置に左右の写しを描く。ゼロなら描かない
            //! @details 補間せず、本体の描く時だけのずれは写しに足さない
            //! @return 全成分が有限の場合 true、それ以外の場合は変えずに false
            [[nodiscard]] bool SetSpread(const NS::Vector3& spread) noexcept;
            //! 左右の写しの離れ。書かれていなければ (0, 0, 0)
            [[nodiscard]] const NS::Vector3& Spread() const noexcept { return m_spread; }
            //! @brief 左右の写しの不透明度を書く
            //! @param[in] opacity 0..1 に丸める
            //! @return 有限で書いた場合 true、それ以外の場合は false
            [[nodiscard]] bool SetSideOpacity(float opacity) noexcept;
            //! 左右の写しの不透明度。書かれていなければ 0
            [[nodiscard]] float SideOpacity() const noexcept { return m_sideOpacity; }
            //! @brief 左右の写し 1 つの世界行列を返す
            //! @details 本体 model の描く行列の、描く時だけのずれを side × 離れ に置き換えた行列
            //! @param[in] root 持ち主の根の世界行列。alpha で補間した物を渡す
            //! @param[in] alpha 前の固定フレームから今の固定フレームまでの補間の割合 0..1
            //! @param[in] side 離れに掛ける向き。+1 か −1
            [[nodiscard]] NS::Matrix SideWorldMatrix(const NS::Model* model,
                                                     const NS::Matrix& root,
                                                     float alpha,
                                                     float side) const noexcept;

            //! @brief 本体 model の今の描く世界行列を、明けの写しの姿として控える
            //! @details 控えた姿は根が動いても動かない。写しを描くのは不透明度が正の間だけ
            //! @param[in] root 持ち主の根の今の固定フレームの世界行列
            void CaptureAfterimage(const NS::Model* model, const NS::Matrix& root) noexcept;
            //! @brief 明けの写しの不透明度を書く
            //! @param[in] opacity 0..1 に丸める。0 で写しを描かない
            //! @return 有限で書いた場合 true、それ以外の場合は false
            [[nodiscard]] bool SetAfterimageOpacity(float opacity) noexcept;
            //! 明けの写しの不透明度。書かれていなければ 0
            [[nodiscard]] float AfterimageOpacity() const noexcept { return m_afterimageOpacity; }
            //! 控えた明けの写しの世界行列。控えていなければ単位行列
            [[nodiscard]] const NS::Matrix& AfterimageWorldMatrix() const noexcept { return m_afterimageWorld; }

            //! 写しの DrawItem を out に積む。本体の Model (source.model) が描けない時は何も積まない
            void Collect(const NS::Gfx::RenderContext& context,
                         const NS::RenderSource& source,
                         std::vector<NS::Gfx::DrawItem>& out) override;
            //! 本体の箱を、描く時だけのずれと離れの分だけ広げ、明けの写しを描く間は控えた姿の箱も包む
            [[nodiscard]] NS::AABB WorldBounds(const NS::RenderSource& source) const noexcept override;
            //! 不透明の並びで描くと、後に描く空と物が写しを上書きする
            [[nodiscard]] NS::RenderBucket Bucket() const noexcept override { return NS::RenderBucket::Transparent; }
            //! 本体と同じ中心
            [[nodiscard]] NS::Vector3 SortCenter(const NS::RenderSource& source) const noexcept override;
            //! @brief 中心が同じ半透明の物 (同じアクターの影) より先に描く
            //! @details 影は写しの上に重ねて描く。中心が同じ物の順を登録の順に任せると、出し入れのたびに入れ替わる
            [[nodiscard]] int SortPriority() const noexcept override { return -1; }

        private:
            NS::Vector3 m_spread{0.0f, 0.0f, 0.0f};
            float m_sideOpacity = 0.0f;
            NS::Matrix m_afterimageWorld = NS::Matrix::Identity;
            float m_afterimageOpacity = 0.0f;
        };
} // namespace GL::Level
