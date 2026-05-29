#pragma once

#include "Framework/Core/Math.h"
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
    class Mesh;
    class ShaderProgram;
    class Skybox;
    class Texture;
    class TextureArray;
} // namespace NS::Graphics

namespace NS::Scene
{
    class IRenderable;
} // namespace NS::Scene

namespace NS::Scene
{
    class ClimbableSurfaceComponent;
    class PoleComponent;
} // namespace NS::Scene

class Block;
class FenceBlock;
class Player;
class PoleBlock;
class SlopeBlock;

/// 編集 / プレイ両モードを 1 scene 内で扱う root scene。
/// LevelData (永続) + PlayState (一時) + EditorMode を value member で保有し、
/// 今後 mode toggle / PlayMode を同 scene 内に追加する基盤になる。
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
    void OnRender() override;
    void OnShutdown() override;

    void RegisterRenderable(NS::Scene::IRenderable* renderable) override;
    void UnregisterRenderable(NS::Scene::IRenderable* renderable) override;

    [[nodiscard]] NS::Game::Level::LevelData& Level() noexcept { return m_level; }
    [[nodiscard]] const NS::Game::Level::LevelData& Level() const noexcept { return m_level; }
    [[nodiscard]] NS::Game::Level::PlayState& Play() noexcept { return m_play; }
    [[nodiscard]] NS::Game::Editor::EditorMode& Editor() noexcept { return m_editor; }
    [[nodiscard]] NS::Game::Level::PlayMode& PlayModeSub() noexcept { return m_playMode; }

    /// 編集 ↔ プレイのモード状態。 単一 enum で同フレーム instant flip する設計
    /// (Mario Builder 64 の current/target 2 変数 async と異なり、 NS は遷移アニメを持たない)。
    enum class Mode : std::uint8_t
    {
        Edit,
        Play
    };

    [[nodiscard]] Mode CurrentMode() const noexcept { return m_mode; }

    /// Edit → Play。 PlayMode::Enter で spawn 位置に player 再構築、 EditorMode 休止、
    /// EditorCamera off → ThirdPersonFollow on、 Player 各 Component 再活性化。
    void EnterPlay() noexcept;

    /// Play → Edit。 PlayMode::Exit で paused/clear/death をリセット、 EditorMode 復帰、
    /// EditorCamera on → ThirdPersonFollow off、 Player 各 Component 休止。
    void EnterEdit() noexcept;

private:
    /// `m_level.blocks` を観測駆動で見て、 `m_blocks` (Block オブジェクト群) と
    /// `m_collisionWorld` (AABB 配列) を再構築する。 mutation 発生 frame だけ呼ばれる
    /// dirty flag 経由の observer pattern (毎 frame の全 alloc churn を回避)。
    void RebuildBlocksFromLevelData();

    std::unique_ptr<NS::Graphics::Mesh> m_cubeMesh;
    std::unique_ptr<NS::Graphics::Texture> m_texture;
    std::unique_ptr<NS::Graphics::TextureArray> m_blockTextures;
    std::unique_ptr<NS::Graphics::ShaderProgram> m_playerShader;
    std::unique_ptr<NS::Graphics::ShaderProgram> m_blockShader;
    std::unique_ptr<NS::Graphics::Material> m_playerMaterial;
    std::unique_ptr<NS::Graphics::Material> m_blockMaterial;
    std::unique_ptr<NS::Graphics::Skybox> m_skybox;
    std::unique_ptr<NS::Graphics::InstanceBatcher> m_instanceBatcher;

    // 角度別 wedge mesh を 4 種だけ shared でキャッシュ。 SlopeBlock 1 個ずつに mesh を持たせず、
    // scene 寿命のあいだ共有して描画コストとメモリを抑える。
    std::unique_ptr<NS::Graphics::Mesh> m_wedgeMesh45;
    std::unique_ptr<NS::Graphics::Mesh> m_wedgeMesh30;
    std::unique_ptr<NS::Graphics::Mesh> m_wedgeMesh22;
    std::unique_ptr<NS::Graphics::Mesh> m_wedgeMesh15;

    // 掴まり系 mesh: 円柱と薄板を 1 度だけ生成して全 instance で共有する。
    std::unique_ptr<NS::Graphics::Mesh> m_poleMesh;
    std::unique_ptr<NS::Graphics::Mesh> m_fenceMesh;

    std::unique_ptr<Player> m_player;
    std::vector<std::unique_ptr<Block>> m_blocks;
    std::vector<std::unique_ptr<SlopeBlock>> m_slopes;
    std::vector<std::unique_ptr<PoleBlock>> m_poles;
    std::vector<std::unique_ptr<FenceBlock>> m_fences;

    std::unique_ptr<CameraRig> m_cameraRig;
    std::unique_ptr<EditorCameraRig> m_editorCameraRig;

    std::vector<NS::Scene::IRenderable*> m_renderList;
    std::vector<NS::Core::AABB> m_collisionWorld;
    std::vector<NS::Physics::Triangle> m_collisionTriangles;
    std::vector<NS::Scene::ClimbableSurfaceComponent*> m_fencePtrs;
    std::vector<NS::Scene::PoleComponent*> m_polePtrs;

    NS::Game::Level::LevelData m_level{};
    NS::Game::Level::PlayState m_play{};
    NS::Game::Editor::EditorMode m_editor{};
    NS::Game::Level::PlayMode m_playMode{};
    Mode m_mode = Mode::Edit;

    /// テーマ swap は同一 frame 内で skybox / block / lighting に同じ ThemeData を反映させる必要がある。
    /// 同じパスを毎フレーム LoadCubemap し直すと texture I/O が走るので、 最後にロードした絶対パスを
    /// 記憶しておき、 ThemeRegistry::Get(...).skyboxCubemapPath と差分が出たフレームだけ Reload する。
    std::filesystem::path m_loadedSkyboxPath{};
};
