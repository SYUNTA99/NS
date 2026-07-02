#pragma once

/// @file LevelPlayScene.h
/// @brief レベルを読み込んで遊べる状態にする root scene。 編集機能は一切持たない
///
/// @details 永続の LevelData を value member で保有し、 一時の PlayState とルールの PlayMode は
/// 進行役 PlayDirector 配下の PlayFlowComponent が所有する。 scene 自身は
/// ブロック構築 / 描画 / Player / カメラ / 当たり判定 / area camera の所有と結線を担う
/// 出荷 / 開発ともこの 1 種類だけを起動 scene に使う。 cursor / palette / ギズモ /
/// free-fly カメラ / モード切替の編集は scene の外側、 `LevelEditorController` が friend 経由で
/// 本 scene を操作して実現する。 scene 自身は「編集されている」ことを知らない

#include "Framework/Core/EditorAccess.h"
#include "Framework/Scene/SceneBase.h"
#include "Game/CameraRig.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelWorld.h"
#include "Game/Level/PlayDirector.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace NS::Graphics
{
    class Skybox;
} // namespace NS::Graphics

namespace NS::Scene
{
    class IRenderable;
    class GameObject;
    class Transform;
    class CameraComponent;
    class CameraBrainComponent;
    class PlacedVirtualCamera;
    struct RenderContext;
} // namespace NS::Scene

class Player;

/// レベルを遊ぶための root scene。 編集機能を持たず、 派生もしない単一の scene 型
class LevelPlayScene : public NS::Scene::SceneBase
{
    // 編集ツールは scene 内部の runtime オブジェクト群 / camera brain / play 状態へ深く触れるため
    // friend で許可する。 scene 側に編集専用の public API を生やさず、 編集の知識を外へ閉じ込める
    // 出荷ビルドではマクロが空に展開され、 editor のクラス名ごとバイナリから消える
    NS_EDITOR_FRIEND(LevelEditorController)

public:
    LevelPlayScene();
    ~LevelPlayScene() override;

    LevelPlayScene(const LevelPlayScene&) = delete;
    LevelPlayScene& operator=(const LevelPlayScene&) = delete;
    LevelPlayScene(LevelPlayScene&&) = delete;
    LevelPlayScene& operator=(LevelPlayScene&&) = delete;

    void OnStart() override;
    void OnUpdate() override;
    void OnShutdown() override;

    [[nodiscard]] NS::Game::Level::LevelData& Level() noexcept { return m_level; }
    [[nodiscard]] const NS::Game::Level::LevelData& Level() const noexcept { return m_level; }
    [[nodiscard]] NS::Game::Level::PlayState& Play() noexcept { return m_director->Flow().Play(); }
    [[nodiscard]] NS::Game::Level::PlayMode& PlayModeSub() noexcept { return m_director->Flow().PlayModeSub(); }

    /// プレイ進行役。 PlayState / PlayMode と進行の分岐は配下の PlayFlowComponent が担う。 scene 生成時から存在する
    [[nodiscard]] NS::Game::Level::PlayDirector& Director() noexcept { return *m_director; }

    /// LevelData から組まれた runtime world。 editor の選択 / gizmo と描画がここから観測する
    [[nodiscard]] NS::Game::Level::LevelWorld& World() noexcept { return m_world; }

    // CameraVolume 1 件に対応する area camera の runtime 実体。 host が PlacedVirtualCamera を所有し、
    // vcam 自身が pose / トリガ / lookAtPlayer を持って自分で active 化する。 Brain は cam を非所有参照する
    struct AreaCamera
    {
        std::unique_ptr<NS::Scene::GameObject> host;
        NS::Scene::PlacedVirtualCamera* cam = nullptr;
    };

    /// 実カメラと vcam 切替を束ねる Brain。 起動前は nullptr
    [[nodiscard]] NS::Scene::CameraBrainComponent* Brain() noexcept { return m_brain; }

    /// Brain の出力先になる実カメラ。 起動前は nullptr
    [[nodiscard]] NS::Scene::CameraComponent* MainCamera() noexcept { return m_mainCamera; }

    /// 実体プレイヤー。 起動前は nullptr
    [[nodiscard]] Player* PlayerRef() noexcept { return m_player.get(); }

    /// 追従カメラの rig。 起動前は nullptr
    [[nodiscard]] CameraRig* Rig() noexcept { return m_cameraRig.get(); }

    /// CameraVolume 1 件に対応する area camera の runtime 実体列
    [[nodiscard]] std::vector<AreaCamera>& AreaCameras() noexcept { return m_areaCameras; }

    /// 直近 OnRenderScene で解決した scene 段の描画設定
    [[nodiscard]] const NS::Graphics::RenderSettings& LastResolvedSettings() const noexcept
    {
        return m_lastResolvedSettings;
    }

    /// テーマの lighting をシーン単位の上書きとして宣言する。push は書かず override を返すだけ
    /// 描画設定の由来表示が解決値と突き合わせて読むため公開する
    NS::Graphics::RenderSettingsOverride BuildSceneOverride() override;

    /// runtime world と衝突世界を LevelData から組み直す。 レベル編集後とプレイ突入時に呼ぶ
    void RebuildWorld();

    /// m_level.cameraVolumes から area camera の PlacedVirtualCamera 群を作り直して Brain へ登録する
    /// 旧 area camera は Brain から外して破棄する。 Brain 構築前の OnStart 序盤は何もしない
    void RebuildAreaCameras();

private:
    /// 基底 OnRender が scene 解決後に呼ぶ描画本体。 ワールドを描き編集ギズモ等は描かない
    void OnRenderScene() override;

    /// world の編集 id を m_level.objects と同サイズの連番へ再構築する。 objects 全置換直後に呼ぶ
    void RebuildObjectIds() noexcept;

    /// 全表示ブロックの Snapshot を取る。 補間描画のため edit / play 共通で毎フレーム
    void SnapshotDisplayBlocks();

    /// 全表示ブロックの OnUpdate を回す。 edit / play 共通
    void UpdateDisplayBlocks();

    /// 起動時のレベル供給: 同梱の `new_level.nslvl` をロードし、 無ければ最小床を seed する
    void LoadInitialLevel();

    // 組み込み mesh / 共有 material / block の TextureArray は Application 所有の AssetManager が持つ
    // scene は使う箇所で都度引く。 メンバとして控えず単一所有元は AssetManager のみ

    std::unique_ptr<NS::Graphics::Skybox> m_skybox;

    // CameraRig が Movement を借用するため m_cameraRig より前に宣言する
    std::unique_ptr<Player> m_player;

    // LevelData から組んだ runtime world。 配置物 / instanced 描画キャッシュ / hazard view / コヨーテ縁を所有する
    NS::Game::Level::LevelWorld m_world;

    std::unique_ptr<CameraRig> m_cameraRig;

    // 実カメラ 1 個 + Brain を載せる host。Brain が follow / free-fly vcam から選んで実カメラへ書く
    std::unique_ptr<NS::Scene::GameObject> m_cameraHost;
    NS::Scene::CameraComponent* m_mainCamera = nullptr;
    NS::Scene::CameraBrainComponent* m_brain = nullptr;

    std::vector<AreaCamera> m_areaCameras;

    NS::Game::Level::LevelData m_level{};

    // プレイ進行役。 PlayState / PlayMode と進行の分岐は配下の PlayFlowComponent が所有する
    std::unique_ptr<NS::Game::Level::PlayDirector> m_director;

    // コヨーテ debug 描画 すなわち 縁の紫線 / カプセル / コヨーテジャンプの赤線 の表示トグル。 F2 で切替える
    bool m_debugCoyoteDraw = true;

    // 直近 OnRenderScene で解決した scene 段設定。 editor の RenderSettings パネルが friend で読む
    NS::Graphics::RenderSettings m_lastResolvedSettings{};

    /// 差分フレームのみ cubemap を再ロードするため前回パスを保持する
    std::filesystem::path m_loadedSkyboxPath{};
};
