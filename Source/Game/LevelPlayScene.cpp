#include "Game/LevelPlayScene.h"

#include "Game/Blocks/BuildPlacedObject.h"
#include "Game/Level/EditTarget.h"
#include "Game/Player.h"
#include "Game/PlayerTuning.h"

#include "Framework/Scene/AssetManager.h"
#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/Components/CameraComponent.h"
#include "Framework/Scene/Components/PlacedVirtualCamera.h"
#include "Framework/Scene/GameObject.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Core/Filesystem.h"
#include "Framework/Core/LogCategories.h"
#include "Framework/Core/Logger.h"
#include "Framework/Graphics/CommandList.h"
#include "Framework/Graphics/DebugDraw.h"
#include "Framework/Graphics/InstanceBatcher.h"
#include "Framework/Graphics/Material.h"
#include "Framework/Graphics/MeshPrimitives.h"
#include "Framework/Graphics/Renderer.h"
#include "Framework/Graphics/Shader.h"
#include "Framework/Graphics/Skybox.h"
#include "Framework/Graphics/StaticMesh.h"
#include "Framework/Graphics/Texture.h"
#include "Framework/Graphics/TextureArray.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/Components/MeshRendererComponent.h"
#include "Framework/Scene/IRenderable.h"
#include "Framework/Scene/RenderContext.h"
#include "Framework/Scene/Transform.h"
#include "Game/Blocks/AutoTile.h"
#include "Game/Level/LevelIO.h"
#include "Game/Theme/ThemeRegistry.h"

using namespace NS::Game::Theme;

namespace
{
    constexpr NS::Math::Vector3 kPlayerColor{0.85f, 0.20f, 0.20f};
    constexpr NS::Math::Vector3 kCellHalfExtents{0.5f, 0.5f, 0.5f};

    /// 編集体験の起点となる最小床。 LevelData に grid block 1 個 + spawn を仕込んでおく
    void SeedInitialLevel(NS::Game::Level::LevelData& level)
    {
        level.objects.clear();
        level.objects.push_back(NS::Game::Level::MakeGridObject(0, 0, 0, 0));
        // spawn は capsule 中心の world 位置。 床ブロック上面 0.5 + capsule(radius 0.4 + halfHeight 0.5) + 1cm
        level.spawnX = 0.0f;
        level.spawnY = 1.41f;
        level.spawnZ = 0.0f;
    }
} // namespace

LevelPlayScene::LevelPlayScene()
{
    // 進行役は OnStart を待たず生成する。 起動前でも editor / テストがプレイ切替と PlayState 参照を回せる
    m_director = std::make_unique<NS::Game::Level::PlayDirector>();
    m_director->AttachScene(this);
}

LevelPlayScene::~LevelPlayScene() = default;

void LevelPlayScene::LoadInitialLevel()
{
    // 同梱の起動レベルがあればそれを、 無ければ最小床を seed する
    const auto exeDir = NS::Core::FileSystem::GetExeDirectory();
    const auto levelPath = exeDir / "Levels" / "new_level.nslvl";
    if (!NS::Game::Level::LoadLevelFromFile(m_level, levelPath))
        SeedInitialLevel(m_level);
    RebuildObjectIds();
}

void LevelPlayScene::RebuildObjectIds() noexcept
{
    NS::Game::Level::EditTarget target{m_level, m_world.EditIds(), m_world.NextEditId()};
    NS::Game::Level::ResetEditIds(target);
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
    const auto exeDir = NS::Core::FileSystem::ContentRoot();

    // 組み込み mesh と共有 material は AssetManager がアプリ寿命で所有する。 ここは使う時に引くだけ
    auto& assets = app->Assets();

    // 接地シャドウは Player が scene 寿命のあいだ参照を握る。 組み込み quad + 共有 shadow material を渡す
    auto* shadowMesh = assets.Builtin("shadowQuad");
    auto* shadowMaterial = assets.SharedMaterial("shadow");

    // 全テーマ block texture を Texture2DArray 1 本に集約。 アセット未取得のため cube_test.png を 40 slice 充填
    {
        NS::Graphics::TextureArrayDesc taDesc{};
        const auto placeholderSlice = exeDir / "Assets" / "Textures" / "cube_test.png";
        constexpr std::size_t kPlaceholderSliceCount = 40; // 5 theme x 8 variant
        taDesc.slicePaths.reserve(kPlaceholderSliceCount);
        for (std::size_t i = 0; i < kPlaceholderSliceCount; ++i)
        {
            taDesc.slicePaths.push_back(placeholderSlice);
        }
        taDesc.generateMipmaps = true;
        taDesc.sRGB = false;
        auto* blockTextures = assets.GetOrCreateTextureArray("block", taDesc);
        if (blockTextures->IsUsingFallback())
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelPlayScene: block 用 TextureArray の slice 読込で失敗あり、 magenta fallback で続行");
    }

    m_world.CreateBatcher();

    // 仮の skybox。 kurt 6-face PNG をロードし、 取得できなければ
    // 1x1 マゼンタ cubemap fallback で続行する。 描画は OnRenderScene 末尾
    m_skybox = NS::Graphics::Skybox::Create();
    if (m_skybox->IsValid())
    {
        const auto kurtDir = exeDir / "Assets" / "Skybox" / "kurt";
        if (!m_skybox->LoadCubemap(kurtDir))
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelPlayScene: kurt cubemap 読込失敗、 magenta fallback で続行: {}",
                        kurtDir.string());
    }
    else
    {
        NS_LOG_ERROR(::NS::Core::LogCat::Game, "LevelPlayScene: Skybox 構築失敗 (Device 不在?)");
    }

    m_player = std::make_unique<Player>(assets.Builtin("cube"), assets.SharedMaterial("player"));
    m_player->AttachScene(this);
    m_player->Root().SetPosition({0.0f, 1.0f, -4.0f});
    // cube mesh の半サイズは 0.5 だが capsule collider は radius=0.4 / halfHeight=0.5
    // すなわち AABB 半サイズ 0.4, 0.9, 0.4。両者が一致するよう scale で mesh を縮める
    m_player->Root().SetScale({0.8f, 1.8f, 0.8f});
    m_player->MeshComp().SetBaseColor(kPlayerColor);
    m_player->Shadow().SetResources(shadowMesh, shadowMaterial);

    // 保存済みチューニングがあればプレイヤーの全コンポーネントへ適用する。 無ければコード既定値のまま
    LoadPlayerTuning(*m_player);

    LoadInitialLevel();
    RebuildWorld();

    m_cameraRig = std::make_unique<CameraRig>(&m_player->Root(), &m_player->Movement());
    m_cameraRig->AttachScene(this);
    // follow vcam の投影設定。 near 0.1 / fov 60 は既定、 play は遠景を 100 までに抑える
    m_cameraRig->Follow().SetFarPlane(100.0f);

    m_player->OnStart();
    m_cameraRig->OnStart();

    // 実カメラ 1 個 + Brain を載せる host を作り、 follow vcam を登録する
    // 描画 / aspect / PlayerInput forward は全て Brain 出力カメラへ集約する
    m_cameraHost = std::make_unique<NS::Scene::GameObject>();
    m_mainCamera = m_cameraHost->AddComponent<NS::Scene::CameraComponent>();
    m_brain = m_cameraHost->AddComponent<NS::Scene::CameraBrainComponent>();
    m_mainCamera->SetAspectRatioFromRenderer(renderer);
    m_mainCamera->SetUp({0.0f, 1.0f, 0.0f});
    m_brain->AddVirtualCamera(&m_cameraRig->Follow());
    m_cameraHost->AttachScene(this);
    m_cameraHost->OnStart();

    // level の cameraVolumes から area camera を生成し Brain へ登録する。 Brain 構築後に呼ぶ必要がある
    RebuildAreaCameras();

    // 進行役の Component に scene / player / camera の解決を済ませる
    m_director->OnStart();

    // 出荷も開発も、 起動直後はプレイ可能な状態にする。 開発時は editor が直後に編集モードへ切替える
    SetPlaying(true);
}

void LevelPlayScene::SetPlaying(bool playing) noexcept
{
    m_playing = playing;
    if (playing)
    {
        // 編集中の変形を確定した最新 level でプレイするため、 collision snapshot を作り直す
        // CommitTransformEdit は dirty を立てないため、 ここで突入時に一度作り直して取りこぼしを防ぐ
        RebuildWorld();
        m_director->Flow().EnterPlay();
        m_director->Flow().SetActive(true);
    }
    else
    {
        m_director->Flow().ExitPlay();
        m_director->Flow().SetActive(false);
    }
}

void LevelPlayScene::OnUpdate()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    // F5 で編集中の HLSL を再起動なしで反映する。 プレイ中の F5 はエディタ UI の表示トグルに使うため
    // ここでは編集モード中だけシェーダを再読み込みする。 ImGui 入力中は誤爆を防ぐため無効化する
    if (!m_playing && !app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::Platform::Key::F5))
    {
        app->Assets().ReloadAllShaders();
        // block 描画の instanced shader は AssetManager 管理外で自前コンパイルなので個別に reload する
        if (auto* batcher = m_world.Batcher())
            batcher->ReloadShaders();
    }

#if !defined(NS_SHIPPING)
    // F2 で コヨーテ debug 描画 すなわち 縁の紫線 / カプセル / コヨーテジャンプの赤線 を切替える
    if (!app->Input().UiWantsKeyboard() && app->Input().Keyboard().IsPressed(NS::Platform::Key::F2))
        m_debugCoyoteDraw = !m_debugCoyoteDraw;
#endif

    // Application が Renderer::Resize を排他で握っているため、 Camera の aspect ratio は
    // Renderer の現在 Size から毎フレーム pull する。 callback 上書きで競合させない
    if (m_mainCamera)
        m_mainCamera->SetAspectRatioFromRenderer(app->Renderer());

    // active vcam が入れ替わったらブレンドを進める。 play / edit 共通で fixed step ごとに 1 度
    if (m_brain)
        m_brain->OnUpdate();

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
        const NS::Game::Level::ObjectInstance& entry = m_level.objects[m_world.SourceIndices()[i]];
        if ((entry.flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
            objects[i]->OnUpdate();
    }
}

NS::Graphics::RenderSettingsOverride LevelPlayScene::BuildSceneOverride()
{
    // 範囲外 themeId は NS::Game::Theme::Get 側で Grass にフォールバックされる
    const ThemeData& theme = Get(m_level.themeId);

    NS::Graphics::RenderSettingsOverride over{};
    if (theme.lightDirection.LengthSquared() > 1e-6f)
    {
        over.lightDir = theme.lightDirection;
    }
    else
    {
        // zero ベクトルは normalize で拡散光が無言で消えるため override せず既定 lightDir に落とす
        static bool s_warnedZeroLightDir = false;
        if (!s_warnedZeroLightDir)
        {
            NS_LOG_WARN(::NS::Core::LogCat::Game,
                        "LevelPlayScene: テーマの lightDirection が zero のため既定 lightDir で描画する");
            s_warnedZeroLightDir = true;
        }
    }
    over.lightColor = theme.lightColor;
    over.ambientColor = theme.ambientColor;
    return over;
}

void LevelPlayScene::OnRenderScene()
{
    auto* app = NS::App::Application::Get();
    if (app == nullptr)
        return;

    NS::Scene::RenderContext ctx{};
    ctx.renderer = &app->Renderer();
    ctx.alpha = NS::Core::FrameTimer::Alpha();

    if (m_brain == nullptr || m_mainCamera == nullptr)
        return;

    // 有効な vcam すなわち edit=free-fly / play=follow を選び、 alpha 補間で実カメラへ書く
    // follow は補間 target を追うのでここで alpha を渡す。 旧 ApplyCameraTransform 相当の補間
    m_brain->Evaluate(ctx.alpha);
    ctx.viewProjection = m_brain->ViewProjection();

    // 半透明 back-to-front ソート用に実カメラの world 座標を渡す。 view 行列の逆変換の平行移動成分
    ctx.cameraPosition = m_mainCamera->Camera().View().Invert().Translation();

    // 基底が BuildSceneOverride() を Resolve するので、 theme override が scene 解決値として ctx に載る
    // mesh 経路は ctx 経由で pull、 block 経路はこの解決値を FrameCB に詰めて同一値を流す
    ctx.resolvedSettings = ResolveSceneSettings(ctx.renderer->Settings());
    // editor の RenderSettings パネルが friend で読むため解決値を退避する
    m_lastResolvedSettings = ctx.resolvedSettings;

    // テーマ swap は同一 frame 内で skybox / block / lighting に同じ ThemeData を反映させる必要がある
    // 範囲外 themeId は NS::Game::Theme::Get 側で Grass にフォールバックされる
    const ThemeData& theme = Get(m_level.themeId);

    // Block 描画は InstanceBatcher bucket 経由に統一。 MeshRendererComponent が非アクティブなので旧 per-block
    // 経路は通らない
    auto* batcher = m_world.Batcher();
    if (batcher && batcher->IsValid())
    {
        // 組み込み cube と共有 block material は AssetManager 所有。 毎フレームここで 1 度だけ引く
        auto& assets = app->Assets();
        auto* cubeMesh = assets.Builtin("cube");
        auto* blockMat = assets.SharedMaterial("block");

        // scene 解決値を block 全体の FrameCB に流す。 baseColor は per-instance で個体色を別途乗算する
        NS::Scene::FrameCB blockCB{};
        blockCB.viewProj = ctx.viewProjection;
        blockCB.lightDir = ctx.resolvedSettings.lightDir;
        blockCB.lightDir.Normalize();
        blockCB.baseColor = NS::Math::Vector3{1.0f, 1.0f, 1.0f}; // per-instance baseColor と乗算するので 1 に固定
        blockCB.lightColor = ctx.resolvedSettings.lightColor;
        blockCB.ambientColor = ctx.resolvedSettings.ambientColor;
        if (blockMat)
            blockMat->SetParams(*ctx.renderer, blockCB);

        // instanceable 判定 / 近傍マスク / slice は RebuildWorld で焼き済。 ここは焼いた slice と
        // 補間 world matrix だけを読み、 毎フレームの文字列走査と近傍マスク O(N^2) を持ち込まない
        batcher->BeginFrame();
        // 個体色は全 instanced block 共通の solid 色。 theme tint は FrameCB の lightColor/ambientColor で行う
        for (const auto& block : m_world.InstancedBlocks())
        {
            NS::Graphics::BlockInstance inst{};
            inst.worldMatrix = m_world.Objects()[block.objectIndex]->Root().InterpolatedWorldMatrix(ctx.alpha);
            inst.baseColor = NS::Game::Blocks::kSolidBaseColor;
            inst.textureSlice = block.textureSlice;
            batcher->Submit(cubeMesh, blockMat, inst);
        }

        // TextureArray を t0 に bind してから FlushAll。 Material::Bind では slot 0 を触っていない
        // SetTexture せず構築したため、 ここで bind した SRV が bucket 描画まで残る
        if (auto* blockTextures = assets.TextureArrayByName("block"))
            ctx.renderer->Commands().SetTextureArray(*blockTextures, 0u, NS::Graphics::ShaderType::Pixel);
        batcher->FlushAll(*ctx.renderer);
    }

    // 不透明 IRenderable。各 Draw が自分の Pipeline を set する。 基底が bucket 分類して登録順に呼ぶ
    DrawOpaque(ctx);

    // Skybox は不透明描画後・半透明前。 view の translation 行 _41/_42/_43 を 0 化して camera 中心に固定する
    if (m_skybox && m_skybox->IsValid())
    {
        // 毎フレーム LoadCubemap すると I/O が常時走るため、 前回パスと差分があるときだけ再ロードする
        if (!theme.skyboxCubemapPath.empty() && theme.skyboxCubemapPath != m_loadedSkyboxPath)
        {
            const auto exeDir = NS::Core::FileSystem::ContentRoot();
            const auto absPath =
                theme.skyboxCubemapPath.is_absolute() ? theme.skyboxCubemapPath : exeDir / theme.skyboxCubemapPath;
            if (m_skybox->LoadCubemap(absPath))
            {
                m_loadedSkyboxPath = theme.skyboxCubemapPath;
            }
            else
            {
                NS_LOG_WARN(::NS::Core::LogCat::Game,
                            "LevelPlayScene: テーマ '{}' の cubemap 読込失敗 ({}), 既存を維持",
                            theme.displayName,
                            absPath.string());
                // 失敗時は m_loadedSkyboxPath は更新しないので次フレームで再試行可能
            }
        }

        const auto& cam = m_mainCamera->Camera();
        NS::Math::Matrix viewNoTranslate = cam.View();
        viewNoTranslate._41 = 0.0f;
        viewNoTranslate._42 = 0.0f;
        viewNoTranslate._43 = 0.0f;
        const NS::Math::Matrix viewProjNoTranslate = viewNoTranslate * cam.Projection();
        m_skybox->Render(*ctx.renderer, viewProjNoTranslate);
    }

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
        if (m_player)
            coyoteReach = m_player->Movement().MaxSpeed() * m_player->Movement().CoyoteTime();
        for (const NS::Game::Blocks::LedgeEdge& edge : m_world.LedgeEdges())
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

        if (m_player)
        {
            auto& movement = m_player->Movement();
            const NS::Math::Vector3 center = m_player->Root().Position();

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
    if (m_cameraHost)
        m_cameraHost->OnEndPlay();
    for (auto& area : m_areaCameras)
    {
        if (area.host)
            area.host->OnEndPlay();
    }
    if (m_cameraRig)
        m_cameraRig->OnEndPlay();
    // 配置物は逆順の OnEndPlay ごと LevelWorld が畳む
    m_world.Clear();
    if (m_player)
        m_player->OnEndPlay();

    // Brain は vcam を非所有参照するので、 rig / area camera より先に host を畳んで無効参照を避ける
    m_cameraHost.reset();
    m_mainCamera = nullptr;
    m_brain = nullptr;
    m_areaCameras.clear();
    m_cameraRig.reset();
    m_player.reset();

    // Skybox / InstanceBatcher は Renderer の DeviceContext を ComPtr で握るため、 Application の Renderer より
    // 先に破棄する。 組み込み / leaf / 共有 material / block TextureArray / skinned model は AssetManager が Clear
    // で解放する
    m_world.ResetBatcher();
    m_skybox.reset();
}

void LevelPlayScene::RebuildWorld()
{
    // 構築は LevelWorld の一本道。 app 不在の起動前 / テストでは assets を渡さず何も組まない
    auto* app = NS::App::Application::Get();
    m_world.Rebuild(m_level, *this, Physics(), app ? &app->Assets() : nullptr);

    if (m_player)
    {
        // 接地シャドウは grid + 自由物の内包 AABB を下方向 ray で拾う。 blob なので OBB 精度は要らない
        std::vector<NS::Math::AABB> shadowReceivers(Physics().Aabbs().begin(), Physics().Aabbs().end());
        const auto& objects = m_world.Objects();
        for (std::size_t i = 0; i < objects.size(); ++i)
        {
            const NS::Game::Level::ObjectInstance& entry = m_level.objects[m_world.SourceIndices()[i]];
            if ((entry.flags & NS::Game::Level::kObjectFlagGridAligned) != 0)
                continue;
            if (auto aabb = NS::Game::Blocks::ColliderWorldAABB(*objects[i]))
                shadowReceivers.push_back(*aabb);
        }
        m_player->Shadow().SetCollisionWorld(shadowReceivers);
    }
}

void LevelPlayScene::RebuildAreaCameras()
{
    if (m_brain == nullptr)
        return;

    // 旧 area camera を Brain から外してから破棄する。 Brain の非所有参照を無効化させない
    for (auto& area : m_areaCameras)
    {
        if (area.cam)
            m_brain->RemoveVirtualCamera(area.cam);
    }
    m_areaCameras.clear();

    m_areaCameras.reserve(m_level.cameraVolumes.size());
    for (const auto& volume : m_level.cameraVolumes)
    {
        AreaCamera area{};
        area.host = std::make_unique<NS::Scene::GameObject>();
        area.cam = area.host->AddComponent<NS::Scene::PlacedVirtualCamera>();
        area.cam->SetView({volume.cameraPositionX, volume.cameraPositionY, volume.cameraPositionZ},
                          {volume.lookTargetX, volume.lookTargetY, volume.lookTargetZ});
        area.cam->SetTrigger({volume.triggerCenterX, volume.triggerCenterY, volume.triggerCenterZ},
                             {volume.triggerExtentX, volume.triggerExtentY, volume.triggerExtentZ});
        area.cam->SetLookAtPlayer(volume.lookAtPlayer != 0);
        area.cam->SetVcamPriority(volume.priority);
        area.cam->SetActive(false); // エリア外。 play 中の進入判定で vcam が自分を active 化する
        area.host->AttachScene(this);
        area.host->OnStart();
        m_brain->AddVirtualCamera(area.cam);
        m_areaCameras.push_back(std::move(area));
    }
}
