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
} // namespace NS
