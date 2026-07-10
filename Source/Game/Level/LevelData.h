#pragma once

/// @file LevelData.h
/// @brief 旧名 LevelData 系から NS::Scene::SceneData 系への別名の橋渡し
///
/// @details 器と汎用ヘルパの実体は Framework/Scene/SceneData.h に在る。 ここは旧名の
/// 別名で既存の消費側を繋ぐだけの薄いヘッダで、 レベル固有のオブジェクト種別ヘルパは
/// Game/Level/LevelObjects.h が持つ

#include "Framework/Scene/SceneData.h"

namespace NS::Game::Level
{

    using FieldValue = NS::Scene::FieldValue;
    using ComponentData = NS::Scene::ComponentData;
    using ObjectInstance = NS::Scene::ObjectData;
    using LevelEnvironment = NS::Scene::SceneEnvironment;
    using LevelData = NS::Scene::SceneData;

    using NS::Scene::kNoObjectId;
    using NS::Scene::kNoObjectIndex;

    using NS::Scene::AllocateObjectId;
    using NS::Scene::EnsureUniqueObjectIds;
    using NS::Scene::EstimatedHeapBytes;
    using NS::Scene::FindComponentData;
    using NS::Scene::FindField;
    using NS::Scene::FindObjectIndexById;
    using NS::Scene::PruneDanglingObjectRefs;

} // namespace NS::Game::Level
