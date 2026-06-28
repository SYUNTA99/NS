#pragma once

/// @file PlayerTuning.h
/// @brief プレイヤー全コンポーネントのチューニング値の読込と、 保存先パスの共有
///
/// @details 反射フィールドを Assets 配下の JSON へ往復させる読込側
/// 読込はプレイヤー生成直後に呼び、 ファイルが無ければコード既定値を使う
/// 保存は出荷不要なので Editor/PlayerTuningIO.h に分け、 パスだけ PlayerTuningPath() で共有する

#include <filesystem>

namespace NS::Scene
{
    class GameObject;
}

/// チューニングファイルの絶対パス。 読込と保存で同じ場所を指すよう共有する
[[nodiscard]] std::filesystem::path PlayerTuningPath();

/// 保存済みチューニングがあれば player の各コンポーネントへ型名一致で適用する。 不在 / 破損時は既定値を保つ
void LoadPlayerTuning(const NS::Scene::GameObject& player) noexcept;
