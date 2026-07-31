#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    /// @brief 据え置きカメラの進入判定へプレイヤー位置を渡す
    /// @details PlacedVirtualCamera は SetActive が「進入中」の信号を兼ねるため、寝た自分を起こせず自走できない
    /// 位置の受け渡しだけをここが持つ。物理の後に走るのでこの tick の位置で判定する
    class AreaCameraActivatorComponent : public NS::Object::Component
    {
    public:
        AreaCameraActivatorComponent() noexcept;

        void OnUpdate() override;

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(AreaCameraActivatorComponent, NS::Object::Component)
    };
} // namespace NS::Game::Level
