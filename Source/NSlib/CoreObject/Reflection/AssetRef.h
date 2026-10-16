#pragma once

#include <string>
#include <type_traits>
#include <utility>

namespace NS
{
    //! @brief ファイルの素材を指す反映の欄。素材の名前と、読んだ物を 1 つで持つ
    //! @details 保存とインスペクタは名前の文字列だけを扱う。名前を替えると読んだ物は空に戻り、次に読むまで空のまま
    template <class T> class AssetRef
    {
    public:
        AssetRef() = default;
        explicit AssetRef(std::string name) : m_name(std::move(name)) {}

        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }

        //! 同じ名前なら読んだ物を残す。違う名前なら空に戻す
        void SetName(std::string name)
        {
            if (name == m_name)
            {
                return;
            }
            m_name = std::move(name);
            m_loaded = nullptr;
        }

        //! 読んだ物。まだ読んでいない・読めなかった時は nullptr
        [[nodiscard]] T* Get() const noexcept { return m_loaded; }

        void SetLoaded(T* loaded) noexcept { m_loaded = loaded; }

    private:
        std::string m_name;
        T* m_loaded = nullptr; // 素材は AssetManager が持つ
    };

    template <class T> struct IsAssetRef : std::false_type
    {
    };
    template <class T> struct IsAssetRef<AssetRef<T>> : std::true_type
    {
    };

    namespace Gfx
    {
        class D3DMaterial;
        class D3DMesh;
        struct AnimClip;
    } // namespace Gfx
    struct SkinnedModel;

    //! @brief ref の名前の素材を AssetManager::Get() の型ごとの口から読み、ref に持たせる
    //! @details 名前の決まり・読めない時の代わりと警告は口が持つ。参照の欄にできる型は、ここに並ぶ型だけ
    //! 並んでいない型の欄を作ると、その欄の NS_REFLECT_FIELD の行でビルドが落ちる
    void LoadAssetRef(AssetRef<Gfx::D3DMesh>& ref);
    void LoadAssetRef(AssetRef<Gfx::D3DMaterial>& ref);
    void LoadAssetRef(AssetRef<const SkinnedModel>& ref);
    void LoadAssetRef(AssetRef<const Gfx::AnimClip>& ref);

    class SceneActor;

    //! @brief actor の部品の参照の欄を全部読む
    //! @details 呼ぶのは SceneActor::ReloadAssets だけ。作る時と欄を書き換えた時の両方がそこを通る
    void LoadAssetRefs(SceneActor& actor);
} // namespace NS
