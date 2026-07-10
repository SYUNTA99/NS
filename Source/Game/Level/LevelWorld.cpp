#include "Game/Level/LevelWorld.h"

#include "Game/Blocks/BuildPlacedObject.h"
#include "Framework/Scene/SceneData.h"
#include "Game/Player.h"

namespace NS::Game::Level
{
    namespace
    {
        // OBB の 3 軸が座標軸に十分沿っていれば軸並行とみなす。 90° 刻みの回転はここに落ちる
        // 各軸は単位ベクトルなので最大成分が 1 に届けば残り 2 成分はほぼ 0 になる
        // しきい 1e-4 は 90° を quaternion 経由で組んだ時の float 誤差を確実に飲み込み、 1° 以上の傾きは OBB へ回す
        [[nodiscard]] bool IsAxisAligned(const NS::Physics::OBB& obb) noexcept
        {
            constexpr float kAlignEpsilon = 1e-4f;
            const auto alignedAxis = [](const NS::Math::Vector3& axis) noexcept {
                const float maxComponent = std::max({std::abs(axis.x), std::abs(axis.y), std::abs(axis.z)});
                return maxComponent >= 1.0f - kAlignEpsilon;
            };
            return alignedAxis(obb.axisX) && alignedAxis(obb.axisY) && alignedAxis(obb.axisZ);
        }
    } // namespace

    LevelWorld::LevelWorld() = default;
    LevelWorld::~LevelWorld() = default;

    void LevelWorld::Rebuild(const NS::Scene::SceneData& level,
                             NS::Scene::SceneBase& scene,
                             NS::Physics::PhysicsWorld& physics,
                             NS::Scene::AssetManager* assets)
    {
        Clear();

        // 衝突は build のたびに Clear -> Add* -> BuildBroadphase で満たし直す。 古い衝突を残さない
        physics.Clear();

        // 参照照合窓口も同じ周期で空へ戻す。 旧 build の実体を指す登録を組み直しへ持ち越さない
        auto* refs = scene.GetSubsystem<NS::Scene::ObjectRefSubsystem>();
        if (refs != nullptr)
            refs->Clear();

        m_objects.reserve(level.objects.size());
        physics.ReserveAabbs(level.objects.size());

        // ファクトリは mesh / material を AssetManager から借りる。 不在の起動前 / テストでは何も組まない
        if (assets == nullptr)
            return;

        // 先に全 object を組んで永続 id を照合窓口へ登録し、 開始は後段でまとめて行う
        // OnStart で ObjectRef を解決する component が、 自分より後ろの object も引けるようにするため
        for (std::size_t objectIndex = 0; objectIndex < level.objects.size(); ++objectIndex)
        {
            const NS::Scene::ObjectData& entry = level.objects[objectIndex];

            auto obj = NS::Game::Blocks::BuildPlacedObject(entry, *assets, level.materialPaths);
            if (!obj)
                continue; // 組み立てる component が無いオブジェクトはファクトリが nullptr を返す

            obj->AttachScene(&scene);
            if (refs != nullptr)
                refs->Register(entry.objectId, obj.get());

            // プレイヤーの型付き view を控える。 所有は他の配置物と同じく m_objects 側
            if (auto* player = dynamic_cast<::Player*>(obj.get()))
                m_playerView = player;

            m_objectSourceIndices.push_back(objectIndex);
            m_objects.push_back(std::move(obj));
        }

        for (auto& objPtr : m_objects)
        {
            NS::Scene::GameObject* obj = objPtr.get();
            obj->OnStart();

            // collider component を全部登録する。 同型を重ねれば複合形状として当たりに効く
            bool hazardRegistered = false;
            for (NS::Scene::Component* comp : obj->Components())
            {
                if (auto* sphere = NS::Scene::ComponentCast<NS::Scene::SphereColliderComponent>(comp))
                    physics.AddSphere(sphere->WorldSphere());
                else if (auto* capsule = NS::Scene::ComponentCast<NS::Scene::CapsuleColliderComponent>(comp))
                    physics.AddCapsule(capsule->WorldCapsule());
                else if (auto* box = NS::Scene::ComponentCast<NS::Scene::BoxColliderComponent>(comp))
                {
                    // 軸並行すなわち回転が 90° 刻みなら従来通り AABB、 傾いた箱だけ OBB
                    // 旧 grid は必ず軸並行なので AABB に落ち、 上を走る / 角に当たる手触りは不変
                    const NS::Physics::OBB obb = box->WorldOBB();
                    if (IsAxisAligned(obb))
                        physics.AddAabb(box->WorldAABB());
                    else
                        physics.AddObb(obb);
                }
                else if (auto* slope = NS::Scene::ComponentCast<NS::Scene::SlopeColliderComponent>(comp))
                    for (const auto& tri : slope->WorldTriangles())
                        physics.AddTriangle(tri);
                else if (NS::Scene::ComponentCast<NS::Scene::HazardComponent>(comp) != nullptr)
                {
                    // hazard の damage は固形 AABB とは別経路の毎フレーム重なり判定で効くため view にも積む
                    if (!hazardRegistered)
                    {
                        m_hazardView.push_back(obj);
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
                m_virtualCameraView.push_back(placed);
            }

            // 追従カメラは休止で開始し、 プレイ突入の進行役が active 化する。 編集中は free-fly が主役のまま
            if (auto* follow = obj->FindComponent<NS::Scene::ThirdPersonFollowComponent>())
            {
                follow->SetActive(false);
                m_followCameraView.push_back(follow);
                m_virtualCameraView.push_back(follow);
            }
        }

        // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
        // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全配置物が振れる
        for (auto& obj : m_objects)
            obj->Root().Snapshot();

        physics.BuildBroadphase();

        // 接地シャドウは各配置物の内包 AABB を下方向 ray で拾う。 blob なので OBB 精度は要らない
        if (m_playerView != nullptr)
        {
            std::vector<NS::Math::AABB> shadowReceivers;
            shadowReceivers.reserve(m_objects.size());
            for (auto& obj : m_objects)
                if (auto aabb = NS::Game::Blocks::ColliderWorldAABB(*obj))
                    shadowReceivers.push_back(*aabb);
            m_playerView->Shadow().SetCollisionWorld(shadowReceivers);
        }
    }

    void LevelWorld::Clear()
    {
        // 配置物は生成の逆順で畳む。 依存し合う component の OnEndPlay 順序を生成時と対称に保つ
        for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it)
            (*it)->OnEndPlay();
        m_objects.clear();
        m_objectSourceIndices.clear();
        m_hazardView.clear();
        m_placedCameraView.clear();
        m_followCameraView.clear();
        m_virtualCameraView.clear();
        m_playerView = nullptr;
    }

} // namespace NS::Game::Level
