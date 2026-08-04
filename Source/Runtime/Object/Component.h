#pragma once

#include "Runtime/Object/Object.h"
#include "Runtime/Object/Reflection/Reflection.h"

namespace NS::Object
{
    class AssetManager;
    class GameObject;
    class Transform;

    /// OnUpdate 実行順を制御する priority。値が小さいほど先、同 priority 内は登録順
    /// 1 体の中の並びに加え、 World::UpdateObjects が配置物を帯ごとに横断して回す時の単位にも使う
    /// どの段階を使うかは component を書く人が決める。 段階の間の値 (+10 等) も自由に使える
    struct TickPriority
    {
        /// Update より先に走らせたい物
        static constexpr int EarlyUpdate = 0;
        /// 世界を動かす。 Component の既定
        static constexpr int Update = 200;
        /// 全ての更新が終わった後
        static constexpr int LateUpdate = 400;
    };

    /// @brief 振る舞いを表現する再利用ブロック。通常は派生して使う
    /// @details GameObject::AddComponent<T>() で生成され、 GameObject が unique_ptr で寿命を所有する
    /// Component 自身は所有者 GameObject を生参照する。 owner は生成後に GameObject が注入する
    /// 兄弟 Component への参照は OnStart で Owner()->FindComponent<T>() により解決する
    /// scene の Subsystem は OnStart で Owner()->OwningScene() 経由で借用する
    /// ライフサイクル:
    ///   - OnStart() — Scene attach 直後に 1 回
    ///   - OnUpdate() — fixed step 内で毎回。 `IsActive()==false` なら skip する
    ///     dt は `NS::Core::FrameTimer::FixedDelta()` で取得する。 全て static なので Application 不要
    ///   - OnEndPlay() — Scene 破棄 / Component 廃棄前に 1 回
    class Component : public Object
    {
    public:
        /// priority をコンストラクタ引数で確定する。基底コンストラクタ内は仮想関数テーブルが未確定なので
        /// 仮想呼び出しを避ける
        explicit Component(int priority = TickPriority::Update) noexcept;

        virtual ~Component() noexcept;

        /// OnUpdate 実行順の priority。既定は `TickPriority::Update` の 200
        [[nodiscard]] int Priority() const noexcept { return m_priority; }

        /// 所有 GameObject。Scene attach 後は non-null
        [[nodiscard]] GameObject* Owner() noexcept { return m_owner; }
        [[nodiscard]] const GameObject* Owner() const noexcept { return m_owner; }

        /// 所有 GameObject の root Transform への近道。型名衝突回避のため RootTransform 命名
        [[nodiscard]] Transform& RootTransform() noexcept;
        [[nodiscard]] const Transform& RootTransform() const noexcept;

        /// @brief この Component が効いているか
        /// @details 自分の active に加えて、 持ち主の階層全体の active まで見る
        /// 更新・ 描画・ 当たりは全てこの問いを使う。 自身の値だけが要る編集 UI は IsActiveSelf を使う
        [[nodiscard]] bool IsActive() const noexcept;

        /// この Component 自身の active 値。 持ち主の状態は含まない
        [[nodiscard]] bool IsActiveSelf() const noexcept { return m_active; }
        /// 自分の active を切り替える。 設定できるのは自分の分だけで、 階層は見ない
        void SetActive(bool active) noexcept { m_active = active; }

        /// データに書かれた active 値。 false は保存にも残り、 読み直しても false のまま
        [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }
        /// データの active を切り替える。 モード切替で使う SetActive とは別系統で、 互いを上書きしない
        void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }

        virtual void OnStart() {}
        virtual void OnUpdate() {}
        virtual void OnEndPlay() {}

        /// 反射で運んだ参照文字列 (mesh / material 等) を資産の実体へ引き当てる。 既定は何もしない
        /// world の組み立てが値の適用後に呼ぶ。 AssetManager が無い間 (テスト等) は呼ばれない
        virtual void ResolveAssets(AssetManager&) {}

        /// このコンポーネント型の反射情報。未反射型は nullptr。エディタが Component* 越しに field を列挙する
        [[nodiscard]] virtual const ReflectionInfo* GetReflection() const noexcept { return nullptr; }

        /// 反射の typeName をクラス名として返す。名前の出所を反射 1 本に保つため派生で個別に返さない
        [[nodiscard]] const char* ClassName() const noexcept override
        {
            const ReflectionInfo* info = GetReflection();
            if (info != nullptr)
                return info->typeName;
            return "";
        }

        /// 自分の反射鎖に target が現れるか。反射照合による is-a 判定。target が nullptr なら常に false
        [[nodiscard]] bool IsA(const ReflectionInfo* target) const noexcept;

    private:
        // owner 注入は AddComponent 経由のみ。Component から GameObject の非公開メンバへはアクセスしない
        friend class GameObject;
        void AttachOwner(GameObject* owner) noexcept { m_owner = owner; }

        GameObject* m_owner = nullptr;         // 所有 GameObject、 attach 前は nullptr
        int m_priority = TickPriority::Update; // OnUpdate 実行順
        bool m_active = true;                  // false なら OnUpdate を skip
        bool m_enabled = true;                 // データの active 値、 false なら OnUpdate を飛ばす
    };

    /// 反射照合で通れば static_cast、外れれば nullptr を返す。comp が nullptr でも安全
    template <class T> [[nodiscard]] T* ComponentCast(Component* comp) noexcept
    {
        if (comp != nullptr && comp->IsA(T::StaticReflection()))
            return static_cast<T*>(comp);
        return nullptr;
    }

    /// const 版。反射照合で通れば static_cast、外れれば nullptr
    template <class T> [[nodiscard]] const T* ComponentCast(const Component* comp) noexcept
    {
        if (comp != nullptr && comp->IsA(T::StaticReflection()))
            return static_cast<const T*>(comp);
        return nullptr;
    }

} // namespace NS::Object
