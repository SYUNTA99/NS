#pragma once

#include "Editor/Undo/IActorSnapshotApplier.h"

#include "NSlib/Core/Types.h"
#include <optional>

namespace NS
{
    class Scene;
}

namespace NS::Editor
{
        //! 1 個体の状態をシーンへ適用する。アクターは Scene::ApplyFromJson のとおり使い回し、作り直した時は古いアクターを返す
        class ActorSnapshotApplier final : public IActorSnapshotApplier
        {
        public:
            explicit ActorSnapshotApplier(Scene* scene) noexcept : m_scene(scene) {}

            //! objectId の現在の姿を写す。居なければ nullopt。シーン未設定も nullopt
            [[nodiscard]] std::optional<nlohmann::json> CaptureActor(uint32 objectId) const override;

            //! objectId を desired の姿へ揃える。シーン未設定なら何もせずに空を返す
            [[nodiscard]] std::unique_ptr<SceneActor> ApplyActorSnapshot(uint32 objectId,
                                                                         const nlohmann::json& desired) override;

            //! シーン未設定なら空を返す
            [[nodiscard]] DetachedActor DetachActor(uint32 objectId) override;

            void ReattachActor(DetachedActor detached) override;

        private:
            Scene* m_scene = nullptr; // 編集対象のシーン、非所有
        };
} // namespace NS::Editor
