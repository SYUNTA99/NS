#pragma once

#include "Game/Level/LevelMessages.h"
#include "NSlib/Core/Math.h"

namespace NS
{
    class Model;
    class SceneActor;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! 体当たりの残像の進行。左右への離れと不透明度を通常の子Actorへ渡す
        //! ModelとActorは所有しない。持ち主がModelを進めた直後、VisualStepの先頭で進める
        class TackleGhost final
        {
        public:
            //! 長さが0以下かカメラが無ければ始めない。次のAdvanceを1フレーム目に数える
            void Start(const NS::HitOwnerRun& run, const NS::SceneActor* owner);
            //! 子が出ている間だけ進め、親の補間前後の姿勢と描画指定を子へ写す
            void Advance(NS::SceneActor* owner);
            void Stop(NS::SceneActor* owner) noexcept;
            //! 不透明度の倍率は次のAdvanceから効く
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

        private:
            TackleGhostDesc m_desc{};
            int m_frame = 0;
            bool m_running = false;
            NS::Vector3 m_spread{0.0f, 0.0f, 0.0f};
            float m_sideOpacity = 0.0f;
            NS::Matrix m_afterimageWorld = NS::Matrix::Identity;
            float m_afterimageOpacity = 0.0f;
        };
    } // namespace Level
} // namespace GL
