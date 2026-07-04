#pragma once

/// @file PlayerTuningIO.h
/// @brief プレイヤー全コンポーネント値をチューニングファイルへ書き出すエディタ専用の保存
///
/// @details 保存はエディタからしか呼ばれず出荷不要なので、 出荷でも要る読込 Game/PlayerTuning.h と分ける
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
