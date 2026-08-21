#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Object/Component.h"

namespace NS::Object
{
    class GameObject;
    class Scene;
} // namespace NS::Object

namespace NS::Game::Level
{
    //! @brief ぶつかった場所の床へ寝かせる跡
    //! @details 半透明の板を置き、時間で縮めて消す。消える時は配置物を破棄せず描画と自身の更新を止める
    //! 更新の最中の破棄は World::UpdateObjects が集めた並びに解放済みの位置を残すため使わない
    //! 依存: NS::Object::MeshRendererComponent, NS::Object::Scene
    class ImpactMarkComponent : public NS::Object::Component
    {
    public:
        //! 指定の位置へ跡の一時オブジェクトを出す。scene が nullptr なら nullptr を返す
        [[nodiscard]] static NS::Object::GameObject* SpawnAt(NS::Object::Scene* scene,
                                                             const NS::Core::Vector3& position);

        //! 出た直後の水平の大きさを跡の直径へ合わせる
        void OnStart() override;

        //! 経過秒を進めて縮め、寿命が尽きたら描画と自身の更新を止める
        void OnUpdate() override;

        // 跡の残り方は衝突の余韻。プレイ中に Inspector で触って詰められるよう公開する
        NS_REFLECT_BEGIN(ImpactMarkComponent, NS::Object::Component)
        NS_REFLECT_FIELD(m_diameter, "跡の直径")
        NS_REFLECT_FIELD(m_lifeSeconds, "跡の残る秒")
        NS_REFLECT_END()

    private:
        float m_diameter = 1.5f;    // 出た直後の水平の大きさ
        float m_lifeSeconds = 6.0f; // 消えるまでの秒
        float m_age = 0.0f;         // 出てからの経過秒
    };
} // namespace NS::Game::Level
