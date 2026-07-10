#include "Editor/LevelEditorController.h"

#include "Editor/EditorCameraRig.h"
#include "Game/LevelPlayScene.h"
#include "Game/Player.h"

#include "Editor/ComponentClipboard.h"
#include "Editor/PlayerTuning.h"
#include "Editor/Undo/AddComponentCommand.h"
#include "Editor/Undo/AddObjectCommand.h"
#include "Editor/Undo/DuplicateObjectCommand.h"
#include "Editor/Undo/RemoveComponentCommand.h"
#include "Editor/Undo/TransformCommand.h"
#include "Framework/UI/ImGuiContext.h"
#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/LevelObjects.h"
#include "Game/Theme/ThemeRegistry.h"

namespace
{
    constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

    // カメラ frustum の far は実カメラだと 1000 で錐台が画面外になるため表示用に近くで切る
    constexpr float kCameraGizmoFar = 8.0f;

    // a→b を 0.5m 刻みで等分し 1 区間おきに線を引いて点線にする。 DebugDraw に dashed が無いので描画側で
    // 間引く。 辺長からセグメント数を出すので、 長い辺も短い辺も破線ピッチが揃う
    void DrawDashedLine(const NS::Math::Vector3& a, const NS::Math::Vector3& b, const NS::Math::Color& color) noexcept
    {
        const float length = (b - a).Length();
        const int rawSegments = static_cast<int>(length / 0.5f);
        const int segments = std::max(rawSegments, 2);
        for (int i = 0; i < segments; i += 2)
        {
            const float t0 = static_cast<float>(i) / static_cast<float>(segments);
            const float t1 = static_cast<float>(i + 1) / static_cast<float>(segments);
            NS::Graphics::DebugDraw::Line(NS::Math::Vector3::Lerp(a, b, t0), NS::Math::Vector3::Lerp(a, b, t1), color);
        }
    }

    // カメラ pose の視錐台を四角錐の点線で描く。 視点から far 面 4 隅へ 4 本 + far 面の 4 辺で、 向きと画角を見せる
    // target==position や up と視線が平行な縮退では基底が作れないので何も描かない
    void DrawCameraFrustum(const NS::Scene::CameraPose& pose, float aspect, const NS::Math::Color& color) noexcept
    {
        NS::Math::Vector3 forward = pose.target - pose.position;
        if (forward.LengthSquared() < 1e-6f)
            return;
        forward.Normalize();
        NS::Math::Vector3 right = pose.up.Cross(forward);
        if (right.LengthSquared() < 1e-6f)
            return;
        right.Normalize();
        const NS::Math::Vector3 up = forward.Cross(right);

        const float halfHeight = std::tan(pose.fovY.value * 0.5f) * kCameraGizmoFar;
        const float halfWidth = halfHeight * aspect;
        const NS::Math::Vector3 farCenter = pose.position + forward * kCameraGizmoFar;
        const NS::Math::Vector3 topLeft = farCenter + up * halfHeight - right * halfWidth;
        const NS::Math::Vector3 topRight = farCenter + up * halfHeight + right * halfWidth;
        const NS::Math::Vector3 bottomLeft = farCenter - up * halfHeight - right * halfWidth;
        const NS::Math::Vector3 bottomRight = farCenter - up * halfHeight + right * halfWidth;

        DrawDashedLine(pose.position, topLeft, color);
        DrawDashedLine(pose.position, topRight, color);
        DrawDashedLine(pose.position, bottomLeft, color);
        DrawDashedLine(pose.position, bottomRight, color);
        DrawDashedLine(topLeft, topRight, color);
        DrawDashedLine(topRight, bottomRight, color);
        DrawDashedLine(bottomRight, bottomLeft, color);
        DrawDashedLine(bottomLeft, topLeft, color);
    }

    // 視点マーカーの world 半径。 カメラから遠いほど半径を伸ばし、 画面上の見かけサイズを一定に近づける
    // 見かけ寸法は world 半径 / clip.w に比例するので、 半径を clip.w に比例させると相殺されて一定になる
    // 近距離は基準半径を下限に据え、 遠距離だけ伸ばす
    [[nodiscard]] float CameraMarkerHalf(const NS::Math::Vector3& center, const NS::Math::Matrix& vp) noexcept
    {
        const float baseHalf = 0.3f;
        const NS::Math::Vector4 clip =
            NS::Math::Vector4::Transform(NS::Math::Vector4{center.x, center.y, center.z, 1.0f}, vp);
        // clip.w がほぼ 0、 カメラ至近や背面では深度で割らず基準半径へ退避する
        if (clip.w <= 1.0e-3f)
            return baseHalf;
        // 深度 10 までは基準半径、 これより遠いほど深度に比例して伸ばし画面上一定に近づける
        const float scale = clip.w / 10.0f;
        return baseHalf * std::max(scale, 1.0f);
    }
} // namespace

LevelEditorController::LevelEditorController(LevelPlayScene* scene) noexcept : m_scene(scene) {}

LevelEditorController::~LevelEditorController() = default;

NS::Scene::SceneData& LevelEditorController::Level() noexcept
{
    return m_scene->Level();
}

NS::Game::Level::PlayState& LevelEditorController::Play() noexcept
{
    return m_scene->Director().Flow().Play();
}

NS::Scene::CameraBrainComponent* LevelEditorController::Brain() const noexcept
{
    NS::Scene::CameraSubsystem* cameras = nullptr;
    if (m_scene != nullptr)
        cameras = m_scene->GetSubsystem<NS::Scene::CameraSubsystem>();
    if (cameras != nullptr)
        return cameras->Brain();
    return nullptr;
}

NS::Scene::CameraComponent* LevelEditorController::MainCamera() const noexcept
{
    NS::Scene::CameraSubsystem* cameras = nullptr;
    if (m_scene != nullptr)
        cameras = m_scene->GetSubsystem<NS::Scene::CameraSubsystem>();
    if (cameras != nullptr)
        return cameras->MainCamera();
    return nullptr;
}

void LevelEditorController::Setup(NS::UI::ImGuiContext* imgui)
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr || m_scene == nullptr)
        return;

    m_imgui = imgui;

    // 起動読込がプレイヤーを既定構成で合成していたら (新規 seed / 旧形式移行)、 保存済みテンプレートを
    // 写してから world を組み直す。 テンプレートは編集の道具なので取込は Game の読込ではなくここで行う
    if (m_scene->PlayerObjectSynthesized())
    {
        const std::size_t playerIndex = NS::Game::Level::FindPlayerObjectIndex(m_scene->Level());
        if (playerIndex != NS::Scene::kNoObjectIndex)
        {
            MergeSavedPlayerTuning(m_scene->Level().objects[playerIndex]);
            m_scene->RebuildWorld();
        }
    }

    // 編集モード専用の free-fly カメラを Player / follow camera と並列で立ち上げる
    // mouse + gamepad で Orbit / Pan / Zoom する
    m_editorCameraRig = std::make_unique<EditorCameraRig>();
    m_editorCameraRig->AttachScene(m_scene);

    // free-fly vcam の投影設定で、 編集は遠景を 5000 まで見せ near 0.1 は既定
    // far は EditorCameraComponent の kMaxDistance より広く取り、 最大ズームアウトでも地形を映す
    m_editorCameraRig->EditorCam().SetNearPlane(0.1f);
    m_editorCameraRig->EditorCam().SetFarPlane(5000.0f);
    m_editorCameraRig->EditorCam().SetFovY(NS::Math::ToRadians(NS::Math::Degrees{60.0f}));

    // 初期視点はプレイヤー実体の位置を中心に少し引いた位置から見下ろす。 不在なら原点
    NS::Math::Vector3 startCenter{0.0f, 0.0f, 0.0f};
    const std::size_t playerIndex = NS::Game::Level::FindPlayerObjectIndex(m_scene->Level());
    if (playerIndex != NS::Scene::kNoObjectIndex)
    {
        const NS::Scene::ObjectData& playerObject = m_scene->Level().objects[playerIndex];
        startCenter = NS::Math::Vector3{playerObject.positionX, playerObject.positionY, playerObject.positionZ};
    }
    m_editorCameraRig->EditorCam().SetCenter(startCenter);
    // 起動直後は出現地点の block を真ん中近めに見せる距離。 1m cube が画面の十数 % を占める
    m_editorCameraRig->EditorCam().SetDistance(5.0f);

    m_editorCameraRig->OnStart();

    // free-fly vcam を Brain へ登録する。 follow / area camera は scene が登録済
    if (Brain())
        Brain()->AddVirtualCamera(&m_editorCameraRig->EditorCam());

    m_editor.SetLevel(&m_scene->Level());
    m_editor.SetInput(&app->Input());
    m_editor.SetImGui(imgui);
    m_editor.SetCameraComponent(MainCamera());
    m_editor.SetActive(true);
    // scene が OnStart で rebuild 済なので、 初回 Tick の二重 rebuild を抑制
    m_editor.ClearLevelDirty();

    // ギズモに依存先を注入する。 選択候補は自由オブジェクト + grid solid ブロックを連結して渡す
    m_gizmo.SetInput(&app->Input());
    m_gizmo.SetImGui(imgui);
    RefreshGizmoSelectables();

    // scene は OnStart でプレイ開始済。 進行役を寝かせ player 凍結 / free-fly camera 有効の編集モードへ切替える
    m_scene->Director().Flow().ExitPlay();
    m_scene->Director().Flow().SetActive(false);
    m_editorCameraRig->EditorCam().SetActive(true);
    m_mode = Mode::Edit;
}

void LevelEditorController::Teardown()
{
    // ギズモは free オブジェクトの Transform を非所有参照するので、 scene 破棄前に選択を外す
    m_gizmo.ClearSelection();
    if (m_editorCameraRig)
    {
        // Brain は free-fly vcam を非所有参照する。 vcam を畳む前に Brain から外して dangling を避ける
        if (m_scene != nullptr && Brain())
            Brain()->RemoveVirtualCamera(&m_editorCameraRig->EditorCam());
        m_editorCameraRig->OnEndPlay();
    }
    m_editorCameraRig.reset();
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();
    m_selectablePickable.clear();
}

void LevelEditorController::EnterPlay() noexcept
{
    if (m_mode == Mode::Play)
        return;
    m_mode = Mode::Play;
    // 編集中の変形を確定した最新 level で world を組み直してから、 進行役を起こしてプレイへ入る
    m_scene->RebuildWorld();
    m_scene->Director().Flow().EnterPlay();
    m_scene->Director().Flow().SetActive(true);
    // プレイ突入の rebuild を跨いでも生ポインタが残らないよう、 候補と選択を実体へ解決し直す
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    m_editor.SetActive(false);
    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(false);
}

void LevelEditorController::EnterEdit() noexcept
{
    if (m_mode == Mode::Edit)
        return;
    m_mode = Mode::Edit;
    // 進行役を寝かせ player 凍結 / follow・area camera 休止 / play 状態リセットを行う
    m_scene->Director().Flow().ExitPlay();
    m_scene->Director().Flow().SetActive(false);
    // Play 中の rebuild を跨いだ選択を、 id から現在の実体へ貼り直してから編集へ戻る
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    m_editor.SetActive(true);
    if (m_editorCameraRig)
        m_editorCameraRig->EditorCam().SetActive(true);
}

void LevelEditorController::Tick()
{
    // プレイ中のクリア / 死亡は PlayFlowComponent が出荷と同じ暗転リスタートで完結させる。
    // editor は割り込まず、 編集へ戻るのは Tab / Pause modal の明示操作だけ
    if (m_mode == Mode::Edit)
        TickEdit();
}

void LevelEditorController::TickEdit()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // F5 で編集中の HLSL を再起動なしで反映する。 プレイ中の F5 はエディタ UI の表示トグルに使うため
    // 編集モードのここでだけ再読み込みする。 ImGui 入力中は誤爆を防ぐため無効化する
    if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::Platform::Key::F5))
    {
        app->Assets().ReloadAllShaders();
    }

    // Esc: Object モードで選択中ならまず選択解除に使い終了させない
    if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
    {
        if (m_editorToolMode == EditorToolMode::Object && m_gizmo.Selected() != nullptr)
        {
            m_gizmo.ClearSelection();
            // Camera の特殊選択も解除し、 次フレームの再貼り付けで掴み続けないようにする
            m_specialSelection = SpecialSelection::None;
            return;
        }
        NS::App::Application::Quit();
        return;
    }

    if (m_editorCameraRig)
    {
        m_editorCameraRig->Root().Snapshot();
        m_editorCameraRig->OnUpdate();
    }

    // free-fly 更新後に実カメラへ反映し、 ギズモ / 編集の ray-pick が当フレームの視点を使えるようにする
    if (Brain())
        Brain()->Evaluate(1.0f);

    // 同時に 1 モードだけが LMB/R/Ctrl+Z を消費する。 Object 中は grid 入力を抑制しギズモへ回す
    // モード切替は EditorLayer の UI ボタン SetObjectToolActive から行う。 Tab は Edit↔Play 専用
    const bool objectMode = (m_editorToolMode == EditorToolMode::Object);
    m_gizmo.SetActive(objectMode);
    m_editor.SetInputSuppressed(objectMode);

    if (objectMode && m_editorCameraRig && Brain())
    {
        // 追従カメラの Root を実プレイ視点位置へ寄せてから候補を作る。 pick 箱 / ギズモがその位置に出る
        SyncFollowCameraPoses();

        // 毎フレーム live な scene から候補 span を作り直し、 選択を id → 実体へ解決し直す
        // Play 突入 / undo / promote の rebuild を跨いでも生ポインタが残らない fail-safe の要
        RefreshGizmoSelectables();
        ResolveSelectionFromId();

        const auto vp = Brain()->ViewProjection();
        const auto viewport = app->Window().Size();
        const bool wasDragging = m_gizmoWasDragging;
        m_gizmo.Tick(vp, viewport);

        // ビューポートでのギズモ選択変化を選択 id と Inspector が見る派生添字へ追従させる
        CaptureSelectionFromGizmo();

        // 追従カメラを掴んでいたら Root 位置を初期姿勢へ逆算し components へ保存する。 位置の書き戻しはこちら
        ApplyFollowCameraGizmoDrag();

        // ギズモ変形の結果を live → model で ObjectData へ反映する。 begin/commit はこの model を基準にする
        // world に居ない実プレイヤーの player object への書き戻しも同じ関数が担う
        SyncFreeObjectTransforms();

        // ドラッグ開始で baseline 退避、 終了で TransformCommand を 1 つ確定する。 grid undo と同じ履歴
        // 追従カメラは Root でなく初期姿勢を変えるので、 Root PRS ベースの TransformCommand は積まない
        const bool nowDragging = m_gizmo.IsDragging();
        if (SelectedFollowCamera() == nullptr)
        {
            if (!wasDragging && nowDragging)
                BeginTransformEdit();
            else if (wasDragging && !nowDragging)
                CommitTransformEdit();
        }
        m_gizmoWasDragging = nowDragging;
    }
    else if (m_transformEditing)
    {
        // Object モードを抜けても未確定の変形があれば確定し、 記録されない変更を残さない
        CommitTransformEdit();
        m_gizmoWasDragging = false;
    }

    m_editor.Tick();
    if (m_editor.IsLevelDirty())
    {
        // 据え置きカメラの Brain 登録もプレイヤーの組み直しも RebuildWorld が面倒を見る
        m_scene->RebuildWorld();
        // undo / redo / ロードは objects を作り直す。 ロードは id が振り直され旧 id が別物に化けるため、
        // ここで選択 id を解除する。 候補 span と gizmo の貼り直しは次フレーム頭の解決に委ねる
        m_selectedObjectId = NS::Scene::kNoObjectId;
        m_selectedObjectIndex = NS::Scene::kNoObjectIndex;
        m_gizmo.ClearSelection();
        m_lastGizmoSelected = nullptr;
        m_editor.ClearLevelDirty();
    }
}

void LevelEditorController::Render()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // Debug provenance パネルの入力を毎フレーム退避する。出所は has_value の突き合わせで逆算するので
    // Resolve のホットパスに追跡を入れず、 環境 service の解決値と代表 object override をそのまま保持する
    if (auto* environment = m_scene->GetSubsystem<NS::Scene::EnvironmentSubsystem>())
    {
        m_debugResolvedSettings = environment->LastResolved();
        m_debugSceneOverride = environment->BuildOverride();
    }
    if (m_scene->PlayerRef())
        m_debugPlayerObjectOverride = m_scene->PlayerRef()->MeshComp().RenderOverride();

    if (m_mode != Mode::Edit)
        return;

    m_editor.RenderCursorPreview();
    if (Brain())
        RenderCameraGizmos(Brain()->ViewProjection(), app->Window().Size());
    RenderColliderWireframes();
    // 蓄積した DebugDraw 線をシーン描画後・ ImGui 前にまとめて 1 描画する
    if (Brain())
        NS::Graphics::DebugDraw::Flush(app->Renderer(), Brain()->ViewProjection());
    // Toolbar UI を ImGui 経由で描画する。 Debug / Development build のみ実機能
    m_editor.Palette().Render();
    // Object モードのギズモは最前面の drawlist に重ねる
    if (m_gizmo.IsActive() && Brain())
        m_gizmo.Render(Brain()->ViewProjection(), app->Window().Size());
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
        m_selectedObjectId = NS::Scene::kNoObjectId;
        m_selectedObjectIndex = NS::Scene::kNoObjectIndex;
        m_lastGizmoSelected = nullptr;
    }
}

bool LevelEditorController::HasInspectableSelection() const noexcept
{
    return m_selectedObjectIndex < m_scene->Level().objects.size();
}

NS::Scene::ObjectData LevelEditorController::SelectedObjectSnapshot() const noexcept
{
    if (m_selectedObjectIndex < m_scene->Level().objects.size())
        return m_scene->Level().objects[m_selectedObjectIndex];
    return NS::Scene::ObjectData{};
}

NS::Scene::GameObject* LevelEditorController::SelectedObjectGameObject() noexcept
{
    if (m_selectedObjectIndex >= m_scene->Level().objects.size())
        return nullptr;

    // player object は world に居ないため、 scene 所有の実 player を runtime 実体として返す
    if (SelectedIsPlayerObject())
        return m_scene->PlayerRef();

    // objects 添字 → runtime インスタンスの逆引き。 free / grid の別は ObjectData が握り runtime list は 1 本
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
        if (m_scene->World().SourceIndices()[i] == m_selectedObjectIndex)
            return m_scene->World().Objects()[i].get();
    return nullptr;
}

bool LevelEditorController::SelectedIsPlayerObject() const noexcept
{
    return HasInspectableSelection() &&
           m_selectedObjectIndex == NS::Game::Level::FindPlayerObjectIndex(m_scene->Level());
}

void LevelEditorController::SyncSelectedObjectComponentsFromComponent()
{
    if (m_selectedObjectIndex >= m_scene->Level().objects.size())
        return;

    NS::Scene::ObjectData& object = m_scene->Level().objects[m_selectedObjectIndex];

    // 反射編集で更新済の runtime コンポーネントを components データへ書き戻す。 BuildFromComponents が読むのは
    // components 側で、 ここを更新しないと次の rebuild で編集が失われる
    if (NS::Scene::GameObject* go = SelectedObjectGameObject())
        NS::Editor::WriteBackComponentEdits(*go, object);
}

NS::Scene::GameObject* LevelEditorController::CameraBrainObject() noexcept
{
    if (Brain())
        return Brain()->Owner();
    return nullptr;
}

NS::Scene::GameObject* LevelEditorController::ActiveVirtualCameraObject() noexcept
{
    if (Brain() == nullptr)
        return nullptr;
    NS::Scene::VirtualCameraComponent* active = Brain()->ActiveVirtualCamera();
    if (active != nullptr)
        return active->Owner();
    return nullptr;
}

void LevelEditorController::RefreshGizmoSelectables()
{
    m_selectablePtrs.clear();
    m_selectableHalfExtents.clear();
    m_selectablePickable.clear();
    m_selectablePtrs.reserve(m_scene->World().Objects().size());
    m_selectableHalfExtents.reserve(m_scene->World().Objects().size());
    m_selectablePickable.reserve(m_scene->World().Objects().size());

    // 可視メッシュを持つ候補は 1、 見えないカメラ等は 0。 ギズモは 1 の候補を優先して pick する
    const auto pushSelectable = [this](NS::Scene::GameObject* object) {
        m_selectablePtrs.push_back(object);
        m_selectableHalfExtents.push_back(kCellHalfExtents);
        const bool hasVisual = NS::Game::Blocks::FindComponent<NS::Scene::MeshRendererComponent>(*object) != nullptr;
        std::uint8_t pickable = std::uint8_t{0};
        if (hasVisual)
            pickable = std::uint8_t{1};
        m_selectablePickable.push_back(pickable);
    };

    // runtime list は 1 本。 全配置物をギズモ候補に積む。 pick OBB は Root().WorldMatrix() が scale 込みで
    // 持ち、 判定は逆変換した unit ローカル空間で行う。 ここで halfExtents に scale を乗せると二重適用になり、
    // 拡大した配置物の判定箱が scale^2 に膨らんで近くのクリックを先に奪うので unit のまま渡す
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
        pushSelectable(m_scene->World().Objects()[i].get());

    // 実プレイヤーも掴める。 player object は world で組まれないため、 live の実 player を候補に積み
    // ビューポート直クリックを player object の通常選択へ流す
    // pick OBB は player の cube mesh と同じ unit 半径。 world scale 0.8/1.8/0.8 は Root().WorldMatrix() が持つ
    if (m_scene->PlayerRef())
        pushSelectable(m_scene->PlayerRef());

    m_gizmo.SetSelectableObjects(m_selectablePtrs, m_selectableHalfExtents, m_selectablePickable);
}

void LevelEditorController::SyncFreeObjectTransforms()
{
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        const std::size_t objectIndex = m_scene->World().SourceIndices()[i];
        if (objectIndex >= m_scene->Level().objects.size())
            continue;
        NS::Scene::ObjectData& object = m_scene->Level().objects[objectIndex];
        // 追従カメラの Transform は実プレイ視点位置の同期先で真実の源でない。 位置は初期姿勢へ逆算して持つ
        if (m_scene->World().Objects()[i]->FindComponent<NS::Scene::ThirdPersonFollowComponent>() != nullptr)
            continue;

        const NS::Scene::Transform& root = m_scene->World().Objects()[i]->Root();
        const NS::Math::Vector3 position = root.Position();
        const NS::Math::Quaternion rotation = root.Rotation();
        const NS::Math::Vector3 scale = root.Scale();

        object.positionX = position.x;
        object.positionY = position.y;
        object.positionZ = position.z;
        object.rotationX = rotation.x;
        object.rotationY = rotation.y;
        object.rotationZ = rotation.z;
        object.rotationW = rotation.w;
        object.scaleX = scale.x;
        object.scaleY = scale.y;
        object.scaleZ = scale.z;
    }

    // player object は world に居ないため、 scene 所有の実 player から別途書き戻す
    const std::size_t playerIndex = NS::Game::Level::FindPlayerObjectIndex(m_scene->Level());
    if (playerIndex != NS::Scene::kNoObjectIndex && m_scene->PlayerRef())
    {
        NS::Scene::ObjectData& playerObject = m_scene->Level().objects[playerIndex];
        NS::Scene::Transform& root = m_scene->PlayerRef()->Root();
        const NS::Math::Vector3 position = root.Position();
        const NS::Math::Quaternion rotation = root.Rotation();
        const NS::Math::Vector3 scale = root.Scale();
        playerObject.positionX = position.x;
        playerObject.positionY = position.y;
        playerObject.positionZ = position.z;
        playerObject.rotationX = rotation.x;
        playerObject.rotationY = rotation.y;
        playerObject.rotationZ = rotation.z;
        playerObject.rotationW = rotation.w;
        playerObject.scaleX = scale.x;
        playerObject.scaleY = scale.y;
        playerObject.scaleZ = scale.z;
        // edit 中の実プレイヤーは scene が Snapshot しないため、 ここで previous=current に揃え補間ジッタを消す
        root.Snapshot();
    }
}

NS::Scene::ThirdPersonFollowComponent* LevelEditorController::SelectedFollowCamera() noexcept
{
    if (NS::Scene::GameObject* go = SelectedObjectGameObject())
        return go->FindComponent<NS::Scene::ThirdPersonFollowComponent>();
    return nullptr;
}

void LevelEditorController::SyncFollowCameraPoses()
{
    // 追従カメラは位置を持たないので、 edit 中は実プレイの視点位置へ Root を寄せて frustum / pick / ギズモを出す
    // ドラッグ中の選択カメラだけは gizmo が Root を握るため触らず、 その位置を初期姿勢へ逆算する側に任せる
    const auto& world = m_scene->World();
    for (std::size_t i = 0; i < world.Objects().size(); ++i)
    {
        auto* follow = world.Objects()[i]->FindComponent<NS::Scene::ThirdPersonFollowComponent>();
        if (follow == nullptr)
            continue;
        if (m_gizmo.IsDragging() && world.SourceIndices()[i] == m_selectedObjectIndex)
            continue;
        world.Objects()[i]->Root().SetPosition(follow->EvaluatePose(1.0f).position);
    }
}

void LevelEditorController::ApplyFollowCameraGizmoDrag()
{
    // ドラッグ中の追従カメラは、 gizmo が動かした Root 位置から初期姿勢の yaw/pitch/距離を逆算して data へ書き戻す
    // 位置は初期姿勢由来なので SyncFreeObjectTransforms でなくここで components 経由に保存する
    if (!m_gizmo.IsDragging())
        return;
    NS::Scene::ThirdPersonFollowComponent* follow = SelectedFollowCamera();
    if (follow == nullptr)
        return;
    NS::Scene::GameObject* go = SelectedObjectGameObject();
    if (go == nullptr || m_selectedObjectIndex >= m_scene->Level().objects.size())
        return;
    follow->SetInitialPoseFromCameraPosition(go->Root().Position());
    NS::Editor::WriteBackComponentEdits(*go, m_scene->Level().objects[m_selectedObjectIndex]);
}

void LevelEditorController::SelectObjectByIndex(std::size_t index) noexcept
{
    // オブジェクトと Camera の特殊選択は排他。 オブジェクトを選んだら解除する
    m_specialSelection = SpecialSelection::None;

    if (index >= m_scene->Level().objects.size())
    {
        m_selectedObjectId = NS::Scene::kNoObjectId;
        m_selectedObjectIndex = NS::Scene::kNoObjectIndex;
        return;
    }
    // 選択の真実は id。 索引が動いても id から引き直せる
    m_selectedObjectId = m_scene->Level().objects[index].objectId;
    m_selectedObjectIndex = index;

    // ハンドルを出すため Object ツールへ切替える。 Build のままだとギズモが描かれない
    SetObjectToolActive(true);

    // player object は world に居ないため、 実プレイヤーを掴んで動かせるようギズモへ直接貼る
    if (SelectedIsPlayerObject() && m_scene->PlayerRef())
    {
        m_gizmo.SetSelected(&m_scene->PlayerRef()->Root());
        m_lastGizmoSelected = m_gizmo.Selected();
        return;
    }

    // 選択した配置物の Root をギズモへ貼る。 runtime list は 1 本
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != index)
            continue;
        m_gizmo.SetSelected(&m_scene->World().Objects()[i]->Root());
        m_lastGizmoSelected = m_gizmo.Selected();
        return;
    }
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
}

void LevelEditorController::SelectCamera() noexcept
{
    m_selectedObjectId = NS::Scene::kNoObjectId;
    m_selectedObjectIndex = NS::Scene::kNoObjectIndex;
    m_gizmo.ClearSelection();
    m_lastGizmoSelected = nullptr;
    m_specialSelection = SpecialSelection::Camera;
}

void LevelEditorController::RenderCameraGizmos(const NS::Math::Matrix& viewProjection,
                                               NS::Math::Size2D viewport) noexcept
{
    // edit 中、 各カメラの視錐台を点線の四角錐で、 視点位置を小箱で可視化する。 据え置きは進入トリガ AABB も出す
    // 選択中は強調色にする。 追従カメラは pose がプレイヤー基準なので、 錐台はプレイ中に居る視点位置へ出る
    const auto& world = m_scene->World();
    // 錐台の横幅は実ビューポート比で出す。 viewport が潰れている時だけ 16:9 目安へ退避する
    const float aspect = [viewport]() -> float {
        if (viewport.height > 0)
            return static_cast<float>(viewport.width) / static_cast<float>(viewport.height);
        return 16.0f / 9.0f;
    }();
    for (std::size_t i = 0; i < world.Objects().size(); ++i)
    {
        auto* vcam = world.Objects()[i]->FindComponent<NS::Scene::VirtualCameraComponent>();
        if (vcam == nullptr)
            continue;
        const bool selected = (world.SourceIndices()[i] == m_selectedObjectIndex);
        const NS::Math::Color camColor = [selected]() -> NS::Math::Color {
            if (selected)
                return NS::Math::Color{1.0f, 0.55f, 0.10f, 1.0f};
            return NS::Math::Color{1.0f, 0.85f, 0.10f, 1.0f};
        }();

        const NS::Scene::CameraPose pose = vcam->EvaluatePose(1.0f);
        DrawCameraFrustum(pose, aspect, camColor);
        // 視点マーカーは遠いカメラでも潰れないよう、 深度に応じて world 半径を伸ばし画面上一定サイズに近づける
        const float markerHalf = CameraMarkerHalf(pose.position, viewProjection);
        NS::Graphics::DebugDraw::AABB(
            NS::Math::AABB{pose.position, NS::Math::Vector3{markerHalf, markerHalf, markerHalf}}, camColor);

        // 据え置きカメラだけ進入トリガ範囲を出す。 追従には無い
        if (auto* placed = world.Objects()[i]->FindComponent<NS::Scene::PlacedVirtualCamera>())
        {
            const NS::Math::Color triggerColor = [selected]() -> NS::Math::Color {
                if (selected)
                    return NS::Math::Color{1.0f, 0.55f, 0.10f, 1.0f};
                return NS::Math::Color{0.20f, 0.70f, 1.0f, 1.0f};
            }();
            NS::Graphics::DebugDraw::AABB(NS::Math::AABB{placed->TriggerCenter(), placed->TriggerExtent()},
                                          triggerColor);
        }
    }
}

void LevelEditorController::RenderColliderWireframes() noexcept
{
    // 配置物の当たり形状を可視化する。 Box は回転込み OBB、 球 / カプセル / slope は collider 由来の AABB
    const NS::Math::Color color{0.35f, 1.0f, 0.45f, 1.0f};

    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (auto* box =
                NS::Game::Blocks::FindComponent<NS::Scene::BoxColliderComponent>(*m_scene->World().Objects()[i]))
        {
            const NS::Physics::OBB obb = box->WorldOBB();
            NS::Graphics::DebugDraw::OBB(obb.center, obb.axisX, obb.axisY, obb.axisZ, obb.halfExtents, color);
        }
        else if (auto aabb = NS::Game::Blocks::ColliderWorldAABB(*m_scene->World().Objects()[i]))
        {
            NS::Graphics::DebugDraw::AABB(*aabb, color);
        }
    }
}

void LevelEditorController::CaptureSelectionFromGizmo() noexcept
{
    NS::Scene::Transform* selected = m_gizmo.Selected();
    // Hierarchy で選んだ非 gizmo 選択を毎フレーム潰さないため前フレームと同じなら据え置き
    if (selected == m_lastGizmoSelected)
        return;
    m_lastGizmoSelected = selected;
    if (selected == nullptr)
    {
        m_selectedObjectId = NS::Scene::kNoObjectId;
        m_selectedObjectIndex = NS::Scene::kNoObjectIndex;
        // ギズモが空クリック等で外れたら特殊選択も解除し、 再貼り付けで掴み続けないようにする
        m_specialSelection = SpecialSelection::None;
        return;
    }
    // ビューポートで実プレイヤーをピックしたら player object の通常選択へ流す。 移動 / 回転 / undo は同じ経路
    if (m_scene->PlayerRef() && selected == &m_scene->PlayerRef()->Root())
    {
        const std::size_t playerIndex = NS::Game::Level::FindPlayerObjectIndex(m_scene->Level());
        m_specialSelection = SpecialSelection::None;
        if (playerIndex != NS::Scene::kNoObjectIndex)
        {
            m_selectedObjectIndex = playerIndex;
            m_selectedObjectId = m_scene->Level().objects[playerIndex].objectId;
        }
        else
        {
            m_selectedObjectId = NS::Scene::kNoObjectId;
            m_selectedObjectIndex = NS::Scene::kNoObjectIndex;
        }
        return;
    }
    // ビューポートでのオブジェクト実ピックは Camera の特殊選択より優先する
    m_specialSelection = SpecialSelection::None;
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (&m_scene->World().Objects()[i]->Root() == selected)
        {
            m_selectedObjectIndex = m_scene->World().SourceIndices()[i];
            m_selectedObjectId = m_scene->Level().objects[m_selectedObjectIndex].objectId;
            return;
        }
    }
    m_selectedObjectId = NS::Scene::kNoObjectId;
    m_selectedObjectIndex = NS::Scene::kNoObjectIndex;
}

void LevelEditorController::ResolveSelectionFromId() noexcept
{
    // id → 現在の objects 添字。 delete / undo で添字はズレるので毎フレーム引き直す
    m_selectedObjectIndex = NS::Scene::FindObjectIndexById(m_scene->Level(), m_selectedObjectId);

    // SetSelected が進行中ドラッグを切ってしまうのでドラッグ中は gizmo の選択を貼り直さない
    if (m_gizmo.IsDragging())
        return;

    // 選択 id が自由オブジェクトを指すなら gizmo に貼り直す。 grid / 不在 / 特殊選択は gizmo を外す
    if (m_specialSelection == SpecialSelection::None && m_selectedObjectIndex != NS::Scene::kNoObjectIndex)
    {
        // player object は world に居ないため、 実プレイヤーへ貼り直す。 rebuild を跨いでも掴める状態を保つ
        if (SelectedIsPlayerObject() && m_scene->PlayerRef())
        {
            NS::Scene::Transform* root = &m_scene->PlayerRef()->Root();
            if (m_gizmo.Selected() != root)
                m_gizmo.SetSelected(root);
            m_lastGizmoSelected = root;
            return;
        }
        for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
        {
            if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
                continue;
            NS::Scene::Transform* root = &m_scene->World().Objects()[i]->Root();
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

void LevelEditorController::SetSelectedFreePosition(NS::Math::Vector3 position) noexcept
{
    // player object は world に居ないため live の実 player を直接動かす。 永続化は Sync が担う
    if (SelectedIsPlayerObject() && m_scene->PlayerRef())
    {
        m_scene->PlayerRef()->Root().SetPosition(position);
        return;
    }
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
            continue;
        m_scene->World().Objects()[i]->Root().SetPosition(position);
        return;
    }
}

void LevelEditorController::SetSelectedFreeRotation(NS::Math::Quaternion rotation) noexcept
{
    // 永続化は gizmo R と同じく SyncFreeObjectTransforms 経由で、 runtime Transform を真実の源にする
    if (SelectedIsPlayerObject() && m_scene->PlayerRef())
    {
        m_scene->PlayerRef()->Root().SetRotation(rotation);
        return;
    }
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
            continue;
        m_scene->World().Objects()[i]->Root().SetRotation(rotation);
        return;
    }
}

void LevelEditorController::SetSelectedFreeScale(NS::Math::Vector3 scale) noexcept
{
    // ImGui の入力で 0 / 負になると描画と当たり判定が壊れるため最小正値で止める
    constexpr float kMinScale = 0.01f;
    scale.x = std::max(scale.x, kMinScale);
    scale.y = std::max(scale.y, kMinScale);
    scale.z = std::max(scale.z, kMinScale);
    if (SelectedIsPlayerObject() && m_scene->PlayerRef())
    {
        m_scene->PlayerRef()->Root().SetScale(scale);
        return;
    }
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != m_selectedObjectIndex)
            continue;
        m_scene->World().Objects()[i]->Root().SetScale(scale);
        return;
    }
}

void LevelEditorController::AddObject()
{
    // 新規オブジェクトは編集視点の中心あたりへ置く。 Add Camera と同じ基準点
    NS::Math::Vector3 center{0.0f, 0.0f, 0.0f};
    if (m_editorCameraRig)
        center = m_editorCameraRig->EditorCam().Center();

    // 既定姿勢の自由配置物。 scale / rotation / collider は既定値のまま
    NS::Scene::ObjectData object{};
    object.positionX = center.x;
    object.positionY = center.y;
    object.positionZ = center.z;
    // 既定の素の cube を自由配置物として実 component で起こす
    object.components = NS::Game::Blocks::MakeFreeCubeComponents();

    // grid 設置と同じ undo 履歴へ載せる。 Do が objects 末尾へ append する
    m_editor.Undo().Push(std::make_unique<NS::Editor::AddObjectCommand>(object), m_scene->Level());

    // 追加した自由オブジェクトの runtime 実体を作り、 選択候補を貼り直して末尾の新規を選択する
    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    SelectObjectByIndex(m_scene->Level().objects.size() - 1);
}

void LevelEditorController::AddComponentToSelected(std::string_view typeName)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Scene::kNoObjectId)
        return;
    if (NS::Scene::FindObjectIndexById(m_scene->Level(), id) == NS::Scene::kNoObjectIndex)
        return;

    NS::Scene::ComponentData payload;
    payload.typeName = std::string(typeName);
    m_editor.Undo().Push(std::make_unique<NS::Editor::AddComponentCommand>(id, std::move(payload)), m_scene->Level());

    // components が変わったので runtime を組み直し、 同じ id の選択を貼り直す
    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::RemoveComponentFromSelected(std::size_t componentIndex)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Scene::kNoObjectId)
        return;
    const std::size_t objectIndex = NS::Scene::FindObjectIndexById(m_scene->Level(), id);
    if (objectIndex == NS::Scene::kNoObjectIndex)
        return;

    // 空構成は build で消えるゴーストになるので最後の 1 個 / 範囲外は消さない。 何もしないコマンドを積まず
    // undo 履歴も汚さない
    const std::vector<NS::Scene::ComponentData>& components = m_scene->Level().objects[objectIndex].components;
    if (componentIndex >= components.size() || components.size() <= 1)
        return;

    // プレイヤーの印である入力 component を消すと player object でなくなり出現位置ごと壊れるため消させない
    if (components[componentIndex].typeName == "PlayerInputComponent")
        return;

    m_editor.Undo().Push(std::make_unique<NS::Editor::RemoveComponentCommand>(id, componentIndex), m_scene->Level());

    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::DuplicateSelectedObject()
{
    // プレイヤーは必ず 1 体。 複製で 2 体目を作らせない
    if (SelectedIsPlayerObject())
        return;
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Scene::kNoObjectId)
        return;
    if (NS::Scene::FindObjectIndexById(m_scene->Level(), id) == NS::Scene::kNoObjectIndex)
        return;

    // 複製は objects 末尾へ積まれる。 組み直してから末尾を新しい選択にする
    m_editor.Undo().Push(std::make_unique<NS::Editor::DuplicateObjectCommand>(id), m_scene->Level());

    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    if (!m_scene->Level().objects.empty())
        SelectObjectByIndex(m_scene->Level().objects.size() - 1);
}

void LevelEditorController::CopyComponentToClipboard(std::size_t componentIndex)
{
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Scene::kNoObjectId)
        return;
    const std::size_t objectIndex = NS::Scene::FindObjectIndexById(m_scene->Level(), id);
    if (objectIndex == NS::Scene::kNoObjectIndex)
        return;
    const std::vector<NS::Scene::ComponentData>& dataComponents =
        m_scene->Level().objects[objectIndex].components;
    if (componentIndex >= dataComponents.size())
        return;

    // runtime の同添字コンポが同型なら Inspector でライブ編集した値ごと写す
    // 未登録型が混じり runtime と data の添字がずれた時は data モデルの値で写して取り違えを防ぐ
    if (NS::Scene::GameObject* go = SelectedObjectGameObject())
    {
        const std::vector<NS::Scene::Component*>& runtime = go->Components();
        if (componentIndex < runtime.size() && runtime[componentIndex] != nullptr)
        {
            NS::Scene::ComponentData captured = NS::Editor::CaptureComponentData(*runtime[componentIndex]);
            if (captured.typeName == dataComponents[componentIndex].typeName)
            {
                m_componentClipboard = std::move(captured);
                return;
            }
        }
    }
    m_componentClipboard = dataComponents[componentIndex];
}

void LevelEditorController::PasteClipboardComponentToSelected()
{
    if (!m_componentClipboard)
        return;
    const std::uint32_t id = m_selectedObjectId;
    if (id == NS::Scene::kNoObjectId)
        return;
    if (NS::Scene::FindObjectIndexById(m_scene->Level(), id) == NS::Scene::kNoObjectIndex)
        return;

    // 同型がすでにあっても末尾へ重ねて貼り、 上書きはしない
    m_editor.Undo().Push(std::make_unique<NS::Editor::AddComponentCommand>(id, *m_componentClipboard),
                         m_scene->Level());

    m_scene->RebuildWorld();
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
}

void LevelEditorController::BeginTransformEdit() noexcept
{
    if (m_transformEditing)
        return;
    if (m_selectedObjectIndex >= m_scene->Level().objects.size())
        return;
    m_editBaseline = m_scene->Level().objects[m_selectedObjectIndex];
    m_editBaselineId = m_scene->Level().objects[m_selectedObjectIndex].objectId;
    m_transformEditing = true;
}

void LevelEditorController::CommitTransformEdit() noexcept
{
    if (!m_transformEditing)
        return;
    m_transformEditing = false;

    const std::size_t index = NS::Scene::FindObjectIndexById(m_scene->Level(), m_editBaselineId);
    if (index == NS::Scene::kNoObjectIndex)
        return;

    // 位置・回転・スケールは live Transform から、 材質など残りは model から取る
    // パネル編集は Sync が 1 フレーム遅れるため model 直読みだと取りこぼす
    NS::Scene::ObjectData after = m_scene->Level().objects[index];
    const NS::Scene::Transform* liveRoot = nullptr;
    if (index == NS::Game::Level::FindPlayerObjectIndex(m_scene->Level()) && m_scene->PlayerRef())
    {
        // player object は world に居ないため live は実 player から取る
        liveRoot = &m_scene->PlayerRef()->Root();
    }
    else
    {
        for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
        {
            if (m_scene->World().SourceIndices()[i] != index)
                continue;
            liveRoot = &m_scene->World().Objects()[i]->Root();
            break;
        }
    }
    if (liveRoot != nullptr)
    {
        const NS::Math::Vector3 p = liveRoot->Position();
        const NS::Math::Quaternion r = liveRoot->Rotation();
        const NS::Math::Vector3 s = liveRoot->Scale();
        after.positionX = p.x;
        after.positionY = p.y;
        after.positionZ = p.z;
        after.rotationX = r.x;
        after.rotationY = r.y;
        after.rotationZ = r.z;
        after.rotationW = r.w;
        after.scaleX = s.x;
        after.scaleY = s.y;
        after.scaleZ = s.z;
    }

    if (after == m_editBaseline)
        return;

    // model を after に確定してから push する。 Push の Do は model == after なので何もせず履歴記録のみ
    m_scene->Level().objects[index] = after;
    m_editor.Undo().Push(std::make_unique<NS::Editor::TransformCommand>(m_editBaselineId, m_editBaseline, after),
                         m_scene->Level());
}

void LevelEditorController::ReselectFreeObjectById(std::uint32_t id) noexcept
{
    const std::size_t index = NS::Scene::FindObjectIndexById(m_scene->Level(), id);
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (m_scene->World().SourceIndices()[i] != index)
            continue;
        m_gizmo.SetSelected(&m_scene->World().Objects()[i]->Root());
        return;
    }
    m_gizmo.ClearSelection();
}

bool LevelEditorController::ApplyMaterialToSelected(const std::filesystem::path& matPath)
{
    auto* app = NS::App::Application::Get();
    if (m_editorToolMode != EditorToolMode::Object || app == nullptr)
        return false;

    NS::Scene::Transform* selected = m_gizmo.Selected();
    if (selected == nullptr)
        return false;

    std::size_t slot = m_scene->World().Objects().size();
    for (std::size_t i = 0; i < m_scene->World().Objects().size(); ++i)
    {
        if (&m_scene->World().Objects()[i]->Root() != selected)
            continue;
        slot = i;
        break;
    }
    if (slot >= m_scene->World().Objects().size())
        return false;

    auto* mesh = NS::Game::Blocks::FindComponent<NS::Scene::MeshRendererComponent>(*m_scene->World().Objects()[slot]);
    if (mesh == nullptr)
        return false;

    const auto loaded = app->Assets().LoadMaterial(matPath);
    if (loaded.material == nullptr)
        return false;

    // .mat パスを ContentRoot 相対で材質表に登録して重複は再利用し、 ObjectData.materialIndex を更新して永続化する
    const auto exeDir = NS::Core::FileSystem::ContentRoot();
    const std::filesystem::path relative = matPath.lexically_relative(exeDir);
    const std::string stored = [&]() -> std::string {
        if (relative.empty())
            return matPath.generic_string();
        return relative.generic_string();
    }();

    int materialIndex = -1;
    for (std::size_t k = 0; k < m_scene->Level().materialPaths.size(); ++k)
    {
        if (m_scene->Level().materialPaths[k] == stored)
        {
            materialIndex = static_cast<int>(k);
            break;
        }
    }
    if (materialIndex < 0)
    {
        m_scene->Level().materialPaths.push_back(stored);
        materialIndex = static_cast<int>(m_scene->Level().materialPaths.size() - 1);
    }

    NS::Scene::ObjectData& object = m_scene->Level().objects[m_scene->World().SourceIndices()[slot]];
    // 差替前を退避して materialIndex 変更を TransformCommand 1 つとして undo 履歴へ載せる
    // undo で materialIndex が戻り、 dirty rebuild が旧材質を焼き直す
    const NS::Scene::ObjectData before = object;
    object.materialIndex = static_cast<std::int16_t>(materialIndex);

    mesh->SetMaterial(loaded.material);
    mesh->SetBaseColor(loaded.baseColor);

    m_editor.Undo().Push(std::make_unique<NS::Editor::TransformCommand>(before.objectId, before, object),
                         m_scene->Level());
    return true;
}

void LevelEditorController::ReloadThemes()
{
    // 雛形を読み直すだけ。 シーンの見た目は environment が持つので、 適用し直すまで絵は変わらない
    NS::Game::Theme::LoadThemesFromDirectory(NS::Core::FileSystem::ContentRoot() / "Assets" / "Themes");
}

void LevelEditorController::ApplyTheme(NS::Game::Theme::ThemeId id)
{
    m_scene->Level().environment = NS::Game::Theme::MakeEnvironmentFromTheme(NS::Game::Theme::Get(id));
    // lighting / skybox は次フレームの設定写しで追従するため即時の組み直しは要らない
}
