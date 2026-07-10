#pragma once

/// @file PlayerTuning.h
/// @brief 新規プレイヤーの既定テンプレート取込 (エディタ専用)
///
/// @details プレイヤーの構成と値の真実はレベルの player object が持つ。 PlayerTuning.json は
/// 旧形式の移行や新規レベルでプレイヤーを合成した時にだけ写す既定テンプレートで、 開発イテレーションの
/// 道具なので取込 / 保存ともエディタの持ち物。 Game は合成が起きた事実だけを報告し、 テンプレートを知らない
/// テンプレートの取込はデータどうしの merge で行い、 live への反映は world の組み直しが担う
/// 保存は Editor/PlayerTuningIO.h、 パスだけ PlayerTuningPath() で共有する

namespace NS::Scene
{
    struct ObjectData;
}

/// テンプレートファイルの絶対パス。 取込と保存で同じ場所を指すよう共有する
[[nodiscard]] std::filesystem::path PlayerTuningPath();

/// テンプレート JSON の components を player object データへ写す。 既存型は同名フィールドの値を
/// 上書きし、 無い型は構成ごと追加する。 解析失敗 / 不正値は読み飛ばして既定を保つ
/// ファイル取込とテストが共有する本体
void MergePlayerTuningText(NS::Scene::ObjectData& playerObject, std::string_view jsonText) noexcept;

/// 保存済みテンプレートがあれば MergePlayerTuningText で写す。 不在 / 破損時は既定のまま
void MergeSavedPlayerTuning(NS::Scene::ObjectData& playerObject) noexcept;
