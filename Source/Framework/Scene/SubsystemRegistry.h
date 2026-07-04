#pragma once

/// @file SubsystemRegistry.h
/// @brief service 型の自己登録テーブルと登録マクロ
///
/// @details 各 service は自身の .cpp で NS_REGISTER_SUBSYSTEM を書くと、型・tier・生成関数・
/// 生成可否判定がこのテーブルへ静的初期化時に積まれる。中央の手書き列挙は持たない
/// SceneBase はこのテーブルを走査して自身の tier の service を生成する
/// StaticLib では自己登録 TU がリンカに除去され得るため、実行体側で除去対策を要する
/// 依存: NS::Scene::SceneSubsystem

#include "Framework/Scene/SceneSubsystem.h"

#include <memory>
#include <typeindex>
#include <vector>

namespace NS::Scene
{
    class SceneBase;

    /// 1 service 型の登録情報。factory は既定構築の生成関数、shouldCreate は対象シーンで生成するか
    struct SubsystemEntry
    {
        std::type_index type;
        SubsystemTier tier;
        std::unique_ptr<SceneSubsystem> (*factory)();
        bool (*shouldCreate)(const SceneBase&);
        const char* name;
    };

    /// 自己登録の集約先。全 TU の登録が同一実体へ積まれる
    class SubsystemRegistry
    {
    public:
        [[nodiscard]] static SubsystemRegistry& Get() noexcept;

        void Register(const SubsystemEntry& entry);
        [[nodiscard]] const std::vector<SubsystemEntry>& Entries() const noexcept;

    private:
        SubsystemRegistry() = default;
        std::vector<SubsystemEntry> m_entries;
    };
} // namespace NS::Scene

/// service 型を自己登録する。その型が見えるスコープで 1 度だけ書く
/// Type は単純名、TierValue は SubsystemTier の値、ShouldCreateFn は bool(const SceneBase&) 適合
#define NS_REGISTER_SUBSYSTEM(Type, TierValue, ShouldCreateFn)                                                         \
    namespace                                                                                                          \
    {                                                                                                                  \
        const bool k_subsystemRegistered_##Type = [] {                                                                 \
            ::NS::Scene::SubsystemRegistry::Get().Register(::NS::Scene::SubsystemEntry{                                \
                std::type_index(typeid(Type)),                                                                         \
                (TierValue),                                                                                           \
                +[]() -> std::unique_ptr<::NS::Scene::SceneSubsystem> { return std::make_unique<Type>(); },            \
                (ShouldCreateFn),                                                                                      \
                #Type});                                                                                               \
            return true;                                                                                               \
        }();                                                                                                           \
    }
