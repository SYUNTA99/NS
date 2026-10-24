#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/System/SceneSystem.h"

#include <memory>
#include <utility>
#include <vector>

namespace NS
{
    class Scene;

    //! 型ごとに一つのシステムを必要時に生成する。配置の再構築時に生成と逆順で破棄する
    class SceneSystemHolder : public NonCopyable
    {
    public:
        explicit SceneSystemHolder(Scene& scene) noexcept : m_scene(scene) {}
        ~SceneSystemHolder() noexcept { Clear(); }

        //! 型 T の物。まだ作っていなければ nullptr
        template <class T> [[nodiscard]] T* Find() const noexcept
        {
            for (const std::pair<const void*, std::unique_ptr<SceneSystem>>& entry : m_objects)
            {
                if (entry.first == KeyOf<T>())
                {
                    return static_cast<T*>(entry.second.get());
                }
            }
            return nullptr;
        }

        //! 型 T の物。まだ作っていなければ T(Scene&) で作る
        template <class T> T& GetOrCreate()
        {
            if (T* found = Find<T>())
            {
                return *found;
            }
            std::unique_ptr<T> created = std::make_unique<T>(m_scene);
            T& result = *created;
            m_objects.emplace_back(KeyOf<T>(), std::move(created));
            return result;
        }

        //! @brief 作った物を全て、作った順の逆に捨てる
        //! @details 捨てる直前に Scene の更新の登録から外す。管理役は自分で解除しない
        void Clear() noexcept;

    private:
        // 型ごとに 1 つのタグ。関数内の static の番地は型ごとに 1 つに決まり、RTTI を使わずに型を見分けられる
        template <class T> [[nodiscard]] static const void* KeyOf() noexcept
        {
            static const char key = 0;
            return &key;
        }

        Scene& m_scene;
        std::vector<std::pair<const void*, std::unique_ptr<SceneSystem>>> m_objects; // 作った順
    };
} // namespace NS
