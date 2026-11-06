#pragma once

#include "Game/Level/ReboundDirections.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/Types.h"

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
            bool curled = false;
            bool canMoveBody = false; // 身体が有効で、当たりの止めの外か
            bool airDashing = false;
            bool dashRebounding = false; // 地形の面で返った反動の状態に居るか
            NS::Vector3 velocity{};      // 身体の今の速度 (m/s)
        };

        //! 地形の面で返った最後の反動の記録。開発用の線と Replay が読む
        struct TerrainReboundRecord
        {
            NS::Vector3 point{};     // 当たったフレームの根の位置
            NS::Vector3 incoming{};  // 当たる直前に進もうとした向き。正規化済み
            NS::Vector3 normal{};    // 遮った面の外向きの法線
            NS::Vector3 reflected{}; // 物理の反射。正規化済み
            NS::Vector3 assigned{};  // 表へ割り当てた向き
            NS::Vector3 shifted{};   // 入力補正の後の向き。返った向き
            float facing = 0.0f;     // 当て方 |incoming·normal|
            float speed = 0.0f;      // 返った速さ (m/s)
            GL::Level::ReboundInputResult input = GL::Level::ReboundInputResult::Kept; // 入力補正が向きをどうしたか
        };

        //! エアダッシュと地形の反動の値。書くのは PlayerActor だけ
        class AirDashStatus
        {
        public:
            //! 今出せるエアダッシュの回数。接地の間と、反動の補正の段に入った時に 1 に戻る
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
                m_dashSpeed = 0.0f;
                m_pressed = false;
                m_intended = NS::Vector3{};
                m_reboundElapsed = 0.0f;
                m_steeredRadians = 0.0f;
                m_steerStarted = false;
                m_reboundStopFrames = 0;
                m_reboundChosen = false;
                m_stickMoving = false;
                m_steadyDirection = NS::Vector3{};
            }

        private:
            friend class ::PlayerActor;

            int m_remaining = 1;
            NS::Vector3 m_direction{};   // 今のダッシュの水平の向き。正規化済み
            float m_dashSpeed = 0.0f;    // 今のダッシュの速さ (m/s)。状態に入った時に控え、途中の読み直しで変えない
            bool m_prevPressHeld = false; // 直前の歩にボタンを押していたか。押した瞬間を見分ける
            bool m_pressed = false;       // この歩に押した瞬間か。DecideStep の頭で書き、状態機械で読む
            NS::Vector3 m_intended{};     // 体を動かす前の速度。遮った面への当て方を測る
            float m_reboundElapsed = 0.0f;
            float m_steeredRadians = 0.0f; // 補正の段で曲げた角度の累計
            bool m_steerStarted = false;   // 補正の段に入ってダッシュを戻したか
            NS::Vector3 m_prevStick{};     // 直前の歩の左スティックの、カメラで回す前の位置。動いたかを動いた量で見る
            bool m_stickMoving = false;    // この歩に左スティックが動いているか。止めの延長とエアダッシュの向きが読む
            NS::Vector3 m_steadyDirection{}; // 最後に止まっていた歩の左スティックの水平の向き。デッドゾーンの中なら 0
            int m_reboundStopFrames = 0;   // 今の反動で止めたフレーム数。当たったフレームを入れて数える
            bool m_reboundChosen = false;  // 今の反動の向きを、止めが明けたフレームに選んだか。選ぶのは反動 1 回に 1 度
            NS::uint32 m_reboundCount = 0;
            TerrainReboundRecord m_last{};
        };
    } // namespace Player
} // namespace GL
