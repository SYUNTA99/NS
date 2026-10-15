#pragma once

#include "NSlib/Object/Object.h"
#include "NSlib/Object/Reflection/Reflection.h"

#include <concepts>

namespace NS::Obj
{
    class AssetManager;
    class SceneActor;
    class Scene;
    class Transform;

    //! @brief SceneActor が所有する部品。更新は持ち主が呼び、体の経過秒は BodyDelta から取る
    class SubObject : public Object
    {
    public:
        SubObject() noexcept;

        virtual ~SubObject() noexcept;

        //! 持ち主の SceneActor。シーンへの所属後は必ず設定される
        [[nodiscard]] SceneActor* Owner() noexcept { return m_owner; }
        [[nodiscard]] const SceneActor* Owner() const noexcept { return m_owner; }

        //! 持ち主の居る Scene。持ち主が無いか Scene に居なければ nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept;

        //! 持ち主の体の 1 歩の秒。持ち主が無ければ固定の 1 歩の秒
        [[nodiscard]] float BodyDelta() const noexcept;

        [[nodiscard]] Transform& RootTransform() noexcept;
        [[nodiscard]] const Transform& RootTransform() const noexcept;

        //! @brief この SubObject が効いているか
        //! @details 自分の active に加えて、owner の階層全体の active まで見る
        //! 更新・ 描画・ 当たりは全てこの問いを使う。自身の値だけが要る編集 UI は IsActiveSelf を使う
        [[nodiscard]] bool IsActive() const noexcept;

        //! この SubObject 自身の active 値。owner の状態は含まない
        [[nodiscard]] bool IsActiveSelf() const noexcept { return m_active; }
        //! @brief 実行中の部品の有効状態を切り替える
        //! @details シーンの休止、ヒットストップ、操作ロックには使わない。階層や保存値は変更しない
        void SetActive(bool active) noexcept { m_active = active; }

        //! データに書かれた active 値。false は保存にも残り、読み直しても false のまま
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        //! データの active を切り替える。実行中に部品を外す SetActive とは別系統で、互いを上書きしない
        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

        //! 配置物の組み立てが値の適用後に呼ぶ。AssetManager が無い間 (テスト等) は呼ばれない
        virtual void ResolveAssets(AssetManager&) {}

        NS_REFLECT_NONE(SubObject, Object)

    private:
        friend class SceneActor;
        void AttachOwner(SceneActor* owner) noexcept { m_owner = owner; }

        SceneActor* m_owner = nullptr; // 所有 SceneActor、attach 前は nullptr
        bool m_active = true;
        bool m_enabled = true;
    };

} // namespace NS::Obj
