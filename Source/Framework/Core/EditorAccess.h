#pragma once

/// @file EditorAccess.h
/// @brief 編集ツールへ private を開放する friend 宣言を、 editor 構成でだけ有効にするマクロ
///
/// @details `NS_EDITOR_FRIEND(Cls)` は editor ビルド (`NS_EDITOR_ENABLED == 1`) では
/// `friend class Cls;` に展開し、 出荷ビルド (`== 0`) では空に展開する
/// runtime クラスは editor のクラス名すら出荷バイナリへ載せず、 開発時だけ内部を開放できる
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
/// @note `NS_EDITOR_ENABLED` は premake が workspace 全体へ定義し、 editor 構成で 1、 出荷で 0
/// @pre friend 宣言が許される位置のクラス本体の中で使う
/// @post 末尾のセミコロンはマクロ側が含むため、 呼び出し側に `;` は付けない

#if NS_EDITOR_ENABLED
#define NS_EDITOR_FRIEND(ClassName) friend class ClassName;
#else
#define NS_EDITOR_FRIEND(ClassName)
#endif
