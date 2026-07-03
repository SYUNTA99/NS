#pragma once

/// @file PlayerTuning.h
/// @brief 新規プレイヤーの既定テンプレート取込と、 player object データの live 適用
///
/// @details プレイヤーの構成と値の真実はレベルの player object が持つ。 PlayerTuning.json は
/// 旧形式の移行や新規レベルでプレイヤーを合成する時にだけ使う既定テンプレートへ降格した
/// テンプレートの取込はデータどうしの merge で、 live への反映は player object データからの適用で行う
/// 保存は出荷不要なので Editor/PlayerTuningIO.h に分け、 パスだけ PlayerTuningPath() で共有する

#include <filesystem>
#include <string_view>

namespace NS::Scene
{
    class GameObject;
}

namespace NS::Game::Level
{
    struct ObjectInstance;
}

/// テンプレートファイルの絶対パス。 取込と保存で同じ場所を指すよう共有する
[[nodiscard]] std::filesystem::path PlayerTuningPath();

/// テンプレート JSON の components を player object データへ写す。 既存型は同名フィールドの値を
/// 上書きし、 無い型は構成ごと追加する。 解析失敗 / 不正値は読み飛ばして既定を保つ
/// ファイル取込とテストが共有する本体
void MergePlayerTuningText(NS::Game::Level::ObjectInstance& playerObject, std::string_view jsonText) noexcept;

/// 保存済みテンプレートがあれば MergePlayerTuningText で写す。 不在 / 破損時は既定のまま
void MergeSavedPlayerTuning(NS::Game::Level::ObjectInstance& playerObject) noexcept;

/// player object データの components を live player へ適用する。 型が player に無ければ登録 factory で
/// 生成して構成へ加え、 あれば値だけ適用する。 未登録型は読み飛ばす
/// startCreated は生成した component を即 OnStart するか。 player の OnStart 前の適用では false にする
void ApplyPlayerObjectComponents(NS::Scene::GameObject& player,
                                 const NS::Game::Level::ObjectInstance& playerObject,
                                 bool startCreated) noexcept;
