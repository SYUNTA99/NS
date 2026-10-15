#pragma once

#include "Game/Level/ImpactInputJudge.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player/ReboundArc.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"

#include <optional>

class PlayerActor;

// 自機の今の動きの値。書くのは PlayerActor だけで、見た目とエフェクトの部品は const の参照で問いの関数を読む
namespace GL
{
    namespace Player
    {
        //! @brief 今どの動きか
        //! @details PlayerActor::Motion
        //! が呼ばれた時の状態機械から組む。状態は状態の段の途中で変わるので、控えて持たない
        struct PlayerMotion
        {
            bool bodySlamming = false;
            bool rebounding = false;
            bool skidding = false;
            bool curled = false;
            bool canMoveBody = false; // 身体が有効で、当たりの止め・反動待ちの外か
        };

        //! 突進の値。向きと溜め量は終了後も使うため状態内には置かず、ResetState で戻す
        class BodySlamStatus
        {
        public:
            //! 発動時に確定した溜め量 0..1
            [[nodiscard]] float Charge01() const noexcept { return m_charge01; }
            //! 発動時に確定した溜めすぎの深さ 0..1。赤で放した突進と通常突進は 0
            [[nodiscard]] float Overcharge01() const noexcept { return m_overcharge01; }
            //! @brief 最後に出した突進の水平の向き
            //! @details 突進が終わった後も、次の突進を出すまで残す
            //! @return 正規化済みの向き。突進を出す前と ResetState の後はゼロ
            [[nodiscard]] NS::Vector3 Direction() const noexcept { return m_dir; }
            //! 最後に出した突進で玉が回る速さ (度/秒)。突進を出す前と ResetState の後は 0
            [[nodiscard]] float SpinSpeed() const noexcept { return m_spinSpeed; }

        private:
            friend class ::PlayerActor;

            float m_spinSpeed = 0.0f;      // 書くのは突進を出した時
            float m_travelled = 0.0f;      // 突進で進んだ水平距離
            float m_distanceTarget = 0.0f; // 突進を終える水平距離
            bool m_isTap = false;          // 溜め量 0 の飛び込みか
            bool m_justStarted = false;    // 発動したフレームか
            NS::Vector3 m_dir{};           // 書くのは出せた時だけ
            float m_charge01 = 0.0f;
            float m_overcharge01 = 0.0f;
            bool m_forced = false;
            bool m_wasSlamming = false; // 直前のフレームを突進中で終えたか。書くのは MoveBody と ResetState
        };

        //! 反動の値
        class ReboundStatus
        {
        public:
            //! 最後に始めた反動の水平の向き。正規化済み。反動を始める前と ResetState の後はゼロ
            [[nodiscard]] NS::Vector3 Direction() const noexcept { return m_direction; }
            //! 最後に始めた反動の玉の回る速さ (度/秒)。開始前と ResetState 後は 0
            [[nodiscard]] float SpinSpeed() const noexcept { return m_spinSpeed; }
            //! 最後に始めた反動の外れの回り方。外れの反動の時だけ値を持つ
            [[nodiscard]] const std::optional<MissTumble>& Tumble() const noexcept { return m_missTumble; }
            //! @brief 反動を始めた回数
            //! @details 見た目が新しい反動の始まりを知るために読む。ResetState では戻さない
            [[nodiscard]] NS::uint32 Count() const noexcept { return m_count; }

        private:
            friend class ::PlayerActor;

            NS::Vector3 m_direction{};
            float m_spinSpeed = 0.0f;
            std::optional<MissTumble> m_missTumble{}; // 書くのは BeginRebound
            NS::uint32 m_count = 0;
        };

        //! 外れの着地からこすって止まる間の値。書くのは BeginSkid と AdvanceSkid
        class SkidStatus
        {
        public:
            //! 着いた水平の速さに今掛けている倍率 0〜1。身体と玉の回転が同じ倍率で落ちる
            [[nodiscard]] float SpeedScale() const noexcept { return m_speedScale; }

        private:
            friend class ::PlayerActor;

            NS::Vector3 m_landingVelocity{}; // 着いたフレームの水平の速度
            int m_elapsedSteps = 0;          // 着いてからのフレーム数
            float m_speedScale = 1.0f;
        };

        //! @brief 決定の段で確定した溜めと狙いの値
        //! @details 観測の段に控えただけの押しと狙いは入れない。読み手が 1 フレーム前の狙いを読まないようにするため
        class ChargeStatus
        {
        public:
            //! 通常突進と溜めの判定
            [[nodiscard]] const GL::Level::ImpactInputJudge& Judge() const noexcept { return m_judge; }
            //! 押している間の狙いの線を返す。線が無い場合は false で outLine を変更しない
            [[nodiscard]] bool TryGetAimLine(GL::Level::AimLine& outLine) const noexcept
            {
                if (!m_hasAimLine)
                {
                    return false;
                }
                outLine = m_aimLine;
                return true;
            }
            //! 揺れを足す前の線で狙う相手を返す。相手が無い場合は false で outTarget を変更しない
            [[nodiscard]] bool TryGetAimTarget(GL::Level::SlamLineTarget& outTarget) const noexcept
            {
                if (!m_hasAimTarget)
                {
                    return false;
                }
                outTarget = m_aimTarget;
                return true;
            }
            //! 紫の揺れを足した線で最初に触れる相手を返す。相手が無い場合は false で outTarget を変更しない
            [[nodiscard]] bool TryGetLineTarget(GL::Level::SlamLineTarget& outTarget) const noexcept
            {
                if (!m_hasLineTarget)
                {
                    return false;
                }
                outTarget = m_lineTarget;
                return true;
            }
            //! 紫の揺れの位相 (ラジアン)。紫でない間は 0
            [[nodiscard]] float SwayPhase() const noexcept { return m_swayPhase; }
            //! 紫の揺れの横のずれ (m)。狙いの線の右が正。紫でない間は 0
            [[nodiscard]] float SwayOffset() const noexcept { return m_swayOffset; }
            //! このフレームに紫の揺れが端を通った場合 true、それ以外の場合は false
            [[nodiscard]] bool SwayReachedEdge() const noexcept { return m_swayReachedEdge; }

        private:
            friend class ::PlayerActor;

            GL::Level::ImpactInputJudge m_judge{};
            GL::Level::AimLine m_aimLine{}; // m_hasAimLine が偽の間は読まない
            bool m_hasAimLine = false;
            GL::Level::SlamLineTarget m_aimTarget{}; // m_hasAimTarget が偽の間は読まない
            bool m_hasAimTarget = false;
            GL::Level::SlamLineTarget m_lineTarget{}; // m_hasLineTarget が偽の間は読まない
            bool m_hasLineTarget = false;
            float m_swayPhase = 0.0f;
            float m_swayOffset = 0.0f;
            int m_swayEdge = 0;         // 位相が通った端の番号。(位相 − π/2) ÷ π の切り捨て
            bool m_hasSwayEdge = false; // 前のフレームの端の番号を控えているか
            bool m_swayReachedEdge = false;
        };
    } // namespace Player
} // namespace GL
