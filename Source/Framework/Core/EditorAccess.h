#pragma once

/// @file EditorAccess.h
/// @brief 編集ツールへ private を開放する friend 宣言を、 editor 構成でだけ有効にするマクロ
///
/// @details `NS_EDITOR_FRIEND(Cls)` は editor を含むビルド (`NS_EDITOR_ENABLED == 1`) では
/// `friend class Cls;` に展開し、 出荷ビルド (`NS_EDITOR_ENABLED == 0`) では空に展開する
/// これにより runtime クラスは editor のクラス名すら出荷バイナリへ載せずに、 開発時だけ
/// 編集ツールへ内部を開放できる
///
/// @code
/// class LevelPlayScene : public NS::Scene::SceneBase
/// {
///     NS_EDITOR_FRIEND(LevelEditorController)
/// public:
///     ...
/// };
/// @endcode
///
/// @note `NS_EDITOR_ENABLED` は premake が workspace 全体へ定義する (editor 構成で 1、 出荷で 0)
/// @pre クラス本体の中で使う (friend 宣言が許される位置)
/// @post 末尾のセミコロンはマクロ側が含むため、 呼び出し側に `;` は付けない

#if NS_EDITOR_ENABLED
#define NS_EDITOR_FRIEND(ClassName) friend class ClassName;
#else
#define NS_EDITOR_FRIEND(ClassName)
#endif
