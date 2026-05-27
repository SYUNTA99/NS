#pragma once

#include "Framework/Core/Math.h"
#include "Framework/Scene/SceneBase.h"
#include "Game/CameraRig.h"
#include "Game/Editor/EditorMode.h"
#include "Game/Level/LevelData.h"
#include "Game/Level/PlayState.h"

#include <memory>
#include <vector>

namespace NS::Graphics
{
    class Material;
    class Mesh;
    class ShaderProgram;
    class Texture;
} // namespace NS::Graphics

namespace NS::Scene
{
    class IRenderable;
} // namespace NS::Scene

class Block;
class Player;

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

private:
    std::unique_ptr<NS::Graphics::Mesh> m_cubeMesh;
    std::unique_ptr<NS::Graphics::Texture> m_texture;
    std::unique_ptr<NS::Graphics::ShaderProgram> m_shader;
    std::unique_ptr<NS::Graphics::Material> m_playerMaterial;
    std::unique_ptr<NS::Graphics::Material> m_blockMaterial;

    std::unique_ptr<Player> m_player;
    std::vector<std::unique_ptr<Block>> m_blocks;

    std::unique_ptr<CameraRig> m_cameraRig;

    std::vector<NS::Scene::IRenderable*> m_renderList;
    std::vector<NS::Core::AABB> m_collisionWorld;

    NS::Game::Level::LevelData m_level{};
    NS::Game::Level::PlayState m_play{};
    NS::Game::Editor::EditorMode m_editor{};
};
