#include "GameCore/Level/PlayFlowComponent.h"

#include "Framework/App/Application.h"
#include "Framework/Core/Clock.h"
#include "Framework/Physics/Capsule.h"
#include "Framework/Platform/Input.h"
#include "Framework/Platform/Keyboard.h"
#include "Framework/Platform/Window.h"
#include "Framework/Scene/CameraSubsystem.h"
#include "Framework/Scene/Components/BoxColliderComponent.h"
#include "Framework/Scene/Components/CameraBrainComponent.h"
#include "Framework/Scene/Components/HazardComponent.h"
#include "Framework/Scene/Components/PlacedVirtualCamera.h"
#include "Framework/Scene/Components/ThirdPersonFollowComponent.h"
#include "Framework/Scene/GameObject.h"
#include "GameCore/Blocks/BuildPlacedObject.h"
#include "GameCore/Level/ClearFadeComponent.h"
#include "GameCore/LevelPlayScene.h"
#include "GameCore/Player.h"

namespace NS::GameCore::Level
{
    LevelPlayScene* PlayFlowComponent::OwnerScene() noexcept
    {
        if (m_scene == nullptr && Owner() != nullptr)
            m_scene = dynamic_cast<LevelPlayScene*>(Owner()->OwningScene());
        return m_scene;
    }

    ClearFadeComponent* PlayFlowComponent::FadeComp() noexcept
    {
        if (m_fade == nullptr && Owner() != nullptr)
            m_fade = Owner()->FindComponent<ClearFadeComponent>();
        return m_fade;
    }

    void PlayFlowComponent::OnStart()
    {
        // 進行のたびに引き直さないよう scene をここで解決して控える
        static_cast<void>(OwnerScene());
    }

    void PlayFlowComponent::EnterPlay() noexcept
    {
        auto* scene = OwnerScene();
        if (scene == nullptr)
            return;

        m_playMode.Enter(scene->Level(), m_play);
        m_playMode.SetActive(true);
        if (auto* player = scene->PlayerRef())
        {
            player->MeshComp().SetActive(true);
            player->Movement().SetActive(true);
            player->InputComp().SetActive(true);
            player->Root().SetPosition(m_play.playerPosition);
            player->Movement().ResetState();
        }
        // 追従カメラは world のカメラ配置物。 プレイの間だけ起こす
        for (auto* follow : scene->World().FollowCameras())
            follow->SetActive(true);

        // プレイ突入はカーソルを消し、 マウスを相対モードにして視点操作をカーソル位置から切り離す
        // Esc で出すまで非表示のまま
        m_playCursorShown = false;
        if (auto* app = NS::App::Application::Get())
        {
            app->Window().SetCursorVisible(false);
            app->Input().Mouse().SetRelativeMode(true);
        }
    }

    void PlayFlowComponent::ExitPlay() noexcept
    {
        auto* scene = OwnerScene();
        if (scene == nullptr)
            return;

        // free-fly カメラは editor が握るためここでは触らない
        m_playMode.Exit(m_play);
        m_playMode.SetActive(false);
        if (auto* player = scene->PlayerRef())
        {
            player->Movement().SetActive(false);
            player->InputComp().SetActive(false);
            // 編集中も実プレイヤーをプレイヤー実体の pose に見せ、 ギズモで掴んで動かせるようにする
            player->MeshComp().SetActive(true);
            const auto& level = scene->Level();
            const std::size_t playerIndex = NS::GameCore::Level::FindPlayerObjectIndex(level);
            if (playerIndex != NS::GameCore::Level::kNoObjectIndex)
            {
                const auto& playerObject = level.objects[playerIndex];
                player->Root().SetPosition({playerObject.positionX, playerObject.positionY, playerObject.positionZ});
                player->Root().SetRotation(NS::Math::Quaternion{
                    playerObject.rotationX, playerObject.rotationY, playerObject.rotationZ, playerObject.rotationW});
            }
            player->Root().Snapshot();
        }
        for (auto* follow : scene->World().FollowCameras())
            follow->SetActive(false);
        for (auto* placed : scene->World().PlacedCameras())
            placed->SetActive(false);

        // 編集モードはカーソルを出し、 相対モードも解いてカーソル位置ベースの操作へ戻す
        if (auto* app = NS::App::Application::Get())
        {
            app->Window().SetCursorVisible(true);
            app->Input().Mouse().SetRelativeMode(false);
        }
    }

    void PlayFlowComponent::RestartLevel() noexcept
    {
        auto* scene = OwnerScene();
        if (scene == nullptr)
            return;

        m_playMode.Enter(scene->Level(), m_play);
        if (auto* player = scene->PlayerRef())
        {
            player->Root().SetPosition(m_play.playerPosition);
            player->Movement().ResetState();
        }
    }

    void PlayFlowComponent::OnUpdate()
    {
        auto* app = NS::App::Application::Get();
        if (app == nullptr)
            return;

        // プレイ中の Esc は 2 段階。 1 回目で隠したカーソルを出し、 出ている状態の 2 回目で終了する
        // 編集中は本 Component ごと寝ているためここへ来ない。 editor が Esc を選択解除 / 終了に使う
        if (app->Input().Keyboard().IsPressed(NS::Platform::Key::Escape))
        {
            if (!m_playCursorShown)
            {
                // カーソルを出すなら相対モードも解く。 見えるカーソルと相対モードの併存は挙動が矛盾する
                m_playCursorShown = true;
                app->Window().SetCursorVisible(true);
                app->Input().Mouse().SetRelativeMode(false);
            }
            else
            {
                NS::App::Application::Quit();
            }
            return;
        }

        Tick(NS::Core::FrameTimer::FixedDelta());
    }

    void PlayFlowComponent::Tick(float dt)
    {
        auto* scene = OwnerScene();
        if (scene == nullptr)
            return;

        // 時間停止中は移動 / 重力 / ゲームルール / カメラ追従を一切進めない
        // 手触り検証でジャンプ弧や着地の一瞬を止めて観察するための停止で、 エディタが paused を立てる
        // 物理を止めても previous == current のまま補間が凍るよう snapshot だけ回し、 凍結フレームのガタつきを消す
        if (m_play.paused)
        {
            if (auto* player = scene->PlayerRef())
                player->Root().Snapshot();
            return;
        }

        // 暗転の間は入力 / 物理 / ゲームルールを止めてプレイヤーを操作不能にし、 タイマーだけ進める
        // 暗転しきった裏でレベルを組み直すので、 全黒の一瞬で spawn への瞬間移動が隠れる
        auto* fade = FadeComp();
        if (fade != nullptr && fade->IsFading())
        {
            fade->Advance(dt);
            if (auto* player = scene->PlayerRef())
                player->Root().Snapshot();
            for (auto* follow : scene->World().FollowCameras())
                follow->OnUpdate();
            return;
        }

        // camera 水平 forward を先に渡してから tick。 priority 順 PlayerInput→CharacterMovement で入力→物理が確定し
        // Transform に書かれる
        if (auto* player = scene->PlayerRef())
        {
            NS::Math::Vector3 camForward{0.0f, 0.0f, 1.0f};
            auto* cameras = scene->GetSubsystem<NS::Scene::CameraSubsystem>();
            if (auto* brain = (cameras != nullptr) ? cameras->Brain() : nullptr)
                camForward = brain->ForwardHorizontal();
            player->InputComp().SetCameraForward(camForward);
            player->OnUpdate();

            // 落下死 / coin / goal / hazard 判定が読む PlayState.playerPosition に Transform をミラーする
            m_play.playerPosition = player->Root().Position();
        }

        // Play のゲームルールである落下死 / coin / goal。 物理は持たず player 位置を読むだけ
        m_playMode.Tick(scene->Level(), m_play, dt);

        // hazard は solid 衝突世界にも含まれ capsule 中心は表面外に留まるため芯線分から AABB の最近距離で判定する
        if (auto* player = scene->PlayerRef())
        {
            NS::Physics::Capsule playerCapsule{};
            playerCapsule.center = player->Root().Position();
            playerCapsule.radius = player->Movement().CapsuleRadius();
            playerCapsule.halfHeight = player->Movement().CapsuleHalfHeight();
            for (auto* hazard : scene->World().HazardView())
            {
                if (!hazard)
                    continue;
                // damage は衝突応答とは別経路の per-frame overlap なので collider と hazard を component で引く
                auto* box = NS::GameCore::Blocks::FindComponent<NS::Scene::BoxColliderComponent>(*hazard);
                auto* damage = NS::GameCore::Blocks::FindComponent<NS::Scene::HazardComponent>(*hazard);
                if (box && damage && NS::Physics::IntersectsCapsuleAabb(playerCapsule, box->WorldAABB()))
                    NS::GameCore::Level::ApplyContactDamage(m_play);
            }
        }

        // area camera: 各 vcam が自分のトリガ AABB でプレイヤー進入を判定し、自分を active 化する
        // active / 解除の切替は Brain が優先度で選びブレンドする
        for (auto* placed : scene->World().PlacedCameras())
            placed->UpdateActivation(m_play.playerPosition);

        // 落下死は即リスタート、 ゴール接触は出荷のみ暗転で仕切り直してループを閉じる
        // どちらも RestartLevel が spawn へ戻し health / coin / flag を全リセットするのでループが続く
        // 開発ビルドは editor が clearTriggered を観測して編集モードへ戻すためここでは扱わない
        if (m_play.deathTriggered)
        {
            RestartLevel();
        }
#if !NS_EDITOR_ENABLED
        else if (m_play.clearTriggered)
        {
            if (fade != nullptr)
                fade->Begin();
        }
#endif

        if (auto* player = scene->PlayerRef())
            player->Root().Snapshot();
        // 追従の spring は player 確定後に進める。 camera 配置物の Root Snapshot は scene が毎フレーム面倒を見る
        for (auto* follow : scene->World().FollowCameras())
            follow->OnUpdate();
    }

} // namespace NS::GameCore::Level
