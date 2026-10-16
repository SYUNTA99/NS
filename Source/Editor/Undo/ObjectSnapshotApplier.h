#pragma once

#include "Editor/Undo/IObjectSnapshotApplier.h"

#include "NSlib/Core/Types.h"
#include <optional>

namespace NS
{
    class Scene;
}

namespace NS
{
    namespace Editor
    {
        //! 1 個体の状態をシーンへ適用する。クラスと部品構成が同じなら実体を保ち、異なる時だけ再構築する
        class ObjectSnapshotApplier final : public IObjectSnapshotApplier
        {
        public:
            explicit ObjectSnapshotApplier(Scene* scene) noexcept : m_scene(scene) {}

            //! objectId の現在の姿を写す。居なければ nullopt。シーン未設定も nullopt
            [[nodiscard]] std::optional<nlohmann::json> CaptureObject(uint32 objectId) const override;

            //! objectId を desired の姿へ揃える。シーン未設定なら何もしない
            void ApplyObjectSnapshot(uint32 objectId, const std::optional<nlohmann::json>& desired) override;

        private:
            Scene* m_scene = nullptr; // 編集対象のシーン、非所有
        };
    } // namespace Editor
} // namespace NS
