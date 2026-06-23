#pragma once

/// @file LevelPlayScene.h
/// @brief レベルを読み込んで遊べる状態にする root scene。 編集機能は一切持たない
///
/// @details LevelData (永続) + PlayState (一時) + PlayMode (ルール) を value member で保有し、
/// ブロック構築 / 描画 / Player / カメラ / 当たり判定 / area camera を駆動する
/// 出荷 / 開発ともこの 1 種類だけを起動 scene に使う。 編集 (cursor / palette / ギズモ /
/// free-fly カメラ / モード切替) は scene の外側、 `LevelEditorController` が friend 経由で
/// 本 scene を操作して実現する。 scene 自身は「編集されている」ことを知らない

#include "Framework/Core/EditorAccess.h"
#include "Framework/Math/Math.h"
#include "Framework/Physics/PhysicsWorld.h"
#include "Framework/Scene/SceneBase.h"
#include "Game/CameraRig.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <vector>

namespace NS::Graphics
{
    class InstanceBatcher;
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
    class PoleComponent;
    struct RenderContext;
} // namespace NS::Scene

class Player;
class SkinnedDebugCharacter;

/// レベルを遊ぶための root scene。 編集機能を持たず、 派生もしない単一の scene 型
class LevelPlayScene : public NS::Scene::SceneBase
{
    // 編集ツールは scene の内部 (runtime オブジェクト群 / camera brain / play 状態) へ深く触れるため
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
    [[nodiscard]] NS::Game::Level::PlayState& Play() noexcept { return m_play; }
    [[nodiscard]] NS::Game::Level::PlayMode& PlayModeSub() noexcept { return m_playMode; }

    /// プレイ更新 (player 物理 / ルール / カメラ追従) が走っているか。 編集中は false
    [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }

private:
    /// テーマの lighting をシーン単位の上書きとして宣言する。push は書かず override を返すだけ
    NS::Graphics::RenderSettingsOverride BuildSceneOverride() override;

    /// 基底 OnRender が scene 解決後に呼ぶ描画本体。 ワールドを描く (編集ギズモ等は描かない)
    void OnRenderScene() override;

    /// プレイ開始 / 停止を切替える。 true で spawn + player/follow camera 有効化、
    /// false で player を凍結し follow / area camera を休止する (editor の編集モード用)
    void SetPlaying(bool playing) noexcept;

    /// dirty flag 検出時のみ m_objects と衝突世界を LevelData から再構築する
    void RebuildBlocksFromLevelData();

    /// m_objectIds を m_level.objects と同サイズの連番へ再構築する (objects 全置換直後に呼ぶ)
    void RebuildObjectIds() noexcept;

    /// m_level.cameraVolumes から area camera (PlacedVirtualCamera) 群を作り直して Brain へ登録する
    /// 旧 area camera は Brain から外して破棄する。 Brain 構築前 (OnStart 序盤) は no-op
    void RebuildAreaCamerasFromLevelData();

    /// プレイ更新本体: 入力 → 物理 → ルール → area camera → 死亡/リスポーン → カメラ追従
    void TickPlay();

    /// 仮 skinned キャラ (glTF) を毎ステップ進めて描画する debug hook
    /// F1 再生/停止、 F2 クリップ送り、 F3/F4 速度。 ImGui 入力中はキー無効
    void UpdateAnimatedModel();

    /// 全表示ブロックの Snapshot を取る (補間描画のため edit / play 共通で毎フレーム)
    void SnapshotDisplayBlocks();

    /// 全表示ブロックの OnUpdate を回す (edit / play 共通)
    void UpdateDisplayBlocks();

    /// 起動時のレベル供給: 同梱 default `.nslvl` をロードし、 無ければ最小床を seed する
    void LoadInitialLevel();

    // builtin mesh / 共有 material / block の TextureArray は AssetManager (Application 所有) が持つ
    // scene は使う箇所で都度引く (メンバとして控えない = 単一所有元は AssetManager のみ)

    std::unique_ptr<NS::Graphics::Skybox> m_skybox;
    std::unique_ptr<NS::Graphics::InstanceBatcher> m_instanceBatcher;

    // 借用元なので m_player より前に宣言する (player を先に破棄し CMC の dangling を防ぐ)
    NS::Physics::PhysicsWorld m_physicsWorld;
    std::unique_ptr<Player> m_player;

    // 仮 skinned キャラ。 形 / 骨 / 材質は AssetManager 所有を参照し、 components を自分で合成する
    std::unique_ptr<SkinnedDebugCharacter> m_animatedModel;

    // 配置物の単一所有リスト。 grid / slope / pole / hazard / water / deco / 自由配置物すべてを
    // generic GameObject として保持する。 RebuildBlocksFromLevelData がファクトリ経由で作り直す
    std::vector<std::unique_ptr<NS::Scene::GameObject>> m_objects;
    // m_objects[i] に対応する m_level.objects の添字 (m_objects と同長・ 1:1)
    std::vector<std::size_t> m_objectSourceIndices;

    std::unique_ptr<CameraRig> m_cameraRig;

    // 実カメラ 1 個 + Brain を載せる host。Brain が follow / free-fly vcam から選んで実カメラへ書く
    std::unique_ptr<NS::Scene::GameObject> m_cameraHost;
    NS::Scene::CameraComponent* m_mainCamera = nullptr;
    NS::Scene::CameraBrainComponent* m_brain = nullptr;

    // CameraVolume 1 件に対応する area camera の runtime 実体。 host が PlacedVirtualCamera を所有し、
    // vcam 自身が pose / トリガ / lookAtPlayer を持って自分で active 化する。 Brain は cam を非所有参照する
    struct AreaCamera
    {
        std::unique_ptr<NS::Scene::GameObject> host;
        NS::Scene::PlacedVirtualCamera* cam = nullptr;
    };
    std::vector<AreaCamera> m_areaCameras;

    std::vector<NS::Scene::PoleComponent*> m_polePtrs;

    NS::Game::Level::LevelData m_level{};

    // m_level.objects と 1:1 の編集セッション識別子。 undo 履歴が free オブジェクトを再特定するため
    // 保持する (非シリアライズ)。 objects 全置換時は RebuildObjectIds で連番へ戻す
    std::vector<std::uint32_t> m_objectIds;
    std::uint32_t m_nextObjectId = 0;

    NS::Game::Level::PlayState m_play{};
    NS::Game::Level::PlayMode m_playMode{};

    // プレイ更新の有効フラグ。 編集モード中は false にして物理 / ルールを止める (editor が SetPlaying で切替)
    bool m_playing = false;

    // 直近 OnRenderScene で解決した scene 段設定。 editor の RenderSettings パネルが friend で読む
    NS::Graphics::RenderSettings m_lastResolvedSettings{};

    // hazard の damage 走査 view。 衝突応答とは別経路 (芯線 vs AABB) で per-frame に当てるため build 時に積む
    // 所有は m_objects 側、 ここは観測のみ
    std::vector<NS::Scene::GameObject*> m_hazardView;

    /// 差分フレームのみ cubemap を再ロードするため前回パスを保持する
    std::filesystem::path m_loadedSkyboxPath{};
};
