#include "Editor/LevelEditorController.h"

#include "Editor/EditorObjects.h"
#include "Editor/GridMath.h"
#include "Editor/HitZoneColors.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/CompositeCommand.h"
#include "Editor/Undo/ObjectSnapshotCommand.h"
#include "Game/Level/CourseDirector.h"
#include "Game/Level/FollowCamera.h"
#include "Game/Level/HitZones.h"
#include "Game/Level/ImpactResolver.h"
#include "Game/Level/SlamAim.h"
#include "Game/Player.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/AABB.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Math.h"
#include "NSlib/Core/OBB.h"
#include "NSlib/Core/Sphere.h"
#include "NSlib/Graphics/DebugDraw.h"
#include "NSlib/Object/AssetManager.h"
#include "NSlib/Object/Components/BoxCollision.h"
#include "NSlib/Object/Components/CameraManager.h"
#include "NSlib/Object/Components/CapsuleCollision.h"
#include "NSlib/Object/Components/Collider.h"
#include "NSlib/Object/Components/HitSensor.h"
#include "NSlib/Object/Components/MeshCollision.h"
#include "NSlib/Object/Components/Model.h"
#include "NSlib/Object/Components/SphereCollision.h"
#include "NSlib/Object/Components/ThirdPersonFollow.h"
#include "NSlib/Object/Components/TransformComponent.h"
#include "NSlib/Object/Components/VirtualCamera.h"
#include "NSlib/Object/ObjectName.h"
#include "NSlib/Object/Reflection/Archetype.h"
#include "NSlib/Object/Reflection/ComponentEntry.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Scene/Scene.h"
#include "NSlib/Object/Scene/SceneCamera.h"
#include "NSlib/Object/Scene/SceneJson.h"
#include "NSlib/Windows/Clock.h"
#include "NSlib/Windows/Filesystem.h"
#include "NSlib/Windows/Input.h"
#include "NSlib/Windows/Keyboard.h"

#include <algorithm>
#include <string>
#include <unordered_set>

namespace
{
    // 部品の欄 1 つの値を JSON で読む。無い欄は null
    nlohmann::json FieldJson(const NS::Obj::Component& comp, std::string_view fieldName)
    {
        const nlohmann::json fields = NS::Obj::SerializeComponentFields(comp);
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

    // index 番目のリフレクション付き component。添字は ForEachPart の並びで、Inspector の並びに揃う
    NS::Obj::Component* ReflectedComponentAt(NS::Obj::Actor& object, std::size_t index) noexcept
    {
        std::size_t count = 0;
        NS::Obj::Component* result = nullptr;
        object.ForEachPart([&count, &result, index](std::string_view, NS::Obj::Component& part) {
            if (part.GetReflection() != nullptr)
            {
                if (count == index)
                {
                    result = &part;
                }
                ++count;
            }
        });
        return result;
    }

    // カメラの視錐台を描く時の far。vcam の既定 1000 のままだと錐台が画面に収まらないので近くで切る
    constexpr float k_CameraGizmoFar = 8.0f;

    // 編集復帰の視点ブレンド秒。CameraManager の vcam 切替の既定 0.35 秒と揃え、モード切替の繋ぎを同じ感触にする
    constexpr float k_EditBlendSeconds = 0.35f;

    // 部品のうち動く体の当たり (Collider) を返す。持たなければ nullptr
    const NS::Obj::Collider* ColliderOf(const NS::Obj::Actor& object) noexcept
    {
        const NS::Obj::Collider* found = nullptr;
        object.ForEachPart([&found](std::string_view, NS::Obj::Component& part) {
            if (found == nullptr)
            {
                found = NS::Obj::ComponentCast<NS::Obj::Collider>(&part);
            }
        });
        return found;
    }

    // 配置物 1 体の当たり形状を線で描く。Box は回転込み OBB、球とカプセルは実形状
    void DrawCollisionWireframe(NS::Gfx::DebugShapes& shapes, NS::Obj::Actor& object, const NS::Color& color) noexcept
    {
        if (NS::Obj::BoxCollision* box = NS::Obj::ComponentCast<NS::Obj::BoxCollision>(object.CollisionPart()))
        {
            shapes.OBB(box->WorldOBB(), color);
        }
        else if (NS::Obj::SphereCollision* sphere =
                     NS::Obj::ComponentCast<NS::Obj::SphereCollision>(object.CollisionPart()))
        {
            shapes.Sphere(sphere->WorldSphere(), color);
        }
        else if (NS::Obj::CapsuleCollision* capsule =
                     NS::Obj::ComponentCast<NS::Obj::CapsuleCollision>(object.CollisionPart()))
        {
            NS::Phys::Capsule worldCapsule = capsule->WorldCapsule();
            worldCapsule.axis.Normalize();
            shapes.Capsule(
                worldCapsule.center, worldCapsule.axis * worldCapsule.halfHeight, worldCapsule.radius, color);
        }
        else if (const NS::Obj::Collider* collider = ColliderOf(object))
        {
            // 移動が掃引するのと同じ、根を中心にした縦のカプセル。根の拡縮は掛けない
            const NS::Phys::Capsule bodyCapsule = collider->CapsuleAt(object.Root().Position());
            shapes.Capsule(bodyCapsule.center, bodyCapsule.axis * bodyCapsule.halfHeight, bodyCapsule.radius, color);
        }
        else if (NS::Obj::ComponentCast<NS::Obj::MeshCollision>(object.CollisionPart()) != nullptr)
        {
            // メッシュの当たりは見た目の三角形そのもの。三角形は多いので、見た目のメッシュを包む箱を出す
            const NS::Obj::Model* renderer = object.ModelPart();
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
    void DrawSensorWireframe(NS::Gfx::DebugShapes& shapes, NS::Obj::Actor& object, const NS::Color& color) noexcept
    {
        for (const NS::Obj::HitSensor* sensor : {object.BodySensorPart(), object.AttackSensorPart()})
        {
            if (sensor == nullptr)
            {
                continue;
            }
            const NS::Obj::SensorVolume volume = sensor->WorldVolume();
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
                          const NS::Game::Level::HitFaceFrame& frame,
                          const NS::Game::Level::HitFaceShape& shape) noexcept
    {
        const std::vector<NS::Vector2> outline = NS::Game::Level::HitFaceShapeOutline(shape);
        const NS::Color edgeColor = NS::Editor::HitZoneColor(shape.tier);
        NS::Color fillColor = edgeColor;
        fillColor.A(k_HitFaceAlpha);
        const NS::Vector3 center = NS::Game::Level::HitFacePoint(frame, shape.centerU, shape.centerV);
        for (std::size_t i = 0; i < outline.size(); ++i)
        {
            const NS::Vector2& from = outline[i];
            const NS::Vector2& to = outline[(i + 1) % outline.size()];
            const NS::Vector3 a = NS::Game::Level::HitFacePoint(frame, from.x, from.y);
            const NS::Vector3 b = NS::Game::Level::HitFacePoint(frame, to.x, to.y);
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
                           const NS::Obj::CameraPose& pose,
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

    // 子を先、自分を後の順で永続 id を集める。削除はこの順で流し、undo は逆順に親から戻る
    void CollectSubtreeIds(const NS::Obj::Actor& root, std::vector<std::uint32_t>& out)
    {
        for (const NS::Obj::Actor* child : root.Children())
        {
            if (child != nullptr && !child->IsTransient())
            {
                CollectSubtreeIds(*child, out);
            }
        }
        out.push_back(root.Id());
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

LevelEditorController::LevelEditorController(NS::Obj::Scene* scene) noexcept : m_scene(scene), m_applier(scene) {}

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

const NS::Obj::ObjectList& LevelEditorController::Objects() const noexcept
{
    return m_scene->Objects();
}

NS::Obj::CameraManager* LevelEditorController::Cameras() const noexcept
{
    if (m_scene == nullptr)
    {
        return nullptr;
    }
    return m_scene->GetCameraManager();
}

NS::Obj::SceneCamera* LevelEditorController::MainCamera() const noexcept
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
    NS::Obj::Actor* bootPlayer = FindPlayer(m_scene->Objects());
    NS::Vector3 startCenter{0.0f, 0.0f, 0.0f};
    if (bootPlayer != nullptr)
    {
        startCenter = bootPlayer->Root().Position();
    }
    m_editorCamera.SetCenter(startCenter);
    // 起動直後は出現地点の block を真ん中近めに見せる距離。1m cube が画面の十数 % を占める
    m_editorCamera.SetDistance(5.0f);

    // 保存は live 実体から作り、読込は取込関数がデータを実体へ写して用済みにする
    m_editor.SetCaptureLevelFn([this]() { return m_scene->ToJson(); });
    m_editor.SetLoadLevelFn([this](nlohmann::json&& fresh) { m_scene->LoadJson(std::move(fresh)); });
    // grid 編集・undo は適用経路を通して live へ写す。セル照会・採番は live 側から引く
    m_editor.SetApplier(&m_applier);
    m_editor.SetFindCellObjectFn([this](std::int16_t x, std::int16_t y, std::int16_t z) {
        return NS::Editor::FindObjectIdAtCell(m_scene->Objects(), x, y, z);
    });
    m_editor.SetCollectCellsFn([this]() {
        std::vector<NS::Editor::EditorMode::CellCoord> cells;
        cells.reserve(m_scene->Objects().ObjectCount());
        for (NS::Obj::Actor* objPtr : m_scene->Objects())
        {
            NS::Obj::Actor& object = *objPtr;
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
            if (!NS::Obj::LoadSceneFromJsonFile(probe, *bootPath))
            {
                m_editor.MarkBootLevelLoadFailed();
            }
        }
    }

    // ギズモに依存先を注入する。選択候補は RefreshGizmoSelectables が別に渡す
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
    // live が唯一の出所なので組み直しは要らない。編集で動いた当たりだけ張り直してプレイへ入る
    m_scene->SyncPhysics();

    // 凍結を取り、そこから走行を最初から。プレイの間の判定と編集復帰の姿はこの凍結を読む
    // 手順は出荷と同じ CourseDirector::StartCourse の持ち物
    if (NS::Game::Level::CourseDirector* director =
            NS::Obj::GetOrCreateSceneObj<NS::Game::Level::CourseDirector>(*m_scene))
    {
        director->StartCourse();
    }
    // 編集の自由視点からプレイ視点へ、vcam 切替と同じブレンドで繋ぐ
    if (NS::Obj::CameraManager* cameras = Cameras())
    {
        cameras->BeginBlendFrom(m_editorCamera.Pose());
    }
    // 世界を回す。止まっているのは編集モードの間だけ
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
    if (NS::Obj::CameraManager* cameras = Cameras())
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
    // free-fly カメラはこの外で editor が握る
    if (m_scene == nullptr)
    {
        return;
    }
    // 編集モードの間は世界を止める
    m_scene->SetSimulationEnabled(false);

    // プレイは試走。位置・生成・破棄・演出の進行を live に残さず、突入時の凍結から世界を組み直す
    // 演出の破棄もゴールの旗戻しも組み直しが済ませるので、個別の後始末は置かない
    // 一時オブジェクトの実カメラは凍結に写らないが、Rebuild が退避して残す
    nlohmann::json baseline = m_scene->PlayBaseline();
    m_scene->LoadJson(std::move(baseline));

    // 編集モードはカーソルを出し、カーソル位置ベースの操作へ戻す
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
    // 編集入力はこの表示矩形基準でレイを飛ばす。hover 偽の間は配置カーソルを立てない
    m_editor.SetViewRect(m_gameViewRect);
    m_editor.SetViewHovered(hovered);
}

void LevelEditorController::ClearGameView() noexcept
{
    // 全画面直描き。フォールバックの全画面矩形を使わせるため矩形無効 + hover 真にする
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
    // 描画視点は SceneViewPose が free-fly の pose を渡すので、ここは入力適用だけでよい
    if (m_mode != Mode::Play)
    {
        return;
    }
    m_editorCamera.ApplyInput(input);
}

std::optional<NS::Obj::CameraPose> LevelEditorController::SceneViewPose() noexcept
{
    // 編集中は実カメラ (TickEdit が free-fly の pose を書く) 任せ。プレイ中だけ自由視点を上書きする
    if (m_mode != Mode::Play)
    {
        return std::nullopt;
    }
    return m_editorCamera.Pose();
}

std::optional<NS::Obj::CameraPose> LevelEditorController::GameViewPose() noexcept
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
        return NS::Obj::MakeSceneJson();
    }
    return m_scene->ToJson();
}

void LevelEditorController::SetSceneViews(std::vector<NS::Obj::SceneView> views)
{
    if (m_scene != nullptr)
    {
        m_scene->SetSceneViews(std::move(views));
    }
}

void LevelEditorController::Tick()
{
    // プレイ中のクリア / 死亡は応答 component が出荷と同じ手順で完結させる
    // editor は割り込まず、編集へ戻るのは Tab / Pause modal の明示操作だけ
    if (m_mode == Mode::Edit)
    {
        TickEdit();
        return;
    }

    // プレイ中の Esc は隠したカーソルを出すだけ。出ている時の Esc で終えると、出したカーソルでタブを押しに
    // 行く途中でアプリごと終わる。プレイから抜けるのは Tab / Start / 帯の停止
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

    // F5 で編集中の HLSL を再起動なしで反映する。プレイ中の F5 はエディタ UI の表示トグルに使うため
    // 編集モードのここでだけ再読み込みする。ImGui 入力中は誤爆を防ぐため無効化する
    if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::OS::Key::F5))
    {
        app->Assets().ReloadAllShaders();
    }

    // Esc: Object モードで選択中ならまず選択解除に使い終了させない
    if (app->Input().Keyboard().IsPressed(NS::OS::Key::Escape))
    {
        if (m_editorToolMode == EditorToolMode::Object && m_gizmo.Selected() != nullptr)
        {
            m_gizmo.ClearSelection();
            return;
        }
        NS::Application::Quit();
        return;
    }

    m_editorCamera.Tick();

    // free-fly 更新後に実カメラへ反映し、ギズモ / 編集の ray-pick が当フレームの視点を使えるようにする
    // 編集中は世界が止まり CameraManager が実カメラを書かないので、この書き込みが上書きされずに残る
    if (NS::Obj::SceneCamera* camera = MainCamera())
    {
        NS::Obj::CameraPose pose = m_editorCamera.Pose();
        if (m_editBlending)
        {
            m_editBlendElapsed += NS::OS::FrameTimer::FixedDelta();
            const float t = std::min(m_editBlendElapsed / k_EditBlendSeconds, 1.0f);
            const float eased = NS::SmoothStep(t); // ease-in-out
            pose = NS::Obj::CameraPose::Lerp(m_editBlendFrom, pose, eased);
            if (t >= 1.0f)
            {
                m_editBlending = false;
            }
        }
        camera->ApplyPose(pose);
    }

    // 同時に 1 モードだけがクリックと R を消費する。Object 中は grid 入力を抑制しギズモへ回す
    // モード切替は Editor の UI ボタンが SetObjectToolActive で入れる。Tab は Edit↔Play 専用
    const bool objectMode = (m_editorToolMode == EditorToolMode::Object);
    m_gizmo.SetActive(objectMode);
    m_editor.SetInputSuppressed(objectMode);

    if (objectMode && Cameras())
    {
        // 追従カメラの Root を実プレイ視点位置へ寄せてからギズモを回す。判定箱とギズモがその位置に出る
        SyncFollowCameraPoses();

        // 毎フレーム live な scene から候補 span を作り直し、選択を id から今のオブジェクトへ引き直す
        // 作り直さないと編集復帰 / undo の rebuild で破棄された実体を指したままになる
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
        // 追従カメラは Root でなく初期姿勢を変えるので、Root 基準のスナップショットは積まない
        const bool nowDragging = m_gizmo.IsDragging();
        if (SelectedFollowCamera() == nullptr)
        {
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
    NS::Application* app = NS::Application::Get();
    if (app == nullptr)
    {
        return;
    }

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
    const EditorToolMode next = [active]() -> EditorToolMode {
        if (active)
        {
            return EditorToolMode::Object;
        }
        return EditorToolMode::Build;
    }();
    if (next == m_editorToolMode)
    {
        return;
    }
    m_editorToolMode = next;
    if (!active)
    {
        m_gizmo.ClearSelection();
        m_selectionIds.clear();
        m_selectedObjectId = NS::Obj::k_NoObjectId;
        m_lastGizmoSelected = nullptr;
    }
}

std::size_t LevelEditorController::SelectedObjectIndex() const noexcept
{
    if (m_selectedObjectId == NS::Obj::k_NoObjectId)
    {
        return NS::Obj::k_NoObjectIndex;
    }
    const NS::Obj::ObjectList& objects = m_scene->Objects();
    const std::size_t index = objects.IndexOfObjectId(m_selectedObjectId);
    if (index == objects.ObjectCount())
    {
        return NS::Obj::k_NoObjectIndex;
    }
    return index;
}

bool LevelEditorController::HasInspectableSelection() const noexcept
{
    return m_selectedObjectId != NS::Obj::k_NoObjectId &&
           m_scene->Objects().FindByObjectId(m_selectedObjectId) != nullptr;
}

NS::Obj::Actor* LevelEditorController::SelectedObjectActor() noexcept
{
    // 選択の真実は永続 id。player も含め全配置物が ObjectList に居るので id で引く
    if (m_selectedObjectId == NS::Obj::k_NoObjectId)
    {
        return nullptr;
    }
    return m_scene->Objects().FindByObjectId(m_selectedObjectId);
}

bool LevelEditorController::SelectedIsPlayerObject() const noexcept
{
    NS::Obj::Actor* player = FindPlayer(m_scene->Objects());
    return player != nullptr && m_selectedObjectId != NS::Obj::k_NoObjectId && m_selectedObjectId == player->Id();
}

void LevelEditorController::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectablePickable.clear();
    m_selectablePtrs.reserve(m_scene->Objects().ObjectCount());
    m_selectablePickable.reserve(m_scene->Objects().ObjectCount());

    // 可視メッシュを持つ候補は 1、見えないカメラ等は 0。ギズモは 1 の候補を先に選ぶ
    // 全配置物をギズモ候補に積む。判定箱はギズモがクリックの時に配置物から引く
    for (NS::Obj::Actor* object : m_scene->Objects())
    {
        if (object->IsTransient())
        {
            continue;
        }
        m_selectablePtrs.push_back(object);
        const bool hasVisual = object->ModelPart() != nullptr;
        std::uint8_t pickable = std::uint8_t{0};
        if (hasVisual)
        {
            pickable = std::uint8_t{1};
        }
        m_selectablePickable.push_back(pickable);
    }

    m_gizmo.SetSelectableObjects(m_selectablePtrs, m_selectablePickable);
}

NS::Obj::ThirdPersonFollow* LevelEditorController::SelectedFollowCamera() noexcept
{
    if (NS::Game::Level::FollowCamera* camera = NS::Obj::Cast<NS::Game::Level::FollowCamera>(SelectedObjectActor()))
    {
        return &camera->Vcam();
    }
    return nullptr;
}

void LevelEditorController::SyncFollowCameraPoses()
{
    // 追従カメラは位置を持たないので、edit 中は実プレイの視点位置へ Root を寄せて frustum / pick / ギズモを出す
    // ドラッグ中の選択カメラだけは gizmo が Root を握るため触らず、その位置を初期姿勢へ逆算する側に任せる
    const NS::Obj::ObjectList& objects = m_scene->Objects();
    for (NS::Obj::Actor* object : objects)
    {
        NS::Game::Level::FollowCamera* camera = NS::Obj::Cast<NS::Game::Level::FollowCamera>(object);
        if (camera == nullptr)
        {
            continue;
        }
        NS::Obj::ThirdPersonFollow* follow = &camera->Vcam();
        if (m_gizmo.IsDragging() && object->Id() == m_selectedObjectId)
        {
            continue;
        }
        object->Root().SetPosition(follow->EvaluatePose(1.0f).position);
    }
}

void LevelEditorController::ApplyFollowCameraGizmoDrag()
{
    // ドラッグ中の追従カメラは、gizmo が動かした Root 位置から初期姿勢の yaw/pitch/距離を逆算して
    // live component へ書き戻す。Root 位置は初期姿勢由来なので保存対象は component 側になる
    if (!m_gizmo.IsDragging())
    {
        return;
    }
    NS::Obj::ThirdPersonFollow* follow = SelectedFollowCamera();
    if (follow == nullptr)
    {
        return;
    }
    NS::Obj::Actor* go = SelectedObjectActor();
    if (go == nullptr)
    {
        return;
    }
    follow->SetInitialPoseFromCameraPosition(go->Root().Position());
}

void LevelEditorController::SelectObjectById(std::uint32_t id) noexcept
{
    m_selectionIds.clear();
    if (id != NS::Obj::k_NoObjectId)
    {
        m_selectionIds.push_back(id);
    }
    SetPrimarySelection(id);
}

bool LevelEditorController::IsObjectSelected(std::uint32_t id) const noexcept
{
    if (id == NS::Obj::k_NoObjectId)
    {
        return false;
    }
    return std::find(m_selectionIds.begin(), m_selectionIds.end(), id) != m_selectionIds.end();
}

void LevelEditorController::ToggleObjectSelection(std::uint32_t id) noexcept
{
    if (id == NS::Obj::k_NoObjectId)
    {
        return;
    }

    const std::vector<std::uint32_t>::iterator it = std::find(m_selectionIds.begin(), m_selectionIds.end(), id);
    if (it != m_selectionIds.end())
    {
        m_selectionIds.erase(it);
        if (m_selectedObjectId != id)
        {
            return;
        }
        // 主対象を外したので、残っている中の直近へ譲る
        std::uint32_t next = NS::Obj::k_NoObjectId;
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

void LevelEditorController::SelectObjects(std::vector<std::uint32_t> ids, std::uint32_t primary) noexcept
{
    m_selectionIds = std::move(ids);
    if (primary != NS::Obj::k_NoObjectId && !IsObjectSelected(primary))
    {
        m_selectionIds.push_back(primary);
    }
    SetPrimarySelection(primary);
}

void LevelEditorController::SetPrimarySelection(std::uint32_t id) noexcept
{
    m_selectedObjectId = id;

    if (id == NS::Obj::k_NoObjectId)
    {
        m_gizmo.ClearSelection();
        m_lastGizmoSelected = nullptr;
        return;
    }

    // ハンドルを出すため Object ツールへ切替える。Build のままだとギズモが描かれない
    SetObjectToolActive(true);

    // player も含め全配置物が ObjectList に居る。選択した Root を id で引いてギズモへ貼る
    if (NS::Obj::Actor* go = m_scene->Objects().FindByObjectId(id))
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
    const NS::Obj::ObjectList& objects = m_scene->Objects();
    // 錐台の横幅は実ビューポート比で出す。viewport が潰れている時だけ 16:9 目安へフォールバックする
    const float aspect = [viewport]() -> float {
        if (viewport.height > 0)
        {
            return static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
        }
        return 16.0f / 9.0f;
    }();
    for (NS::Obj::Actor* object : objects)
    {
        NS::Game::Level::FollowCamera* camera = NS::Obj::Cast<NS::Game::Level::FollowCamera>(object);
        if (camera == nullptr)
        {
            continue;
        }
        NS::Obj::VirtualCamera* vcam = &camera->Vcam();
        const bool selected = (object->Id() == m_selectedObjectId);
        const NS::Color camColor = [selected]() -> NS::Color {
            if (selected)
            {
                return NS::Color{1.0f, 0.55f, 0.10f, 1.0f};
            }
            return NS::Color{1.0f, 0.85f, 0.10f, 1.0f};
        }();

        const NS::Obj::CameraPose pose = vcam->EvaluatePose(1.0f);
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
    for (const std::uint32_t id : m_selectionIds)
    {
        NS::Obj::Actor* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
        {
            continue;
        }

        const NS::Matrix world = object->Root().WorldMatrix();
        const NS::Vector3 scale = object->Root().Scale();

        // 行の基底が各軸の向き。正規化して大きさは halfExtent へ回す
        NS::OBB obb{};
        obb.center = NS::Vector3{world._41, world._42, world._43};
        obb.axisX = NS::Vector3{world._11, world._12, world._13};
        obb.axisY = NS::Vector3{world._21, world._22, world._23};
        obb.axisZ = NS::Vector3{world._31, world._32, world._33};
        obb.axisX.Normalize();
        obb.axisY.Normalize();
        obb.axisZ.Normalize();
        // 1m 立方の cube mesh の半サイズ 0.5 に拡縮を掛ける
        obb.halfExtentX = std::abs(scale.x) * 0.5f;
        obb.halfExtentY = std::abs(scale.y) * 0.5f;
        obb.halfExtentZ = std::abs(scale.z) * 0.5f;
        shapes.OBB(obb, color);
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
        for (NS::Obj::Actor* objPtr : m_scene->Objects())
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
        return;
    }

    // 編集中は選んだ分だけ。全部出すと線が重なって、どれの形か読み取れない
    for (const std::uint32_t id : m_selectionIds)
    {
        if (NS::Obj::Actor* objPtr = m_scene->Objects().FindByObjectId(id))
        {
            DrawCollisionWireframe(shapes, *objPtr, color);
            DrawSensorWireframe(shapes, *objPtr, sensorColor);
        }
    }
}

void LevelEditorController::RenderHitFaces(NS::Gfx::DebugShapes& shapes) noexcept
{
    if (m_scene == nullptr)
    {
        return;
    }

    Player* player = FindPlayer(m_scene->Objects());
    // 自機が居ない場面は半径 0 として、相手の輪郭の大きさで描く
    float playerRadius = 0.0f;
    NS::Vector3 ballCenter{};
    NS::Game::Level::SlamLineTarget aim{};
    bool aiming = false;
    if (player != nullptr)
    {
        // 突進の玉は、狙う相手の探し方と同じ Player::SlamBallAt から引く
        const NS::Sphere ball = player->SlamBallAt(player->Root().Position());
        ballCenter = ball.center;
        playerRadius = ball.radius;
        aiming = player->TryGetAimTarget(aim);
    }
    const NS::Vector3 cameraPosition = m_editorCamera.Pose().position;

    for (NS::Obj::Actor* object : m_scene->Objects())
    {
        if (!object->IsActiveInHierarchy())
        {
            continue;
        }
        const NS::Game::Level::HitZones* zones =
            NS::Obj::ComponentCast<NS::Game::Level::HitZones>(object->Part("HitZones"));
        const NS::Obj::HitSensor* bodySensor = object->BodySensorPart();
        if (zones == nullptr || bodySensor == nullptr)
        {
            continue;
        }
        // 調べる対象から外した体は、突進が当たらないので面も出さない
        if (!bodySensor->IsValid())
        {
            continue;
        }
        const NS::Obj::SensorVolume body = bodySensor->WorldVolume();
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
        NS::Game::Level::HitFaceFrame frame;
        if (!NS::Game::Level::MakeHitFaceFrame(body, direction, playerRadius, frame))
        {
            continue;
        }
        for (const NS::Game::Level::HitFaceShape& shape :
             NS::Game::Level::HitFaceShapes(zones->Face(), frame.bodyShape))
        {
            DrawHitFaceShape(shapes, frame, shape);
        }
    }

    if (player == nullptr)
    {
        return;
    }
    const NS::Game::Level::ImpactRecord& impact = player->Resolver().LastImpact();
    // まだ 1 度も当てていない
    if (impact.sequence == 0)
    {
        return;
    }
    shapes.Sphere(NS::Sphere{impact.surfacePoint, k_HitTouchMarkerRadius}, NS::Editor::HitZoneColor(impact.tier));
}

void LevelEditorController::CaptureSelectionFromGizmo() noexcept
{
    NS::Obj::Transform* selected = m_gizmo.Selected();
    // Hierarchy で選んだ非 gizmo 選択を毎フレーム潰さないため前フレームと同じなら据え置き
    if (selected == m_lastGizmoSelected)
    {
        return;
    }
    m_lastGizmoSelected = selected;
    if (selected == nullptr)
    {
        m_selectionIds.clear();
        m_selectedObjectId = NS::Obj::k_NoObjectId;
        return;
    }
    // player も ObjectList に居るので、ビューポートのピックと同じ経路で引ける
    for (NS::Obj::Actor* object : m_scene->Objects())
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
    m_selectedObjectId = NS::Obj::k_NoObjectId;
}

void LevelEditorController::ResolveSelectionFromId() noexcept
{
    // SetSelected が進行中ドラッグを切ってしまうのでドラッグ中は gizmo の選択を貼り直さない
    if (m_gizmo.IsDragging())
    {
        return;
    }

    // 選択 id が現存する配置物を指すなら gizmo に貼り直す。不在なら gizmo を外す
    // player も ObjectList に居るので id で引ける。rebuild を跨いでも掴める状態を保つ
    if (m_selectedObjectId != NS::Obj::k_NoObjectId)
    {
        if (NS::Obj::Actor* go = m_scene->Objects().FindByObjectId(m_selectedObjectId))
        {
            NS::Obj::Transform* root = &go->Root();
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
    // live の Root を直接動かす。undo の記録は CommitTransformEdit、当たりの追従は SyncPhysics が担う
    if (NS::Obj::Actor* go = SelectedObjectActor())
    {
        go->Root().SetPosition(position);
        if (NS::Obj::TransformComponent* transform =
                NS::Obj::ComponentCast<NS::Obj::TransformComponent>(go->Part(NS::Obj::k_TransformPartName)))
        {
            MirrorPlayEditToBaseline(*transform, NS::Obj::k_PositionFieldName);
        }
    }
}

void LevelEditorController::SetSelectedFreeRotation(NS::Quaternion rotation)
{
    if (NS::Obj::Actor* go = SelectedObjectActor())
    {
        go->Root().SetRotation(rotation);
        if (NS::Obj::TransformComponent* transform =
                NS::Obj::ComponentCast<NS::Obj::TransformComponent>(go->Part(NS::Obj::k_TransformPartName)))
        {
            MirrorPlayEditToBaseline(*transform, NS::Obj::k_RotationFieldName);
        }
    }
}

void LevelEditorController::SetSelectedFreeScale(NS::Vector3 scale)
{
    // ImGui の入力で 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
    constexpr float k_MinScale = 0.01f;
    scale.x = std::max(scale.x, k_MinScale);
    scale.y = std::max(scale.y, k_MinScale);
    scale.z = std::max(scale.z, k_MinScale);
    if (NS::Obj::Actor* go = SelectedObjectActor())
    {
        go->Root().SetScale(scale);
        if (NS::Obj::TransformComponent* transform =
                NS::Obj::ComponentCast<NS::Obj::TransformComponent>(go->Part(NS::Obj::k_TransformPartName)))
        {
            MirrorPlayEditToBaseline(*transform, NS::Obj::k_ScaleFieldName);
        }
    }
}

void LevelEditorController::MirrorPlayEditToBaseline(const NS::Obj::Component& comp, std::string_view fieldName)
{
    // 写すのはプレイ中だけ。編集モードで写すと次のプレイ突入の捕捉と二重管理になる
    if (m_mode != Mode::Play || m_scene == nullptr)
    {
        return;
    }
    m_scene->WritePlayBaselineField(comp, fieldName);
}

bool LevelEditorController::PromoteFieldToArchetype(NS::Obj::Component& comp, std::string_view fieldName)
{
    NS::Obj::Actor* owner = comp.Owner();
    const NS::Obj::ReflectionInfo* info = comp.GetReflection();
    if (m_scene == nullptr || owner == nullptr || info == nullptr)
    {
        return false;
    }
    const std::string className{owner->ClassName()};
    const std::string_view typeName{info->typeName};

    // 上げる前の既定の値。これと同じ値の個体は上書きしていないので、新しい既定値へ付いて行く
    if (NS::Obj::FindBaselinePart(comp) == nullptr)
    {
        return false;
    }

    std::vector<NS::Obj::Component*> followers;
    for (NS::Obj::Actor* actor : m_scene->Objects())
    {
        if (actor == owner || actor->IsTransient() || className != actor->ClassName())
        {
            continue;
        }
        NS::Obj::Component* part = actor->Part(owner->PartName(comp));
        if (part == nullptr || part->GetReflection() == nullptr || typeName != part->GetReflection()->typeName)
        {
            continue;
        }
        if (!NS::Obj::IsFieldOverridden(*part, fieldName))
        {
            followers.push_back(part);
        }
    }

    if (!NS::Obj::WriteFieldToArchetype(comp, fieldName))
    {
        return false;
    }
    if (!NS::Obj::ArchetypeLibrary::Get().Save(className))
    {
        NS_LOG_WARN(App, "種類の既定値 {} をファイルへ書けなかった。今の起動の間だけ効く", className);
    }

    // 付いて行く個体へ新しい値を写す。資産の参照の欄なら実体も引き直す
    nlohmann::json fields = nlohmann::json::object();
    fields[std::string{fieldName}] = FieldJson(comp, fieldName);
    NS::Application* app = NS::Application::Get();
    for (NS::Obj::Component* part : followers)
    {
        (void)NS::Obj::ApplyJsonFields(*part, fields);
        if (app != nullptr)
        {
            part->ResolveAssets(app->Assets());
        }
    }
    // 当たりの大きさの欄なら body を張り直す
    m_scene->SyncPhysics();
    return true;
}

void LevelEditorController::PlaceItem(const NS::Editor::PlacementItem& item)
{
    // 新しい配置物は編集視点の中心あたりへ置く。採番・履歴・選択は PushCreateObject が担う
    nlohmann::json object = item.prototype;
    NS::Obj::SetObjectPosition(object, m_editorCamera.Center());
    PushCreateObject(std::move(object));
}

void LevelEditorController::AddMeshParts(std::string_view meshPath)
{
    // 参照は ContentRoot 相対で持つ。build 時にこの文字列から実体を引く
    const std::string meshRef = RelativeToRoot(meshPath, NS::OS::FileSystem::ContentRoot());

    nlohmann::json object = NS::Editor::MakeMeshPartsPrototype(meshRef);
    NS::Obj::SetObjectPosition(object, m_editorCamera.Center());
    NS::Obj::SetObjectJsonName(object, NS::OS::FileSystem::Stem(meshPath));
    PushCreateObject(std::move(object));
}

void LevelEditorController::PushCreateObject(nlohmann::json object)
{
    // 新規配置物に永続 id を 1 個振る
    const std::uint32_t id = m_scene->Objects().AllocateObjectId();
    NS::Obj::SetObjectJsonId(object, id);

    // 追加を undo 履歴へ。Do がひな形から live へ 1 体組んで入れる
    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::nullopt, std::move(object)),
                         m_applier);

    // 組み直し後の新規を選択する。選択候補も貼り直す
    m_selectedObjectId = id;
    m_selectionIds.assign(1, id);
    SetObjectToolActive(true);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::RenameObject(std::uint32_t id, std::string_view name)
{
    if (id == NS::Obj::k_NoObjectId)
    {
        return;
    }
    NS::Obj::Actor* object = m_scene->Objects().FindByObjectId(id);
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

bool LevelEditorController::SetObjectParent(std::uint32_t id, std::uint32_t parentId)
{
    if (id == NS::Obj::k_NoObjectId || id == parentId)
    {
        return false;
    }

    NS::Obj::Actor* child = m_scene->Objects().FindByObjectId(id);
    if (child == nullptr)
    {
        return false;
    }

    NS::Obj::Actor* parent = nullptr;
    if (parentId != NS::Obj::k_NoObjectId)
    {
        parent = m_scene->Objects().FindByObjectId(parentId);
        if (parent == nullptr)
        {
            return false;
        }
        // 自分の子孫を親にすると輪になる
        for (NS::Obj::Actor* ancestor = parent; ancestor != nullptr; ancestor = ancestor->Parent())
        {
            if (ancestor == child)
            {
                return false;
            }
        }
    }

    std::optional<nlohmann::json> before = m_applier.CaptureObject(id);
    if (!before || child->Parent() == parent)
    {
        return false;
    }

    // 親空間が変わっても見た目が動かないよう、今のワールド変換から新しいローカル変換を割り出す
    NS::Matrix local = child->Root().WorldMatrix();
    if (parent != nullptr)
    {
        local *= parent->Root().WorldMatrix().Invert();
    }

    // 実体の親を直接付け替える。付け替えは local の値を保つので、割り出した local を後から入れる
    child->SetParent(parent);
    NS::Vector3 scale{};
    NS::Quaternion rotation{};
    NS::Vector3 position{};
    if (local.Decompose(scale, rotation, position))
    {
        child->Root().SetPosition(position);
        child->Root().SetRotation(rotation);
        child->Root().SetScale(scale);
    }
    else
    {
        // 分解できない変換は local を触らず親だけ差し替える。見た目は動くが編集は通す
        NS_LOG_WARN(App, "変換を分解できないため object {} の見た目を保てなかった", id);
    }

    std::optional<nlohmann::json> after = m_applier.CaptureObject(id);
    // 実体は既に after なので Do を呼ばず履歴だけ積む
    m_editor.Undo().Record(
        std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)));

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    return true;
}

void LevelEditorController::SetComponentEnabledOnSelected(std::size_t componentIndex, bool enabled)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Obj::k_NoObjectId)
    {
        return;
    }
    NS::Obj::Actor* object = m_scene->Objects().FindByObjectId(id);
    if (object == nullptr)
    {
        return;
    }
    NS::Obj::Component* comp = ReflectedComponentAt(*object, componentIndex);
    if (comp == nullptr || comp->IsEnabled() == enabled)
    {
        return;
    }
    // 入力 component を休止させると player が動かなくなる。根の部品も同様に守る
    const std::string_view typeName = comp->ClassName();
    if (typeName == "PlayerInput" || object->PartName(*comp) == NS::Obj::k_TransformPartName)
    {
        return;
    }

    // 実体の有効を直接切り替え、当たりを張り直す。実体は既に after なので Do を呼ばず履歴だけ積む
    std::optional<nlohmann::json> before = m_applier.CaptureObject(id);
    comp->SetEnabled(enabled);
    m_scene->SyncPhysics();
    std::optional<nlohmann::json> after = m_applier.CaptureObject(id);
    m_editor.Undo().Record(
        std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)));

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DuplicateSelectedObject()
{
    if (m_selectionIds.empty())
    {
        return;
    }

    const NS::Obj::Actor* player = FindPlayer(m_scene->Objects());

    // 選択物の忠実コピーを新しい永続 id で増やす
    // 元の id から新しい id への表を作り、コピーの中の親と参照を付け替えるのに使う
    std::vector<nlohmann::json> copies;
    copies.reserve(m_selectionIds.size());
    std::unordered_map<std::uint32_t, std::uint32_t> idMap;
    for (const std::uint32_t id : m_selectionIds)
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
        const std::uint32_t newId = m_scene->Objects().AllocateObjectId();
        idMap.emplace(id, newId);
        NS::Obj::SetObjectJsonId(*source, newId);

        copies.push_back(std::move(*source));
    }
    if (copies.empty())
    {
        return;
    }

    // 親も一緒に複製したなら、コピーの親はコピー側へ向ける。親が選択外ならそのまま元の親へぶら下がる
    // 参照も同じで、コピーした範囲の中を指す物だけコピー側へ向け、範囲の外を指す物は元の相手のまま残す
    std::unordered_set<std::uint32_t> copiedIds;
    for (nlohmann::json& copy : copies)
    {
        const std::unordered_map<std::uint32_t, std::uint32_t>::const_iterator parent =
            idMap.find(NS::Obj::ObjectJsonParent(copy));
        if (NS::Obj::ObjectJsonParent(copy) != NS::Obj::k_NoObjectId && parent != idMap.end())
        {
            NS::Obj::SetObjectJsonParent(copy, parent->second);
        }
        NS::Obj::RemapObjectRefs(copy, idMap);
        copiedIds.insert(NS::Obj::ObjectJsonId(copy));
    }

    // 1 体ずつ組んで入れるので、コピーの親はコピーの子より先に入れる。後だと子が親を引けず根に落ちる
    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    std::vector<std::uint32_t> created;
    commands.reserve(copies.size());
    created.reserve(copies.size());
    std::unordered_set<std::uint32_t> placed;
    while (created.size() < copies.size())
    {
        const std::size_t before = created.size();
        for (nlohmann::json& copy : copies)
        {
            const std::uint32_t copyId = NS::Obj::ObjectJsonId(copy);
            const std::uint32_t parentId = NS::Obj::ObjectJsonParent(copy);
            if (placed.contains(copyId) || (copiedIds.contains(parentId) && !placed.contains(parentId)))
            {
                continue;
            }
            placed.insert(copyId);
            created.push_back(copyId);
            commands.push_back(std::make_unique<NS::Editor::ObjectSnapshotCommand>(copyId, std::nullopt, copy));
        }
        // 親子が輪になったデータは進まない。残りは並びのまま入れる
        if (created.size() == before)
        {
            for (nlohmann::json& copy : copies)
            {
                const std::uint32_t copyId = NS::Obj::ObjectJsonId(copy);
                if (!placed.insert(copyId).second)
                {
                    continue;
                }
                created.push_back(copyId);
                commands.push_back(std::make_unique<NS::Editor::ObjectSnapshotCommand>(copyId, std::nullopt, copy));
            }
        }
    }

    m_editor.Undo().Push(MakeUndoUnit(std::move(commands)), m_applier);

    SetObjectToolActive(true);
    SelectObjects(created, created.back());
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DeleteSelectedObject()
{
    if (m_selectionIds.empty())
    {
        return;
    }

    // 親だけ消すと子が宙に浮くので、ぶら下がっている分もまとめて消す
    std::vector<std::uint32_t> victims;
    for (const std::uint32_t id : m_selectionIds)
    {
        NS::Obj::Actor* target = m_scene->Objects().FindByObjectId(id);
        if (target != nullptr)
        {
            CollectSubtreeIds(*target, victims);
        }
    }

    // 親と子を両方選んでいると同じ物が二度並ぶ。葉が先の順は崩さずに重複だけ落とす
    std::vector<std::uint32_t> ordered;
    ordered.reserve(victims.size());
    for (const std::uint32_t victim : victims)
    {
        if (std::find(ordered.begin(), ordered.end(), victim) == ordered.end())
        {
            ordered.push_back(victim);
        }
    }

    // プレイヤーが消えるとレベルが遊べなくなる。子孫に紛れていても止める
    const NS::Obj::Actor* player = FindPlayer(m_scene->Objects());
    if (player != nullptr && std::find(ordered.begin(), ordered.end(), player->Id()) != ordered.end())
    {
        NS_LOG_WARN(App, "プレイヤーを含むため削除しなかった");
        return;
    }

    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    commands.reserve(ordered.size());
    for (const std::uint32_t victim : ordered)
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

    m_selectionIds.clear();
    m_selectedObjectId = NS::Obj::k_NoObjectId;
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
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
    for (const std::uint32_t id : m_selectionIds)
    {
        NS::Obj::Actor* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
        {
            continue;
        }
        const NS::Matrix world = object->Root().WorldMatrix();
        const NS::Vector3 center{world._41, world._42, world._43};
        centers.push_back(center);
        sum += center;

        const NS::Vector3 scale = object->Root().Scale();
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

    NS::Obj::Actor* primary = m_scene->Objects().FindByObjectId(m_selectedObjectId);
    if (primary == nullptr)
    {
        return;
    }
    m_dragPrimaryWorld = primary->Root().WorldMatrix();

    for (const std::uint32_t id : m_selectionIds)
    {
        if (id == m_selectedObjectId)
        {
            continue;
        }
        NS::Obj::Actor* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
        {
            continue;
        }

        // 選択中の物にぶら下がっている分は親が動けば付いてくる。二重に動かさない
        bool underSelected = false;
        for (const NS::Obj::Actor* ancestor = object->Parent(); ancestor != nullptr; ancestor = ancestor->Parent())
        {
            if (IsObjectSelected(ancestor->Id()))
            {
                underSelected = true;
                break;
            }
        }
        if (underSelected)
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
    NS::Obj::Actor* primary = m_scene->Objects().FindByObjectId(m_selectedObjectId);
    if (primary == nullptr)
    {
        return;
    }

    // 主対象が動いた分をワールド空間の差分として取り、残りへ同じだけ効かせる
    const NS::Matrix delta = m_dragPrimaryWorld.Invert() * primary->Root().WorldMatrix();
    for (const DragFollower& follower : m_dragFollowers)
    {
        NS::Obj::Actor* object = m_scene->Objects().FindByObjectId(follower.id);
        if (object == nullptr)
        {
            continue;
        }

        NS::Matrix local = follower.world * delta;
        if (const NS::Obj::Actor* parent = object->Parent())
        {
            local *= parent->Root().WorldMatrix().Invert();
        }

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
    for (const std::uint32_t id : m_selectionIds)
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

    // ドラッグは live Root を既に動かしている。after は live の忠実な写し。baseline と同じなら履歴に積まない
    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    for (std::pair<std::uint32_t, nlohmann::json>& entry : m_editBaselines)
    {
        const std::uint32_t& id = entry.first;
        nlohmann::json& baseline = entry.second;
        std::optional<nlohmann::json> after = m_applier.CaptureObject(id);
        if (!after || *after == baseline)
        {
            continue;
        }
        commands.push_back(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, baseline, std::move(*after)));
    }
    m_editBaselines.clear();
    if (commands.empty())
    {
        return;
    }

    // live は既に after なので Do を呼ばず履歴だけ積む。undo で baseline へ、redo で after へ戻す
    m_editor.Undo().Record(MakeUndoUnit(std::move(commands)));
}

void LevelEditorController::BeginComponentEdit() noexcept
{
    if (m_componentEditing)
    {
        return;
    }
    std::optional<nlohmann::json> baseline = m_applier.CaptureObject(m_selectedObjectId);
    if (!baseline)
    {
        return;
    }
    m_componentEditBaseline = std::move(*baseline);
    m_componentEditBaselineId = m_selectedObjectId;
    m_componentEditing = true;
}

void LevelEditorController::CommitComponentEdit() noexcept
{
    if (!m_componentEditing)
    {
        return;
    }
    m_componentEditing = false;

    // リフレクション編集は live component へ直接入っている。after は live の忠実な写しで、baseline と同じなら積まない
    std::optional<nlohmann::json> after = m_applier.CaptureObject(m_componentEditBaselineId);
    if (!after || *after == m_componentEditBaseline)
    {
        return;
    }

    // live は既に after なので Do を呼ばず履歴だけ積む
    m_editor.Undo().Record(std::make_unique<NS::Editor::ObjectSnapshotCommand>(
        m_componentEditBaselineId, m_componentEditBaseline, std::move(*after)));
}

bool LevelEditorController::ApplyMaterialToSelected(std::string_view matPath)
{
    NS::Application* app = NS::Application::Get();
    if (m_editorToolMode != EditorToolMode::Object || app == nullptr)
    {
        return false;
    }

    if (m_selectedObjectId == NS::Obj::k_NoObjectId)
    {
        return false;
    }
    NS::Obj::Actor* go = SelectedObjectActor();
    if (go == nullptr)
    {
        return false;
    }

    NS::Obj::Model* mesh = go->ModelPart();
    if (mesh == nullptr)
    {
        return false;
    }

    const NS::Obj::LoadedMaterial loaded = app->Assets().LoadMaterial(matPath);
    if (loaded.material == nullptr)
    {
        return false;
    }

    // .mat パスは ContentRoot 相対で持つ。材質の正データは matRef なので live component へ書き込む
    const std::string stored = RelativeToRoot(matPath, NS::OS::FileSystem::ContentRoot());

    // 差替前を忠実に写す。matRef を live へ書き込み、差替後との差分を undo 履歴へ積む
    std::optional<nlohmann::json> before = m_applier.CaptureObject(m_selectedObjectId);

    mesh->SetMaterial(loaded.material);
    mesh->SetBaseColor(loaded.baseColor);
    mesh->SetMaterialRef(stored);

    std::optional<nlohmann::json> after = m_applier.CaptureObject(m_selectedObjectId);
    if (before && after && !(*before == *after))
    {
        m_editor.Undo().Record(std::make_unique<NS::Editor::ObjectSnapshotCommand>(
            m_selectedObjectId, std::move(before), std::move(after)));
    }
    return true;
}
