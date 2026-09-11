#pragma once

#include "Runtime/Object/Component.h"

namespace NS::Game::Level
{
    //! @brief 据え置きカメラの進入判定へプレイヤー位置を渡す
    //! @details PlacedVirtualCamera は SetActive が「進入中」の信号を兼ねるため、休止中の自分を有効化できず自走できない
    //! 位置の受け渡しだけをここが持つ。LateUpdate で走り、移動後の位置で判定する
    class AreaCameraActivatorComponent : public NS::Object::Component
    {
    public:
        AreaCameraActivatorComponent() noexcept;

        void OnUpdate() override;

        // 状態は保存しない。型検索で引けるよう型名だけ登録する
        NS_REFLECT_NONE(AreaCameraActivatorComponent, NS::Object::Component)
    };
} // namespace NS::Game::Level
