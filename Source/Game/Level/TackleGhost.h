#pragma once

#include "Game/Level/LevelMessages.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/IRenderable.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <vector>

namespace GL
{
    namespace Level
    {
        //! 体当たりの残像。親の Model を、左右の写しと明けの写しとして半透明で描く子アクター
        //! 親が OnInit の中で CreateChildActor で作る。自分の Model は持たず、親の Model を描く
        class TackleGhost final : public NS::SceneActor, public NS::IRenderable
        {
        public:
            NS_REFLECT_BEGIN(TackleGhost, NS::SceneActor)
            NS_FIELD(m_sortPriority, "描く順")
            NS_REFLECT_END()

            //! 長さが0以下かカメラが無ければ始めない。次の VisualStep を1フレーム目に数える
            void Start(const NS::HitOwnerRun& run);
            void Stop() noexcept;
            //! 不透明度の倍率は次の VisualStep から効く
            void ScaleOpacity(float scale) noexcept;

            //! 根から ±spread m の位置に左右の写しを描く。ゼロなら描かない
            //! 補間せず、本体の描く時だけのずれは写しに足さない
            //! @return 全成分が有限の場合 true、それ以外の場合は変えずに false
            [[nodiscard]] bool SetSpread(const NS::Vector3& spread) noexcept;
            //! 左右の写しの離れ。書かれていなければ (0, 0, 0)
            [[nodiscard]] const NS::Vector3& Spread() const noexcept { return m_spread; }
            //! 左右の写しの不透明度を書く
            //! @param[in] opacity 0..1 に丸める
            //! @return 有限で書いた場合 true、それ以外の場合は false
            [[nodiscard]] bool SetSideOpacity(float opacity) noexcept;
            //! 左右の写しの不透明度。書かれていなければ 0
            [[nodiscard]] float SideOpacity() const noexcept { return m_sideOpacity; }
            //! 左右の写し 1 つの世界行列を返す
            //! 本体 model の描く行列の、描く時だけのずれを side × 離れ に置き換えた行列
            //! @param[in] root 持ち主の根の世界行列。alpha で補間した物を渡す
            //! @param[in] alpha 前の固定フレームから今の固定フレームまでの補間の割合 0..1
            //! @param[in] side 離れに掛ける向き。+1 か −1
            [[nodiscard]] NS::Matrix SideWorldMatrix(const NS::Model* model,
                                                     const NS::Matrix& root,
                                                     float alpha,
                                                     float side) const noexcept;

            //! 本体 model の今の描く世界行列を、明けの写しの姿として控える
            //! 控えた姿は根が動いても動かない。写しを描くのは不透明度が正の間だけ
            //! @param[in] root 持ち主の根の今の固定フレームの世界行列
            void CaptureAfterimage(const NS::Model* model, const NS::Matrix& root) noexcept;
            //! 明けの写しの不透明度を書く
            //! @param[in] opacity 0..1 に丸める。0 で写しを描かない
            //! @return 有限で書いた場合 true、それ以外の場合は false
            [[nodiscard]] bool SetAfterimageOpacity(float opacity) noexcept;
            //! 明けの写しの不透明度。書かれていなければ 0
            [[nodiscard]] float AfterimageOpacity() const noexcept { return m_afterimageOpacity; }
            //! 控えた明けの写しの世界行列。控えていなければ単位行列
            [[nodiscard]] const NS::Matrix& AfterimageWorldMatrix() const noexcept { return m_afterimageWorld; }

            //! 描く写しの数だけ、親の Model の DrawItem を積む
            void Collect(const NS::Gfx::RenderContext& context,
                         const NS::RenderSource& source,
                         std::vector<NS::Gfx::DrawItem>& out) override;
            [[nodiscard]] NS::AABB WorldBounds(const NS::RenderSource& source) const noexcept override;
            [[nodiscard]] NS::Gfx::RenderPass Bucket() const noexcept override
            {
                return NS::Gfx::RenderPass::Transparent;
            }
            [[nodiscard]] NS::Vector3 SortCenter(const NS::RenderSource& source) const noexcept override;
            //! 中心が同じ半透明の物の描画順。小さいほど先に描く。影より先に描くため既定は -1
            [[nodiscard]] int SortPriority() const noexcept override { return m_sortPriority; }
            void PrepareRender() override;
            void OnEndPlay() override;

        protected:
            //! 親の Model を描く物として自分を登録する。親に Model が無ければ登録しない
            void OnInit() override;
            void OnAppear() override;
            void OnDisappear() noexcept override;
            //! 親が Model を進めた後に呼ばれる。離れと不透明度を進め、描く写しを決める
            void VisualStep() override;

        private:
            //! 描く元の親の Model。親に Model が無ければ nullptr
            [[nodiscard]] const NS::Model* ParentModel() const noexcept;

            TackleGhostDesc m_desc{};
            int m_frame = 0;
            bool m_running = false;
            bool m_drawRegistered = false;
            NS::Vector3 m_spread{0.0f, 0.0f, 0.0f};
            float m_sideOpacity = 0.0f;
            NS::Matrix m_afterimageWorld = NS::Matrix::Identity;
            float m_afterimageOpacity = 0.0f;
            int m_sortPriority = -1;
            std::vector<NS::ModelDraw> m_draws;
        };
    } // namespace Level
} // namespace GL
