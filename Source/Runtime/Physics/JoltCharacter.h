#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Core/NonCopyable.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>

#include <memory>

namespace NS::Physics
{
    class PhysicsScene;

    //! @brief カプセル 1 本を 1 フレームずつ動かす JPH::CharacterVirtual のラッパー
    //! @details 重力は掛けないので、縦の速度は呼出側が作って Step の引数で渡す
    //! @pre physics は JoltCharacter より長く生きる
	class JoltCharacter : public NS::Core::NonCopyable
    {
    public:
        //! 半径 radius・半分の高さ halfHeight の縦カプセルで作る
        JoltCharacter(PhysicsScene& physics, float radius, float halfHeight);
        ~JoltCharacter();

        //! カプセルの寸法を変える。同じ値なら作り直さない
        void Resize(float radius, float halfHeight);

        //! @brief 開始位置と開始速度を渡して dt 秒ぶん進め、補正後の状態を内部へ保持する
        //! @param[in] position 開始位置
        //! @param[in] velocity 開始速度
        //! @param[in] dt 進める秒数
        //! @param[in] maxStepHeight 走ったまま登れる段の高さ (m)。負の値は 0 として扱う
        //! 半径 × (1 − cos 45°) より低い値を渡しても、その高さまでの角は滑って登る
        void Step(const NS::Core::Vector3& position, const NS::Core::Vector3& velocity, float dt, float maxStepHeight);

        //! 直近の Step 後の位置
        [[nodiscard]] NS::Core::Vector3 Position() const;
        //! 直近の Step 後の補正済み速度
        [[nodiscard]] NS::Core::Vector3 Velocity() const;
        //! 直近の Step 後に接地しているか。登れない急な面に挟まれて下へ滑れない時も接地に数える
        [[nodiscard]] bool IsGrounded() const;

    private:
        PhysicsScene& m_physics;
        float m_radius = 0.4f;
        float m_halfHeight = 0.5f;
        JPH::Ref<JPH::CharacterVirtual> m_character;
    };
} // namespace NS::Physics
