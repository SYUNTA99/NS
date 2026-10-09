#pragma once

#include "NSlib/Object/Object.h"
#include "NSlib/Object/Reflection/Reflection.h"

#include <concepts>
#include <string>
#include <string_view>

namespace NS::Obj
{
    class AssetManager;
    class Actor;
    class Scene;
    class Transform;

    //! @brief 振る舞いを表現する再利用ブロック。通常は派生して使う
    //! SubObject 自身は所有者 Actor を生参照する。owner は生成後に Actor が注入する
    //! scene が持つ物は OnStart で OwningScene() 経由で借りる
    //! ライフサイクル:
    //!   - OnStart() — Scene attach 直後に 1 回
    //!   - OnUpdate() — 持ち主の Actor のクラスが自分の Update の中で呼んだ時だけ。IsActive()==false なら呼ばれない
    //!     dt は NS::OS::FrameTimer::FixedDelta() で取得する。全て static なので Application 不要
    //!   - OnEndPlay() — Scene 破棄 / SubObject 廃棄前に 1 回
    class SubObject : public Object
    {
    public:
        SubObject() noexcept;

        virtual ~SubObject() noexcept;

        //! 持ち主の中の部品名。保存の鍵とインスペクタの見出しに使う。Actor が部品を積む時に付ける
        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }

        //! 所有 Actor。Scene attach 後は non-null
        [[nodiscard]] Actor* Owner() noexcept { return m_owner; }
        [[nodiscard]] const Actor* Owner() const noexcept { return m_owner; }

        //! 持ち主の居る Scene。持ち主が無いか Scene に居なければ nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept;

        //! 所有 Actor の root Transform への近道。型名衝突回避のため RootTransform 命名
        [[nodiscard]] Transform& RootTransform() noexcept;
        [[nodiscard]] const Transform& RootTransform() const noexcept;

        //! @brief この SubObject が効いているか
        //! @details 自分の active に加えて、owner の階層全体の active まで見る
        //! 更新・ 描画・ 当たりは全てこの問いを使う。自身の値だけが要る編集 UI は IsActiveSelf を使う
        [[nodiscard]] bool IsActive() const noexcept;

        //! この SubObject 自身の active 値。owner の状態は含まない
        [[nodiscard]] bool IsActiveSelf() const noexcept { return m_active; }
        //! @brief 自分の active を切り替える
        //! @details 設定できるのは自分の分だけで、階層は見ない。実行中に部品を外す口で、外した仮想カメラは
        //! CameraManager が選ばない。編集の休止・当たりの止め・操作の停止の印には使わない。
        //! 編集の休止の正は Scene の世界の駆動、当たりの止めの正は ImpactResolver の数え、操作の停止の正は
        //! PlayerInput の止め
        void SetActive(bool active) noexcept { m_active = active; }

        //! データに書かれた active 値。false は保存にも残り、読み直しても false のまま
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        //! データの active を切り替える。実行中に部品を外す SetActive とは別系統で、互いを上書きしない
        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

        //! 持ち主が世界へ出直した時に呼ばれる。自分が効いていない間は呼ばれない。既定は何もしない
        virtual void OnAppear() {}
        //! 持ち主が世界から外れた時に呼ばれる。既定は何もしない
        virtual void OnKill() noexcept {}
        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnEndPlay() {}

        //! リフレクションで運んだ参照文字列 (mesh / material 等) を資産の実体へ引き当てる。既定は何もしない
        //! 配置物の組み立てが値の適用後に呼ぶ。AssetManager が無い間 (テスト等) は呼ばれない
        virtual void ResolveAssets(AssetManager&) {}

        NS_REFLECT_NONE(SubObject, Object)

    private:
        friend class Actor;
        void AttachOwner(Actor* owner) noexcept { m_owner = owner; }
        void SetName(std::string_view name) { m_name = name; }

        std::string m_name;
        Actor* m_owner = nullptr; // 所有 Actor、attach 前は nullptr
        bool m_active = true;     // false なら OnUpdate を skip
        bool m_enabled = true;    // データの active 値、false なら OnUpdate を飛ばす
    };

} // namespace NS::Obj
