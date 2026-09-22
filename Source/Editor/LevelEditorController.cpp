#include "Editor/LevelEditorController.h"

#include "Editor/EditorObjects.h"
#include "Editor/LevelFilePaths.h"
#include "Editor/Undo/CompositeCommand.h"
#include "Editor/Undo/ObjectSnapshotCommand.h"
#include "Game/Level/BlockObject.h"
#include "Game/Level/Respawner.h"
#include "Game/Player.h"
#include "Game/Player/PlayerComponent.h"
#include "Runtime/App/Application.h"
#include "Runtime/Core/AABB.h"
#include "Runtime/Platform/Clock.h"
#include "Runtime/Platform/Filesystem.h"
#include "Runtime/Core/Logger.h"
#include "Runtime/Core/Math.h"
#include "Runtime/Core/OBB.h"
#include "Runtime/Graphics/DebugDraw.h"
#include "Runtime/Object/AssetManager.h"
#include "Runtime/Object/Components/BoxCollider.h"
#include "Runtime/Object/Components/CameraBrain.h"
#include "Runtime/Object/Components/CameraComponent.h"
#include "Runtime/Object/Components/CapsuleCollider.h"
#include "Runtime/Object/Components/MeshRenderer.h"
#include "Runtime/Object/Components/PlacedVirtualCamera.h"
#include "Runtime/Object/Components/PlayerInput.h"
#include "Runtime/Object/Components/SlopeCollider.h"
#include "Runtime/Object/Components/SphereCollider.h"
#include "Runtime/Object/Components/ThirdPersonFollow.h"
#include "Runtime/Object/Components/TransformComponent.h"
#include "Runtime/Object/Components/VirtualCamera.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Scene/Scene.h"
#include "Runtime/Object/Scene/SceneJson.h"
#include "Runtime/Platform/Input.h"
#include "Runtime/Platform/Keyboard.h"

#include <algorithm>

namespace
{
    std::string RelativeToRoot(std::string_view absPath, std::string_view root)
    {
        const std::string absNorm = NS::Platform::FileSystem::Normalize(absPath);
        const std::string rootNorm = NS::Platform::FileSystem::Normalize(root);
        if (absNorm.size() > rootNorm.size() + 1 &&
            ::_strnicmp(absNorm.c_str(), rootNorm.c_str(), rootNorm.size()) == 0 && absNorm[rootNorm.size()] == '/')
        {
            return absNorm.substr(rootNorm.size() + 1);
        }
        return absNorm;
    }

    // カメラの視錐台を描く時の far。vcam の既定 1000 のままだと錐台が画面に収まらないので近くで切る
    constexpr float k_CameraGizmoFar = 8.0f;

    // 編集復帰の視点ブレンド秒。Brain の vcam 切替の既定 0.35 秒と揃え、モード切替の繋ぎを同じ感触にする
    constexpr float k_EditBlendSeconds = 0.35f;

    // 配置物 1 体の当たり形状を線で描く。Box は回転込み OBB、球とカプセルは実形状、slope は collider 由来の AABB
    void DrawColliderWireframe(NS::Obj::GameObject& object, const NS::Core::Color& color) noexcept
    {
        if (auto* box = object.FindComponent<NS::Obj::BoxCollider>())
        {
            NS::Gfx::DebugDraw::OBB(box->WorldOBB(), color);
        }
        else if (auto* sphere = object.FindComponent<NS::Obj::SphereCollider>())
        {
            NS::Gfx::DebugDraw::Sphere(sphere->WorldSphere(), color);
        }
        else if (auto* capsule = object.FindComponent<NS::Obj::CapsuleCollider>())
        {
            NS::Phys::Capsule worldCapsule = capsule->WorldCapsule();
            worldCapsule.axis.Normalize();
            NS::Gfx::DebugDraw::Capsule(
                worldCapsule.center, worldCapsule.axis * worldCapsule.halfHeight, worldCapsule.radius, color);
        }
        else if (auto* slope = object.FindComponent<NS::Obj::SlopeCollider>())
        {
            // 斜面は三角の集まりなので、包む箱を出して面の広がりを見せる
            const auto tris = slope->WorldTriangles();
            NS::Core::Vector3 lo = tris[0].v0;
            NS::Core::Vector3 hi = lo;
            for (const auto& tri : tris)
            {
                for (const NS::Core::Vector3* vertex : {&tri.v0, &tri.v1, &tri.v2})
                {
                    lo = NS::Core::Vector3::Min(lo, *vertex);
                    hi = NS::Core::Vector3::Max(hi, *vertex);
                }
            }
            NS::Gfx::DebugDraw::AABB(NS::Core::AABB{(lo + hi) * 0.5f, (hi - lo) * 0.5f}, color);
        }
    }

    // a→b を 0.5m を目安に等分し 1 区間おきに線を引いて点線にする。DebugDraw に破線が無いので描画側で
    // 間引く。辺長からセグメント数を出すので、長い辺でも刻みが粗くならない
    void DrawDashedLine(const NS::Core::Vector3& a, const NS::Core::Vector3& b, const NS::Core::Color& color) noexcept
    {
        const float length = (b - a).Length();
        const int rawSegments = static_cast<int>(length / 0.5f);
        const int segments = std::max(rawSegments, 2);
        for (int i = 0; i < segments; i += 2)
        {
            const float t0 = static_cast<float>(i) / static_cast<float>(segments);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
            NS::Gfx::DebugDraw::Line(NS::Core::Vector3::Lerp(a, b, t0), NS::Core::Vector3::Lerp(a, b, t1), color);
        }
    }

    // カメラ pose の視錐台を四角錐の点線で描く。視点から far 面 4 隅へ 4 本 + far 面の 4 辺で、向きと画角を見せる
    // target==position や up と視線が平行な縮退では基底が作れないので何も描かない
    void DrawCameraFrustum(const NS::Obj::CameraPose& pose, float aspect, const NS::Core::Color& color) noexcept
    {
        NS::Core::Vector3 forward = pose.target - pose.position;
        if (forward.LengthSquared() < 1e-6f)
            return;
        forward.Normalize();
        NS::Core::Vector3 right = pose.up.Cross(forward);
        if (right.LengthSquared() < 1e-6f)
            return;
        right.Normalize();
        const NS::Core::Vector3 up = forward.Cross(right);

        const float halfHeight = std::tan(pose.fovY.value * 0.5f) * k_CameraGizmoFar;
        const float halfWidth = halfHeight * aspect;
        const NS::Core::Vector3 farCenter = pose.position + forward * k_CameraGizmoFar;
        const NS::Core::Vector3 topLeft = farCenter + up * halfHeight - right * halfWidth;
        const NS::Core::Vector3 topRight = farCenter + up * halfHeight + right * halfWidth;
        const NS::Core::Vector3 bottomLeft = farCenter - up * halfHeight - right * halfWidth;
        const NS::Core::Vector3 bottomRight = farCenter - up * halfHeight + right * halfWidth;

        DrawDashedLine(pose.position, topLeft, color);
        DrawDashedLine(pose.position, topRight, color);
        DrawDashedLine(pose.position, bottomLeft, color);
        DrawDashedLine(pose.position, bottomRight, color);
        DrawDashedLine(topLeft, topRight, color);
        DrawDashedLine(topRight, bottomRight, color);
        DrawDashedLine(bottomRight, bottomLeft, color);
        DrawDashedLine(bottomLeft, topLeft, color);
    }

    // 視点マーカーのワールド空間での半径。カメラから遠いほど半径を伸ばし、画面上の見かけサイズを一定に近づける
    // 見かけ寸法はワールド空間の半径 / clip.w に比例するので、半径を clip.w に比例させると相殺されて一定になる
    [[nodiscard]] float CameraMarkerHalf(const NS::Core::Vector3& center, const NS::Core::Matrix& vp) noexcept
    {
        const float baseHalf = 0.3f;
        const NS::Core::Vector4 clip =
            NS::Core::Vector4::Transform(NS::Core::Vector4{center.x, center.y, center.z, 1.0f}, vp);
        // clip.w がほぼ 0 になるカメラ至近や背面では基準半径へフォールバックする
        if (clip.w <= 1.0e-3f)
            return baseHalf;
        // 深度 10 までは基準半径、これより遠いほど深度に比例して伸ばし画面上一定に近づける
        const float scale = clip.w / 10.0f;
        return baseHalf * std::max(scale, 1.0f);
    }

    // 子を先、自分を後の順で永続 id を集める。削除はこの順で流し、undo は逆順に親から戻る
    void CollectSubtreeIds(const NS::Obj::GameObject& root, std::vector<std::uint32_t>& out)
    {
        for (const NS::Obj::GameObject* child : root.Children())
        {
            if (child != nullptr && !child->IsTransient())
                CollectSubtreeIds(*child, out);
        }
        out.push_back(root.Id());
    }

    // 1 本なら包まずそのまま返す。CompositeCommand を挟むのは複数を 1 回の undo で往復させたい時だけ
    std::unique_ptr<NS::Editor::ICommand> MakeUndoUnit(std::vector<std::unique_ptr<NS::Editor::ICommand>> commands)
    {
        if (commands.size() == 1)
            return std::move(commands.front());
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
        if (m_mode == Mode::Play)
        {
            if (auto* app = NS::App::Application::Get())
            {
                const bool paused = m_scene->IsSimulationPaused();
                app->Window().SetCursorVisible(paused);
                app->Window().SetCursorLocked(!paused);
                app->Input().Mouse().SetRelativeMode(!paused);
            }
        }
    }
}

NS::Obj::SceneEnvironment& LevelEditorController::Environment() noexcept
{
    return m_scene->Environment();
}

const NS::Obj::ObjectList& LevelEditorController::Objects() const noexcept
{
    return m_scene->Objects();
}

NS::Obj::CameraBrain* LevelEditorController::Brain() const noexcept
{
    if (m_scene == nullptr)
        return nullptr;
    return m_scene->CameraBrain();
}

NS::Obj::CameraComponent* LevelEditorController::MainCamera() const noexcept
{
    if (m_scene == nullptr)
        return nullptr;
    return m_scene->MainCamera();
}

void LevelEditorController::Setup(NS::UI::ImGuiContext* imgui)
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr || m_scene == nullptr)
        return;

    m_imgui = imgui;

    // 編集の投影設定で、遠景を 5000 まで見せ near 0.1 は既定
    // far は EditorCamera の k_MaxDistance より広く取り、最大ズームアウトでも地形を映す
    m_editorCamera.SetNearPlane(0.1f);
    m_editorCamera.SetFarPlane(5000.0f);
    m_editorCamera.SetFovY(NS::Core::ToRadians(NS::Core::Degrees{60.0f}));

    // 初期視点はプレイヤーの位置を中心に少し引いた位置から見下ろす。不在なら原点
    NS::Obj::GameObject* bootPlayer = FindPlayer(m_scene->Objects());
    NS::Core::Vector3 startCenter{0.0f, 0.0f, 0.0f};
    if (bootPlayer != nullptr)
        startCenter = bootPlayer->Root().Position();
    m_editorCamera.SetCenter(startCenter);
    // 起動直後は出現地点の block を真ん中近めに見せる距離。1m cube が画面の十数 % を占める
    m_editorCamera.SetDistance(5.0f);

    // 保存は live 実体から作り、読込は取込関数がデータを実体へ写して用済みにする
    m_editor.SetCaptureLevelFn([this]() { return m_scene->CaptureLiveToSceneData(); });
    m_editor.SetLoadLevelFn([this](NS::Obj::SceneData&& fresh) { m_scene->LoadFromData(std::move(fresh)); });
    // grid 編集・undo は適用経路を通して live へ写す。セル照会・採番は live 側から引く
    m_editor.SetApplier(&m_applier);
    m_editor.SetFindCellObjectFn([this](std::int16_t x, std::int16_t y, std::int16_t z) {
        return NS::Editor::FindObjectIdAtCell(m_scene->Objects(), x, y, z);
    });
    m_editor.SetCollectCellsFn([this]() {
        std::vector<NS::Editor::EditorMode::CellCoord> cells;
        cells.reserve(m_scene->Objects().ObjectCount());
        for (NS::Obj::GameObject* objPtr : m_scene->Objects())
        {
            NS::Obj::GameObject& object = *objPtr;
            if (!NS::Editor::IsCellBrushObject(object))
                continue;
            cells.push_back(NS::Editor::EditorMode::CellCoord{
                NS::Editor::ObjectCellX(object), NS::Editor::ObjectCellY(object), NS::Editor::ObjectCellZ(object)});
        }
        return cells;
    });
    m_editor.SetAllocateIdFn([this]() { return m_scene->Objects().AllocateObjectId(); });
    m_editor.SetInput(&app->Input());
    m_editor.SetImGui(imgui);
    m_editor.SetCameraComponent(MainCamera());
    m_editor.SetActive(true);
    // 読み込みで組み上がり済なので、初回 Tick の貼り直しを省く
    m_editor.ClearLevelDirty();

    // 起動シーンが実在するのに読めていない時は印す。終了保存が元ファイルを潰さず退避名へ逃げる
    if (const auto bootPath = NS::Editor::BuildLevelPath("Scenes/new_scene"))
    {
        if (NS::Platform::FileSystem::Exists(*bootPath))
        {
            NS::Obj::SceneData probe;
            if (!NS::Obj::LoadSceneFromJsonFile(probe, *bootPath))
                m_editor.MarkBootLevelLoadFailed();
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
        return;
    m_mode = Mode::Play;
    // モード遷移へ押しっぱなしを持ち越さないよう消す
    if (auto* app = NS::App::Application::Get())
    {
        app->Input().Keyboard().ClearState();
        app->Input().Mouse().ClearState();
    }
    // live が唯一の出所なので組み直しは要らない。編集で動いた当たりだけ張り直してプレイへ入る
    m_scene->SyncPhysics();

    // プレイの間の判定と編集復帰の姿は、突入時に凍結したスナップショットを読む
    (void)m_scene->BeginPlayBaseline();
    if (auto* player = FindPlayer(m_scene->Objects()))
    {
        // 編集で休止させた自機と入力を起こす。休止させる側は LeavePlayForEdit
        if (auto* movement = player->FindComponent<NS::Game::Player::PlayerComponent>())
            movement->SetActive(true);
        if (auto* input = player->FindComponent<NS::Obj::PlayerInput>())
            input->SetActive(true);
    }
    // 走行を最初から。手順は出荷と同じ respawner の持ち物
    m_scene->Objects().ForEachComponent<NS::Game::Level::Respawner>(
        [](NS::Game::Level::Respawner& respawner) { respawner.RestartRun(); });
    // 追従カメラも配置物の 1 体。プレイの間だけ有効化する
    m_scene->Objects().ForEachComponent<NS::Obj::ThirdPersonFollow>(
        [](NS::Obj::ThirdPersonFollow& follow) { follow.SetActive(true); });
    // 編集の自由視点からプレイ視点へ、vcam 切替と同じブレンドで繋ぐ
    if (auto* brain = Brain())
        brain->BeginBlendFrom(m_editorCamera.Pose());
    // 世界を回す。止まっているのは編集モードの間だけ
    m_scene->SetSimulationEnabled(true);

    // プレイ突入はカーソルを消し、マウスを相対モードにして視点操作をカーソル位置から切り離す
    if (auto* app = NS::App::Application::Get())
    {
        app->Window().SetCursorVisible(false);
        // 固定しないとクリックが他のパネルへ落ち、押しっぱなしの体当たり入力が届かないフレームができる
        app->Window().SetCursorLocked(true);
        app->Input().Mouse().SetRelativeMode(true);
    }

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    m_editor.SetActive(false);
}

void LevelEditorController::EnterEdit() noexcept
{
    if (m_mode == Mode::Edit)
        return;
    m_mode = Mode::Edit;
    // モード遷移へ押しっぱなしを持ち越さないよう消す
    if (auto* app = NS::App::Application::Get())
    {
        app->Input().Keyboard().ClearState();
        app->Input().Mouse().ClearState();
    }
    // プレイを終え、凍結スナップショットから編集の姿へ組み直す
    LeavePlayForEdit();
    // プレイ視点から自由視点へ繋ぐ。始点は直前まで実カメラに書かれていた pose
    if (auto* brain = Brain())
    {
        m_editBlendFrom = brain->LastPose();
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
        return;
    m_scene->StepSimulation();
}

void LevelEditorController::LeavePlayForEdit()
{
    // free-fly カメラはこの外で editor が握る
    if (m_scene == nullptr)
        return;
    // 編集モードの間は世界を止める
    m_scene->SetSimulationEnabled(false);

    // プレイは試走。位置・生成・破棄・演出の進行を live に残さず、突入時の凍結から世界を組み直す
    // 演出の破棄もゴールの旗戻しも組み直しが済ませるので、個別の後始末は置かない
    // 一時オブジェクトの実カメラは凍結に写らないが、Rebuild が退避して残す
    NS::Obj::SceneData baseline = m_scene->PlayBaseline();
    m_scene->LoadFromData(std::move(baseline));

    if (auto* player = FindPlayer(m_scene->Objects()))
    {
        // 操作系は生成時 active のまま組み上がるので、編集中だけ休止させる。起こす側は EnterPlay
        // follow と vcam はコンストラクタが休止で作るので、ここで寝かせる行は要らない
        if (auto* movement = player->FindComponent<NS::Game::Player::PlayerComponent>())
            movement->SetActive(false);
        if (auto* input = player->FindComponent<NS::Obj::PlayerInput>())
            input->SetActive(false);
    }

    // 編集モードはカーソルを出し、相対モードも解いてカーソル位置ベースの操作へ戻す
    if (auto* app = NS::App::Application::Get())
    {
        app->Window().SetCursorVisible(true);
        app->Window().SetCursorLocked(false);
        app->Input().Mouse().SetRelativeMode(false);
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
        if (auto* app = NS::App::Application::Get())
            app->Window().SetCursorLockPoint(x + width / 2, y + height / 2);
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
        return m_gameViewRect;
    // 未設定時は全画面をフォールバックとする。ウィンドウ不在は 0 サイズ
    NS::Editor::ViewRect full{};
    if (auto* app = NS::App::Application::Get())
    {
        const NS::Core::Size2D size = app->Window().Size();
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
        return;
    m_editorCamera.ApplyInput(input);
}

std::optional<NS::Obj::CameraPose> LevelEditorController::SceneViewPose() noexcept
{
    // 編集中は実カメラ (TickEdit が free-fly の pose を書く) 任せ。プレイ中だけ自由視点を上書きする
    if (m_mode != Mode::Play)
        return std::nullopt;
    return m_editorCamera.Pose();
}

std::optional<NS::Obj::CameraPose> LevelEditorController::GameViewPose() noexcept
{
    // プレイ中は Brain (follow・ブレンド維持) 任せ。編集中だけゲームカメラを上書きする
    if (m_mode == Mode::Play || Brain() == nullptr)
        return std::nullopt;
    return Brain()->EvaluateTopPose(1.0f);
}

void LevelEditorController::SetSceneViews(std::vector<NS::Obj::SceneView> views)
{
    if (m_scene != nullptr)
        m_scene->SetSceneViews(std::move(views));
}

void LevelEditorController::Tick()
{
    // プレイ中のクリア / 死亡は応答 component が出荷と同じ手順で完結させる
    // editor は割り込まず、編集へ戻るのは Tab / Pause modal の明示操作だけ
    if (m_mode == Mode::Edit)
        TickEdit();
}

void LevelEditorController::TickEdit()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // F5 で編集中の HLSL を再起動なしで反映する。プレイ中の F5 はエディタ UI の表示トグルに使うため
    // 編集モードのここでだけ再読み込みする。ImGui 入力中は誤爆を防ぐため無効化する
    if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::Platform::Key::F5))
        app->Assets().ReloadAllShaders();

    // Esc: Object モードで選択中ならまず選択解除に使い終了させない
    if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
    {
        if (m_editorToolMode == EditorToolMode::Object && m_gizmo.Selected() != nullptr)
        {
            m_gizmo.ClearSelection();
            return;
        }
        NS::App::Application::Quit();
        return;
    }

    m_editorCamera.Tick();

    // free-fly 更新後に実カメラへ反映し、ギズモ / 編集の ray-pick が当フレームの視点を使えるようにする
    // 編集中は active な vcam が無く Brain は実カメラに触れないので、この書き込みが上書きされずに残る
    if (auto* camera = MainCamera())
    {
        NS::Obj::CameraPose pose = m_editorCamera.Pose();
        if (m_editBlending)
        {
            m_editBlendElapsed += NS::Platform::FrameTimer::FixedDelta();
            const float t = std::min(m_editBlendElapsed / k_EditBlendSeconds, 1.0f);
            const float eased = t * t * (3.0f - 2.0f * t); // smoothstep で ease-in-out
            pose = NS::Obj::CameraPose::Lerp(m_editBlendFrom, pose, eased);
            if (t >= 1.0f)
                m_editBlending = false;
        }
        camera->ApplyPose(pose);
    }

    // 同時に 1 モードだけがクリックと R を消費する。Object 中は grid 入力を抑制しギズモへ回す
    // モード切替は Editor の UI ボタンが SetObjectToolActive で入れる。Tab は Edit↔Play 専用
    const bool objectMode = (m_editorToolMode == EditorToolMode::Object);
    m_gizmo.SetActive(objectMode);
    m_editor.SetInputSuppressed(objectMode);

    if (objectMode && Brain())
    {
        // 追従カメラの Root を実プレイ視点位置へ寄せてからギズモを回す。判定箱とギズモがその位置に出る
        SyncFollowCameraPoses();

        // 毎フレーム live な scene から候補 span を作り直し、選択を id から今のオブジェクトへ引き直す
        // 作り直さないと編集復帰 / undo の rebuild で破棄された実体を指したままになる
        RefreshGizmoSelectables();
        ResolveSelectionFromId();

        const auto vp = Brain()->ViewProjection();
        const bool wasDragging = m_gizmoWasDragging;
        m_gizmo.Tick(vp, CurrentViewRect(), m_gameViewHovered);

        // ビューポートでのギズモ選択変化を選択 id と Inspector が見る派生添字へ追従させる
        CaptureSelectionFromGizmo();

        // 追従カメラを掴んでいたら Root 位置を初期姿勢へ逆算し components へ保存する。位置の書き戻しはこちら
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
            ApplyDragToFollowers();
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

void LevelEditorController::Render()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    if (m_mode != Mode::Edit)
    {
        // プレイ中も Scene には当たりを見せる。動いている形をそのまま追えるよう選択に関わらず全部出す
        // 線を積むのは Scene が映っているフレームだけ。ビュー列の先頭が Scene なので、
        // 溜めた線は Scene の描画で消え、ゲーム画面へは残らない
        if (m_sceneViewVisible)
            RenderColliderWireframes(true);
        return;
    }

    m_editor.RenderCursorPreview();
    if (Brain())
        RenderCameraGizmos(Brain()->ViewProjection(), app->Window().Size());
    RenderColliderWireframes(false);
    RenderSelectionOutlines();
    // 蓄積した DebugDraw 線をシーン描画後・ImGui 前にまとめて 1 描画する
    if (Brain())
        NS::Gfx::DebugDraw::Flush(app->Renderer(), Brain()->ViewProjection());
    // Object モードはブラシを置かないので Build モードの時だけ出す
    // Game ビュー前面などで編集ビューが隠れているフレームは、ゲーム画面へ被せないよう出さない
    if (!ObjectToolActive() && !m_gameViewHidden)
        m_editor.Palette().Render(CurrentViewRect());
    // Object モードのギズモは最前面の drawlist に重ねる
    if (m_gizmo.IsActive() && Brain())
        m_gizmo.Render(Brain()->ViewProjection(), CurrentViewRect());
}

void LevelEditorController::SetObjectToolActive(bool active) noexcept
{
    const EditorToolMode next = [active]() -> EditorToolMode {
        if (active)
            return EditorToolMode::Object;
        return EditorToolMode::Build;
    }();
    if (next == m_editorToolMode)
        return;
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
        return NS::Obj::k_NoObjectIndex;
    const auto& objects = m_scene->Objects();
    for (std::size_t i = 0; i < objects.ObjectCount(); ++i)
        if (objects.ObjectAt(i)->Id() == m_selectedObjectId)
            return i;
    return NS::Obj::k_NoObjectIndex;
}

bool LevelEditorController::HasInspectableSelection() const noexcept
{
    return m_selectedObjectId != NS::Obj::k_NoObjectId &&
           m_scene->Objects().FindByObjectId(m_selectedObjectId) != nullptr;
}

NS::Obj::GameObject* LevelEditorController::SelectedObjectGameObject() noexcept
{
    // 選択の真実は永続 id。player も含め全配置物が ObjectList に居るので id で引く
    if (m_selectedObjectId == NS::Obj::k_NoObjectId)
        return nullptr;
    return m_scene->Objects().FindByObjectId(m_selectedObjectId);
}

bool LevelEditorController::SelectedIsPlayerObject() const noexcept
{
    NS::Obj::GameObject* player = FindPlayer(m_scene->Objects());
    return player != nullptr && m_selectedObjectId != NS::Obj::k_NoObjectId && m_selectedObjectId == player->Id();
}

std::vector<NS::Obj::ObjectRefLocation> LevelEditorController::ReferencesToSelected()
{
    if (m_selectedObjectId == NS::Obj::k_NoObjectId)
        return {};
    return NS::Obj::FindReferencesTo(m_scene->Objects(), m_selectedObjectId);
}

void LevelEditorController::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectablePickable.clear();
    m_selectablePtrs.reserve(m_scene->Objects().ObjectCount());
    m_selectablePickable.reserve(m_scene->Objects().ObjectCount());

    // 可視メッシュを持つ候補は 1、見えないカメラ等は 0。ギズモは 1 の候補を先に選ぶ
    const auto pushSelectable = [this](NS::Obj::GameObject* object) {
        m_selectablePtrs.push_back(object);
        const bool hasVisual = object->FindComponent<NS::Obj::MeshRenderer>() != nullptr;
        std::uint8_t pickable = std::uint8_t{0};
        if (hasVisual)
            pickable = std::uint8_t{1};
        m_selectablePickable.push_back(pickable);
    };

    // 全配置物をギズモ候補に積む。判定箱はギズモがクリックの時に配置物から引く
    for (NS::Obj::GameObject* object : m_scene->Objects())
    {
        if (object->IsTransient())
            continue;
        pushSelectable(object);
    }

    m_gizmo.SetSelectableObjects(m_selectablePtrs, m_selectablePickable);
}

NS::Obj::ThirdPersonFollow* LevelEditorController::SelectedFollowCamera() noexcept
{
    if (NS::Obj::GameObject* go = SelectedObjectGameObject())
        return go->FindComponent<NS::Obj::ThirdPersonFollow>();
    return nullptr;
}

void LevelEditorController::SyncFollowCameraPoses()
{
    // 追従カメラは位置を持たないので、edit 中は実プレイの視点位置へ Root を寄せて frustum / pick / ギズモを出す
    // ドラッグ中の選択カメラだけは gizmo が Root を握るため触らず、その位置を初期姿勢へ逆算する側に任せる
    const auto& objects = m_scene->Objects();
    for (NS::Obj::GameObject* object : objects)
    {
        auto* follow = object->FindComponent<NS::Obj::ThirdPersonFollow>();
        if (follow == nullptr)
            continue;
        if (m_gizmo.IsDragging() && object->Id() == m_selectedObjectId)
            continue;
        object->Root().SetPosition(follow->EvaluatePose(1.0f).position);
    }
}

void LevelEditorController::ApplyFollowCameraGizmoDrag()
{
    // ドラッグ中の追従カメラは、gizmo が動かした Root 位置から初期姿勢の yaw/pitch/距離を逆算して
    // live component へ書き戻す。Root 位置は初期姿勢由来なので保存対象は component 側になる
    if (!m_gizmo.IsDragging())
        return;
    NS::Obj::ThirdPersonFollow* follow = SelectedFollowCamera();
    if (follow == nullptr)
        return;
    NS::Obj::GameObject* go = SelectedObjectGameObject();
    if (go == nullptr)
        return;
    follow->SetInitialPoseFromCameraPosition(go->Root().Position());
}

void LevelEditorController::SelectObjectByIndex(std::size_t index) noexcept
{
    // 選択の真実は id。添字が動いても id から引き直せる
    const NS::Obj::GameObject* object = m_scene->Objects().ObjectAt(index);
    if (object == nullptr)
    {
        SelectObjectById(NS::Obj::k_NoObjectId);
        return;
    }
    SelectObjectById(object->Id());
}

void LevelEditorController::SelectObjectById(std::uint32_t id) noexcept
{
    m_selectionIds.clear();
    if (id != NS::Obj::k_NoObjectId)
        m_selectionIds.push_back(id);
    SetPrimarySelection(id);
}

bool LevelEditorController::IsObjectSelected(std::uint32_t id) const noexcept
{
    if (id == NS::Obj::k_NoObjectId)
        return false;
    return std::find(m_selectionIds.begin(), m_selectionIds.end(), id) != m_selectionIds.end();
}

void LevelEditorController::ToggleObjectSelection(std::uint32_t id) noexcept
{
    if (id == NS::Obj::k_NoObjectId)
        return;

    const auto it = std::find(m_selectionIds.begin(), m_selectionIds.end(), id);
    if (it != m_selectionIds.end())
    {
        m_selectionIds.erase(it);
        if (m_selectedObjectId != id)
            return;
        // 主対象を外したので、残っている中の直近へ譲る
        std::uint32_t next = NS::Obj::k_NoObjectId;
        if (!m_selectionIds.empty())
            next = m_selectionIds.back();
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
        m_selectionIds.push_back(primary);
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
    if (NS::Obj::GameObject* go = m_scene->Objects().FindByObjectId(id))
    {
        m_gizmo.SetSelected(&go->Root());
        m_lastGizmoSelected = m_gizmo.Selected();
        return;
    }
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::RenderCameraGizmos(const NS::Core::Matrix& viewProjection,
                                               NS::Core::Size2D viewport) noexcept
{
    // edit 中、各カメラの視錐台を点線の四角錐で、視点位置を小箱で可視化する。据え置きは進入トリガ AABB も出す
    // 選択中は強調色にする。追従カメラは pose がプレイヤー基準なので、錐台はプレイ中に居る視点位置へ出る
    const auto& objects = m_scene->Objects();
    // 錐台の横幅は実ビューポート比で出す。viewport が潰れている時だけ 16:9 目安へフォールバックする
    const float aspect = [viewport]() -> float {
        if (viewport.height > 0)
            return static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
        return 16.0f / 9.0f;
    }();
    for (NS::Obj::GameObject* object : objects)
    {
        auto* vcam = object->FindComponent<NS::Obj::VirtualCamera>();
        if (vcam == nullptr)
            continue;
        const bool selected = (object->Id() == m_selectedObjectId);
        const NS::Core::Color camColor = [selected]() -> NS::Core::Color {
            if (selected)
                return NS::Core::Color{1.0f, 0.55f, 0.10f, 1.0f};
            return NS::Core::Color{1.0f, 0.85f, 0.10f, 1.0f};
        }();

        const NS::Obj::CameraPose pose = vcam->EvaluatePose(1.0f);
        DrawCameraFrustum(pose, aspect, camColor);
        const float markerHalf = CameraMarkerHalf(pose.position, viewProjection);
        NS::Gfx::DebugDraw::AABB(
            NS::Core::AABB{pose.position, NS::Core::Vector3{markerHalf, markerHalf, markerHalf}}, camColor);

        // 据え置きカメラだけ進入トリガ範囲を出す。追従には無い
        if (auto* placed = object->FindComponent<NS::Obj::PlacedVirtualCamera>())
        {
            const NS::Core::Color triggerColor = [selected]() -> NS::Core::Color {
                if (selected)
                    return NS::Core::Color{1.0f, 0.55f, 0.10f, 1.0f};
                return NS::Core::Color{0.20f, 0.70f, 1.0f, 1.0f};
            }();
            NS::Gfx::DebugDraw::AABB(NS::Core::AABB{placed->TriggerCenter(), placed->TriggerExtent()},
                                          triggerColor);
        }
    }
}

void LevelEditorController::RenderSelectionOutlines() noexcept
{
    // ギズモが出るのは主対象だけなので、一緒に選んでいる分は枠で見せる
    if (m_selectionIds.size() < 2)
        return;

    const NS::Core::Color color{1.0f, 0.65f, 0.15f, 1.0f};
    for (const std::uint32_t id : m_selectionIds)
    {
        NS::Obj::GameObject* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
            continue;

        const NS::Core::Matrix world = object->Root().WorldMatrix();
        const NS::Core::Vector3 scale = object->Root().Scale();

        // 行の基底が各軸の向き。正規化して大きさは halfExtent へ回す
        NS::Core::OBB obb{};
        obb.center = NS::Core::Vector3{world._41, world._42, world._43};
        obb.axisX = NS::Core::Vector3{world._11, world._12, world._13};
        obb.axisY = NS::Core::Vector3{world._21, world._22, world._23};
        obb.axisZ = NS::Core::Vector3{world._31, world._32, world._33};
        obb.axisX.Normalize();
        obb.axisY.Normalize();
        obb.axisZ.Normalize();
        obb.halfExtentX = std::abs(scale.x) * NS::Game::Level::k_CellHalfExtents.x;
        obb.halfExtentY = std::abs(scale.y) * NS::Game::Level::k_CellHalfExtents.y;
        obb.halfExtentZ = std::abs(scale.z) * NS::Game::Level::k_CellHalfExtents.z;
        NS::Gfx::DebugDraw::OBB(obb, color);
    }
}

void LevelEditorController::RenderColliderWireframes(bool all) noexcept
{
    const NS::Core::Color color{0.35f, 1.0f, 0.45f, 1.0f};

    // プレイ中は動いている形を追えるよう全部出す
    if (all)
    {
        for (NS::Obj::GameObject* objPtr : m_scene->Objects())
            DrawColliderWireframe(*objPtr, color);
        return;
    }

    // 編集中は選んだ分だけ。全部出すと線が重なって、どれの形か読み取れない
    for (const std::uint32_t id : m_selectionIds)
    {
        if (NS::Obj::GameObject* objPtr = m_scene->Objects().FindByObjectId(id))
            DrawColliderWireframe(*objPtr, color);
    }
}

void LevelEditorController::CaptureSelectionFromGizmo() noexcept
{
    NS::Obj::Transform* selected = m_gizmo.Selected();
    // Hierarchy で選んだ非 gizmo 選択を毎フレーム潰さないため前フレームと同じなら据え置き
    if (selected == m_lastGizmoSelected)
        return;
    m_lastGizmoSelected = selected;
    if (selected == nullptr)
    {
        m_selectionIds.clear();
        m_selectedObjectId = NS::Obj::k_NoObjectId;
        return;
    }
    // player も ObjectList に居るので、ビューポートのピックと同じ経路で引ける
    for (NS::Obj::GameObject* object : m_scene->Objects())
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
        return;

    // 選択 id が現存する配置物を指すなら gizmo に貼り直す。不在なら gizmo を外す
    // player も ObjectList に居るので id で引ける。rebuild を跨いでも掴める状態を保つ
    if (m_selectedObjectId != NS::Obj::k_NoObjectId)
    {
        if (NS::Obj::GameObject* go = m_scene->Objects().FindByObjectId(m_selectedObjectId))
        {
            NS::Obj::Transform* root = &go->Root();
            if (m_gizmo.Selected() != root)
                m_gizmo.SetSelected(root);
            m_lastGizmoSelected = root;
            return;
        }
    }
    if (m_gizmo.Selected() != nullptr)
        m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SetSelectedFreePosition(NS::Core::Vector3 position)
{
    // live の Root を直接動かす。undo の記録は CommitTransformEdit、当たりの追従は SyncPhysics が担う
    if (NS::Obj::GameObject* go = SelectedObjectGameObject())
    {
        go->Root().SetPosition(position);
        if (auto* transform = go->FindComponent<NS::Obj::TransformComponent>())
            MirrorPlayEditToBaseline(*transform, NS::Obj::k_PositionFieldName);
    }
}

void LevelEditorController::SetSelectedFreeRotation(NS::Core::Quaternion rotation)
{
    if (NS::Obj::GameObject* go = SelectedObjectGameObject())
    {
        go->Root().SetRotation(rotation);
        if (auto* transform = go->FindComponent<NS::Obj::TransformComponent>())
            MirrorPlayEditToBaseline(*transform, NS::Obj::k_RotationFieldName);
    }
}

void LevelEditorController::SetSelectedFreeScale(NS::Core::Vector3 scale)
{
    // ImGui の入力で 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
    constexpr float k_MinScale = 0.01f;
    scale.x = std::max(scale.x, k_MinScale);
    scale.y = std::max(scale.y, k_MinScale);
    scale.z = std::max(scale.z, k_MinScale);
    if (NS::Obj::GameObject* go = SelectedObjectGameObject())
    {
        go->Root().SetScale(scale);
        if (auto* transform = go->FindComponent<NS::Obj::TransformComponent>())
            MirrorPlayEditToBaseline(*transform, NS::Obj::k_ScaleFieldName);
    }
}

void LevelEditorController::MirrorPlayEditToBaseline(const NS::Obj::Component& comp, std::string_view fieldName)
{
    // 写すのはプレイ中だけ。編集モードで写すと次のプレイ突入の捕捉と二重管理になる
    if (m_mode != Mode::Play || m_scene == nullptr)
        return;
    m_scene->WritePlayBaselineField(comp, fieldName);
}

void LevelEditorController::AddObject()
{
    AddPrimitive(NS::Editor::PrimitiveKind::Cube);
}

void LevelEditorController::AddPrimitive(NS::Editor::PrimitiveKind kind)
{
    // 新規オブジェクトは編集視点の中心あたりへ置く
    const NS::Core::Vector3 center = m_editorCamera.Center();

    // 構成を先に確定してから transform を書き込む。採番・履歴・選択は PushCreateObject が担う
    NS::Obj::ObjectData object{};
    object.components = NS::Editor::MakePrimitiveComponents(kind);
    NS::Obj::SetObjectPosition(object, center);
    PushCreateObject(std::move(object));
}

void LevelEditorController::AddObjectWithMesh(std::string_view meshPath)
{
    // 参照は ContentRoot 相対で持つ。build 時にこの文字列から実体を引く
    const std::string meshRef = RelativeToRoot(meshPath, NS::Platform::FileSystem::ContentRoot());

    const NS::Core::Vector3 center = m_editorCamera.Center();

    // 描いた形と当たりをずらさない。MeshCollider が描画と同じ三角形から当たりを作る
    NS::Obj::ObjectData object{};
    object.components =
        nlohmann::json::array({NS::Game::Level::MakeMeshRendererEntry(meshRef, "", NS::Game::Level::k_SolidBaseColor),
                               NS::Obj::MakeComponentEntry("MeshCollider")});
    NS::Obj::SetObjectPosition(object, center);
    object.name = NS::Platform::FileSystem::Stem(meshPath);

    PushCreateObject(std::move(object));
}

void LevelEditorController::PushCreateObject(NS::Obj::ObjectData object)
{
    // 新規配置物に永続 id を 1 個振る
    const std::uint32_t id = m_scene->Objects().AllocateObjectId();
    object.objectId = id;

    // 追加を undo 履歴へ。Do が live へ 1 体差し込んで全配置物を組み直す
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
        return;
    std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(id);
    if (!before)
        return;
    if (before->name == name)
        return; // 同じ名前で履歴を汚さない

    NS::Obj::ObjectData after = *before;
    after.name = std::string(name);

    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)),
                         m_applier);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

bool LevelEditorController::SetObjectParent(std::uint32_t id, std::uint32_t parentId)
{
    if (id == NS::Obj::k_NoObjectId || id == parentId)
        return false;

    NS::Obj::GameObject* child = m_scene->Objects().FindByObjectId(id);
    if (child == nullptr)
        return false;

    NS::Obj::GameObject* parent = nullptr;
    if (parentId != NS::Obj::k_NoObjectId)
    {
        parent = m_scene->Objects().FindByObjectId(parentId);
        if (parent == nullptr)
            return false;
        // 自分の子孫を親にすると輪になる
        for (NS::Obj::GameObject* ancestor = parent; ancestor != nullptr; ancestor = ancestor->Parent())
        {
            if (ancestor == child)
                return false;
        }
    }

    std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(id);
    if (!before || before->parentId == parentId)
        return false;

    // 親空間が変わっても見た目が動かないよう、今のワールド変換から新しいローカル変換を割り出す
    NS::Core::Matrix local = child->Root().WorldMatrix();
    if (parent != nullptr)
        local *= parent->Root().WorldMatrix().Invert();

    NS::Obj::ObjectData after = *before;
    after.parentId = parentId;

    NS::Core::Vector3 scale{};
    NS::Core::Quaternion rotation{};
    NS::Core::Vector3 position{};
    if (local.Decompose(scale, rotation, position))
    {
        NS::Obj::SetObjectPosition(after, position);
        NS::Obj::SetObjectRotation(after, rotation);
        NS::Obj::SetObjectScale(after, scale);
    }
    else
    {
        // 分解できない変換は local を触らず親だけ差し替える。見た目は動くが編集は通す
        NS_LOG_WARN(App, "変換を分解できないため object {} の見た目を保てなかった", id);
    }

    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)),
                         m_applier);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    return true;
}

void LevelEditorController::AddComponentToSelected(std::string_view typeName)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Obj::k_NoObjectId)
        return;
    std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(id);
    if (!before)
        return;

    // 現状の忠実な姿へ 1 個足す。field 無しの雛形は build 時に既定値で起きる
    NS::Obj::ObjectData after = *before;
    after.components.push_back(NS::Obj::MakeComponentEntry(typeName));

    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)),
                         m_applier);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::RemoveComponentFromSelected(std::size_t componentIndex)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Obj::k_NoObjectId)
        return;
    std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(id);
    if (!before)
        return;

    // component が 0 個の配置物は build で消えるので最後の 1 個 / 範囲外は消さない。履歴も汚さない
    const nlohmann::json& components = before->components;
    if (componentIndex >= components.size() || components.size() <= 1)
        return;
    // 入力 component を消すと player を操作できなくなる。transform は root なので同様に守る
    const std::string_view typeName = NS::Obj::ComponentEntryType(components[componentIndex]);
    if (typeName == "PlayerInput" || typeName == "TransformComponent")
        return;

    NS::Obj::ObjectData after = *before;
    after.components.erase(after.components.begin() + static_cast<std::ptrdiff_t>(componentIndex));

    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)),
                         m_applier);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::SetComponentEnabledOnSelected(std::size_t componentIndex, bool enabled)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Obj::k_NoObjectId)
        return;
    std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(id);
    if (!before)
        return;
    if (componentIndex >= before->components.size())
        return;
    // 入力 component を休止させると player が動かなくなる。transform は root なので同様に守る
    const std::string_view typeName = NS::Obj::ComponentEntryType(before->components[componentIndex]);
    if (typeName == "PlayerInput" || typeName == "TransformComponent")
        return;

    NS::Obj::ObjectData after = *before;
    NS::Obj::SetComponentEntryEnabled(after.components[componentIndex], enabled);

    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)),
                         m_applier);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DuplicateSelectedObject()
{
    if (m_selectionIds.empty())
        return;

    const NS::Obj::GameObject* player = FindPlayer(m_scene->Objects());

    // 選択物の忠実コピーを新しい永続 id で増やす
    std::vector<std::pair<std::uint32_t, NS::Obj::ObjectData>> copies;
    copies.reserve(m_selectionIds.size());
    for (const std::uint32_t id : m_selectionIds)
    {
        // プレイヤーは必ず 1 体。複製で 2 体目を作らせない
        if (player != nullptr && id == player->Id())
            continue;
        std::optional<NS::Obj::ObjectData> source = m_applier.CaptureObject(id);
        if (!source)
            continue;
        const std::uint32_t newId = m_scene->Objects().AllocateObjectId();
        source->objectId = newId;
        copies.emplace_back(id, std::move(*source));
    }
    if (copies.empty())
        return;

    // 親も一緒に複製したなら、コピーの親はコピー側へ向ける。親が選択外ならそのまま元の親へぶら下がる
    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    std::vector<std::uint32_t> created;
    commands.reserve(copies.size());
    created.reserve(copies.size());
    for (auto& [sourceId, copy] : copies)
    {
        (void)sourceId;
        if (copy.parentId != NS::Obj::k_NoObjectId)
        {
            for (const auto& [otherSourceId, otherCopy] : copies)
            {
                if (otherSourceId == copy.parentId)
                {
                    copy.parentId = otherCopy.objectId;
                    break;
                }
            }
        }
        created.push_back(copy.objectId);
        commands.push_back(
            std::make_unique<NS::Editor::ObjectSnapshotCommand>(copy.objectId, std::nullopt, std::move(copy)));
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
        return;

    // 親だけ消すと子が宙に浮くので、ぶら下がっている分もまとめて消す
    std::vector<std::uint32_t> victims;
    for (const std::uint32_t id : m_selectionIds)
    {
        NS::Obj::GameObject* target = m_scene->Objects().FindByObjectId(id);
        if (target != nullptr)
            CollectSubtreeIds(*target, victims);
    }

    // 親と子を両方選んでいると同じ物が二度並ぶ。葉が先の順は崩さずに重複だけ落とす
    std::vector<std::uint32_t> ordered;
    ordered.reserve(victims.size());
    for (const std::uint32_t victim : victims)
    {
        if (std::find(ordered.begin(), ordered.end(), victim) == ordered.end())
            ordered.push_back(victim);
    }

    // プレイヤーが消えるとレベルが遊べなくなる。子孫に紛れていても止める
    const NS::Obj::GameObject* player = FindPlayer(m_scene->Objects());
    if (player != nullptr && std::find(ordered.begin(), ordered.end(), player->Id()) != ordered.end())
    {
        NS_LOG_WARN(App, "プレイヤーを含むため削除しなかった");
        return;
    }

    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    commands.reserve(ordered.size());
    for (const std::uint32_t victim : ordered)
    {
        std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(victim);
        if (!before)
            continue;
        commands.push_back(
            std::make_unique<NS::Editor::ObjectSnapshotCommand>(victim, std::move(before), std::nullopt));
    }
    if (commands.empty())
        return;

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
        return;

    // 選んだ分を全部収める。中心は重心、距離は一番外側までの広がりで決める
    NS::Core::Vector3 sum{0.0f, 0.0f, 0.0f};
    std::vector<NS::Core::Vector3> centers;
    float extent = 0.0f;
    centers.reserve(m_selectionIds.size());
    for (const std::uint32_t id : m_selectionIds)
    {
        NS::Obj::GameObject* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
            continue;
        const NS::Core::Matrix world = object->Root().WorldMatrix();
        const NS::Core::Vector3 center{world._41, world._42, world._43};
        centers.push_back(center);
        sum += center;

        const NS::Core::Vector3 scale = object->Root().Scale();
        const float half =
            std::max({std::abs(scale.x), std::abs(scale.y), std::abs(scale.z)}) * NS::Game::Level::k_CellHalfExtents.y;
        extent = std::max(extent, half);
    }
    if (centers.empty())
        return;

    const NS::Core::Vector3 center = sum / static_cast<float>(centers.size());
    for (const NS::Core::Vector3& each : centers)
        extent = std::max(extent, (each - center).Length());

    const float distance = std::max(extent * 4.0f, NS::Editor::EditorCamera::k_MinDistance);

    m_editorCamera.SetCenter(center);
    m_editorCamera.SetDistance(distance);
}

void LevelEditorController::CaptureDragFollowers() noexcept
{
    m_dragFollowers.clear();
    if (m_selectionIds.size() < 2)
        return;

    NS::Obj::GameObject* primary = m_scene->Objects().FindByObjectId(m_selectedObjectId);
    if (primary == nullptr)
        return;
    m_dragPrimaryWorld = primary->Root().WorldMatrix();

    for (const std::uint32_t id : m_selectionIds)
    {
        if (id == m_selectedObjectId)
            continue;
        NS::Obj::GameObject* object = m_scene->Objects().FindByObjectId(id);
        if (object == nullptr)
            continue;

        // 選択中の物にぶら下がっている分は親が動けば付いてくる。二重に動かさない
        bool underSelected = false;
        for (const NS::Obj::GameObject* ancestor = object->Parent(); ancestor != nullptr;
             ancestor = ancestor->Parent())
        {
            if (IsObjectSelected(ancestor->Id()))
            {
                underSelected = true;
                break;
            }
        }
        if (underSelected)
            continue;

        m_dragFollowers.push_back(DragFollower{id, object->Root().WorldMatrix()});
    }
}

void LevelEditorController::ApplyDragToFollowers() noexcept
{
    if (m_dragFollowers.empty())
        return;
    NS::Obj::GameObject* primary = m_scene->Objects().FindByObjectId(m_selectedObjectId);
    if (primary == nullptr)
        return;

    // 主対象が動いた分をワールド空間の差分として取り、残りへ同じだけ効かせる
    const NS::Core::Matrix delta = m_dragPrimaryWorld.Invert() * primary->Root().WorldMatrix();
    for (const DragFollower& follower : m_dragFollowers)
    {
        NS::Obj::GameObject* object = m_scene->Objects().FindByObjectId(follower.id);
        if (object == nullptr)
            continue;

        NS::Core::Matrix local = follower.world * delta;
        if (const NS::Obj::GameObject* parent = object->Parent())
            local *= parent->Root().WorldMatrix().Invert();

        NS::Core::Vector3 scale{};
        NS::Core::Quaternion rotation{};
        NS::Core::Vector3 position{};
        if (!local.Decompose(scale, rotation, position))
            continue;
        object->Root().SetPosition(position);
        object->Root().SetRotation(rotation);
        object->Root().SetScale(scale);
    }
}

void LevelEditorController::CopyComponentToClipboard(std::size_t componentIndex)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Obj::k_NoObjectId)
        return;
    // live の忠実な写しから 1 component を控える。Inspector でライブ編集した値ごと入る
    const std::optional<NS::Obj::ObjectData> captured = m_applier.CaptureObject(id);
    if (!captured || componentIndex >= captured->components.size())
        return;
    m_componentClipboard = captured->components[componentIndex];
}

void LevelEditorController::PasteClipboardComponentToSelected()
{
    if (!m_componentClipboard)
        return;
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Obj::k_NoObjectId)
        return;
    std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(id);
    if (!before)
        return;

    // 同型がすでにあっても末尾へ重ねて貼り、上書きはしない
    NS::Obj::ObjectData after = *before;
    after.components.push_back(*m_componentClipboard);

    m_editor.Undo().Push(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, std::move(before), std::move(after)),
                         m_applier);

    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::BeginTransformEdit() noexcept
{
    if (m_transformEditing)
        return;
    m_editBaselines.clear();
    // 選択している分をまとめて控える。動かなかった物は確定時に落ちる
    for (const std::uint32_t id : m_selectionIds)
    {
        std::optional<NS::Obj::ObjectData> baseline = m_applier.CaptureObject(id);
        if (baseline)
            m_editBaselines.emplace_back(id, std::move(*baseline));
    }
    if (m_editBaselines.empty())
        return;
    m_transformEditing = true;
}

void LevelEditorController::CommitTransformEdit() noexcept
{
    if (!m_transformEditing)
        return;
    m_transformEditing = false;

    // ドラッグは live Root を既に動かしている。after は live の忠実な写し。baseline と同じなら履歴に積まない
    std::vector<std::unique_ptr<NS::Editor::ICommand>> commands;
    for (auto& [id, baseline] : m_editBaselines)
    {
        std::optional<NS::Obj::ObjectData> after = m_applier.CaptureObject(id);
        if (!after || *after == baseline)
            continue;
        commands.push_back(std::make_unique<NS::Editor::ObjectSnapshotCommand>(id, baseline, std::move(*after)));
    }
    m_editBaselines.clear();
    if (commands.empty())
        return;

    // live は既に after なので Do を呼ばず履歴だけ積む。undo で baseline へ、redo で after へ戻す
    m_editor.Undo().Record(MakeUndoUnit(std::move(commands)));
}

void LevelEditorController::BeginComponentEdit() noexcept
{
    if (m_componentEditing)
        return;
    std::optional<NS::Obj::ObjectData> baseline = m_applier.CaptureObject(m_selectedObjectId);
    if (!baseline)
        return;
    m_componentEditBaseline = std::move(*baseline);
    m_componentEditBaselineId = m_selectedObjectId;
    m_componentEditing = true;
}

void LevelEditorController::CommitComponentEdit() noexcept
{
    if (!m_componentEditing)
        return;
    m_componentEditing = false;

    // リフレクション編集は live component へ直接入っている。after は live の忠実な写しで、baseline と同じなら積まない
    std::optional<NS::Obj::ObjectData> after = m_applier.CaptureObject(m_componentEditBaselineId);
    if (!after || *after == m_componentEditBaseline)
        return;

    // live は既に after なので Do を呼ばず履歴だけ積む
    m_editor.Undo().Record(std::make_unique<NS::Editor::ObjectSnapshotCommand>(
        m_componentEditBaselineId, m_componentEditBaseline, std::move(*after)));
}

bool LevelEditorController::ApplyMaterialToSelected(std::string_view matPath)
{
    auto* app = NS::App::Application::Get();
    if (m_editorToolMode != EditorToolMode::Object || app == nullptr)
        return false;

    if (m_selectedObjectId == NS::Obj::k_NoObjectId)
        return false;
    NS::Obj::GameObject* go = SelectedObjectGameObject();
    if (go == nullptr)
        return false;

    auto* mesh = go->FindComponent<NS::Obj::MeshRenderer>();
    if (mesh == nullptr)
        return false;

    const auto loaded = app->Assets().LoadMaterial(matPath);
    if (loaded.material == nullptr)
        return false;

    // .mat パスは ContentRoot 相対で持つ。材質の正データは matRef なので live component へ書き込む
    const std::string stored = RelativeToRoot(matPath, NS::Platform::FileSystem::ContentRoot());

    // 差替前を忠実に写す。matRef を live へ書き込み、差替後との差分を undo 履歴へ積む
    std::optional<NS::Obj::ObjectData> before = m_applier.CaptureObject(m_selectedObjectId);

    mesh->SetMaterial(loaded.material);
    mesh->SetBaseColor(loaded.baseColor);
    mesh->SetMaterialRef(stored);

    std::optional<NS::Obj::ObjectData> after = m_applier.CaptureObject(m_selectedObjectId);
    if (before && after && !(*before == *after))
    {
        m_editor.Undo().Record(std::make_unique<NS::Editor::ObjectSnapshotCommand>(
            m_selectedObjectId, std::move(before), std::move(after)));
    }
    return true;
}
