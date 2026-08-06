#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/GameObject.h"
#include "Runtime/Object/Reflection/ObjectRef.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

namespace NS::Physics
{
    class PhysicsWorld;
}

namespace NS::Object
{
    class Scene;
    struct ObjectData;
    struct ObjectRefLocation;
    struct SceneData;

    /// ObjectData 1 件から配置物を組むファクトリ。 組めない object は nullptr を返し読み飛ばされる
    using ObjectFactoryFn = std::function<std::unique_ptr<GameObject>(const ObjectData&)>;

    /// @brief SceneData から組む runtime world の所有と構築を一手に担う
    /// @details 配置物 GameObject と衝突プリミティブを SceneData から一括で組み直す
    /// runtime も editor も同じ Rebuild 経路を通り、scene は公開読み口から観測して描画するだけ
    /// 配置物 1 件の組み立ては呼出側のファクトリに委ね、GameObject の型選択や資産解決は持たない
    /// 機能別の型付き控えも持たず、欲しい component 型は ForEachComponent で問い合わせる
    /// 特定の 1 体は永続 id の解決で引く
    /// 依存: NS::Object::GameObject / ObjectData, NS::Physics::PhysicsWorld
    class World : public NS::Core::NonCopyable
    {
    public:
        World();
        ~World();

        /// data の objects から全 runtime 表現を組み直す。 既存の配置物と衝突 world は必ず先に空へ戻す
        /// factory が空の起動前 / テストでは物を組まず、 衝突 world も空のまま返る
        /// 読込直後の 1 回だけ通る経路で、 編集中は個別の Append / RemoveByObjectId で live を直接いじる
        void Rebuild(const SceneData& data,
                     Scene& scene,
                     NS::Physics::PhysicsWorld& physics,
                     const ObjectFactoryFn& factory);

        /// 配置物を逆順に破棄して所有物を空へ戻す。 scene の OnShutdown と Rebuild 冒頭が呼ぶ
        void Clear();

        /// 型 T の配置物を world の中で作って加える。 所有は world が握り、 呼出側へは生ポインタだけ返す
        /// scene attach と objectId の書き込みは呼出側が返り値へ済ませる
        template <class T, class... Args> T* Spawn(Args&&... args)
        {
            auto obj = std::make_unique<T>(std::forward<Args>(args)...);
            T* raw = obj.get();
            Append(std::move(obj));
            return raw;
        }

        /// 組み上がった配置物を 1 体 world へ加える。 scene attach と objectId の書き込みは呼出側が済ませて渡す
        /// 型が実行時にしか決まらないファクトリ経由の組み立て用。 型が分かっているなら Spawn<T> を使う
        GameObject* Append(std::unique_ptr<GameObject> obj);

        /// objectId 一致の配置物を破棄して world から外す。 居なければ何もしない
        /// 当たり箱もここで揃えるので、 組み直さずに 1 体だけ消せる
        /// 子は根として残る (親子の切り離しは GameObject の破棄が行う)
        void RemoveByObjectId(std::uint32_t objectId, NS::Physics::PhysicsWorld& physics);

        /// objectId 一致の配置物を返す。 居なければ nullptr。 選択・編集の live 索引
        /// 0 は未採番の印なので常に nullptr
        [[nodiscard]] GameObject* FindByObjectId(std::uint32_t objectId) noexcept;

        /// ObjectRef の指す配置物を返す。 未設定と該当なしは nullptr
        /// 索引は所有リストそのものなので、 破棄した相手を指す参照は必ず nullptr になる
        [[nodiscard]] GameObject* FindObject(ObjectRef ref) noexcept;

        /// 全配置物の collider を physics へ入れ直し broadphase を張り直す。 object は作り直さない
        /// 編集や配置変更の後、 全 rebuild せず当たりだけ同期するための軽い経路
        void RebuildPhysics(NS::Physics::PhysicsWorld& physics) const;

        /// priority が [firstPriority, lastPriority) の Component を昇順で回す。 同じ priority の中は配置物の並び順
        /// 帯の一部だけ回したい呼び出し側が使う。 一時オブジェクトも同じ帯に乗る
        void UpdateObjects(int firstPriority, int lastPriority = std::numeric_limits<int>::max());

        /// 全配置物の Component を priority の昇順で一括で回し、 最後に SnapshotObjects で補間用の前回値を揃える
        /// 並び順の一覧は別に持たない。 各 component がコンストラクタで指定する priority だけで並びが決まる
        void UpdateAllObjects();

        /// 全配置物の Root を Snapshot する (previous を current へ揃える)
        /// 時間停止の凍結フレームでも呼び、 補間が凍って見た目が振れないようにする
        void SnapshotObjects();

        /// 永続 object id を 1 個割り当ててカウンタを進める。 新規配置物の採番は world が一手に担う
        [[nodiscard]] std::uint32_t AllocateObjectId() noexcept { return m_nextObjectId++; }

        /// 次に割り当てる永続 id。 保存がファイルへ書き戻すために読む
        [[nodiscard]] std::uint32_t NextObjectId() const noexcept { return m_nextObjectId; }

        /// 抱えている配置物の数
        [[nodiscard]] std::size_t ObjectCount() const noexcept { return m_objects.size(); }

        /// index 番目の配置物。 範囲外は nullptr。 所有は world が握ったまま外へ出さない
        [[nodiscard]] GameObject* ObjectAt(std::size_t index) const noexcept;

        /// 範囲 for 用の反復子。 辿ると生ポインタが出るので、 所有の入れ物を外へ見せずに全配置物を回せる
        /// 添字が要る呼び出し側は ObjectCount / ObjectAt を使う
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
            friend class World;
            explicit Iterator(const std::unique_ptr<GameObject>* slot) noexcept : m_slot(slot) {}
            const std::unique_ptr<GameObject>* m_slot = nullptr;
        };

        [[nodiscard]] Iterator begin() const noexcept { return Iterator{m_objects.data()}; }
        [[nodiscard]] Iterator end() const noexcept { return Iterator{m_objects.data() + m_objects.size()}; }

        /// 全配置物から型 T の component を訪ねる。 リフレクションの is-a 照合なので抽象基底型でも派生を引ける
        /// 型付き控えの代わりの問い合わせ口で、 寿命は world が握ったまま
        template <class T, class Fn> void ForEachComponent(Fn&& fn) const
        {
            for (const auto& obj : m_objects)
                for (Component* comp : obj->Components())
                    if (auto* typed = ComponentCast<T>(comp))
                        fn(*typed);
        }

    private:
        std::vector<std::unique_ptr<GameObject>> m_objects; // 配置物の単一所有リスト
        std::uint32_t m_nextObjectId = 1;                   // 次に割り当てる永続 id。 単調増加で欠番は再利用しない
    };

    /// `targetId` を指す ObjectRef フィールドを live の全配置物からリフレクションで集める
    /// 削除前に何が参照しているかを調べる関数。 k_NoObjectId 相当の 0 は未設定の印なので空を返す
    [[nodiscard]] std::vector<ObjectRefLocation> FindReferencesTo(const World& world, std::uint32_t targetId);

} // namespace NS::Object
