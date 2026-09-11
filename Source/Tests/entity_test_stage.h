#pragma once

#include <Runtime/Object/GameObject.h>
#include <Runtime/Object/Scene/Scene.h>
#include <Runtime/Physics/PhysicsWorld.h>

namespace NsTest
{
    // 登場人物は持ち主の Scene の world で動く。Scene に一時オブジェクトを 1 体入れ、その world に床や壁を置いて試す
    // Scene は必ずカメラを 1 台持つので、カメラの無い前提の試しには使えない
    struct EntityStage
    {
        NS::Object::Scene scene;
        NS::Object::GameObject& owner = *scene.SpawnTransient<NS::Object::GameObject>();
        NS::Physics::PhysicsWorld& world = scene.Physics();
    };
} // namespace NsTest
