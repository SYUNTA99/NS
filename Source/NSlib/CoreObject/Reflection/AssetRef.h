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

    //! @brief ref の名前の素材を AssetManager::Get() から読み、ref に持たせる。素材の型ごとに特殊化を 1 つ書く
    //! @details 読めなかった名前はログに出し、型ごとの代わりの素材を持たせる。特殊化の無い型はリンクで落ちる
    template <class T> void LoadAssetRef(AssetRef<T>& ref);

    namespace Gfx
    {
        class Material;
        class Mesh;
    } // namespace Gfx

    //! 組み込みの形の名前か、Content の下のメッシュのパス。読めなければ cube
    template <> void LoadAssetRef<Gfx::Mesh>(AssetRef<Gfx::Mesh>& ref);
    //! 共有のマテリアルの名前か、Content の下の .mat のパス。読めなければ共有の player
    template <> void LoadAssetRef<Gfx::Material>(AssetRef<Gfx::Material>& ref);

    class SceneActor;

    //! @brief actor の部品の参照の欄を全部読む
    //! @details 呼ぶのは SceneActor::ReloadAssets だけ。作る時と欄を書き換えた時の両方がそこを通る
    void LoadAssetRefs(SceneActor& actor);
} // namespace NS
