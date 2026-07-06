#pragma once

/// @file LevelWorld.h
/// @brief LevelWorld — LevelData から組む runtime world 表現の所有と構築
///
/// @details 配置物 GameObject / 衝突プリミティブ / hazard 走査 view /
/// コヨーテ縁を LevelData から一括で組み直す。 runtime も editor も同じ Rebuild 経路を通り、
/// scene は公開読み口からの観測と描画だけを行う
/// 依存: NS::Scene::GameObject, NS::Physics::PhysicsWorld

#include "GameCore/Blocks/LedgeEdges.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace NS::Physics
{
    class PhysicsWorld;
}

namespace NS::Scene
{
    class AssetManager;
    class GameObject;
    class PlacedVirtualCamera;
    class SceneBase;
    class ThirdPersonFollowComponent;
    class VirtualCameraComponent;
} // namespace NS::Scene

class Player;

namespace NS::GameCore::Level
{
    struct LevelData;

    /// LevelData から組まれる runtime world。 所有と構築を一手に担う
    class LevelWorld
    {
    public:
        LevelWorld();
        ~LevelWorld();

        LevelWorld(const LevelWorld&) = delete;
        LevelWorld& operator=(const LevelWorld&) = delete;
        LevelWorld(LevelWorld&&) = delete;
        LevelWorld& operator=(LevelWorld&&) = delete;

        /// level の objects から全 runtime 表現を組み直す。 既存の配置物と衝突 world は必ず先に空へ戻す
        /// assets が nullptr の起動前 / テストでは物を組まず、 衝突 world も空のまま返る
        void Rebuild(const LevelData& level,
                     NS::Scene::SceneBase& scene,
                     NS::Physics::PhysicsWorld& physics,
                     NS::Scene::AssetManager* assets);

        /// 配置物を逆順に畳んで所有物を空へ戻す。 scene の OnShutdown と Rebuild 冒頭が呼ぶ
        void Clear();

        /// 配置物の単一所有リスト。 grid / slope / hazard / water / deco / 自由配置物すべてを generic に持つ
        [[nodiscard]] std::vector<std::unique_ptr<NS::Scene::GameObject>>& Objects() noexcept { return m_objects; }
        [[nodiscard]] const std::vector<std::unique_ptr<NS::Scene::GameObject>>& Objects() const noexcept
        {
            return m_objects;
        }

        /// Objects()[i] に対応する level.objects の添字。 Objects() と同長・ 1:1
        [[nodiscard]] const std::vector<std::size_t>& SourceIndices() const noexcept { return m_objectSourceIndices; }

        /// hazard の damage 走査 view。 所有は Objects() 側でここは観測のみ
        [[nodiscard]] const std::vector<NS::Scene::GameObject*>& HazardView() const noexcept { return m_hazardView; }

        /// 据え置きカメラの走査 view。 所有は Objects() 側で、 進入判定の駆動が読む
        [[nodiscard]] const std::vector<NS::Scene::PlacedVirtualCamera*>& PlacedCameras() const noexcept
        {
            return m_placedCameraView;
        }

        /// 追従カメラの走査 view。 所有は Objects() 側で、 プレイ進行の active 切替と spring 更新が読む
        [[nodiscard]] const std::vector<NS::Scene::ThirdPersonFollowComponent*>& FollowCameras() const noexcept
        {
            return m_followCameraView;
        }

        /// 仮想カメラ全種の走査 view。 据え置きと追従を束ね、 Brain への登録 / 解除が読む
        [[nodiscard]] const std::vector<NS::Scene::VirtualCameraComponent*>& VirtualCameras() const noexcept
        {
            return m_virtualCameraView;
        }

        /// world が player object から組んだ実体プレイヤーの型付き view。 所有は Objects() 側で、
        /// player object の無い level と組み直し前は nullptr
        [[nodiscard]] ::Player* PlayerView() const noexcept { return m_playerView; }

        /// コヨーテ debug 用に焼いた踏み外せる縁の world 線分。 出荷では焼かれず常に空
        [[nodiscard]] const std::vector<NS::GameCore::Blocks::LedgeEdge>& LedgeEdges() const noexcept
        {
            return m_ledgeEdges;
        }

    private:
        std::vector<std::unique_ptr<NS::Scene::GameObject>> m_objects;
        std::vector<std::size_t> m_objectSourceIndices;
        std::vector<NS::Scene::GameObject*> m_hazardView;
        std::vector<NS::Scene::PlacedVirtualCamera*> m_placedCameraView;
        std::vector<NS::Scene::ThirdPersonFollowComponent*> m_followCameraView;
        std::vector<NS::Scene::VirtualCameraComponent*> m_virtualCameraView;
        std::vector<NS::GameCore::Blocks::LedgeEdge> m_ledgeEdges;
        ::Player* m_playerView = nullptr;
    };

} // namespace NS::GameCore::Level
