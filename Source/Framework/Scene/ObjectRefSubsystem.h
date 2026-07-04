#pragma once

/// @file ObjectRefSubsystem.h
/// @brief NS::Scene::ObjectRefSubsystem — 永続 object id から live GameObject を引く照合窓口
///
/// @details ObjectRef が保持する永続 id と実行時実体の対応表をシーン単位で握る
/// 登録は world を組む側が行い、組み直しのたびに Clear → Register で満たし直す
/// 参照を解決する Component は OnStart で Resolve を呼び、以降は生ポインタで使う
/// 実体の寿命は登録側が支配する非所有参照で、未設定 / 未登録の id は nullptr を返す
/// 依存: NS::Scene::SceneSubsystem, NS::Scene::ObjectRef

#include "Framework/Scene/ObjectRef.h"
#include "Framework/Scene/SceneSubsystem.h"

#include <cstdint>
#include <unordered_map>

namespace NS::Scene
{
    class GameObject;

    /// 永続 id → live GameObject の対応表。実体は非所有
    class ObjectRefSubsystem final : public SceneSubsystem
    {
    public:
        /// id と実体の対応を登録する。id 0 と null は無視し、同 id は後勝ちで上書きする
        void Register(std::uint32_t id, GameObject* object);

        /// 対応表を空へ戻す。world の組み直しと scene の畳みが呼ぶ
        void Clear() noexcept { m_objects.clear(); }

        /// 参照から実体を引く。未設定 / 未登録は nullptr
        [[nodiscard]] GameObject* Resolve(ObjectRef ref) const noexcept;

        /// シーン破棄で対応だけ手放す。実体の破棄は所有者に任せる
        void Deinitialize() noexcept override { m_objects.clear(); }

    private:
        std::unordered_map<std::uint32_t, GameObject*> m_objects;
    };
} // namespace NS::Scene
