#include "Game/Level/LevelWorld.h"

#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/InstanceBatcher.h"
#include "Framework/Physics/PhysicsWorld.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/Components/PlacedVirtualCamera.h"
#include "Framework/Scene/Components/SlopeColliderComponent.h"
#include "Framework/Scene/Components/SphereColliderComponent.h"
#include "Framework/Scene/GameObject.h"
#include "Game/Blocks/AutoTile.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelData.h"
#include "Game/Theme/ThemeId.h"

#include <cmath>
#include <cstdint>

namespace NS::Game::Level
{

    LevelWorld::LevelWorld() = default;
    LevelWorld::~LevelWorld() = default;

    void LevelWorld::Rebuild(const LevelData& level,
                             NS::Scene::SceneBase& scene,
                             NS::Physics::PhysicsWorld& physics,
                             NS::Scene::AssetManager* assets)
    {
        Clear();

        // 衝突は build のたびに Clear -> Add* -> BuildBroadphase で満たし直す。 古い衝突を残さない
        physics.Clear();

        m_objects.reserve(level.objects.size());
        physics.ReserveAabbs(level.objects.size());

        // ファクトリは mesh / material を AssetManager から借りる。 不在の起動前 / テストでは何も組まない
        if (assets == nullptr)
            return;

        for (std::size_t objectIndex = 0; objectIndex < level.objects.size(); ++objectIndex)
        {
            const ObjectInstance& entry = level.objects[objectIndex];

            auto obj = NS::Game::Blocks::BuildPlacedObject(entry, *assets, level.materialPaths);
            if (!obj)
                continue; // 組み立てる component が無いオブジェクトはファクトリが nullptr を返す

            obj->AttachScene(&scene);
            obj->OnStart();

            const bool gridAligned = (entry.flags & kObjectFlagGridAligned) != 0;

            // grid solid は個別 Draw を殺して InstanceBatcher へ委ねる。 描画段が Objects() を直読みして instanceable
            // 判定 OnStart で RegisterRenderable 済なので MeshRenderer を非アクティブにするだけでよい
            if (NS::Game::Blocks::IsGridSolidObject(entry))
                if (auto* mesh = NS::Game::Blocks::FindComponent<NS::Scene::MeshRendererComponent>(*obj))
                    mesh->SetActive(false);

            // collider component を全部登録する。 同型を重ねれば複合形状として当たりに効く
            bool hazardRegistered = false;
            for (NS::Scene::Component* comp : obj->Components())
            {
                if (auto* sphere = dynamic_cast<NS::Scene::SphereColliderComponent*>(comp))
                    physics.AddSphere(sphere->WorldSphere());
                else if (auto* capsule = dynamic_cast<NS::Scene::CapsuleColliderComponent*>(comp))
                    physics.AddCapsule(capsule->WorldCapsule());
                else if (auto* box = dynamic_cast<NS::Scene::BoxColliderComponent*>(comp))
                {
                    // 同じ Box でも gridAligned なら軸並行 AABB、 自由配置なら回転込み OBB
                    if (gridAligned)
                        physics.AddAabb(box->WorldAABB());
                    else
                        physics.AddObb(box->WorldOBB());
                }
                else if (auto* slope = dynamic_cast<NS::Scene::SlopeColliderComponent*>(comp))
                    for (const auto& tri : slope->WorldTriangles())
                        physics.AddTriangle(tri);
                else if (dynamic_cast<NS::Scene::HazardComponent*>(comp) != nullptr)
                {
                    // hazard の damage は固形 AABB とは別経路の毎フレーム重なり判定で効くため view にも積む
                    if (!hazardRegistered)
                    {
                        m_hazardView.push_back(obj.get());
                        hazardRegistered = true;
                    }
                }
            }
            // water / deco は collider を持たないため当たり無し・ view 不要

            // 据え置きカメラはエリア外で開始し、 play 中の進入判定が自分を active 化する
            if (auto* placed = obj->FindComponent<NS::Scene::PlacedVirtualCamera>())
            {
                placed->SetActive(false);
                m_placedCameraView.push_back(placed);
            }

            m_objectSourceIndices.push_back(objectIndex);
            m_objects.push_back(std::move(obj));
        }

        // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
        // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全配置物が振れる
        for (auto& obj : m_objects)
            obj->Root().Snapshot();

        // instanced block の静的属性を焼く。 描画ループの per-frame 文字列走査と近傍マスクの O(N^2) を畳む
        // instancing は描画段の判断で、 grid 固形だけを instanced bucket へ流す。 position は Snapshot 後で確定済
        for (std::size_t i = 0; i < m_objects.size(); ++i)
        {
            const ObjectInstance& entry = level.objects[m_objectSourceIndices[i]];
            if (!NS::Game::Blocks::IsGridSolidObject(entry))
                continue;
            const NS::Math::Vector3 wp = m_objects[i]->Root().Position();
            const std::int16_t x = static_cast<std::int16_t>(std::lround(wp.x));
            const std::int16_t y = static_cast<std::int16_t>(std::lround(wp.y));
            const std::int16_t z = static_cast<std::int16_t>(std::lround(wp.z));
            const std::uint8_t mask = NS::Game::Blocks::ComputeNeighborMask(level, x, y, z);
            const std::uint16_t slice =
                NS::Game::Blocks::LookupTextureSlice(static_cast<NS::Game::Theme::ThemeId>(level.themeId), mask);
            m_instancedBlocks.push_back(InstancedBlock{i, static_cast<float>(slice)});
        }

#if !defined(NS_SHIPPING)
        // コヨーテ debug 用に踏み外せる縁を焼く。 level が変わらない限り不変なのでここで 1 度だけ
        m_ledgeEdges = NS::Game::Blocks::ComputeTopLedgeEdges(level);
#endif

        physics.BuildBroadphase();
    }

    void LevelWorld::Clear()
    {
        // 配置物は生成の逆順で畳む。 依存し合う component の OnEndPlay 順序を生成時と対称に保つ
        for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it)
            (*it)->OnEndPlay();
        m_objects.clear();
        m_objectSourceIndices.clear();
        m_instancedBlocks.clear();
        m_hazardView.clear();
        m_placedCameraView.clear();
    }

    void LevelWorld::CreateBatcher()
    {
        m_instanceBatcher = NS::Graphics::InstanceBatcher::Create();
        if (!m_instanceBatcher->IsValid())
            NS_LOG_WARN(::NS::Core::LogCat::Game, "LevelWorld: InstanceBatcher 構築失敗、 block 描画はスキップされる");
    }

    void LevelWorld::ResetBatcher() noexcept
    {
        m_instanceBatcher.reset();
    }

} // namespace NS::Game::Level
