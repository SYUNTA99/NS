#include "Editor/LevelEditorController.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedSubObjs.h"

#include "Editor/EditorObjects.h"
#include "Editor/GridMath.h"
#include "Editor/HitZoneColors.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/CompositeCommand.h"
#include "Editor/Undo/ObjectSnapshotCommand.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/FollowCameraActor.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/PropActor.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamAim.h"
#include "Game/PlayerActor.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/CoreObject/AssetManager.h"
#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/SubObjectEntry.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneCamera.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/CoreObject/SubObjects/BoxCollision.h"
#include "NSlib/CoreObject/SubObjects/CapsuleCollision.h"
#include "NSlib/CoreObject/SubObjects/Collider.h"
#include "NSlib/CoreObject/SubObjects/HitSensor.h"
#include "NSlib/CoreObject/SubObjects/MeshCollision.h"
#include "NSlib/CoreObject/SubObjects/Model.h"
#include "NSlib/CoreObject/SubObjects/SphereCollision.h"
#include "NSlib/CoreObject/SubObjects/ThirdPersonFollow.h"
#include "NSlib/CoreObject/SubObjects/Transform.h"
#include "NSlib/CoreObject/SubObjects/VirtualCamera.h"
#include "NSlib/CoreObject/System/CameraManager.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace
{
    // 部品の欄の値を JSON で読む。欄が無ければ値なしを返す
    nlohmann::json FieldJson(const NS::Object& comp, std::string_view fieldName)
    {
        const nlohmann::json fields = NS::SerializeSubObjectFields(comp);
        const nlohmann::json::const_iterator it = fields.find(std::string{fieldName});
        if (it == fields.end())
        {
            return nullptr;
        }
        return *it;
    }

    std::string RelativeToRoot(std::string_view absPath, std::string_view root)
    {
        const std::string absNorm = NS::OS::FileSystem::Normalize(absPath);
        const std::string rootNorm = NS::OS::FileSystem::Normalize(root);
        if (absNorm.size() > rootNorm.size() + 1 &&
            ::_strnicmp(absNorm.c_str(), rootNorm.c_str(), rootNorm.size()) == 0 && absNorm[rootNorm.size()] == '/')
        {
            return absNorm.substr(rootNorm.size() + 1);
        }
        return absNorm;
    }

    // カメラの視錐台を描く時の far。vcam の既定 1000 のままだと錐台が画面に収まらないので近くで切る
    constexpr float k_CameraGizmoFar = 8.0f;

    // 編集復帰の視点ブレンド秒。CameraManager の vcam 切替の既定 0.35 秒と揃え、モード切替の繋ぎを同じ感触にする
    constexpr float k_EditBlendSeconds = 0.35f;

    // 部品のうち動く体の当たり (Collider) を返す。持たなければ nullptr
    const NS::Collider* ColliderOf(const NS::SceneActor& object) noexcept
    {
        const NS::Collider* found = nullptr;
        NS::ForEachReflectedSubObj(object, [&](const NS::SubObjectDesc&, const NS::Object* subObject) {
            if (found == nullptr)
            {
                found = NS::Cast<NS::Collider>(subObject);
            }
        });
        return found;
    }

    // 配置物 1 体の当たり形状を線で描く。Box は回転込み OBB、球とカプセルは実形状
    void DrawCollisionWireframe(NS::Gfx::DebugShapes& shapes, NS::SceneActor& object, const NS::Color& color) noexcept
    {
        if (NS::BoxCollision* box = NS::Cast<NS::BoxCollision>(object.CollisionSubObj()))
        {
            shapes.OBB(box->WorldOBB(), color);
        }
        else if (NS::SphereCollision* sphere = NS::Cast<NS::SphereCollision>(object.CollisionSubObj()))
        {
            shapes.Sphere(sphere->WorldSphere(), color);
        }
        else if (NS::CapsuleCollision* capsule = NS::Cast<NS::CapsuleCollision>(object.CollisionSubObj()))
        {
            NS::Phys::Capsule worldCapsule = capsule->WorldCapsule();
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
        else if (NS::Cast<NS::MeshCollision>(object.CollisionSubObj()) != nullptr)
        {
            // メッシュの当たりは見た目の三角形そのもの。三角形は多いので、見た目のメッシュを包む箱を出す
            const NS::Model* renderer = object.ModelSubObj();
            if (renderer == nullptr || renderer->GetMesh() == nullptr)
            {
                return;
            }
            const NS::AABB& local = renderer->GetMesh()->LocalBounds();
            const NS::Matrix world = object.Root().WorldMatrix();
            const NS::AffineDecomposition parts = NS::DecomposeAffine(world);
            const NS::Vector3 center = NS::Vector3::Transform(NS::Vector3{local.Center}, world);
            const NS::Vector3 half{local.Extents.x * std::abs(parts.scale.x),
                                   local.Extents.y * std::abs(parts.scale.y),
                                   local.Extents.z * std::abs(parts.scale.z)};
            shapes.OBB(NS::MakeOBB(center, parts.rotation, half), color);
        }
    }

    // 配置物 1 体のヒットセンサーの形を線で描く。範囲 (落下死・ゴール) は地形の当たりを持たないので、ここで見せる
    void DrawSensorWireframe(NS::Gfx::DebugShapes& shapes, NS::SceneActor& object, const NS::Color& color) noexcept
    {
        for (const NS::HitSensor* sensor : {object.BodySensorSubObj(), object.AttackSensorSubObj()})
        {
            if (sensor == nullptr)
            {
                continue;
            }
            const NS::SensorVolume volume = sensor->WorldVolume();
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
    // 直近の当たりの触れた点の印。相手の表面に出るので、玉の模様を隠さない小ささ
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

    // 1 本なら包まずそのまま返す。CompositeCommand を挟むのは複数を 1 回の undo で往復させたい時だけ
    std::unique_ptr<NS::Editor::ICommand> MakeUndoUnit(std::vector<std::unique_ptr<NS::Editor::ICommand>> commands)
    {
        if (commands.size() == 1)
        {
            return std::move(commands.front());
        }
        return std::make_unique<NS::Editor::CompositeCommand>(std::move(commands));
    }
} // namespace

LevelEditorController::LevelEditorController(NS::Scene* scene) noexcept : m_scene(scene), m_applier(scene) {}

LevelEditorController::~LevelEditorController() = default;

bool LevelEditorController::PlayPaused() const noexcept
{
    return m_scene != nullptr && m_scene->IsSimulationPaused();
}

void LevelEditorController::TogglePlayPause() noexcept
{
    if (m_scene != nullptr)
    {
        m_scene->SetSimulationPaused(!m_scene->IsSimulationPaused());
        // 固定したままだと Inspector を触れず、プレイ中に値を調整する動線が消える。止めている間は解く
        // 再開は Game が映っている時だけ固定へ戻す。Scene を見ている時はタブを押せるよう出したまま
        if (m_mode == Mode::Play)
        {
            ApplyPlayCursor(NS::Editor::CursorAfterPauseToggle(
                {.paused = m_scene->IsSimulationPaused(), .gameViewInFront = !m_gameViewHidden}));
        }
    }
}

void LevelEditorController::RecaptureCursorOnGameClick(bool gameImageClicked) noexcept
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    const bool recapture = NS::Editor::ShouldRecaptureCursor({.playMode = m_mode == Mode::Play,
                                                              .paused = PlayPaused(),
                                                              .cursorReleased = app->Window().IsCursorVisible(),
                                                              .gameImageClicked = gameImageClicked});
    if (recapture)
    {
        ApplyPlayCursor(NS::Editor::PlayCursor::Captured);
    }
}

void LevelEditorController::ApplyPlayCursor(NS::Editor::PlayCursor cursor) noexcept
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    app->SetCursorCaptured(cursor == NS::Editor::PlayCursor::Captured);
}

const NS::ActorList& LevelEditorController::Objects() const noexcept
{
    return m_scene->Objects();
}

NS::CameraManager* LevelEditorController::Cameras() const noexcept
{
    if (m_scene == nullptr)
    {
        return nullptr;
    }
    return m_scene->GetCameraManager();
}

NS::SceneCamera* LevelEditorController::MainCamera() const noexcept
{
    if (m_scene == nullptr)
    {
        return nullptr;
    }
    return m_scene->MainCamera();
}

void LevelEditorController::Setup(NS::UI::ImGuiContext* imgui)
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr || m_scene == nullptr)
    {
        return;
    }

    // 編集の投影設定で、遠景を 5000 まで見せ near 0.1 は既定
    // far は EditorCamera の k_MaxDistance より広く取り、最大ズームアウトでも地形を映す
    m_editorCamera.SetNearPlane(0.1f);
    m_editorCamera.SetFarPlane(5000.0f);
    m_editorCamera.SetFovY(NS::ToRadians(NS::Degrees{60.0f}));

    // 初期視点はプレイヤーの位置を中心に少し引いた位置から見下ろす。不在なら原点
    NS::SceneActor* bootPlayer = FindPlayer(m_scene->Objects());
    NS::Vector3 startCenter{0.0f, 0.0f, 0.0f};
    if (bootPlayer != nullptr)
    {
        startCenter = bootPlayer->Root().Position();
    }
    m_editorCamera.SetCenter(startCenter);
    // 起動直後は出現地点の block を真ん中近めに見せる距離。1m cube が画面の十数 % を占める
    m_editorCamera.SetDistance(5.0f);

    // 保存は実体から作り、読込は取込関数がデータを実体へ写して用済みにする
    m_editor.SetCaptureLevelFn([this]() { return m_scene->ToJson(); });
    m_editor.SetLoadLevelFn([this](nlohmann::json&& fresh) { m_scene->ResetFromJson(std::move(fresh)); });
    // セル編集と取り消しは適用処理を通す。照会・採番は実体で行う
    m_editor.SetApplier(&m_applier);
    m_editor.SetFindCellObjectFn([this](NS::int16 x, NS::int16 y, NS::int16 z) {
        return NS::Editor::FindObjectIdAtCell(m_scene->Objects(), x, y, z);
    });
    m_editor.SetCellOccupiedFn([this](NS::int16 x, NS::int16 y, NS::int16 z) {
        return NS::Editor::HasPlacedObjectAtCell(m_scene->Objects(), x, y, z);
    });
    m_editor.SetCollectCellsFn([this]() {
        std::vector<NS::Editor::EditorMode::CellCoord> cells;
        cells.reserve(m_scene->Objects().ObjectCount());
        for (NS::SceneActor* objPtr : m_scene->Objects())
        {
            NS::SceneActor& object = *objPtr;
            if (!NS::Editor::IsCellBrushObject(object))
            {
                continue;
            }
            cells.push_back(NS::Editor::EditorMode::CellCoord{
                NS::Editor::ObjectCellX(object), NS::Editor::ObjectCellY(object), NS::Editor::ObjectCellZ(object)});
        }
        return cells;
    });
    m_editor.SetAllocateIdFn([this]() { return m_scene->Objects().AllocateObjectId(); });
    m_editor.SetInput(&app->Input());
    m_editor.SetImGui(imgui);
    m_editor.SetSceneCamera(MainCamera());
    m_editor.SetActive(true);
    // 読み込みで組み上がり済なので、初回 Tick の貼り直しを省く
    m_editor.ClearLevelDirty();

    // 起動シーンが実在するのに読めていない時は印す。終了保存が元ファイルを潰さず退避名へ逃げる
    if (const std::optional<std::string> bootPath = NS::Editor::BuildLevelPath("Scenes/new_scene"))
    {
        if (NS::OS::FileSystem::Exists(*bootPath))
        {
            nlohmann::json probe;
            if (!NS::LoadSceneFromJsonFile(probe, *bootPath))
            {
                m_editor.MarkBootLevelLoadFailed();
            }
        }
    }

    m_gizmo.SetInput(&app->Input());
    m_gizmo.SetImGui(imgui);

    // scene は読み込みからプレイが回っている。プレイを終えて操作系を休止させた編集モードへ切替える
    LeavePlayForEdit();
    m_mode = Mode::Edit;
    // 選択候補の生ポインタは組み直しの後に集める。先に集めると破棄済みの相手を指す
    RefreshGizmoSelectables();
}

void LevelEditorController::Teardown()
{
    // ギズモは配置物の Transform を非所有参照するので、scene 破棄前に選択を外す
    m_gizmo.ClearSelection();
    m_selectablePtrs.clear();
    m_selectablePickable.clear();
}

void LevelEditorController::EnterPlay() noexcept
{
    if (m_mode == Mode::Play)
    {
        return;
    }
    m_mode = Mode::Play;
    // モード遷移へ押しっぱなしを持ち越さないよう消す
    if (NS::Application* app = NS::Application::Get())
    {
        app->Input().Keyboard().ClearState();
        app->Input().Mouse().ClearState();
    }
    // 引いている途中の変形を確定してギズモを休ませる。引きを持ち越すと、編集へ戻る組み直しで消えた物を読む
    CommitTransformEdit();
    m_dragFollowers.clear();
    m_gizmoWasDragging = false;
    m_gizmo.SetActive(false);
    // 実体が唯一の出所なので組み直しは要らない。編集で動いた当たりだけ張り直してプレイへ入る
    m_scene->SyncPhysics();

    // 凍結を取り、そこから走行を最初から。プレイの間の判定と編集復帰の姿はこの凍結を読む
    // 手順は出荷と同じ CourseDirector::StartCourse の持ち物
    if (GL::Level::CourseDirector* director = NS::GetOrCreateSceneSystem<GL::Level::CourseDirector>(m_scene))
    {
        director->StartCourse();
    }
    // 編集の自由視点からプレイ視点へ、vcam 切替と同じブレンドで繋ぐ
    if (NS::CameraManager* cameras = Cameras())
    {
        cameras->BeginBlendFrom(m_editorCamera.Pose());
    }
    m_scene->SetSimulationEnabled(true);

    // プレイ突入はカーソルを消し、マウスを相対モードにして視点操作をカーソル位置から切り離す
    ApplyPlayCursor(NS::Editor::PlayCursor::Captured);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    m_editor.SetActive(false);
}

void LevelEditorController::EnterEdit() noexcept
{
    if (m_mode == Mode::Edit)
    {
        return;
    }
    m_mode = Mode::Edit;
    // モード遷移へ押しっぱなしを持ち越さないよう消す
    if (NS::Application* app = NS::Application::Get())
    {
        app->Input().Keyboard().ClearState();
        app->Input().Mouse().ClearState();
    }
    // プレイを終え、凍結スナップショットから編集の姿へ組み直す
    LeavePlayForEdit();
    // プレイ視点から自由視点へ繋ぐ。始点は直前まで実カメラに書かれていた pose
    if (NS::CameraManager* cameras = Cameras())
    {
        m_editBlendFrom = cameras->LastPose();
        m_editBlendElapsed = 0.0f;
        m_editBlending = true;
    }
    // 編集復帰の組み直しを跨いだ選択を、id から現在のオブジェクトへ貼り直してから編集へ戻る
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    m_editor.SetActive(true);
}

void LevelEditorController::RequestStepFrame() noexcept
{
    if (m_mode != Mode::Play || m_scene == nullptr)
    {
        return;
    }
    m_scene->StepSimulation();
}

void LevelEditorController::LeavePlayForEdit()
{
    if (m_scene == nullptr)
    {
        return;
    }
    m_scene->SetSimulationEnabled(false);

    // プレイで変わった配置を編集へ持ち込まないため、開始時の控えから復元する
    nlohmann::json baseline = m_scene->PlayBaseline();
    m_scene->ResetFromJson(std::move(baseline));

    if (NS::Application* app = NS::Application::Get())
    {
        app->SetCursorCaptured(false);
    }
}

void LevelEditorController::SetGameView(int x, int y, int width, int height, bool hovered) noexcept
{
    m_gameViewRect = NS::Editor::ViewRect{x, y, width, height};
    m_gameViewRectValid = true;
    m_gameViewHovered = hovered;
    m_gameViewHidden = false;
    // 窓の中心だと別のパネルの上へ乗ることがある。プレイ中は Game ビューの中心へ留める
    if (m_mode == Mode::Play)
    {
        if (NS::Application* app = NS::Application::Get())
        {
            app->Window().SetCursorLockPoint(x + width / 2, y + height / 2);
        }
    }
    // 編集入力は表示矩形から光線を飛ばす。カーソルが重なっていない間は配置位置を表示しない
    m_editor.SetViewRect(m_gameViewRect);
    m_editor.SetViewHovered(hovered);
}

void LevelEditorController::ClearGameView() noexcept
{
    // 全画面描画では予備の矩形を使い、カーソルが重なっている状態にする
    m_gameViewRectValid = false;
    m_gameViewHovered = true;
    m_gameViewHidden = false;
    m_editor.SetViewRect(CurrentViewRect());
    m_editor.SetViewHovered(true);
}

void LevelEditorController::HideGameView() noexcept
{
    // 前面のパネルが裏へ隠れた。配置カーソルと編集オーバーレイを止める
    m_gameViewHidden = true;
    m_gameViewHovered = false;
    m_editor.SetViewHovered(false);
}

NS::Editor::ViewRect LevelEditorController::CurrentViewRect() const noexcept
{
    if (m_gameViewRectValid)
    {
        return m_gameViewRect;
    }
    // 未設定時は全画面をフォールバックとする。ウィンドウ不在は 0 サイズ
    NS::Editor::ViewRect full{};
    if (NS::Application* app = NS::Application::Get())
    {
        const NS::Size2D size = app->Window().Size();
        full.width = size.width;
        full.height = size.height;
    }
    return full;
}

void LevelEditorController::TickPlaySceneView(const NS::Editor::EditorCameraInput& input) noexcept
{
    // プレイ中に Scene タブへ自由視点を映すフレームだけ効かせる。編集モード中は実カメラを触らない
    // 描画視点は SceneViewPose が自由視点の pose を渡すので、ここは入力適用だけでよい
    if (m_mode != Mode::Play)
    {
        return;
    }
    m_editorCamera.ApplyInput(input);
}

std::optional<NS::CameraPose> LevelEditorController::SceneViewPose() noexcept
{
    // 編集中は実カメラ (TickEdit が自由視点の pose を書く) 任せ。プレイ中だけ自由視点を上書きする
    if (m_mode != Mode::Play)
    {
        return std::nullopt;
    }
    return m_editorCamera.Pose();
}

std::optional<NS::CameraPose> LevelEditorController::GameViewPose() noexcept
{
    // プレイ中は CameraManager (follow・ブレンド維持) 任せ。編集中だけゲームカメラを上書きする
    if (m_mode == Mode::Play || Cameras() == nullptr)
    {
        return std::nullopt;
    }
    return Cameras()->EvaluateTopPose(1.0f);
}

nlohmann::json LevelEditorController::SceneSnapshot() const
{
    if (m_scene == nullptr)
    {
        return NS::MakeSceneJson();
    }
    return m_scene->ToJson();
}

void LevelEditorController::SetSceneViews(std::vector<NS::SceneView> views)
{
    if (m_scene != nullptr)
    {
        m_scene->SetSceneViews(std::move(views));
    }
}

void LevelEditorController::Tick()
{
    // プレイ中のクリア / 死亡は応答部品が出荷と同じ手順で完結させる
    // editor は割り込まず、編集へ戻るのは Tab / Pause modal の明示操作だけ
    if (m_mode == Mode::Edit)
    {
        TickEdit();
        return;
    }

    // Esc はカーソルの解放に使い、パネルを操作する途中でプレイを終了させない
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }
    if (app->Input().Keyboard().IsPressed(NS::OS::Key::Escape) && !app->Window().IsCursorVisible())
    {
        ApplyPlayCursor(NS::Editor::PlayCursor::Released);
    }
}

void LevelEditorController::TickEdit()
{
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }

    // F5 のシェーダ再読込は編集時だけ。プレイ時は UI 表示の切替に使う
    if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::OS::Key::F5))
    {
        app->Assets().ReloadAllShaders();
    }

    // Esc: Object モードで選択中ならまず選択解除に使い終了させない
    if (app->Input().Keyboard().IsPressed(NS::OS::Key::Escape))
    {
        if (m_objectToolActive && m_gizmo.Selected() != nullptr)
        {
            // 選択の id ごと外す。ギズモだけ外すと、次のフレームに id から選び直される
            SelectObjectById(NS::k_NoObjectId);
            return;
        }
        NS::Application::Quit();
        return;
    }

    m_editorCamera.Tick();

    // 自由視点を更新して実カメラへ反映し、ギズモと編集の光線判定に同じ視点を使う
    // 編集中は世界が止まり CameraManager が実カメラを書かないので、この書き込みが上書きされずに残る
    if (NS::SceneCamera* camera = MainCamera())
    {
        NS::CameraPose pose = m_editorCamera.Pose();
        if (m_editBlending)
        {
            m_editBlendElapsed += NS::OS::FrameTimer::FixedDelta();
            const float t = std::min(m_editBlendElapsed / k_EditBlendSeconds, 1.0f);
            const float eased = NS::SmoothStep(t);
            pose = NS::CameraPose::Lerp(m_editBlendFrom, pose, eased);
            if (t >= 1.0f)
            {
                m_editBlending = false;
            }
        }
        camera->ApplyPose(pose);
    }

    // 同時に 1 モードだけがクリックと R を消費する。Object 中は grid 入力を抑制しギズモへ回す
    // モード切替は Editor の UI ボタンが SetObjectToolActive で入れる。Tab は Edit↔Play 専用
    const bool objectMode = m_objectToolActive;
    m_gizmo.SetActive(objectMode);
    m_editor.SetInputSuppressed(objectMode);

    if (objectMode && Cameras())
    {
        // 追従カメラの Root を実プレイ視点位置へ寄せてからギズモを回す。判定箱とギズモがその位置に出る
        SyncFollowCameraPoses();

        // 組み直しで破棄された配置物を参照しないよう、候補と選択を現在の実体へ引き直す
        RefreshGizmoSelectables();
        ResolveSelectionFromId();

        const NS::Matrix vp = Cameras()->ViewProjection();
        const bool wasDragging = m_gizmoWasDragging;
        m_gizmo.Tick(vp, CurrentViewRect(), m_gameViewHovered);

        // ビューポートでのギズモ選択変化を選択 id と Inspector が見る派生添字へ追従させる
        CaptureSelectionFromGizmo();

        // 追従カメラを掴んでいたら Root 位置を初期姿勢へ逆算し ThirdPersonFollow へ書き戻す。位置の書き戻しはこちら
        ApplyFollowCameraGizmoDrag();

        // ドラッグ開始で baseline を控え、終了で変わった分のスナップショットを履歴へ積む。grid undo と同じ経路
        // 追従カメラの引きは初期姿勢の欄を変える。控えは部品の値まで写すので同じ道で積める
        const bool nowDragging = m_gizmo.IsDragging();
        if (!wasDragging && nowDragging)
        {
            BeginTransformEdit();
            // 掴んだ瞬間はまだ動いていない。ここで控えれば差分の基準が揃う
            CaptureDragFollowers();
        }
        else if (wasDragging && !nowDragging)
        {
            CommitTransformEdit();
            m_dragFollowers.clear();
        }
        // ギズモは主対象しか動かさないので、残りの選択はここで追わせる
        if (nowDragging)
        {
            ApplyDragToFollowers();
        }
        m_gizmoWasDragging = nowDragging;
    }
    else if (m_transformEditing)
    {
        // Object モードを抜けても未確定の変形があれば確定し、記録されない変更を残さない
        CommitTransformEdit();
        m_gizmoWasDragging = false;
    }

    // 掴んでいる間は選択の貼り直しを飛ばすので、undo が旧 Transform を指す
    m_editor.SetUndoRedoSuppressed(m_gizmo.IsDragging());
    m_editor.Tick();
    if (m_editor.IsLevelDirty())
    {
        // grid 編集・undo・読込は applier / reload が配置物を組み直し済。候補と gizmo を今の実体へ貼り直す
        // 消えた選択や読込での id 振り直しは解決が自然に外す
        RefreshGizmoSelectables();
        ResolveSelectionFromId();
        m_editor.ClearLevelDirty();
    }
}

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

void LevelEditorController::Render()
{
    // プレイ中に Scene タブへ出す図形は DrawSceneViewShapes が積む。ImGui へ重ねる物は無い
    if (m_mode != Mode::Edit)
    {
        return;
    }

    m_editor.RenderCursorPreview();
    // Object モードはブラシを置かないので Build モードの時だけ出す
    // Game ビュー前面などで編集ビューが隠れているフレームは、ゲーム画面へ被せないよう出さない
    if (!ObjectToolActive() && !m_gameViewHidden)
    {
        m_editor.Palette().Render(CurrentViewRect());
    }
    // Object モードのギズモは最前面の drawlist に重ねる
    if (m_gizmo.IsActive() && Cameras())
    {
        m_gizmo.Render(Cameras()->ViewProjection(), CurrentViewRect());
    }
}

void LevelEditorController::SetObjectToolActive(bool active) noexcept
{
    if (active == m_objectToolActive)
    {
        return;
    }
    m_objectToolActive = active;
    if (!active)
    {
        SelectObjectById(NS::k_NoObjectId);
    }
}

std::size_t LevelEditorController::SelectedObjectIndex() const noexcept
{
    if (m_selectedObjectId == NS::k_NoObjectId)
    {
        return NS::k_NoObjectIndex;
    }
    const NS::ActorList& objects = m_scene->Objects();
    const std::size_t index = objects.IndexOfObjectId(m_selectedObjectId);
    if (index == objects.ObjectCount())
    {
        return NS::k_NoObjectIndex;
    }
    return index;
}

bool LevelEditorController::HasInspectableSelection() const noexcept
{
    return m_selectedObjectId != NS::k_NoObjectId && m_scene->Objects().FindByObjectId(m_selectedObjectId) != nullptr;
}

NS::SceneActor* LevelEditorController::SelectedObjectActor() noexcept
{
    // 選択の真実は永続 id。player も含め全配置物が ActorList に居るので id で引く
    if (m_selectedObjectId == NS::k_NoObjectId)
    {
        return nullptr;
    }
    return m_scene->Objects().FindByObjectId(m_selectedObjectId);
}

bool LevelEditorController::SelectedIsPlayerObject() const noexcept
{
    NS::SceneActor* player = FindPlayer(m_scene->Objects());
    return player != nullptr && m_selectedObjectId != NS::k_NoObjectId && m_selectedObjectId == player->Id();
}

void LevelEditorController::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectablePickable.clear();
    m_selectablePtrs.reserve(m_scene->Objects().ObjectCount());
    m_selectablePickable.reserve(m_scene->Objects().ObjectCount());

    // 可視メッシュを持つ候補は 1、見えないカメラ等は 0。ギズモは 1 の候補を先に選ぶ
    // 全配置物をギズモ候補に積む。判定箱はギズモがクリックの時に配置物から引く
    for (NS::SceneActor* object : m_scene->Objects())
    {
        if (object->IsTransient())
        {
            continue;
        }
        m_selectablePtrs.push_back(object);
        m_selectablePickable.push_back(static_cast<NS::uint8>(object->ModelSubObj() != nullptr));
    }

    m_gizmo.SetSelectableObjects(m_selectablePtrs, m_selectablePickable);
}

NS::ThirdPersonFollow* LevelEditorController::SelectedFollowCamera() noexcept
{
    if (GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(SelectedObjectActor()))
    {
        return &camera->Vcam();
    }
    return nullptr;
}

void LevelEditorController::SyncFollowCameraPoses()
{
    // 追従カメラは位置を持たないので、edit 中は実プレイの視点位置へ Root を寄せて frustum / pick / ギズモを出す
    // ドラッグ中の選択カメラだけは gizmo が Root を握るため触らず、その位置を初期姿勢へ逆算する側に任せる
    const NS::ActorList& objects = m_scene->Objects();
    for (NS::SceneActor* object : objects)
    {
        GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(object);
        if (camera == nullptr)
        {
            continue;
        }
        NS::ThirdPersonFollow* follow = &camera->Vcam();
        if (m_gizmo.IsDragging() && object->Id() == m_selectedObjectId)
        {
            continue;
        }
        object->Root().SetPosition(follow->EvaluatePose(1.0f).position);
    }
}

void LevelEditorController::ApplyFollowCameraGizmoDrag()
{
    // ドラッグ中の追従カメラは、gizmo が動かした Root 位置から初期姿勢の方位角・仰角/距離を逆算して
    // 実体の部品へ書き戻す。Root 位置は初期姿勢由来なので保存対象は部品側になる
    if (!m_gizmo.IsDragging())
    {
        return;
    }
    NS::ThirdPersonFollow* follow = SelectedFollowCamera();
    if (follow == nullptr)
    {
        return;
    }
    NS::SceneActor* go = SelectedObjectActor();
    if (go == nullptr)
    {
        return;
    }
    follow->SetInitialPoseFromCameraPosition(go->Root().Position());
}

void LevelEditorController::SelectObjectById(NS::uint32 id) noexcept
{
    m_selectionIds.clear();
    if (id != NS::k_NoObjectId)
    {
        m_selectionIds.push_back(id);
    }
    SetPrimarySelection(id);
}

bool LevelEditorController::IsObjectSelected(NS::uint32 id) const noexcept
{
    if (id == NS::k_NoObjectId)
    {
        return false;
    }
    return std::find(m_selectionIds.begin(), m_selectionIds.end(), id) != m_selectionIds.end();
}

void LevelEditorController::ToggleObjectSelection(NS::uint32 id) noexcept
{
    if (id == NS::k_NoObjectId)
    {
        return;
    }

    const std::vector<NS::uint32>::iterator it = std::find(m_selectionIds.begin(), m_selectionIds.end(), id);
    if (it != m_selectionIds.end())
    {
        m_selectionIds.erase(it);
        if (m_selectedObjectId != id)
        {
            return;
        }
        // 主対象を外したので、残っている中の直近へ譲る
        NS::uint32 next = NS::k_NoObjectId;
        if (!m_selectionIds.empty())
        {
            next = m_selectionIds.back();
        }
        SetPrimarySelection(next);
        return;
    }

    m_selectionIds.push_back(id);
    SetPrimarySelection(id);
}

void LevelEditorController::SelectObjects(std::vector<NS::uint32> ids, NS::uint32 primary) noexcept
{
    m_selectionIds = std::move(ids);
    if (primary != NS::k_NoObjectId && !IsObjectSelected(primary))
    {
        m_selectionIds.push_back(primary);
    }
    SetPrimarySelection(primary);
}

void LevelEditorController::SetPrimarySelection(NS::uint32 id) noexcept
{
    m_selectedObjectId = id;

    if (id == NS::k_NoObjectId)
    {
        m_gizmo.ClearSelection();
        m_lastGizmoSelected = nullptr;
        return;
    }

    // ハンドルを出すため Object ツールへ切替える。Build のままだとギズモが描かれない
    SetObjectToolActive(true);

    // player も含め全配置物が ActorList に居る。選択した Root を id で引いてギズモへ貼る
    if (NS::SceneActor* go = m_scene->Objects().FindByObjectId(id))
    {
        m_gizmo.SetSelected(&go->Root());
        m_lastGizmoSelected = m_gizmo.Selected();
        return;
    }
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::RenderCameraGizmos(NS::Gfx::DebugShapes& shapes,
                                               const NS::Matrix& viewProjection,
                                               NS::Size2D viewport) noexcept
{
    // edit 中、各カメラの視錐台を点線の四角錐で、視点位置を小箱で可視化する
    // 選択中は強調色にする。追従カメラは pose がプレイヤー基準なので、錐台はプレイ中に居る視点位置へ出る
    const NS::ActorList& objects = m_scene->Objects();
    // 錐台の横幅は実ビューポート比で出す。viewport が潰れている時だけ 16:9 目安へフォールバックする
    const float aspect = [viewport]() -> float {
        if (viewport.height > 0)
        {
            return static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
        }
        return 16.0f / 9.0f;
    }();
    for (NS::SceneActor* object : objects)
    {
        GL::Level::FollowCameraActor* camera = NS::Cast<GL::Level::FollowCameraActor>(object);
        if (camera == nullptr)
        {
            continue;
        }
        NS::VirtualCamera* vcam = &camera->Vcam();
        const bool selected = (object->Id() == m_selectedObjectId);
        const NS::Color camColor = [selected]() -> NS::Color {
            if (selected)
            {
                return NS::Color{1.0f, 0.55f, 0.10f, 1.0f};
            }
            return NS::Color{1.0f, 0.85f, 0.10f, 1.0f};
        }();

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
        if (id == m_selectedObjectId)
        {
            continue;
        }
        NS::SceneActor* object = m_scene->Objects().FindByObjectId(id);
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
        for (NS::SceneActor* objPtr : m_scene->Objects())
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
        return;
    }

    // 編集中は選んだ分だけ。全部出すと線が重なって、どれの形か読み取れない
    for (const NS::uint32 id : m_selectionIds)
    {
        if (NS::SceneActor* objPtr = m_scene->Objects().FindByObjectId(id))
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
    }
}

void LevelEditorController::RenderHitFaces(NS::Gfx::DebugShapes& shapes) noexcept
{
    PlayerActor* player = FindPlayer(m_scene->Objects());
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

    for (NS::SceneActor* object : m_scene->Objects())
    {
        if (!object->IsAppeared())
        {
            continue;
        }
        const GL::Level::PropActor* prop = NS::Cast<GL::Level::PropActor>(object);
        const NS::HitSensor* bodySensor = object->BodySensorSubObj();
        if (prop == nullptr || bodySensor == nullptr)
        {
            continue;
        }
        const GL::Level::HitZones& zones = prop->Zones();
        // 調べる対象から外した体は、突進が当たらないので面も出さない
        if (!bodySensor->IsValid())
        {
            continue;
        }
        const NS::SensorVolume body = bodySensor->WorldVolume();
        NS::Vector3 direction = body.Center() - cameraPosition;
        if (player != nullptr)
        {
            direction = body.Center() - ballCenter;
        }
        if (aiming && aim.target.id == object->Id())
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

void LevelEditorController::CaptureSelectionFromGizmo() noexcept
{
    NS::Transform* selected = m_gizmo.Selected();
    // 一覧で選んだ非 gizmo 選択を毎フレーム潰さないため前フレームと同じなら据え置き
    if (selected == m_lastGizmoSelected)
    {
        return;
    }
    m_lastGizmoSelected = selected;
    if (selected == nullptr)
    {
        m_selectionIds.clear();
        m_selectedObjectId = NS::k_NoObjectId;
        return;
    }
    // player も ActorList に居るので、ビューポートのピックと同じ経路で引ける
    for (NS::SceneActor* object : m_scene->Objects())
    {
        if (&object->Root() == selected)
        {
            // ビューポートのクリックは 1 体に絞る。複数選択はヒエラルキー側の Ctrl / Shift で組む
            m_selectedObjectId = object->Id();
            m_selectionIds.assign(1, m_selectedObjectId);
            return;
        }
    }
    m_selectionIds.clear();
    m_selectedObjectId = NS::k_NoObjectId;
}

void LevelEditorController::ResolveSelectionFromId() noexcept
{
    // SetSelected が進行中ドラッグを切ってしまうのでドラッグ中は gizmo の選択を貼り直さない
    if (m_gizmo.IsDragging())
    {
        return;
    }

    // 選択 id が現存する配置物を指すなら gizmo に貼り直す。不在なら gizmo を外す
    // player も ActorList に居るので id で引ける。rebuild を跨いでも掴める状態を保つ
    if (m_selectedObjectId != NS::k_NoObjectId)
    {
        if (NS::SceneActor* go = m_scene->Objects().FindByObjectId(m_selectedObjectId))
        {
            NS::Transform* root = &go->Root();
            if (m_gizmo.Selected() != root)
            {
                m_gizmo.SetSelected(root);
            }
            m_lastGizmoSelected = root;
            return;
        }
    }
    if (m_gizmo.Selected() != nullptr)
    {
        m_gizmo.ClearSelection();
    }
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SetSelectedFreePosition(NS::Vector3 position)
{
    // 実体の Root を直接動かす。取り消し の記録は CommitTransformEdit、当たりの追従は SyncPhysics が担う
    if (NS::SceneActor* go = SelectedObjectActor())
    {
        go->Root().SetPosition(position);
        MirrorRootEditToBaseline(*go, NS::k_PositionFieldName);
    }
}

void LevelEditorController::SetSelectedFreeRotation(NS::Quaternion rotation)
{
    if (NS::SceneActor* go = SelectedObjectActor())
    {
        go->Root().SetRotation(rotation);
        MirrorRootEditToBaseline(*go, NS::k_RotationFieldName);
    }
}

void LevelEditorController::SetSelectedFreeScale(NS::Vector3 scale)
{
    // ImGui の入力で 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
    constexpr float k_MinScale = 0.01f;
    scale.x = std::max(scale.x, k_MinScale);
    scale.y = std::max(scale.y, k_MinScale);
    scale.z = std::max(scale.z, k_MinScale);
    if (NS::SceneActor* go = SelectedObjectActor())
    {
        go->Root().SetScale(scale);
        MirrorRootEditToBaseline(*go, NS::k_ScaleFieldName);
    }
}

void LevelEditorController::MirrorRootEditToBaseline(NS::SceneActor& actor, std::string_view fieldName)
{
    const NS::SubObjectDesc* rootRow = nullptr;
    NS::ForEachReflectedSubObj(actor, [&](const NS::SubObjectDesc& field, const NS::Object* part) {
        if (part == &actor.Root())
        {
            rootRow = &field;
        }
    });
    if (rootRow != nullptr)
    {
        MirrorPlayEditToBaseline(actor, *rootRow, fieldName);
    }
}

void LevelEditorController::MirrorPlayEditToBaseline(const NS::SceneActor& actor,
                                                     const NS::SubObjectDesc& row,
                                                     std::string_view fieldName)
{
    // 写すのはプレイ中だけ。編集モードで写すと次のプレイ突入の捕捉と二重管理になる
    if (m_mode != Mode::Play || m_scene == nullptr)
    {
        return;
    }
    m_scene->WritePlayBaselineField(actor, row, fieldName);
}

bool LevelEditorController::PromoteFieldToArchetype(const NS::SceneActor& actor,
                                                    const NS::SubObjectDesc& row,
                                                    std::string_view fieldName)
{
    const NS::Object* comp = row.get(&actor);
    if (m_scene == nullptr || comp == nullptr || comp->GetReflection() == nullptr)
    {
        return false;
    }
    const std::string className{actor.ClassName()};
    const std::string_view typeName{comp->GetReflection()->typeName};

    // 上げる前の既定の値。これと同じ値の個体は上書きしていないので、新しい既定値へ付いて行く
    if (NS::FindBaselineSubObj(actor, row) == nullptr)
    {
        return false;
    }

    // 同じクラスの個体は同じ表を持つので、row をそのまま使える
    std::vector<NS::SceneActor*> followers;
    for (NS::SceneActor* other : m_scene->Objects())
    {
        if (other == &actor || other->IsTransient() || className != other->ClassName())
        {
            continue;
        }
        const NS::Object* part = row.get(other);
        if (part == nullptr || part->GetReflection() == nullptr || typeName != part->GetReflection()->typeName)
        {
            continue;
        }
        if (!NS::IsFieldOverridden(*other, row, fieldName))
        {
            followers.push_back(other);
        }
    }

    if (!NS::WriteFieldToArchetype(actor, row, fieldName))
    {
        return false;
    }
    if (!NS::ArchetypeLibrary::Get().Save(className))
    {
        NS_LOG_WARN(App, "種類の既定値 {} をファイルへ書けなかった。今の起動の間だけ効く", className);
    }

    // 付いて行く個体へ新しい値を写す。資産の参照の欄なら実体も引き直す
    nlohmann::json fields = nlohmann::json::object();
    fields[std::string{fieldName}] = FieldJson(*comp, fieldName);
    for (NS::SceneActor* follower : followers)
    {
        (void)NS::ApplyJsonFields(*row.get(follower), fields);
        follower->ReloadAssets();
    }
    // 当たりの大きさの欄なら body を張り直す
    m_scene->SyncPhysics();
    return true;
}

void LevelEditorController::PlaceItem(const NS::Editor::PlacementItem& item)
{
    nlohmann::json object = item.prototype;
    NS::SetObjectPosition(object, m_editorCamera.Center());
    PushCreateObject(std::move(object));
}

void LevelEditorController::AddMeshParts(std::string_view meshPath)
{
    // 参照は ContentRoot 相対で持つ。build 時にこの文字列から実体を引く
    const std::string meshRef = RelativeToRoot(meshPath, NS::OS::FileSystem::ContentRoot());

    nlohmann::json object = NS::Editor::MakeMeshPartsPrototype(meshRef);
    NS::SetObjectPosition(object, m_editorCamera.Center());
    NS::SetObjectJsonName(object, NS::OS::FileSystem::Stem(meshPath));
    PushCreateObject(std::move(object));
}

void LevelEditorController::PushCreateObject(nlohmann::json object)
{
    // プレイ中に組み替えた物は、編集へ戻る組み直しで消える。履歴だけがプレイの姿を指して残る
    if (m_mode != Mode::Edit)
    {
        return;
    }
    const NS::uint32 id = m_scene->Objects().AllocateObjectId();
    NS::SetObjectJsonId(object, id);

    // 追加を 取り消し 履歴へ。Do がひな形から実体へ 1 体組んで入れる
    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::nullopt, std::move(object)),
                         m_applier);

    // 組み直し後の新規を選択する。選択候補も貼り直す
    m_selectedObjectId = id;
    m_selectionIds.assign(1, id);
    SetObjectToolActive(true);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::RenameObject(NS::uint32 id, std::string_view name)
{
    if (m_mode != Mode::Edit || id == NS::k_NoObjectId)
    {
        return;
    }
    NS::SceneActor* object = m_scene->Objects().FindByObjectId(id);
    std::optional<nlohmann::json> before = m_applier.CaptureObject(id);
    if (object == nullptr || !before)
    {
        return;
    }

    // 実体の名前を直接変える。他の配置物と重なれば番号が付く。参照は id で持つので切れない
    const std::string previous = object->Name();
    m_scene->Objects().RenameObject(*object, name);
    if (object->Name() == previous)
    {
        return; // 同じ名前で履歴を汚さない
    }

    std::optional<nlohmann::json> after = m_applier.CaptureObject(id);
    // 実体は既に after なので Do を呼ばず履歴だけ積む
    m_editor.Undo().Record(
        std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)));

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DuplicateSelectedObject()
{
    if (m_mode != Mode::Edit || m_selectionIds.empty())
    {
        return;
    }

    const NS::SceneActor* player = FindPlayer(m_scene->Objects());

    // 選択物の忠実コピーを新しい永続 id で増やす
    // 元の id から新しい id への表を作り、コピーの中の参照を付け替えるのに使う
    std::vector<nlohmann::json> copies;
    copies.reserve(m_selectionIds.size());
    std::unordered_map<NS::uint32, NS::uint32> idMap;
    for (const NS::uint32 id : m_selectionIds)
    {
        // プレイヤーは必ず 1 体。複製で 2 体目を作らせない
        if (player != nullptr && id == player->Id())
        {
            continue;
        }
        std::optional<nlohmann::json> source = m_applier.CaptureObject(id);
        if (!source)
        {
            continue;
        }
        const NS::uint32 newId = m_scene->Objects().AllocateObjectId();
        idMap.emplace(id, newId);
        NS::SetObjectJsonId(*source, newId);

        copies.push_back(std::move(*source));
    }
    if (copies.empty())
    {
        return;
    }

    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    std::vector<NS::uint32> created;
    commands.reserve(copies.size());
    created.reserve(copies.size());
    for (nlohmann::json& copy : copies)
    {
        NS::RemapObjectRefs(copy, idMap);
        const NS::uint32 copyId = NS::ObjectJsonId(copy);
        created.push_back(copyId);
        commands.push_back(std::make_unique<NS::Editor::ObjectSnapshotCommand>(copyId, std::nullopt, copy));
    }

    m_editor.Undo().Push(MakeUndoUnit(std::move(commands)), m_applier);

    SelectObjects(created, created.back());
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DeleteSelectedObject()
{
    if (m_mode != Mode::Edit || m_selectionIds.empty())
    {
        return;
    }

    const NS::SceneActor* player = FindPlayer(m_scene->Objects());
    if (player != nullptr &&
        std::find(m_selectionIds.begin(), m_selectionIds.end(), player->Id()) != m_selectionIds.end())
    {
        NS_LOG_WARN(App, "プレイヤーを含むため削除しなかった");
        return;
    }

    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    commands.reserve(m_selectionIds.size());
    for (const NS::uint32 victim : m_selectionIds)
    {
        std::optional<nlohmann::json> before = m_applier.CaptureObject(victim);
        if (!before)
        {
            continue;
        }
        commands.push_back(
            std::make_unique<NS::Editor::ObjectSnapshotCommand>(victim, std::move(before), std::nullopt));
    }
    if (commands.empty())
    {
        return;
    }

    m_editor.Undo().Push(MakeUndoUnit(std::move(commands)), m_applier);

    SelectObjectById(NS::k_NoObjectId);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::FocusSelectedInView() noexcept
{
    if (m_selectionIds.empty())
    {
        return;
    }

    // 選んだ分を全部収める。中心は重心、距離は一番外側までの広がりで決める
    NS::Vector3 sum{0.0f, 0.0f, 0.0f};
    std::vector<NS::Vector3> centers;
    float extent = 0.0f;
    centers.reserve(m_selectionIds.size());
    for (const NS::uint32 id : m_selectionIds)
    {
        NS::SceneActor* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
        {
            continue;
        }
        const NS::Matrix world = object->Root().WorldMatrix();
        const NS::Vector3 center{world._41, world._42, world._43};
        centers.push_back(center);
        sum += center;

        const NS::Vector3 scale = NS::DecomposeAffine(world).scale;
        // 1m 立方の cube mesh の半サイズ 0.5 に拡縮を掛ける
        const float half = std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}) * 0.5f;
        extent = std::max(extent, half);
    }
    if (centers.empty())
    {
        return;
    }

    const NS::Vector3 center = sum / static_cast<float>(centers.size());
    for (const NS::Vector3& each : centers)
    {
        extent = std::max(extent, (each - center).Length());
    }

    const float distance = std::max(extent * 4.0f, NS::Editor::EditorCamera::k_MinDistance);

    m_editorCamera.SetCenter(center);
    m_editorCamera.SetDistance(distance);
}

void LevelEditorController::CaptureDragFollowers() noexcept
{
    m_dragFollowers.clear();
    if (m_selectionIds.size() < 2)
    {
        return;
    }

    NS::SceneActor* primary = m_scene->Objects().FindByObjectId(m_selectedObjectId);
    if (primary == nullptr)
    {
        return;
    }
    m_dragPrimaryWorld = primary->Root().WorldMatrix();

    for (const NS::uint32 id : m_selectionIds)
    {
        if (id == m_selectedObjectId)
        {
            continue;
        }
        NS::SceneActor* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
        {
            continue;
        }

        m_dragFollowers.push_back(DragFollower{id, object->Root().WorldMatrix()});
    }
}

void LevelEditorController::ApplyDragToFollowers() noexcept
{
    if (m_dragFollowers.empty())
    {
        return;
    }
    NS::SceneActor* primary = m_scene->Objects().FindByObjectId(m_selectedObjectId);
    if (primary == nullptr)
    {
        return;
    }

    // 主対象が動いた分をワールド空間の差分として取り、残りへ同じだけ効かせる
    const NS::Matrix delta = m_dragPrimaryWorld.Invert() * primary->Root().WorldMatrix();
    for (const DragFollower& follower : m_dragFollowers)
    {
        NS::SceneActor* object = m_scene->Objects().FindByObjectId(follower.id);
        if (object == nullptr)
        {
            continue;
        }

        NS::Matrix local = follower.world * delta;

        NS::Vector3 scale{};
        NS::Quaternion rotation{};
        NS::Vector3 position{};
        if (!local.Decompose(scale, rotation, position))
        {
            continue;
        }
        object->Root().SetPosition(position);
        object->Root().SetRotation(rotation);
        object->Root().SetScale(scale);
    }
}

void LevelEditorController::BeginTransformEdit() noexcept
{
    if (m_transformEditing)
    {
        return;
    }
    m_editBaselines.clear();
    // 選択している分をまとめて控える。動かなかった物は確定時に落ちる
    for (const NS::uint32 id : m_selectionIds)
    {
        std::optional<nlohmann::json> baseline = m_applier.CaptureObject(id);
        if (baseline)
        {
            m_editBaselines.emplace_back(id, std::move(*baseline));
        }
    }
    if (m_editBaselines.empty())
    {
        return;
    }
    m_transformEditing = true;
}

void LevelEditorController::CommitTransformEdit() noexcept
{
    if (!m_transformEditing)
    {
        return;
    }
    m_transformEditing = false;

    // ドラッグは実体の Root を既に動かしている。after は実体の忠実な写し。baseline と同じなら履歴に積まない
    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    for (std::pair<NS::uint32, nlohmann::json>& entry : m_editBaselines)
    {
        const NS::uint32& id = entry.first;
        nlohmann::json& baseline = entry.second;
        std::optional<nlohmann::json> after = m_applier.CaptureObject(id);
        if (!after || *after == baseline)
        {
            continue;
        }
        commands.push_back(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, baseline, std::move(*after)));
    }
    m_editBaselines.clear();
    // プレイ中の控えはプレイの姿。編集へ戻った後に戻すと、プレイの位置を編集の物へ書く
    if (m_mode != Mode::Edit || commands.empty())
    {
        return;
    }

    // 実体は既に after なので Do を呼ばず履歴だけ積む。取り消し で baseline へ、やり直し で after へ戻す
    m_editor.Undo().Record(MakeUndoUnit(std::move(commands)));
}

void LevelEditorController::BeginSubObjectEdit() noexcept
{
    if (m_subObjectEditing)
    {
        return;
    }
    std::optional<nlohmann::json> baseline = m_applier.CaptureObject(m_selectedObjectId);
    if (!baseline)
    {
        return;
    }
    m_subObjectEditBaseline = std::move(*baseline);
    m_subObjectEditBaselineId = m_selectedObjectId;
    m_subObjectEditing = true;
}

void LevelEditorController::CommitSubObjectEdit() noexcept
{
    if (!m_subObjectEditing)
    {
        return;
    }
    m_subObjectEditing = false;

    // リフレクション編集は実体の部品へ直接入っている。after は実体の忠実な写しで、baseline と同じなら積まない
    // プレイ中の控えはプレイの姿なので積まない
    if (m_mode != Mode::Edit)
    {
        return;
    }
    std::optional<nlohmann::json> after = m_applier.CaptureObject(m_subObjectEditBaselineId);
    if (!after || *after == m_subObjectEditBaseline)
    {
        return;
    }

    // 実体は既に after なので Do を呼ばず履歴だけ積む
    m_editor.Undo().Record(std::make_unique<NS::Editor::ObjectSnapshotCommand>(
        m_subObjectEditBaselineId, m_subObjectEditBaseline, std::move(*after)));
}

bool LevelEditorController::ApplyMaterialToSelected(std::string_view matPath)
{
    NS::Application* app = NS::Application::Get();
    if (!m_objectToolActive || app == nullptr)
    {
        return false;
    }

    if (m_selectedObjectId == NS::k_NoObjectId)
    {
        return false;
    }
    NS::SceneActor* go = SelectedObjectActor();
    if (go == nullptr)
    {
        return false;
    }

    NS::Model* mesh = go->ModelSubObj();
    if (mesh == nullptr)
    {
        return false;
    }

    // .mat パスは ContentRoot 相対で持つ。材質の正データは欄の名前なので、名前を書いて欄から読み直す
    const std::string stored = RelativeToRoot(matPath, NS::OS::FileSystem::ContentRoot());
    NS::AssetManager& assets = app->Assets();
    if (assets.TryLoadMaterial(stored) == nullptr)
    {
        return false;
    }

    // 差替前を忠実に写す。matRef を実体へ書き込み、差替後との差分を 取り消し 履歴へ積む
    std::optional<nlohmann::json> before = m_applier.CaptureObject(m_selectedObjectId);

    mesh->SetMaterialRef(stored);
    go->ReloadAssets();
    mesh->SetBaseColor(assets.MaterialBaseColor(stored));

    std::optional<nlohmann::json> after = m_applier.CaptureObject(m_selectedObjectId);
    if (before && after && !(*before == *after))
    {
        m_editor.Undo().Record(std::make_unique<NS::Editor::ObjectSnapshotCommand>(
            m_selectedObjectId, std::move(before), std::move(after)));
    }
    return true;
}
