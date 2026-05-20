#pragma once

/// @file third_person_follow_component.h
/// @brief  で実装。critically-damped spring 追従 +  マニュアル回転 +
///         FOV/sensitivity/invert 保持 + dynamic zoom (〜C5)。

#include "ns/scene/component.h"

namespace ns::scene
{
    class ThirdPersonFollowComponent : public Component
    {};
} // namespace ns::scene
