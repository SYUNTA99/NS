#pragma once

#include "Editor/Undo/IObjectSnapshotApplier.h"

#include <cstdint>
#include <optional>

namespace NS::Obj
{
    class Scene;
}

namespace NS::Editor
{
    //! @brief 編集した 1 体分の姿を live のシーンへ写す口
    //! @details Scene の公開 API だけを組み合わせる
    //! 適用は対象 1 体だけへ姿を書き戻す・入れる・消す。他の配置物は触らないので、実行時の状態も保たれる
    //! component の構成が同じなら実体はそのまま値だけ写り、増減した時だけ作り直して OnStart からやり直す
    class ObjectSnapshotApplier final : public IObjectSnapshotApplier
    {
    public:
        explicit ObjectSnapshotApplier(NS::Obj::Scene* scene) noexcept : m_scene(scene) {}

        //! objectId の現在の姿を写す。居なければ nullopt。シーン未設定も nullopt
        [[nodiscard]] std::optional<nlohmann::json> CaptureObject(std::uint32_t objectId) const override;

        //! objectId を desired の姿へ揃える。シーン未設定なら何もしない
        void ApplyObjectSnapshot(std::uint32_t objectId, const std::optional<nlohmann::json>& desired) override;

    private:
        NS::Obj::Scene* m_scene = nullptr; // 編集対象のシーン、非所有
    };
} // namespace NS::Editor
