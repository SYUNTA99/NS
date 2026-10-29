#include "Editor/LevelEditorController.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "Editor/GridMath.h"
#include "Editor/HitZoneColors.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/SlamAim.h"
#include "Game/PlayerActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/ClassObjs/BoxCollision.h"
#include "NSlib/CoreObject/ClassObjs/CapsuleCollision.h"
#include "NSlib/CoreObject/ClassObjs/Collider.h"
#include "NSlib/CoreObject/ClassObjs/HitSensor.h"
#include "NSlib/CoreObject/ClassObjs/MeshCollision.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/ClassObjs/SphereCollision.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/ClassObjs/VirtualCamera.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/Gfx/Render/DebugDraw.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    // カメラの視錐台を描く時の far。vcam の既定 1000 のままだと錐台が画面に収まらないので近くで切る
    constexpr float k_CameraGizmoFar = 8.0f;

    // 部品のうち動く体の当たり (Collider) を返す。持たなければ nullptr
    const NS::Collider* ColliderOf(const NS::SceneActor& object) noexcept
    {
        const NS::Collider* found = nullptr;
        NS::ForEachReflectedClassObj(&object, [&](const NS::ClassObjInfo&, const NS::Object* classObj) {
            if (found == nullptr)
            {
                found = NS::Cast<NS::Collider>(classObj);
            }
        });
        return found;
    }

    // アクター 1 体の当たり形状を線で描く。Box は回転込み OBB、球とカプセルは実形状
    void DrawCollisionWireframe(NS::Gfx::DebugShapes& shapes, NS::SceneActor& object, const NS::Color& color) noexcept
    {
        if (NS::BoxCollision* box = NS::Cast<NS::BoxCollision>(object.CollisionClassObj()))
        {
            shapes.OBB(box->WorldOBB(object.Root().WorldMatrix()), color);
        }
        else if (NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(object.CollisionClassObj()))
        {
            shapes.Sphere(sphere->WorldSphere(object.Root().WorldMatrix()), color);
        }
        else if (NS::CapsuleCollision* capsule = NS::Cast<NS::CapsuleCollision>(object.CollisionClassObj()))
        {
            NS::Phys::Capsule worldCapsule = capsule->WorldCapsule(object.Root().WorldMatrix());
            worldCapsule.axis.Normalize();
            shapes.Capsule(
                worldCapsule.center, worldCapsule.axis * worldCapsule.halfHeight, worldCapsule.radius, color);
        }
        else if (const NS::Collider* collider = ColliderOf(object))
        {
            // 移動が掃引するのと同じ、根を中心にした縦のカプセル。根の拡縮は掛けない
            const NS::Phys::Capsule bodyCapsule = collider->CapsuleAt(object.Root().Position());
            shapes.Capsule(bodyCapsule.center, bodyCapsule.axis * bodyCapsule.halfHeight, bodyCapsule.radius, color);
        }
        else if (NS::Cast<NS::MeshCollision>(object.CollisionClassObj()) != nullptr)
        {
            // メッシュの当たりは見た目の三角形そのもの。三角形は多いので、見た目のメッシュを包む箱を出す
            const NS::Model* renderer = object.ModelClassObj();
            if (renderer == nullptr || renderer->GetMesh()->LocalBounds() == nullptr)
            {
                return;
            }
            const NS::AABB& local = *renderer->GetMesh()->LocalBounds();
            const NS::Matrix world = object.Root().WorldMatrix();
            const NS::AffineDecomposition parts = NS::DecomposeAffine(world);
            const NS::Vector3 center = NS::Vector3::Transform(NS::Vector3{local.Center}, world);
            const NS::Vector3 half{local.Extents.x * std::abs(parts.scale.x),
                                   local.Extents.y * std::abs(parts.scale.y),
                                   local.Extents.z * std::abs(parts.scale.z)};
            shapes.OBB(NS::MakeOBB(center, parts.rotation, half), color);
        }
    }

    // アクター 1 体のヒットセンサーの形を線で描く。範囲 (落下死・ゴール) は地形の当たりを持たないので、ここで見せる
    void DrawSensorWireframe(NS::Gfx::DebugShapes& shapes, NS::SceneActor& object, const NS::Color& color) noexcept
    {
        const NS::HitSensorsObj* sensors = object.HitSensorsClassObj();
        if (sensors == nullptr)
        {
            return;
        }
        for (const NS::HitSensor* sensor : sensors->Sensors())
        {
            const NS::SensorVolume volume = sensor->WorldVolume(object.Root().WorldMatrix());
            if (volume.isBox)
            {
                shapes.OBB(volume.box, color);
                continue;
            }
            const NS::Vector3 center = (volume.a + volume.b) * 0.5f;
            const NS::Vector3 axis = (volume.b - volume.a) * 0.5f;
            if (axis.LengthSquared() > 0.0f)
            {
                shapes.Capsule(center, axis, volume.radius, color);
            }
            else
            {
                shapes.Sphere(NS::Sphere{center, volume.radius}, color);
            }
        }
    }

    // 正面の面の塗りの不透明度。外れの青に赤を重ねても、向こうの相手と床が透けて見える濃さ
    constexpr float k_HitFaceAlpha = 0.35f;
    // 直近の当たりの触れた点のマーカー。相手の表面に出るので、玉の模様を隠さない小ささ
    constexpr float k_HitTouchMarkerRadius = 0.08f;

    // 面の上の形を 1 つ描く。中心から縁の点へ扇に塗り、縁の点を線で結ぶ
    // 形と縁の点と面の置き方は HitZones の関数が出す。判定と同じ値から描くので、色の境目が判定とずれない
    void DrawHitFaceShape(NS::Gfx::DebugShapes& shapes,
                          const GL::Level::HitFaceFrame& frame,
                          const GL::Level::HitFaceShape& shape) noexcept
    {
        const std::vector<NS::Vector2> outline = GL::Level::HitFaceShapeOutline(shape);
        const NS::Color edgeColor = NS::Editor::HitZoneColor(shape.tier);
        NS::Color fillColor = edgeColor;
        fillColor.A(k_HitFaceAlpha);
        const NS::Vector3 center = GL::Level::HitFacePoint(frame, shape.centerU, shape.centerV);
        for (std::size_t i = 0; i < outline.size(); ++i)
        {
            const NS::Vector2& from = outline[i];
            const NS::Vector2& to = outline[(i + 1) % outline.size()];
            const NS::Vector3 a = GL::Level::HitFacePoint(frame, from.x, from.y);
            const NS::Vector3 b = GL::Level::HitFacePoint(frame, to.x, to.y);
            shapes.Triangle(center, a, b, fillColor);
            shapes.Line(a, b, edgeColor);
        }
    }

    // a→b を 0.5m を目安に等分し 1 区間おきに線を引いて点線にする。DebugShapes に破線が無いので描画側で
    // 間引く。辺長からセグメント数を出すので、長い辺でも刻みが粗くならない
    void DrawDashedLine(NS::Gfx::DebugShapes& shapes,
                        const NS::Vector3& a,
                        const NS::Vector3& b,
                        const NS::Color& color) noexcept
    {
        const float length = (b - a).Length();
        const int rawSegments = static_cast<int>(length / 0.5f);
        const int segments = std::max(rawSegments, 2);
        for (int i = 0; i < segments; i += 2)
        {
            const float t0 = static_cast<float>(i) / static_cast<float>(segments);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
            shapes.Line(NS::Vector3::Lerp(a, b, t0), NS::Vector3::Lerp(a, b, t1), color);
        }
    }

    // カメラ pose の視錐台を四角錐の点線で描く。視点から far 面 4 隅へ 4 本 + far 面の 4 辺で、向きと画角を見せる
    // target==position や up と視線が平行な縮退では基底が作れないので何も描かない
    void DrawCameraFrustum(NS::Gfx::DebugShapes& shapes,
                           const NS::CameraPose& pose,
                           float aspect,
                           const NS::Color& color) noexcept
    {
        NS::Vector3 forward = pose.target - pose.position;
        if (forward.LengthSquared() < 1e-6f)
        {
            return;
        }
        forward.Normalize();
        NS::Vector3 right = pose.up.Cross(forward);
        if (right.LengthSquared() < 1e-6f)
        {
            return;
        }
        right.Normalize();
        const NS::Vector3 up = forward.Cross(right);

        const float halfHeight = std::tan(pose.fovY.value * 0.5f) * k_CameraGizmoFar;
        const float halfWidth = halfHeight * aspect;
        const NS::Vector3 farCenter = pose.position + forward * k_CameraGizmoFar;
        const NS::Vector3 topLeft = farCenter + up * halfHeight - right * halfWidth;
        const NS::Vector3 topRight = farCenter + up * halfHeight + right * halfWidth;
        const NS::Vector3 bottomLeft = farCenter - up * halfHeight - right * halfWidth;
        const NS::Vector3 bottomRight = farCenter - up * halfHeight + right * halfWidth;

        DrawDashedLine(shapes, pose.position, topLeft, color);
        DrawDashedLine(shapes, pose.position, topRight, color);
        DrawDashedLine(shapes, pose.position, bottomLeft, color);
        DrawDashedLine(shapes, pose.position, bottomRight, color);
        DrawDashedLine(shapes, topLeft, topRight, color);
        DrawDashedLine(shapes, topRight, bottomRight, color);
        DrawDashedLine(shapes, bottomRight, bottomLeft, color);
        DrawDashedLine(shapes, bottomLeft, topLeft, color);
    }

    // 視点マーカーのワールド空間での半径。カメラから遠いほど半径を伸ばし、画面上の見かけサイズを一定に近づける
    [[nodiscard]] float CameraMarkerHalf(const NS::Vector3& center, const NS::Matrix& vp) noexcept
    {
        const float baseHalf = 0.3f;
        return baseHalf * NS::Editor::ScreenConstantScale(center, vp);
    }
} // namespace

void LevelEditorController::DrawSceneViewShapes(NS::Gfx::DebugShapes& shapes, const NS::Matrix& viewProjection) noexcept
{
    if (m_scene == nullptr)
    {
        return;
    }

    if (m_mode != Mode::Edit)
    {
        // 動いている形をそのまま追えるよう選択に関わらず全部出す
        RenderCollisionWireframes(shapes, true);
        RenderHitFaces(shapes);
        return;
    }

    m_editor.DrawCursorShapes(shapes);
    if (m_editTool == EditTool::StageBuild && m_stagePlacement)
    {
        m_stageBuilder.DrawPreview(shapes, *m_stagePlacement);
    }
    // 錐台の横幅はゲーム画面の比で出す。窓が無い時は RenderCameraGizmos が 16:9 で代える
    NS::Size2D viewport{};
    if (const NS::Application* app = NS::Application::Get())
    {
        viewport = app->Window().Size();
    }
    RenderCameraGizmos(shapes, viewProjection, viewport);
    RenderCollisionWireframes(shapes, false);
    RenderHitFaces(shapes);
    RenderSelectionOutlines(shapes);
}

void LevelEditorController::RenderCameraGizmos(NS::Gfx::DebugShapes& shapes,
                                               const NS::Matrix& viewProjection,
                                               NS::Size2D viewport) noexcept
{
    // edit 中、各カメラの視錐台を点線の四角錐で、視点位置を小箱で可視化する
    // 選択中は強調色にする。追従カメラは pose がプレイヤー基準なので、錐台はプレイ中に居る視点位置へ出る
    const NS::ActorList& objects = m_scene->Actors();
    // 錐台の横幅は実ビューポート比で出す。viewport が潰れている時だけ 16:9 目安へフォールバックする
    float aspect = 16.0f / 9.0f;
    if (viewport.height > 0)
    {
        aspect = static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
    }
    for (NS::SceneActor* object : objects)
    {
        GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(object);
        if (camera == nullptr)
        {
            continue;
        }
        NS::VirtualCamera* vcam = &camera->Vcam();
        const bool selected = (object->Id() == m_selectedActorId);
        NS::Color camColor{1.0f, 0.85f, 0.10f, 1.0f};
        if (selected)
        {
            camColor = NS::Color{1.0f, 0.55f, 0.10f, 1.0f};
        }

        const NS::CameraPose pose = vcam->EvaluatePose(1.0f);
        DrawCameraFrustum(shapes, pose, aspect, camColor);
        const float markerHalf = CameraMarkerHalf(pose.position, viewProjection);
        shapes.AABB(NS::AABB{pose.position, NS::Vector3{markerHalf, markerHalf, markerHalf}}, camColor);
    }
}

void LevelEditorController::RenderSelectionOutlines(NS::Gfx::DebugShapes& shapes) noexcept
{
    // ギズモが出るのは主対象だけなので、一緒に選んでいる分は枠で見せる
    if (m_selectionIds.size() < 2)
    {
        return;
    }

    const NS::Color color{1.0f, 0.65f, 0.15f, 1.0f};
    for (const NS::uint32 id : m_selectionIds)
    {
        if (id == m_selectedActorId)
        {
            continue;
        }
        NS::SceneActor* object = m_scene->Actors().FindByActorId(id);
        if (object == nullptr)
        {
            continue;
        }

        const NS::Matrix world = object->Root().WorldMatrix();
        const NS::AffineDecomposition parts = NS::DecomposeAffine(world);
        // 1m 立方の cube mesh の半サイズ 0.5 に拡縮を掛ける
        const NS::Vector3 half{
            std::abs(parts.scale.x) * 0.5f, std::abs(parts.scale.y) * 0.5f, std::abs(parts.scale.z) * 0.5f};
        shapes.OBB(NS::MakeOBB(world.Translation(), parts.rotation, half), color);
    }
}

void LevelEditorController::RenderCollisionWireframes(NS::Gfx::DebugShapes& shapes, bool all) noexcept
{
    const NS::Color color{0.35f, 1.0f, 0.45f, 1.0f};
    // センサーは当たりの緑と見分けが付く橙
    const NS::Color sensorColor{1.0f, 0.6f, 0.2f, 1.0f};

    // プレイ中は動いている形を追えるよう全部出す
    if (all)
    {
        for (NS::SceneActor* objPtr : m_scene->Actors())
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
        return;
    }

    // 編集中は選んだ分だけ。全部出すと線が重なって、どれの形か読み取れない
    for (const NS::uint32 id : m_selectionIds)
    {
        if (NS::SceneActor* objPtr = m_scene->Actors().FindByActorId(id))
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
    }
}

void LevelEditorController::RenderHitFaces(NS::Gfx::DebugShapes& shapes) noexcept
{
    PlayerActor* player = FindPlayer(m_scene->Actors());
    // 自機が居ない場面は半径 0 として、相手の輪郭の大きさで描く
    float playerRadius = 0.0f;
    NS::Vector3 ballCenter{};
    GL::Level::SlamLineTarget aim{};
    bool aiming = false;
    if (player != nullptr)
    {
        // 突進の玉は、狙う相手の探し方と同じ PlayerActor::SlamBallAt から引く
        const NS::Sphere ball = player->SlamBallAt(player->Root().Position());
        ballCenter = ball.center;
        playerRadius = ball.radius;
        aiming = player->TryGetAimTarget(aim);
    }
    const NS::Vector3 cameraPosition = m_editorCamera.Pose().position;

    for (NS::SceneActor* object : m_scene->Actors())
    {
        if (!object->IsAppeared())
        {
            continue;
        }
        const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(object);
        const NS::HitSensorsObj* sensors = object->HitSensorsClassObj();
        if (prop == nullptr || sensors == nullptr)
        {
            continue;
        }
        const NS::HitSensor* bodySensor = sensors->Find("Body");
        if (bodySensor == nullptr)
        {
            continue;
        }
        const GL::Level::HitZones& zones = prop->Zones();
        // 調べる対象から外した体は、突進が当たらないので面も出さない
        if (!bodySensor->IsValid())
        {
            continue;
        }
        const NS::SensorVolume body = bodySensor->WorldVolume(object->Root().WorldMatrix());
        NS::Vector3 direction = body.Center() - cameraPosition;
        if (player != nullptr)
        {
            direction = body.Center() - ballCenter;
        }
        if (aiming && aim.target == object)
        {
            direction = aim.direction;
        }
        // 真上や真下から見て水平の向きが決まらない相手は描かない
        GL::Level::HitFaceFrame frame;
        if (!GL::Level::MakeHitFaceFrame(body, direction, playerRadius, frame))
        {
            continue;
        }
        for (const GL::Level::HitFaceShape& shape : GL::Level::HitFaceShapes(zones.Face(), frame.bodyShape))
        {
            DrawHitFaceShape(shapes, frame, shape);
        }
    }

    if (player == nullptr)
    {
        return;
    }
    const GL::Level::ImpactRecord& impact = player->Resolver().Answer().lastImpact;
    if (impact.sequence == 0)
    {
        return;
    }
    shapes.Sphere(NS::Sphere{impact.surfacePoint, k_HitTouchMarkerRadius}, NS::Editor::HitZoneColor(impact.tier));
}
