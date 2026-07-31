#pragma once

/// editor ビルドでだけ friend 宣言を有効にするマクロ
/// @details editor 構成では `friend class ClassName;` に展開、出荷ビルドでは空に展開する。
/// 出荷バイナリに editor クラス名を残さず、開発時だけ内部を開放できる。
/// @code
/// class MyScene : public NS::Object::Scene
/// {
///     NS_EDITOR_FRIEND(LevelEditorController)
/// public:
///     ...
/// };
/// @endcode
/// @note セミコロンはマクロ側が含むため、呼び出し側に `;` は付けない
#if NS_EDITOR_ENABLED
#define NS_EDITOR_FRIEND(ClassName) friend class ClassName;
#else
#define NS_EDITOR_FRIEND(ClassName)
#endif