#pragma once

/// @file LevelPlayScene.h
/// @brief レベルを読み込んで遊べる状態にする root scene。 編集機能は一切持たない
///
/// @details 永続の LevelData を value member で保有し、 一時の PlayState とルールの PlayMode は
/// 進行役 PlayDirector 配下の PlayFlowComponent が所有する。 scene 自身は
/// ブロック構築 / 描画 / Player / 当たり判定の所有と結線を担う。 実カメラ + Brain は
/// CameraSubsystem が、 追従 / 据え置きカメラは LevelWorld が配置物として所有する
/// 出荷 / 開発ともこの 1 種類だけを起動 scene に使う。 cursor / palette / ギズモ /
/// free-fly カメラ / モード切替の編集は scene の外側、 `LevelEditorController` が公開 API と
/// LevelData 経由で本 scene を操作して実現する。 scene 自身は「編集されている」ことを知らない

#include "Framework/Scene/SceneBase.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/LevelWorld.h"
#include "Game/Level/PlayDirector.h"

#include <filesystem>
#include <memory>

namespace NS::Graphics
{
    class Skybox;
} // namespace NS::Graphics

namespace NS::Scene
{
    class IRenderable;
    class GameObject;
    class Transform;
    struct RenderContext;
} // namespace NS::Scene

class Player;

/// レベルを遊ぶための root scene。 編集機能を持たず、 派生もしない単一の scene 型
class LevelPlayScene : public NS::Scene::SceneBase
{
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
    /// プレイ進行役。 PlayState / PlayMode と進行の分岐は配下の PlayFlowComponent が担う。 scene 生成時から存在する
    [[nodiscard]] NS::Game::Level::PlayDirector& Director() noexcept { return *m_director; }

    /// LevelData から組まれた runtime world。 editor の選択 / gizmo と描画がここから観測する
    [[nodiscard]] NS::Game::Level::LevelWorld& World() noexcept { return m_world; }

    // brain / 実カメラの公開アクセサは持たない。 外の消費者は CameraSubsystem 経由で引く
    // 据え置き / 追従カメラは通常の配置物として LevelWorld が所有し、 World() の走査 view が返す

    /// 実体プレイヤー。 起動前は nullptr
    [[nodiscard]] Player* PlayerRef() noexcept { return m_player.get(); }

    /// 直近 OnRenderScene で解決した scene 段の描画設定
    [[nodiscard]] const NS::Graphics::RenderSettings& LastResolvedSettings() const noexcept
    {
        return m_lastResolvedSettings;
    }

    /// テーマの lighting をシーン単位の上書きとして宣言する。push は書かず override を返すだけ
    /// 描画設定の由来表示が解決値と突き合わせて読むため公開する
    NS::Graphics::RenderSettingsOverride BuildSceneOverride() override;

    /// runtime world と衝突世界を LevelData から組み直す。 レベル編集後とプレイ突入時に呼ぶ
    /// 据え置きカメラの Brain 登録もここで面倒を見る。 Brain 構築前の OnStart 序盤は登録しない
    /// player object は world で組まれない代わりに、 components の値を live player へ適用する
    void RebuildWorld();

    /// レベルの player object の pose を live player へ適用する。 player object 不在なら何もしない
    /// undo / redo / ロードでデータ側の pose が変わった直後に editor が呼ぶ。 プレイ中には呼ばない
    void ApplyPlayerPoseFromLevel() noexcept;

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

    std::unique_ptr<Player> m_player;

    // LevelData から組んだ runtime world。 配置物 / instanced 描画キャッシュ / hazard view / コヨーテ縁を所有する
    // 実カメラ + Brain は CameraSubsystem が、 追従 / 据え置きカメラは world が配置物として所有する
    NS::Game::Level::LevelWorld m_world;

    NS::Game::Level::LevelData m_level{};

    // プレイ進行役。 PlayState / PlayMode と進行の分岐は配下の PlayFlowComponent が所有する
    std::unique_ptr<NS::Game::Level::PlayDirector> m_director;

    // player の OnStart 済みか。 以降にデータへ足された component は適用時に自分で開始する
    bool m_playerStarted = false;

    // コヨーテ debug 描画 すなわち 縁の紫線 / カプセル / コヨーテジャンプの赤線 の表示トグル。 F2 で切替える
    bool m_debugCoyoteDraw = true;

    // 直近 OnRenderScene で解決した scene 段設定。 editor の RenderSettings パネルが LastResolvedSettings() で読む
    NS::Graphics::RenderSettings m_lastResolvedSettings{};

    /// 差分フレームのみ cubemap を再ロードするため前回パスを保持する
    std::filesystem::path m_loadedSkyboxPath{};
};
