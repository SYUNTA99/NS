#pragma once

#include "Editor/Undo/IObjectSnapshotApplier.h"
#include "Runtime/Object/Scene/SceneData.h"

#include <cstdint>
#include <optional>

namespace NS::Object
{
    class Scene;
}

namespace NS::Editor
{
    /// @brief 編集した 1 体分の姿を live のシーンへ写す口
    /// @details Scene の公開 API だけを組み合わせる
    /// 適用は現 live を作業データへ写し、 対象 1 体を差し替え/新規/除去して世界ごと組み直す
    /// 組み直すと全 component が OnStart からやり直しになるので、 走行中には使えない編集専用の道
    class ObjectSnapshotApplier final : public IObjectSnapshotApplier
    {
    public:
        explicit ObjectSnapshotApplier(NS::Object::Scene* scene) noexcept : m_scene(scene) {}

        /// objectId の現在の姿を写す。 居なければ nullopt。 シーン未設定も nullopt
        [[nodiscard]] std::optional<NS::Object::ObjectData> CaptureObject(std::uint32_t objectId) const override;

        /// objectId を desired の姿へ揃える。 シーン未設定なら何もしない
        void ApplyObjectSnapshot(std::uint32_t objectId,
                                 const std::optional<NS::Object::ObjectData>& desired) override;

    private:
        NS::Object::Scene* m_scene = nullptr; // 編集対象のシーン、 非所有
    };
} // namespace NS::Editor
