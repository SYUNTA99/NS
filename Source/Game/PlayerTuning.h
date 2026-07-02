#pragma once

/// @file PlayerTuning.h
/// @brief プレイヤーの component 構成とチューニング値の読込、 保存先パスの共有
///
/// @details 反射フィールドを Assets 配下の JSON へ往復させる読込側
/// JSON の components 一覧が構成の出所で、 player に無い型は登録 factory で生成して加え、
/// ある型は値だけ適用する。 同型は 1 個までで最初の 1 件へ適用する
/// 読込はプレイヤー生成直後かつ OnStart 前に呼び、 ファイルが無ければコード既定の構成と値を使う
/// 保存は出荷不要なので Editor/PlayerTuningIO.h に分け、 パスだけ PlayerTuningPath() で共有する

#include <filesystem>
#include <string_view>

namespace NS::Scene
{
    class GameObject;
}

/// チューニングファイルの絶対パス。 読込と保存で同じ場所を指すよう共有する
[[nodiscard]] std::filesystem::path PlayerTuningPath();

/// JSON テキストの components 一覧を player へ適用する。 型が player に無ければ登録 factory で
/// 生成して構成へ加え、 あれば値だけ適用する。 解析失敗 / 未登録型は読み飛ばして既定を保つ
/// ファイル読込とテストが共有する本体
void ApplyPlayerTuningText(NS::Scene::GameObject& player, std::string_view jsonText) noexcept;

/// 保存済みチューニングがあれば構成と値を player へ適用する。 不在 / 破損時は既定のまま
void LoadPlayerTuning(NS::Scene::GameObject& player) noexcept;
