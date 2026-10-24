#pragma once

#include <string>
#include <string_view>
#include <type_traits>

namespace NS
{
    class AnimClip;
    class Material;
    class Mesh;
    class SkinnedModel;

    //! @brief 素材の姿を指す欄の型。保存とインスペクタは、指す素材の道の文字列を扱う
    //! @details 素材の欄にできる型は、ここに並ぶ型だけ。並んでいない型の欄は FieldTypeOf の static_assert で落ちる
    template <class T> struct IsAssetPointer : std::false_type
    {
    };
    template <> struct IsAssetPointer<Mesh*> : std::true_type
    {
    };
    template <> struct IsAssetPointer<Material*> : std::true_type
    {
    };
    template <> struct IsAssetPointer<SkinnedModel*> : std::true_type
    {
    };
    template <> struct IsAssetPointer<AnimClip*> : std::true_type
    {
    };

    //! @brief name の素材を AssetManager::Get() の型ごとの口から引き、field に入れる
    //! @details 読めない名前でも、その名前を持つ中身の空の素材が入るので、field は nullptr にならない
    void LoadAssetField(Mesh*& field, std::string_view name);
    void LoadAssetField(Material*& field, std::string_view name);
    void LoadAssetField(SkinnedModel*& field, std::string_view name);
    void LoadAssetField(AnimClip*& field, std::string_view name);
    //! field が指す素材の道。nullptr なら空
    [[nodiscard]] const std::string& AssetFieldName(const Mesh* field) noexcept;
    [[nodiscard]] const std::string& AssetFieldName(const Material* field) noexcept;
    [[nodiscard]] const std::string& AssetFieldName(const SkinnedModel* field) noexcept;
    [[nodiscard]] const std::string& AssetFieldName(const AnimClip* field) noexcept;

    class SceneActor;

    //! @brief actor の部品の素材の欄を全部、今の道で引き直す
    //! @details 呼ぶのは SceneActor::ReloadAssets だけ。作る時と欄を書き換えた時の両方がそこを通る
    void LoadAssetFields(SceneActor* actor);
} // namespace NS
