#pragma once

#include "Editor/EditorActors.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Phys/PhysicsScene.h"

#include <string_view>
#include <utility>

//! doc の中で名前が name のアクターの id。ファイルから読んだ id は並び順で振り直されるので、置いた物は名前で探す
//! 居なければ k_NoActorId
inline NS::uint32 ActorIdByName(const nlohmann::json& doc, std::string_view name)
{
    for (const nlohmann::json& actor : NS::SceneJsonActors(doc))
    {
        if (NS::ActorJsonName(actor) == name)
        {
            return NS::ActorJsonId(actor);
        }
    }
    return NS::k_NoActorId;
}

//! .actor の名前と id だけを持つアクターを doc へ足す
inline void AddActorJson(nlohmann::json& doc, std::string_view actorFile, NS::uint32 id)
{
    nlohmann::json actor = NS::MakeActorJson();
    NS::SetActorJsonFile(actor, actorFile);
    NS::SetActorJsonId(actor, id);
    NS::SceneJsonActors(doc).push_back(std::move(actor));
}

//! .actor の名前・id・位置を持つアクターを doc へ足す
inline void AddActorJson(nlohmann::json& doc, std::string_view actorFile, NS::uint32 id, const NS::Vector3& position)
{
    nlohmann::json actor = NS::MakeActorJson();
    NS::SetActorJsonFile(actor, actorFile);
    NS::SetActorJsonId(actor, id);
    NS::Editor::SetActorJsonPosition(actor, position);
    NS::SceneJsonActors(doc).push_back(std::move(actor));
}

//! .actor の名前・id・位置・大きさを持つアクターを doc へ足す
inline void AddActorJson(nlohmann::json& doc,
                         std::string_view actorFile,
                         NS::uint32 id,
                         const NS::Vector3& position,
                         const NS::Vector3& scale)
{
    nlohmann::json actor = NS::MakeActorJson();
    NS::SetActorJsonFile(actor, actorFile);
    NS::SetActorJsonId(actor, id);
    NS::Editor::SetActorJsonPosition(actor, position);
    NS::Editor::SetActorJsonScale(actor, scale);
    NS::SceneJsonActors(doc).push_back(std::move(actor));
}

//! 回転の無い地形の箱をシーンの物理へ足す
inline JPH::BodyID
AddTerrainBox(NS::Scene& scene, const NS::Vector3& center, float halfExtentX, float halfExtentY, float halfExtentZ)
{
    NS::OBB box{};
    box.center = center;
    box.halfExtentX = halfExtentX;
    box.halfExtentY = halfExtentY;
    box.halfExtentZ = halfExtentZ;
    return scene.Physics().AddBox(box, NS::Phys::k_TerrainObjectLayer);
}
