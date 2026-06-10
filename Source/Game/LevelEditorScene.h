#pragma once

#include "Framework/Math/Math.h"
#include "Framework/Physics/SweptTriangle.h"
#include "Framework/Scene/SceneBase.h"
#include "Game/CameraRig.h"
#include "Game/Editor/EditorMode.h"
#include "Game/EditorCameraRig.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayMode.h"
#include "Game/Level/PlayState.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace NS::Graphics
{
    class InstanceBatcher;
    class Material;
    class StaticMesh;
    class SkeletalMesh;
    class Shader;
    class Skybox;
    class Texture;
    class TextureArray;
} // namespace NS::Graphics

namespace NS::Scene
{
    class IRenderable;
    class GameObject;
    class MeshRendererComponent;
    class SkeletalAnimationComponent;
} // namespace NS::Scene

namespace NS::Scene
{
    class PoleComponent;
} // namespace NS::Scene

class Block;
class DecorationBlock;
class HazardBlock;
class Player;
class PoleBlock;
class SlopeBlock;
class WaterBlock;

/// 編集 / プレイ両モードを 1 scene 内で扱う root scene
/// LevelData (永続) + PlayState (一時) + EditorMode を value member で保有する
class LevelEditorScene : public NS::Scene::SceneBase
{
public:
    LevelEditorScene();
    ~LevelEditorScene() override;

    LevelEditorScene(const LevelEditorScene&) = delete;
    LevelEditorScene& operator=(const LevelEditorScene&) = delete;
    LevelEditorScene(LevelEditorScene&&) = delete;
    LevelEditorScene& operator=(LevelEditorScene&&) = delete;

    void OnStart() override;
    void OnUpdate() override;
    void OnShutdown() override;

    void RegisterRenderable(NS::Scene::IRenderable* renderable) override;
    void UnregisterRenderable(NS::Scene::IRenderable* renderable) override;

    [[nodiscard]] NS::Game::Level::LevelData& Level() noexcept { return m_level; }
    [[nodiscard]] const NS::Game::Level::LevelData& Level() const noexcept { return m_level; }
    [[nodiscard]] NS::Game::Level::PlayState& Play() noexcept { return m_play; }
    [[nodiscard]] NS::Game::Editor::EditorMode& Editor() noexcept { return m_editor; }
    [[nodiscard]] NS::Game::Level::PlayMode& PlayModeSub() noexcept { return m_playMode; }

    /// 編集 ↔ プレイのモード状態。 単一 enum で同フレーム即切替する
    enum class Mode : std::uint8_t
    {
        Edit,
        Play
    };

    [[nodiscard]] Mode CurrentMode() const noexcept { return m_mode; }

    /// Edit → Play 遷移。 spawn 位置に player 再構築、 EditorCamera off、 Player Component 再活性化
    void EnterPlay() noexcept;

    /// Play → Edit 遷移。 paused/clear/death をリセット、 EditorCamera on、 Player Component 休止
    void EnterEdit() noexcept;

private:
    /// 基底 OnRender が scene 解決後に呼ぶ描画本体。ctx を組み立てて全 Renderable を描く
    void OnRenderScene() override;

    /// dirty flag 検出時のみ m_blocks と m_collisionWorld を LevelData から再構築する
    void RebuildBlocksFromLevelData();

    /// 仮 skinned キャラ (glTF) を毎ステップ進めて描画する debug hook
    /// F1 再生/停止、 F2 クリップ送り、 F3/F4 速度。 ImGui 入力中はキー無効
    void UpdateAnimatedModel();

    std::unique_ptr<NS::Graphics::StaticMesh> m_cubeMesh;
    std::unique_ptr<NS::Graphics::Texture> m_texture;
    std::unique_ptr<NS::Graphics::TextureArray> m_blockTextures;
    std::unique_ptr<NS::Graphics::Shader> m_standardVS; // player / block 共有 (standard.vs)
    std::unique_ptr<NS::Graphics::Shader> m_playerPS;   // player / block / skinned 共有 (player.ps)
    std::unique_ptr<NS::Graphics::Material> m_playerMaterial;
    std::unique_ptr<NS::Graphics::Material> m_blockMaterial;
    std::unique_ptr<NS::Graphics::Skybox> m_skybox;
    std::unique_ptr<NS::Graphics::InstanceBatcher> m_instanceBatcher;

    // 角度別 wedge mesh を 4 種だけ shared でキャッシュ。 SlopeBlock 1 個ずつに mesh を持たせず、
    // scene 寿命のあいだ共有して描画コストとメモリを抑える
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh45;
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh30;
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh22;
    std::unique_ptr<NS::Graphics::StaticMesh> m_wedgeMesh15;

    // 掴まり系 mesh: 円柱を 1 度だけ生成して全 instance で共有する
    std::unique_ptr<NS::Graphics::StaticMesh> m_poleMesh;

    // 仮 skinned キャラの描画リソース (mesh / shader / material)。 アセット未取得時は全て null
    std::unique_ptr<NS::Graphics::SkeletalMesh> m_skinnedMesh;
    std::unique_ptr<NS::Graphics::Shader> m_skinnedVS;
    std::unique_ptr<NS::Graphics::Material> m_skinnedMaterial;

    std::unique_ptr<Player> m_player;

    // 仮 skinned キャラ本体。 GameObject が MeshRenderer + SkeletalAnimation を所有し、 参照をキャッシュする
    std::unique_ptr<NS::Scene::GameObject> m_animatedModel;
    NS::Scene::MeshRendererComponent* m_animMesh = nullptr;
    NS::Scene::SkeletalAnimationComponent* m_animPlayer = nullptr;
    float m_animSpeed = 1.0f;
    std::vector<std::unique_ptr<Block>> m_blocks;
    std::vector<std::unique_ptr<SlopeBlock>> m_slopes;
    std::vector<std::unique_ptr<PoleBlock>> m_poles;
    std::vector<std::unique_ptr<HazardBlock>> m_hazards;
    std::vector<std::unique_ptr<WaterBlock>> m_waters;
    std::vector<std::unique_ptr<DecorationBlock>> m_decorations;

    std::unique_ptr<CameraRig> m_cameraRig;
    std::unique_ptr<EditorCameraRig> m_editorCameraRig;

    std::vector<NS::Scene::IRenderable*> m_renderList;
    std::vector<NS::Math::AABB> m_collisionWorld;
    std::vector<NS::Physics::Triangle> m_collisionTriangles;
    std::vector<NS::Scene::PoleComponent*> m_polePtrs;

    NS::Game::Level::LevelData m_level{};
    NS::Game::Level::PlayState m_play{};
    NS::Game::Editor::EditorMode m_editor{};
    NS::Game::Level::PlayMode m_playMode{};
    Mode m_mode = Mode::Edit;

    /// 差分フレームのみ cubemap を再ロードするため前回パスを保持する
    std::filesystem::path m_loadedSkyboxPath{};
};
