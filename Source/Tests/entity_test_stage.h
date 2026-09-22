#pragma once

#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Physics/PhysicsScene.h>

namespace NsTest
{
    // 登場人物は持ち主の Scene の PhysicsScene で動く。Scene に一時オブジェクトを 1 体入れ、その PhysicsScene に床や壁を置いて試す
    // Scene は必ずカメラを 1 台持つので、カメラの無い前提の試しには使えない
    struct EntityStage
    {
        NS::Obj::Scene scene;
        NS::Obj::GameObject& owner = *scene.SpawnTransient<NS::Obj::GameObject>();
        NS::Phys::PhysicsScene& physics = scene.Physics();
    };
} // namespace NsTest
