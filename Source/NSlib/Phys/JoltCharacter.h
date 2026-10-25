#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/Core/NonCopyable.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

namespace NS
{
    namespace Phys
    {
        class PhysicsScene;

        //! @brief 縦のカプセルか球 1 つを 1 フレームずつ動かす JPH::CharacterVirtual のラッパー
        //! @details 重力は掛けないので、縦の速度は呼出側が作って Step の引数で渡す
        //! @pre physics は JoltCharacter より長く生きる
        class JoltCharacter : public NonCopyable
        {
        public:
            //! 半径 radius・半分の高さ halfHeight の縦カプセルで作る。halfHeight が 0 なら半径 radius の球で作る
            //! @pre radius は 0 より大きい
            //! @pre halfHeight は 0 以上。負の値は球で作るが、Step の足元の検査は中心より上から測る
            JoltCharacter(PhysicsScene& physics, float radius, float halfHeight);
            ~JoltCharacter();

            //! カプセルの寸法を変える。同じ値なら作り直さない。halfHeight が 0 なら半径 radius の球にする
            //! @pre radius は 0 より大きい。halfHeight は 0 以上
            void Resize(float radius, float halfHeight);

            //! dt 秒後の補正した姿勢と速度を保持する。重力は呼び手が速度へ加える
            //! maxStepHeight は登れる段差 m。負は 0、半径 × (1 − cos 45°) までは角を滑って登る
            void Step(const Vector3& position, const Vector3& velocity, float dt, float maxStepHeight);

            //! 直近の Step 後の位置
            [[nodiscard]] Vector3 Position() const;
            //! 直近の Step 後の補正済み速度
            [[nodiscard]] Vector3 Velocity() const;
            //! 直近の Step 後に接地しているか。登れない急な面に挟まれて下へ滑れない時も接地に数える
            [[nodiscard]] bool IsGrounded() const;
            //! @brief 直近の Step で、渡した速度の向きへ進むのを遮った面の外向きの単位法線を引く
            //! @details 実際に当たった接触のうち、速度に一番向かい合った面を選ぶ。面に沿う速度は遮ったに数えない
            //! @return 遮った面が無かった場合 false で、outNormal は書き換えない
            [[nodiscard]] bool BlockingNormal(Vector3& outNormal) const;

        private:
            PhysicsScene& m_physics;
            float m_radius = 0.4f;
            float m_halfHeight = 0.5f;
            JPH::Ref<JPH::CharacterVirtual> m_character;
            bool m_blocked = false;
            Vector3 m_blockNormal{0.0f, 0.0f, 0.0f};
        };

        //! 単位法線の Y 成分から、45° 以下の登れる斜面かを判定する
        [[nodiscard]] bool IsWalkableNormal(float normalY) noexcept;
    } // namespace Phys
} // namespace NS
