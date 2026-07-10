#pragma once

/// @file LevelPlayScene.h
/// @brief レベルを読み込んで遊べる状態にする root scene。 編集機能は一切持たない
///
/// @details 永続の LevelData を value member で保有し、 一時の PlayState とルールの PlayMode は
/// 進行役 PlayDirector 配下の PlayFlowComponent が所有する。 scene 自身は
/// world の組み直しの号令と描画統括を担う。 実カメラ + Brain は CameraSubsystem が、
/// プレイヤー / 追従 / 据え置きカメラを含む全配置物は LevelWorld が所有する
/// 出荷 / 開発ともこの 1 種類だけを起動 scene に使う。 cursor / palette / ギズモ /
/// free-fly カメラ / モード切替の編集は scene の外側、 `LevelEditorController` が公開 API と
/// LevelData 経由で本 scene を操作して実現する。 scene 自身は「編集されている」ことを知らない

#include "Game/Level/LevelData.h"
#include "Game/Level/LevelWorld.h"
#include "Game/Level/PlayDirector.h"

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

    /// 実体プレイヤー。 world が player object から組む。 起動前と player object の無い level では nullptr
    [[nodiscard]] Player* PlayerRef() noexcept { return m_world.PlayerView(); }

    /// runtime world と衝突世界を LevelData から組み直す。 レベル編集後とプレイ突入時に呼ぶ
    /// 据え置きカメラの Brain 登録もここで面倒を見る。 Brain 構築前の OnStart 序盤は登録しない
    void RebuildWorld();

    /// 起動読込がプレイヤー実体を既定構成で合成したら true (新規 seed / 旧形式移行)
    /// 呼出側がテンプレート適用などの後処理を判断する。 scene 自身は既定構成のまま進める
    [[nodiscard]] bool PlayerObjectSynthesized() const noexcept { return m_playerObjectSynthesized; }

private:
    /// 基底 OnRender が scene 解決後に呼ぶ描画本体。 ワールドを描き編集ギズモ等は描かない
    void OnRenderScene() override;

    /// 全表示オブジェクトの Snapshot を取る。 補間描画のため edit / play 共通で毎フレーム
    void SnapshotDisplayObjects();

    /// 起動時のレベル供給: 同梱の `new_level.scene` をロードし、 無ければ最小床を seed する
    void LoadInitialLevel();

    // 組み込み mesh / 共有 material は Application 所有の AssetManager が持つ
    // scene は使う箇所で都度引く。 メンバとして控えず単一所有元は AssetManager のみ
    // skybox 装置と scene 段解決値の控えは EnvironmentSubsystem が持ち、 scene は毎フレーム設定を書くだけ

    // LevelData から組んだ runtime world。 配置物 / instanced 描画キャッシュ / hazard view を所有する
    // 実カメラ + Brain は CameraSubsystem が、 プレイヤー / 追従 / 据え置きカメラは world が配置物として所有する
    NS::Game::Level::LevelWorld m_world;

    NS::Game::Level::LevelData m_level{};

    // プレイ進行役。 PlayState / PlayMode と進行の分岐は配下の PlayFlowComponent が所有する
    std::unique_ptr<NS::Game::Level::PlayDirector> m_director;

    // コヨーテ debug 描画 すなわち 縁の紫線 / カプセル / コヨーテジャンプの赤線 の表示トグル。 F2 で切替える
    bool m_debugCoyoteDraw = true;

    // 起動読込でプレイヤー実体を既定構成で合成したか。 PlayerObjectSynthesized() の実体
    bool m_playerObjectSynthesized = false;
};
