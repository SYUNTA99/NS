#pragma once

/// @file SceneSubsystem.h
/// @brief NS::Scene::SceneSubsystem — シーンに紐づく service の基底と、その解決口
///
/// @details service は依存を受け取らない既定構築で生成し、依存解決は Initialize で行う
/// tier は所有者の寿命を表す。App は Application が所有しシーンを跨いで生き、Scene は
/// SceneBase が所有しシーンと共に生成・破棄される。ISubsystemProvider は App tier の
/// 解決口で、Scene 層はこの抽象だけを知り App 層の実装へ委譲する
/// 依存: なし

#include <cstdint>
#include <typeindex>

namespace NS::Scene
{
    class SceneBase;

    /// service の所有スコープ。App はシーンを跨ぐ寿命、Scene はシーン単位の寿命
    enum class SubsystemTier : std::uint8_t
    {
        App,
        Scene
    };

    /// シーンに紐づく service の基底。生成後 Initialize で依存を解決し、破棄前に Deinitialize する
    class SceneSubsystem
    {
    public:
        virtual ~SceneSubsystem() = default;

        SceneSubsystem(const SceneSubsystem&) = delete;
        SceneSubsystem& operator=(const SceneSubsystem&) = delete;
        SceneSubsystem(SceneSubsystem&&) = delete;
        SceneSubsystem& operator=(SceneSubsystem&&) = delete;

        /// 所属シーンが確定した直後に 1 回呼ばれる。ここで他 service や資源を解決する
        virtual void Initialize(SceneBase&) noexcept {}

        /// 破棄前に 1 回呼ばれる。Initialize で確保したものを解放する
        virtual void Deinitialize() noexcept {}

    protected:
        SceneSubsystem() = default;
    };

    /// App tier service の解決口。Scene 層はこの抽象だけを知り、App 層の Application が実装する
    /// 見つからなければ nullptr を返す
    class ISubsystemProvider
    {
    public:
        virtual ~ISubsystemProvider() = default;
        [[nodiscard]] virtual SceneSubsystem* FindAppSubsystem(std::type_index type) const noexcept = 0;
    };
} // namespace NS::Scene
