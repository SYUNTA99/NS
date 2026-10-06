#pragma once

#include "NSlib/Core/NonCopyable.h"

#include <memory>
#include <utility>
#include <vector>

namespace NS::Obj
{
    class Scene;

    //! @brief シーンに 1 つの物の共通基底。コースの進行役・当たりの調べ役など
    //! @details SceneObjHolder が型ごとに 1 つ作って持つ。作るのは初めて引いた時で、コンストラクタがシーンを受け取る
    class ISceneObj : public NS::NonCopyable
    {
    public:
        virtual ~ISceneObj() noexcept = default;
    };

    //! @brief シーンに 1 つの物の置き場。オデッセイの SceneObjHolder に当たる
    //! @details 型ごとに 1 つを持ち、IUseSceneObj の窓口から FindSceneObj / GetOrCreateSceneObj で引く
    //! 配置物を組み直す時に全て捨て、最初の状態から作り直させる。捨てる順は作った順の逆
    class SceneObjHolder : public NS::NonCopyable
    {
    public:
        explicit SceneObjHolder(Scene& scene) noexcept : m_scene(scene) {}
        ~SceneObjHolder() noexcept { Clear(); }

        //! 型 T の物。まだ作っていなければ nullptr
        template <class T> [[nodiscard]] T* Find() const noexcept
        {
            for (const std::pair<const void*, std::unique_ptr<ISceneObj>>& entry : m_objects)
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

        //! 作った物を全て、作った順の逆に捨てる
        void Clear() noexcept
        {
            while (!m_objects.empty())
            {
                m_objects.pop_back();
            }
        }

    private:
        // 型ごとに 1 つの印。関数内の static の番地は型ごとに 1 つに決まり、RTTI を使わずに型を見分けられる
        template <class T> [[nodiscard]] static const void* KeyOf() noexcept
        {
            static const char key = 0;
            return &key;
        }

        Scene& m_scene;
        std::vector<std::pair<const void*, std::unique_ptr<ISceneObj>>> m_objects; // 作った順
    };
} // namespace NS::Obj
