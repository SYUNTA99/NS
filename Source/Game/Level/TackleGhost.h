#pragma once

#include "Game/Level/LevelMessages.h"
#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Object.h"

namespace NS
{
    class SceneActor;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! エアダッシュの残像 (名前は前の体当たりの残像のまま)。持ち主の Model を、左右の写しと明けの写しとして半透明で描かせる部品
        //! 持ち主が OnInit で作り、毎ステップ Step で持ち主の Model へ写しの列を書く。持ち主も Model も覚えず、呼ぶたびに受ける
        class TackleGhost final : public NS::Object
        {
        public:
            NS_REFLECT_BEGIN(TackleGhost, NS::Object)
            NS_FIELD(m_sortPriority, "描く順")
            NS_REFLECT_END()

            //! 長さが0以下かカメラが無ければ始めない。次の Step を1フレーム目に数える
            //! @param[in] owner 持ち主。カメラの向きと根の位置で、離れのピクセルを m に直す
            void Start(const NS::SceneActor* owner, const NS::HitOwnerRun& run);
            //! 進みを止め、離れと不透明度を 0 にし、model の写しの列を空にする
            //! @param[in] model 持ち主の Model
            void Stop(NS::Model* model) noexcept;
            //! 不透明度の倍率は次の Step から効く
            void ScaleOpacity(float scale) noexcept;
            //! 始めていれば 1 フレーム進め、今の離れと不透明度から写しの列を作って model へ書く
            //! 持ち主の HitReaction が Model を進めた後、見た目を更新する前に呼ぶ
            //! @param[in] owner 持ち主。写し取る姿の根は持ち主の根の今の固定フレームの世界行列
            //! @param[in] model 持ち主の Model
            void Step(const NS::SceneActor* owner, NS::Model* model);

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
            //! 写しの組の、中心の距離が同じ半透明の物との描く順。小さいほど先に描く
            [[nodiscard]] int SortPriority() const noexcept { return m_sortPriority; }

        private:
            TackleGhostDesc m_desc{};
            int m_frame = 0;
            bool m_running = false;
            NS::Vector3 m_spread{0.0f, 0.0f, 0.0f};
            float m_sideOpacity = 0.0f;
            NS::Matrix m_afterimageWorld = NS::Matrix::Identity;
            float m_afterimageOpacity = 0.0f;
            int m_sortPriority = -1; // 中心が同じ半透明の物の描く順。小さいほど先に描く。影より先に描くため既定は -1
        };
    } // namespace Level
} // namespace GL
