#pragma once

/// @file ThemeRegistry.h
/// @brief 視覚フィールドを保有する 5 テーマの lookup。 `.asset` ファイル読込で値を差し替えられる
///
/// @details `NS::GameCore::Theme::Get(id)` は 5 件の `ThemeData` への const 参照を返し、 範囲外は Grass
/// にフォールバックする 静的 storage 上に並ぶので参照は frame 越しに有効、 再読込で中身だけ変わる

#include "GameCore/Level/LevelData.h"
#include "GameCore/Theme/ThemeData.h"
#include "GameCore/Theme/ThemeId.h"

namespace NS::GameCore::Theme
{
    /// 5 テーマの中から id に対応する ThemeData を返す
    /// Count 以上の範囲外なら入力境界の防御として ThemeId::Grass にフォールバックする
    [[nodiscard]] const ThemeData& Get(ThemeId id) noexcept;

    /// ディレクトリの `.asset` 雛形 5 件を ThemeId と 1:1 の固定名で読み、 registry を上書きする
    /// 呼ぶ度に中立の既定値 ThemeData{} へ戻してから読むため、 欠落・破損・種別違い・値不正のテーマは中立のままになる
    /// 種別欄 `"type"` が `"theme"` でないファイルは雛形として読まない。 未知キーは無視する。 起動列と editor
    /// の雛形再読込が呼ぶ
    void LoadThemesFromDirectory(const std::filesystem::path& directory);

    /// テーマの視覚フィールドをシーンの環境値へ写して返す。 雛形からシーンへの写し込みの唯一の入口で、
    /// 新規シーン・テーマ適用・旧形式の読込移行が使う。 skybox パスは '/' 区切りへ正規化する
    [[nodiscard]] NS::GameCore::Level::LevelEnvironment MakeEnvironmentFromTheme(const ThemeData& theme);
} // namespace NS::GameCore::Theme
