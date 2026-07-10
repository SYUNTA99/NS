#pragma once

/// @file PlayerTuningIO.h
/// @brief プレイヤー全コンポーネント値をチューニングファイルへ書き出すエディタ専用の保存
///
/// @details 読込 merge は Editor/PlayerTuning.h、 保存はここ。 どちらもエディタ専用で出荷には積まない
/// 直列化は SerializeComponent、 保存先は PlayerTuningPath() を共有して読込と一致させる

namespace NS::Scene
{
    class GameObject;
}

namespace NS::Editor
{
    /// player の全コンポーネントの現在値を書き出す。 成功で true
    [[nodiscard]] bool SavePlayerTuning(const NS::Scene::GameObject& player) noexcept;
} // namespace NS::Editor
