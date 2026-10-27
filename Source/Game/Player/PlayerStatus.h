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
        //! 今どの動きか
        //! PlayerActor::Motion
        //! が呼ばれた時の状態機械から組む。状態は状態の段の途中で変わるので、控えて持たない
        struct PlayerMotion
        {
            bool bodySlamming = false;  // 突進か、突進を続ける当たる直前の状態に居るか
            bool beforeContact = false; // 当たる直前の状態に居るか
            bool rebounding = false;
            bool reboundLanded = false; // 反動のまま着いたか。答えるのは PlayerActor::IsReboundLanded
            bool skidding = false;
            bool curled = false;
            bool canMoveBody = false; // 身体が有効で、当たりの止め・反動待ちの外か
            bool airDashing = false;
            bool dashRebounding = false; // 地形の面で返った反動の状態に居るか
            NS::Vector3 velocity{};      // 身体の今の速度 (m/s)
        };

        //! 突進の値。向きと溜め量は終了後も使うため状態内には置かず、ResetState で戻す
        class BodySlamStatus
        {
        public:
            //! 発動時に確定した溜め量 0..1
            [[nodiscard]] float Charge01() const noexcept { return m_charge01; }
            //! 発動時に確定した溜めすぎの深さ 0..1。赤で放した突進と通常突進は 0
            [[nodiscard]] float Overcharge01() const noexcept { return m_overcharge01; }
            //! 最後に出した突進の水平の向き
            //! 突進が終わった後も、次の突進を出すまで残す
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
            //! 反動を始めた回数
            //! 見た目が新しい反動の始まりを知るために読む。ResetState では戻さない
            [[nodiscard]] NS::uint32 Count() const noexcept { return m_count; }

            //! 通算の反動回数を保ったまま、今の反動の状態だけを初期値へ戻す
            void Reset() noexcept
            {
                m_direction = NS::Vector3{};
                m_spinSpeed = 0.0f;
                m_missTumble.reset();
            }

        private:
            friend class ::PlayerActor;

            NS::Vector3 m_direction{};
            float m_spinSpeed = 0.0f;
            std::optional<MissTumble> m_missTumble{}; // 書くのは BeginRebound
            NS::uint32 m_count = 0;
        };

        //! 地形の面で返った最後の反動の記録。開発用の線と Replay が読む
        struct TerrainReboundRecord
        {
            NS::Vector3 point{};     // 当たったフレームの根の位置
            NS::Vector3 incoming{};  // 当たる直前に進もうとした向き。正規化済み
            NS::Vector3 normal{};    // 遮った面の外向きの法線
            NS::Vector3 reflected{}; // 物理の反射。正規化済み
            NS::Vector3 assigned{};  // 表へ割り当てた向き
            NS::Vector3 shifted{};   // 1 段のずらしの後の向き。返った向き
            float facing = 0.0f;     // 当て方 |incoming·normal|
            float speed = 0.0f;      // 返った速さ (m/s)
        };

        //! エアダッシュと地形の反動の値。書くのは PlayerActor だけ
        class AirDashStatus
        {
        public:
            [[nodiscard]] int Remaining() const noexcept { return m_remaining; }
            //! 地形の反動を始めてからの秒。反動の状態の外では最後の反動の値のまま
            [[nodiscard]] float ReboundElapsed() const noexcept { return m_reboundElapsed; }
            //! 地形の反動を始めた回数。ResetState では戻さない
            [[nodiscard]] NS::uint32 ReboundCount() const noexcept { return m_reboundCount; }
            [[nodiscard]] const TerrainReboundRecord& LastRebound() const noexcept { return m_last; }

            //! 押しの控え・通算の反動回数・直前の反動記録を保ったまま、ダッシュの状態を初期値へ戻す
            void Reset() noexcept
            {
                m_remaining = 1;
                m_direction = NS::Vector3{};
                m_intendedDirection = NS::Vector3{};
                m_tiltDirection = NS::Vector3{};
                m_tiltSeconds = 0.0f;
                m_dashSpeed = 0.0f;
                m_pressed = false;
                m_intended = NS::Vector3{};
                m_reboundElapsed = 0.0f;
                m_steeredRadians = 0.0f;
                m_steerStarted = false;
                m_shiftApplied = false;
            }

        private:
            friend class ::PlayerActor;

            int m_remaining = 1;
            NS::Vector3 m_direction{};   // 今のダッシュの水平の向き。正規化済み
            NS::Vector3 m_intendedDirection{}; // ダッシュの向きに使う。無ければゼロ
            NS::Vector3 m_tiltDirection{};     // 今倒している水平の向き。倒していなければゼロ
            float m_tiltSeconds = 0.0f;        // m_tiltDirection を倒し続けた秒
            float m_dashSpeed = 0.0f;    // 今のダッシュの速さ (m/s)。状態に入った時に控え、途中の読み直しで変えない
            bool m_prevPressHeld = false; // 直前の歩にボタンを押していたか。押した瞬間を見分ける
            bool m_pressed = false;       // この歩に押した瞬間か。DecideStep の頭で書き、状態機械で読む
            NS::Vector3 m_intended{};     // 体を動かす前の速度。遮った面への当て方を測る
            float m_reboundElapsed = 0.0f;
            float m_steeredRadians = 0.0f; // 補正の段で曲げた角度の累計
            bool m_steerStarted = false;   // 補正の段に入ってダッシュを戻したか
            bool m_shiftApplied = false;   // 今の反動でずらしを掛けたか。掛けるのは反動 1 回に 1 度
            NS::uint32 m_reboundCount = 0;
            TerrainReboundRecord m_last{};
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

        //! 決定の段で確定した溜めと狙いの値
        //! DecideStep の頭で控えただけの押しと狙いは入れない。
        //!          読み手が 1 フレーム前の狙いを読まないようにするため
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
