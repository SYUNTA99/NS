#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ComponentRef.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace NS::Phys
{
    class PhysicsScene;
} // namespace NS::Phys

namespace NS::Obj
{
    class Scene;
    struct ObjectData;
    struct SceneData;

    //! ObjectData 1 件から配置物を組むファクトリ。組めないデータには nullptr を返し、Rebuild が読み飛ばす
    using ObjectFactoryFn = std::function<std::unique_ptr<GameObject>(const ObjectData&)>;

    //! @brief 配置物 GameObject の単一所有リスト
    //! @details SceneData から一括で組み直す。runtime も editor も同じ Rebuild 経路を通る
    //! 配置物 1 件の組み立ては呼出側のファクトリに委ね、GameObject の型選択や資産解決は持たない
    //! 機能別の型付き控えも持たず、欲しい component 型は ForEachComponent で問い合わせる
    //! 特定の 1 体は永続 id の解決で引く
    //! const の参照で受けても中身は守れない。ObjectAt と範囲 for と ForEachComponent が渡すのは
    //! 非 const の GameObject* と Component* で、呼び出し側はそこから書き換えられる
    //! 依存: NS::Obj::GameObject / ObjectData, NS::Phys::PhysicsScene
    class ObjectList : public NS::Core::NonCopyable
    {
    public:
        ObjectList();
        ~ObjectList();

        //! data の objects から配置物を組み直す。既存の配置物は先に空へ戻し、一時オブジェクトだけ残す
        //! 当たりの同期は含まない。呼出側が続けて SyncPhysics を呼ぶ
        //! factory が空の起動前 / テストでは物を組まない
        void Rebuild(const SceneData& data, Scene& scene, const ObjectFactoryFn& factory);

        //! 配置物の OnEndPlay を逆順に呼んでから所有物を空へ戻す。scene の OnShutdown と Rebuild 冒頭が呼ぶ
        void Clear();

        //! 型 T の配置物を作って加える。所有は ObjectList が持ち、呼出側へは生ポインタだけ返す
        //! id は振らない。未採番の配置物は参照で引けない。参照で引く相手は AppendWithNewId で加える
        //! scene attach は呼出側が返り値へ済ませる
        template <class T, class... Args> T* Spawn(Args&&... args)
        {
            std::unique_ptr<T> obj = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = obj.get();
            Append(std::move(obj));
            return raw;
        }

        //! 組み上がった配置物を id を振らずに 1 体加える。scene attach は呼出側が済ませて渡す
        //! 実行時に湧く一時オブジェクト用。保存もされず、参照で引かれることも無い
        GameObject* Append(std::unique_ptr<GameObject> obj);

        //! 組み上がった配置物と、その全 component に新しい永続 id を振り、名前を付けて 1 体加える
        //! 名前は既存と重なれば番号を付ける。scene attach は呼出側が済ませて渡す
        GameObject* AppendWithNewId(std::unique_ptr<GameObject> obj, std::string name);

        //! objectId 一致の配置物を破棄して所有リストから外す。居なければ何もしない
        //! 当たり箱もここで揃えるので、組み直さずに 1 体だけ消せる
        //! 子は根として残る。親子の切り離しは GameObject の破棄が行う
        //! 0 は未採番の印なので何もしない
        void RemoveByObjectId(std::uint32_t objectId);

        //! objectId 一致の配置物を返す。居なければ nullptr。選択・編集の live 索引
        //! 0 は未採番の印なので常に nullptr。索引から引くので、毎フレーム引いても全配置物を辿らない
        [[nodiscard]] GameObject* FindByObjectId(std::uint32_t objectId) noexcept;

        //! ObjectRef の指す配置物を返す。未設定と該当なしは nullptr
        //! 並びが変わるたびに索引を捨てるので、破棄した相手を指す参照は必ず nullptr になる
        //! 別の配置物への参照はポインタで控えず、ObjectRef で持って使うたびにここで引く
        [[nodiscard]] GameObject* FindObject(ObjectRef ref) noexcept;

        //! @brief ComponentRef の指す Component を返す。未設定と該当なしは nullptr
        //! @details 持ち主の配置物を索引で引き、その中から id で Component を探す。控えずに使うたびに引く
        [[nodiscard]] Component* FindComponent(ComponentRefValue ref) noexcept;

        //! ComponentRef<T> の指す T を返す。未設定・該当なし・型が合わない相手は nullptr
        template <class T> [[nodiscard]] T* FindComponent(const ComponentRef<T>& ref) noexcept
        {
            return ComponentCast<T>(FindComponent(static_cast<const ComponentRefValue&>(ref)));
        }

        //! 稼働中の collider を PhysicsScene へ body として入れ、broadphase を張り直す
        //! 稼働していない collider は body を外す。既存 body は同じ id のまま shape と姿勢を更新する
        //! 続けて RigidBody が collider の形を集めて動く body を張り直す
        void SyncPhysics(NS::Phys::PhysicsScene& physics);

        //! priority が [firstPriority, lastPriority) の Component を昇順で回す。同じ priority の中は配置物の並び順
        //! 帯の一部だけ回したい呼び出し側が使う。一時オブジェクトも同じ帯に乗る
        void UpdateObjects(int firstPriority, int lastPriority = std::numeric_limits<int>::max());

        //! 全配置物の Component を priority の昇順で一括で回す。補間用の前回値は呼ぶ側が更新の前に SnapshotObjects
        //! で揃える。並び順の登録簿は持たない。各 component がコンストラクタで指定する priority だけで並びが決まる
        void UpdateAllObjects();

        //! 全配置物の Root を Snapshot する。previous を current へ揃える
        void SnapshotObjects();

        //! 永続 object id を 1 個割り当ててカウンタを進める
        [[nodiscard]] std::uint32_t AllocateObjectId() noexcept { return m_nextObjectId++; }

        //! 次に割り当てる永続 id。保存がファイルへ書き戻すために読む
        [[nodiscard]] std::uint32_t NextObjectId() const noexcept { return m_nextObjectId; }

        //! 抱えている配置物の数
        [[nodiscard]] std::size_t ObjectCount() const noexcept { return m_objects.size(); }

        //! index 番目の配置物。範囲外は nullptr。所有は ObjectList が持ったまま外へ出さない
        [[nodiscard]] GameObject* ObjectAt(std::size_t index) const noexcept;

        //! 範囲 for 用の反復子。辿ると生ポインタが出るので、所有の入れ物を外へ見せずに全配置物を回せる
        //! 添字が要る呼び出し側は ObjectCount / ObjectAt を使う
        //! std::vector<GameObject*> を返す関数は作らない。毎回確保になる
        //! 配置物の生ポインタの配列もメンバに持たない。m_objects と食い違う
        class Iterator
        {
        public:
            [[nodiscard]] GameObject* operator*() const noexcept { return m_slot->get(); }
            Iterator& operator++() noexcept
            {
                ++m_slot;
                return *this;
            }
            [[nodiscard]] bool operator!=(const Iterator& rhs) const noexcept { return m_slot != rhs.m_slot; }

        private:
            friend class ObjectList;
            explicit Iterator(const std::unique_ptr<GameObject>* slot) noexcept : m_slot(slot) {}
            const std::unique_ptr<GameObject>* m_slot = nullptr;
        };

        [[nodiscard]] Iterator begin() const noexcept { return Iterator{m_objects.data()}; }
        [[nodiscard]] Iterator end() const noexcept { return Iterator{m_objects.data() + m_objects.size()}; }

        //! 全配置物から型 T の component を訪ねる。リフレクションの is-a 照合なので抽象基底型でも派生を引ける
        //! 型付き控えの代わりの問い合わせ口で、寿命は ObjectList が持ったまま
        template <class T, class Fn> void ForEachComponent(Fn&& fn) const
        {
            for (const std::unique_ptr<GameObject>& obj : m_objects)
            {
                for (Component* comp : obj->Components())
                {
                    if (T* typed = ComponentCast<T>(comp))
                    {
                        fn(*typed);
                    }
                }
            }
        }

    private:
        //! 並びが変わったので索引を捨てる。並びを変える箇所は必ず呼び、破棄した配置物を索引に残さない
        void MarkIndexDirty() noexcept;

        //! entry の component の id を obj の実体へ書く。id を書くのはシーンの配置物を持つここだけ
        void AssignComponentIds(GameObject& obj, const ObjectData& entry);

        //! 欄の型に合わない Component を指す ComponentRef を警告する。引けば nullptr になるだけなので値は変えない
        void WarnMismatchedComponentRefs();

        std::vector<std::unique_ptr<GameObject>> m_objects; // 配置物の単一所有リスト
        std::vector<Component*> m_scheduled;                // UpdateObjects が priority 順に並べ直す作業用の並び
        std::unordered_map<std::uint32_t, GameObject*> m_index; // 永続 id から配置物への索引。汚れていれば次に引く時に作り直す
        bool m_indexDirty = true;                               // 索引が所有リストと食い違っているか
        std::uint32_t m_nextObjectId = 1;                   // 次に割り当てる永続 id。単調増加で欠番は再利用しない
        bool m_updating = false;                            // UpdateObjects の実行中か。入れ子の呼び出しの検知に使う
    };

} // namespace NS::Obj
