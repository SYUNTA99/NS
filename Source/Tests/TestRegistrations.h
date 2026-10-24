#pragma once

#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/CoreObject/System/HitSensorDirector.h"

#include <algorithm>
#include <cstddef>
#include <vector>

//! 調べ役に入っているセンサーのうち、調べる対象の数
inline std::size_t EnabledSensorCount(const NS::Scene& scene)
{
    const std::vector<NS::HitSensor*>& sensors = scene.HitSensors().Sensors();
    return static_cast<std::size_t>(
        std::count_if(sensors.begin(), sensors.end(), [](const NS::HitSensor* sensor) { return sensor->IsValid(); }));
}

//! 部品がシーンの管理クラスへ入れた数。無効の組も数える
struct Registrations
{
    std::size_t renderables = 0;
    std::size_t overlays = 0;
    std::size_t lights = 0;
    std::size_t cameras = 0;
    std::size_t sensors = 0;

    bool operator==(const Registrations&) const = default;
};

inline Registrations CountRegistrations(NS::Scene& scene)
{
    return Registrations{scene.RenderableCount(),
                         scene.OverlayCount(),
                         scene.LightCount(),
                         scene.GetCameraManager()->VirtualCameraCount(),
                         scene.HitSensors().Sensors().size()};
}
