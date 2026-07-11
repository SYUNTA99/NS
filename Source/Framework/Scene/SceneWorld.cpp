#include "Framework/Scene/SceneWorld.h"

#include "Framework/Physics/PhysicsWorld.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CapsuleColliderComponent.h"
#include "Framework/Scene/Components/SlopeColliderComponent.h"
#include "Framework/Scene/Components/SphereColliderComponent.h"
#include "Framework/Scene/ObjectRefSubsystem.h"
#include "Framework/Scene/SceneBase.h"
#include "Framework/Scene/SceneData.h"

#include <algorithm>
#include <cmath>

namespace NS::Scene
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

    SceneWorld::SceneWorld() = default;
    SceneWorld::~SceneWorld() = default;

    void SceneWorld::Rebuild(const SceneData& data,
                             SceneBase& scene,
                             NS::Physics::PhysicsWorld& physics,
                             const ObjectFactoryFn& factory)
    {
        Clear();

        // 衝突は build のたびに Clear -> Add* -> BuildBroadphase で満たし直す。 古い衝突を残さない
        physics.Clear();

        // 参照照合窓口も同じ周期で空へ戻す。 旧 build の実体を指す登録を組み直しへ持ち越さない
        auto* refs = scene.GetSubsystem<ObjectRefSubsystem>();
        if (refs != nullptr)
            refs->Clear();

        m_objects.reserve(data.objects.size());
        physics.ReserveAabbs(data.objects.size());

        // 配置物の組み立ては呼出側の知識。 ファクトリの無い起動前 / テストでは何も組まない
        if (!factory)
            return;

        // 先に全 object を組んで永続 id を照合窓口へ登録し、 開始は後段でまとめて行う
        // OnStart で ObjectRef を解決する component が、 自分より後ろの object も引けるようにするため
        for (std::size_t objectIndex = 0; objectIndex < data.objects.size(); ++objectIndex)
        {
            const ObjectData& entry = data.objects[objectIndex];

            auto obj = factory(entry);
            if (!obj)
                continue; // 組み立てる component が無いオブジェクトはファクトリが nullptr を返す

            obj->AttachScene(&scene);
            if (refs != nullptr)
                refs->Register(entry.objectId, obj.get());

            m_objectSourceIndices.push_back(objectIndex);
            m_objects.push_back(std::move(obj));
        }

        for (auto& objPtr : m_objects)
        {
            GameObject* obj = objPtr.get();
            obj->OnStart();

            // collider component を全部登録する。 同型を重ねれば複合形状として当たりに効く
            for (Component* comp : obj->Components())
            {
                if (auto* sphere = ComponentCast<SphereColliderComponent>(comp))
                    physics.AddSphere(sphere->WorldSphere());
                else if (auto* capsule = ComponentCast<CapsuleColliderComponent>(comp))
                    physics.AddCapsule(capsule->WorldCapsule());
                else if (auto* box = ComponentCast<BoxColliderComponent>(comp))
                {
                    // 軸並行すなわち回転が 90° 刻みなら従来通り AABB、 傾いた箱だけ OBB
                    // 旧 grid は必ず軸並行なので AABB に落ち、 上を走る / 角に当たる手触りは不変
                    const NS::Physics::OBB obb = box->WorldOBB();
                    if (IsAxisAligned(obb))
                        physics.AddAabb(box->WorldAABB());
                    else
                        physics.AddObb(obb);
                }
                else if (auto* slope = ComponentCast<SlopeColliderComponent>(comp))
                    for (const auto& tri : slope->WorldTriangles())
                        physics.AddTriangle(tri);
            }
            // hazard の damage は固形 AABB とは別経路の毎フレーム重なり判定で効くため物理には積まない
            // water / deco は collider を持たないため当たり無し
        }

        // 生成直後は previous PRS が原点/単位回転のため Snapshot で current に揃える
        // 欠かすと InterpolatedWorldMatrix(alpha) が原点→配置先を補間し編集のたびに全配置物が振れる
        for (auto& obj : m_objects)
            obj->Root().Snapshot();

        physics.BuildBroadphase();
    }

    void SceneWorld::Clear()
    {
        // 配置物は生成の逆順で畳む。 依存し合う component の OnEndPlay 順序を生成時と対称に保つ
        for (auto it = m_objects.rbegin(); it != m_objects.rend(); ++it)
            (*it)->OnEndPlay();
        m_objects.clear();
        m_objectSourceIndices.clear();
    }

} // namespace NS::Scene
