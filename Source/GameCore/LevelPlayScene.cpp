#include "GameCore/LevelPlayScene.h"

#include "GameCore/Blocks/BuildPlacedObject.h"
#include "GameCore/Player.h"
#include "GameCore/PlayerTuning.h"

#include "Framework/Scene/CameraSubsystem.h"
#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/EnvironmentSubsystem.h"
#include "Framework/Scene/GameObject.h"
#include "Framework/Scene/ObjectRefSubsystem.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/Transform.h"
#include "GameCore/Level/LevelIO.h"
#include "GameCore/Theme/ThemeRegistry.h"

using namespace NS::GameCore::Theme;

namespace
{
    constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

    /// 編集体験の起点となる最小床。 LevelData に grid block 1 個 + プレイヤー実体を仕込んでおく
    void SeedInitialLevel(NS::GameCore::Level::LevelData& level)
    {
        level.objects.clear();
        // 新規シーンの既定の見た目は Grass 雛形を写し込む。 以降はシーンの環境欄が正になる
        level.environment = NS::GameCore::Theme::MakeEnvironmentFromTheme(
            NS::GameCore::Theme::Get(NS::GameCore::Theme::ThemeId::Grass));
        level.objects.push_back(NS::GameCore::Level::MakeGridObject(0, 0, 0, 0));
        // プレイヤーは capsule 中心を床ブロック上面 0.5 + capsule 半径込み半高 0.9 + 1cm へ置く
        level.objects.push_back(NS::GameCore::Level::MakePlayerObject(
            NS::Math::Vector3{0.0f, NS::GameCore::Level::kDefaultPlayerSpawnY, 0.0f}, NS::Math::Quaternion{}));
        // 新規プレイヤーには保存済みテンプレートの構成と値を写す
        MergeSavedPlayerTuning(level.objects.back());
        NS::GameCore::Level::EnsureUniqueObjectIds(level);
        // 追従カメラも配置物。 プレイヤーへの Target 参照が要るため採番の後に足し、 増分をもう一度採番する
        const std::size_t playerIndex = NS::GameCore::Level::FindPlayerObjectIndex(level);
        level.objects.push_back(NS::GameCore::Level::MakeFollowCameraObject(
            (playerIndex != NS::GameCore::Level::kNoObjectIndex) ? level.objects[playerIndex].objectId : 0u));
        NS::GameCore::Level::EnsureUniqueObjectIds(level);
    }
} // namespace

LevelPlayScene::LevelPlayScene()
{
    // 進行役は OnStart を待たず生成する。 起動前でも editor / テストがプレイ切替と PlayState 参照を回せる
    m_director = std::make_unique<NS::GameCore::Level::PlayDirector>();
    m_director->AttachScene(this);
}

LevelPlayScene::~LevelPlayScene() = default;

void LevelPlayScene::LoadInitialLevel()
{
    // 同梱の起動レベルがあればそれを、 無ければ最小床を seed する
    const auto exeDir = NS::Core::FileSystem::GetExeDirectory();
    const auto levelPath = exeDir / "Levels" / "new_level.scene";
    if (!NS::GameCore::Level::LoadLevelFromFile(m_level, levelPath))
        SeedInitialLevel(m_level);
}

void LevelPlayScene::OnStart()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
    {
        NS_LOG_ERROR(::NS::Core::LogCat::Game, "LevelPlayScene::OnStart: Application::Get()==null");
        return;
    }

    auto& renderer = app->Renderer();
    // プレイヤーの構成と値の真実はレベルの player object。 world が他の配置物と同じ一本道で組む
    // 実カメラ + Brain は CameraSubsystem 所有で、 プレイヤー / 追従カメラは world が配置物として組む
    LoadInitialLevel();
    RebuildWorld();

    // 初回描画から正しい縦横比で出す。 以降は OnUpdate が Renderer の現在 Size を毎フレーム引き写す
    if (auto* cameras = GetSubsystem<NS::Scene::CameraSubsystem>())
        if (auto* mainCamera = cameras->MainCamera())
            mainCamera->SetAspectRatioFromRenderer(renderer);

    // 進行役の Component に scene / player / camera の解決を済ませる
    m_director->OnStart();

    // 出荷も開発も、 起動直後はプレイ可能な状態にする。 開発時は editor が直後に編集モードへ切替える
    // 編集中の変形を確定した最新 level で world を組み直してからプレイへ入る。 進行役は生成時から起きている
    RebuildWorld();
    m_director->Flow().EnterPlay();
}

void LevelPlayScene::OnUpdate()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

#if !defined(NS_SHIPPING)
    // F2 で コヨーテ debug 描画 すなわち 縁の紫線 / カプセル / コヨーテジャンプの赤線 を切替える
    if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::Platform::Key::F2))
        m_debugCoyoteDraw = !m_debugCoyoteDraw;
#endif

    // Application が Renderer::Resize を排他で握っているため、 Camera の aspect ratio は
    // Renderer の現在 Size から毎フレーム pull する。 callback 上書きで競合させない
    auto* cameras = GetSubsystem<NS::Scene::CameraSubsystem>();
    if (auto* mainCamera = (cameras != nullptr) ? cameras->MainCamera() : nullptr)
        mainCamera->SetAspectRatioFromRenderer(app->Renderer());

    // active vcam が入れ替わったらブレンドを進める。 play / edit 共通で fixed step ごとに 1 度
    if (auto* brain = (cameras != nullptr) ? cameras->Brain() : nullptr)
        brain->OnUpdate();

    SnapshotDisplayBlocks();

    // 編集中はプレイ更新を止める。 free-fly カメラ / 編集入力は overlay layer の editor 側が回す
    // 進行の分岐は配下の PlayFlowComponent が担い、 編集モード中は寝ているため素通りする
    m_director->OnUpdate();

    UpdateDisplayBlocks();
}

void LevelPlayScene::SnapshotDisplayBlocks()
{
    // 各配置物 GameObject の Snapshot は edit / play 共通。 静的 display object なので常時
    for (auto& obj : m_world.Objects())
        obj->Root().Snapshot();
}

void LevelPlayScene::UpdateDisplayBlocks()
{
    // gridAligned な配置物のみ OnUpdate する。 自由配置物は旧挙動を保つため OnUpdate 対象外
    const auto& objects = m_world.Objects();
    for (std::size_t i = 0; i < objects.size(); ++i)
    {
        const NS::GameCore::Level::ObjectInstance& entry = m_level.objects[m_world.SourceIndices()[i]];
        if ((entry.flags & NS::GameCore::Level::kObjectFlagGridAligned) != 0)
            objects[i]->OnUpdate();
    }
}

void LevelPlayScene::OnRenderScene()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // シーンが所有する環境をそのまま skybox / block / lighting の出所にする
    // 環境設定へ毎フレーム写すだけで、 上書き宣言の構築と skybox の差分再読込は EnvironmentSubsystem が担う
    auto* environment = GetSubsystem<NS::Scene::EnvironmentSubsystem>();
    if (environment != nullptr)
    {
        NS::Scene::EnvironmentSettings settings{};
        settings.lightDirection = m_level.environment.lightDirection;
        settings.lightColor = m_level.environment.lightColor;
        settings.ambientColor = m_level.environment.ambientColor;
        settings.skyboxCubemapPath = m_level.environment.skyboxCubemapPath;
        environment->SetSettings(settings);
    }

    NS::Scene::RenderContext ctx{};
    ctx.renderer = &app->Renderer();
    ctx.alpha = NS::Core::FrameTimer::Alpha();

    auto* cameras = GetSubsystem<NS::Scene::CameraSubsystem>();
    auto* brain = (cameras != nullptr) ? cameras->Brain() : nullptr;
    auto* mainCamera = (cameras != nullptr) ? cameras->MainCamera() : nullptr;
    if (brain == nullptr || mainCamera == nullptr)
        return;

    // 有効な vcam すなわち edit=free-fly / play=follow を選び、 alpha 補間で実カメラへ書く
    // follow は補間 target を追うのでここで alpha を渡す。 旧 ApplyCameraTransform 相当の補間
    brain->Evaluate(ctx.alpha);
    ctx.viewProjection = brain->ViewProjection();

    // 半透明 back-to-front ソート用に実カメラの world 座標を渡す。 view 行列の逆変換の平行移動成分
    ctx.cameraPosition = mainCamera->Camera().View().Invert().Translation();

    // 基底が BuildSceneOverride() を Resolve するので、 環境設定の上書きが scene 解決値として ctx に載る
    // mesh 経路は ctx 経由で pull、 block 経路はこの解決値を FrameCB に詰めて同一値を流す
    // editor の由来表示が読む解決値の控えは基底が EnvironmentSubsystem へ格納する
    ctx.resolvedSettings = ResolveSceneSettings(ctx.renderer->Settings());

    // 不透明 IRenderable。各 Draw が自分の Pipeline を set する。 基底が bucket 分類して登録順に呼ぶ
    DrawOpaque(ctx);

    // Skybox は不透明描画後・半透明前。 cubemap の差分再読込と camera 中心固定は EnvironmentSubsystem が行う
    if (environment != nullptr)
        environment->DrawSky(*ctx.renderer, mainCamera->Camera());

    // 半透明 IRenderable は不透明 + skybox の後。カメラから遠い順に各 Draw が alpha/additive Pipeline を set する
    DrawTransparent(ctx);

#if !defined(NS_SHIPPING)
    // コヨーテタイムの debug 可視化を world 描画後にまとめて出す。 player は fixed step で記録した赤線を持つが、
    // ここで render rate に蓄積し直すことで高リフレッシュでもちらつかせない
    if (m_debugCoyoteDraw)
    {
        // 縁から空セル側へ伸ばすコヨーテ到達距離 = 最高速 × 猶予秒。 Inspector で Coyote Time を変えると即追従する
        // 内側の縁を踏み外し点、 外側の明るい線を猶予の限界として、 間を横線で塗り「範囲」を面で見せる
        const NS::Math::Color ledgeColor{0.65f, 0.30f, 1.0f, 1.0f};
        const NS::Math::Color limitColor{1.0f, 0.20f, 0.90f, 1.0f};
        float coyoteReach = 0.0f;
        if (auto* player = PlayerRef())
            coyoteReach = player->Movement().MaxSpeed() * player->Movement().CoyoteTime();
        for (const NS::GameCore::Blocks::LedgeEdge& edge : m_world.LedgeEdges())
        {
            const NS::Math::Vector3 off{edge.outward.x * coyoteReach, 0.0f, edge.outward.z * coyoteReach};
            const NS::Math::Vector3 outerA{edge.a.x + off.x, edge.a.y, edge.a.z + off.z};
            const NS::Math::Vector3 outerB{edge.b.x + off.x, edge.b.y, edge.b.z + off.z};
            NS::Graphics::DebugDraw::Line(edge.a, edge.b, ledgeColor);
            NS::Graphics::DebugDraw::Line(edge.a, outerA, ledgeColor);
            NS::Graphics::DebugDraw::Line(edge.b, outerB, ledgeColor);
            NS::Graphics::DebugDraw::Line(outerA, outerB, limitColor);
            for (int hatch = 1; hatch <= 2; ++hatch)
            {
                const float t = static_cast<float>(hatch) / 3.0f;
                const NS::Math::Vector3 ha{edge.a.x + off.x * t, edge.a.y, edge.a.z + off.z * t};
                const NS::Math::Vector3 hb{edge.b.x + off.x * t, edge.b.y, edge.b.z + off.z * t};
                NS::Graphics::DebugDraw::Line(ha, hb, ledgeColor);
            }
        }

        if (auto* player = PlayerRef())
        {
            auto& movement = player->Movement();
            const NS::Math::Vector3 center = player->Root().Position();

            // 接地状態は頭上に浮かべた箱で示す。 カプセルはメッシュに埋もれて色が見えないため別表示にする
            // 接地=緑 / 空中=黄。 頭の上へ出して body に隠れさせない
            const float headTop = center.y + movement.CapsuleHalfHeight() + movement.CapsuleRadius();
            const NS::Math::Color groundedColor = movement.IsGrounded() ? NS::Math::Color{0.2f, 1.0f, 0.2f, 1.0f}
                                                                        : NS::Math::Color{1.0f, 1.0f, 0.2f, 1.0f};
            const NS::Math::AABB groundedMarker(NS::Math::Vector3{center.x, headTop + 0.45f, center.z},
                                                NS::Math::Vector3{0.18f, 0.18f, 0.18f});
            NS::Graphics::DebugDraw::AABB(groundedMarker, groundedColor);

            // 縁→跳躍点の赤い span と、 跳躍点に立てる赤い縦マーカーで「どこで猶予内に跳んだか」を示す
            const NS::Math::Color coyoteColor{1.0f, 0.15f, 0.15f, 1.0f};
            for (const auto& marker : movement.CoyoteJumpMarkers())
            {
                NS::Graphics::DebugDraw::Line(marker.edge, marker.jump, coyoteColor);
                const NS::Math::Vector3 tickTop{marker.jump.x, marker.jump.y + 0.6f, marker.jump.z};
                NS::Graphics::DebugDraw::Line(marker.jump, tickTop, coyoteColor);
            }
        }
    }
    // 編集モードでは LevelEditorController がギズモ等を足して別途 Flush するが、 プレイ中はここが唯一の Flush
    NS::Graphics::DebugDraw::Flush(*ctx.renderer, ctx.viewProjection);
#endif

    // 暗転や HUD の重ね物は Overlay バケットが全描画の最後に最前面で描く
    DrawOverlay(ctx);
}

void LevelPlayScene::OnShutdown()
{
    // 進行役は player / camera を非所有参照するだけなので先に畳む
    if (m_director)
        m_director->OnEndPlay();
    // Brain は world のカメラ配置物を非所有参照する。 world を畳む前に外して無効参照を避ける
    auto* cameras = GetSubsystem<NS::Scene::CameraSubsystem>();
    if (auto* brain = (cameras != nullptr) ? cameras->Brain() : nullptr)
        for (auto* vcam : m_world.VirtualCameras())
            brain->RemoveVirtualCamera(vcam);
    // 参照照合窓口も world より先に空へ戻し、 畳み中の解決に宙参照を返さない
    if (auto* refs = GetSubsystem<NS::Scene::ObjectRefSubsystem>())
        refs->Clear();
    // 配置物はプレイヤー込みで逆順の OnEndPlay ごと LevelWorld が畳む。 実カメラ + Brain は CameraSubsystem が畳む
    m_world.Clear();
}

void LevelPlayScene::RebuildWorld()
{
    // 旧 world のカメラ配置物を Brain から外してから組み直す。 Brain の非所有参照を無効化させない
    auto* cameras = GetSubsystem<NS::Scene::CameraSubsystem>();
    auto* brain = (cameras != nullptr) ? cameras->Brain() : nullptr;
    if (brain != nullptr)
        for (auto* vcam : m_world.VirtualCameras())
            brain->RemoveVirtualCamera(vcam);

    // 構築は LevelWorld の一本道で、 参照照合窓口の張り替えとプレイヤーの組み立てもここに含む
    // app 不在の起動前 / テストでは assets を渡さず何も組まない
    auto* app = NS::App::Application::Get();
    m_world.Rebuild(m_level, *this, Physics(), app ? &app->Assets() : nullptr);

    // 組み直しで生まれたカメラ配置物を Brain へ登録し直す。 追従と据え置きの両方が載る
    if (brain != nullptr)
        for (auto* vcam : m_world.VirtualCameras())
            brain->AddVirtualCamera(vcam);
}
