#pragma once

#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/ITickable.h"
#include "Runtime/Object/ObjectJson.h"
#include "Runtime/Object/Reflection/ActorRef.h"

#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
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
    //! 配置物の JSON 1 件から配置物を組むファクトリ。組めない JSON には nullptr を返し、Rebuild が読み飛ばす
    using ObjectFactoryFn = std::function<std::unique_ptr<Actor>(const nlohmann::json&)>;

    //! @brief 配置物 Actor の単一所有リスト
    //! @details シーンの JSON 文書から一括で組み直す。runtime も editor も同じ Rebuild 経路を通る
    //! 1 体ずつ入れる口は Scene の湧かす口だけに開き、undo は組み直さずにそちらを通る
    //! 配置物 1 件の組み立ては呼出側のファクトリに委ね、Actor の型選択や資産解決は持たない
    //! 特定の 1 体は永続 id の解決で引く
    //! 非 const の Actor* と Component* で、呼び出し側はそこから書き換えられる
    //! 依存: NS::Obj::Actor, ObjectJson, NS::Phys::PhysicsScene
    class ObjectList : public NS::Core::NonCopyable
    {
    public:
        ObjectList();
        ~ObjectList();

        //! scene の objects から配置物を組み直す。既存の配置物は先に空へ戻し、一時オブジェクトだけ残す
        //! 当たりの同期は含まない。呼出側が続けて SyncPhysics を呼ぶ
        //! factory が空の起動前 / テストでは物を組まない。段の更新の最中は断る
        void Rebuild(const nlohmann::json& scene, Scene& owner, const ObjectFactoryFn& factory);

        //! objectId の配置物の並びの位置。居なければ ObjectCount()
        [[nodiscard]] std::size_t IndexOfObjectId(std::uint32_t objectId) const noexcept;

        //! obj の名前を変える。他の配置物と重なれば番号を付ける。名前を書けるのはシーンの配置物を持つここだけ
        void RenameObject(Actor& obj, std::string_view name);

        //! 配置物の OnEndPlay を逆順に呼んでから所有物を空へ戻す。scene の OnShutdown と Rebuild 冒頭が呼ぶ
        //! 段の更新の最中は断る。その段で後に呼ぶ予定の配置物を解放しない
        void Clear();

        //! objectId 一致の配置物を破棄して所有リストから外す。居なければ何もしない
        //! 当たり箱もここで揃えるので、組み直さずに 1 体だけ消せる
        //! 子は根として残る。親子の切り離しは Actor の破棄が行う
        //! 0 は未採番の印なので何もしない。段の更新の最中は断る
        void RemoveByObjectId(std::uint32_t objectId);

        //! @brief 世界から外れた一時オブジェクトを破棄して所有リストから外す
        //! @details 一時オブジェクトは id を持たず、出し直す道も無い。残すと出すたびに並びが伸び続ける
        //! 段の更新の最中は断るので、段を回し終えた後に呼ぶ
        void RemoveKilledTransients();

        //! objectId 一致の配置物を返す。居なければ nullptr。選択・編集の live 索引
        //! 0 は未採番の印なので常に nullptr。索引から引くので、毎フレーム引いても全配置物を辿らない
        [[nodiscard]] Actor* FindByObjectId(std::uint32_t objectId) noexcept;

        //! ActorRef の指す配置物を返す。未設定と該当なしは nullptr
        //! 並びが変わるたびに索引を捨てるので、破棄した相手を指す参照は必ず nullptr になる
        //! 別の配置物への参照はポインタで控えず、ActorRef で持って使うたびにここで引く
        [[nodiscard]] Actor* FindObject(ActorRef ref) noexcept;

        //! 稼働中の collider に持ち主の Scene の PhysicsScene へ body を入れさせ、physics の broadphase を張り直す
        //! 稼働していない collider は body を外す。既存 body は同じ id のまま shape と姿勢を更新する
        //! physics はこの並びを持つ Scene の物。collider は渡した物でなく、自分の持ち主の Scene へ入れる
        void SyncPhysics(NS::Phys::PhysicsScene& physics);

        //! @brief 段 phase に属する物を 1 回ずつ呼ぶ
        //! @details 出ている Actor を配置の並びに呼び、続けて AddTicker で登録した物を登録順に呼ぶ
        //! 登録物は段の Actor が出した物を受けてまとめる役なので Actor の後に動く
        //! 出ているかは IsActiveInHierarchy で見る。Input の段は全 Actor の ReadInput
        //! RenderPrep の段は全 Actor の PrepareRender、他は Phase が一致する Actor の Update
        //! 途中で外れた物はそのフレームの残りでは呼ばない。入れ子で呼ぶことはできない
        void ExecutePhase(UpdatePhase phase);
        //! @brief 部品でない物を段 phase に登録する
        //! @details 登録済みなら段だけを差し替える。nullptr は無視する
        void AddTicker(ITickable* ticker, UpdatePhase phase);
        //! 登録を外す。更新の最中に外した物は、そのフレームの残りでは呼ばれない
        void RemoveTicker(ITickable* ticker) noexcept;

        //! 全配置物の補間の前の値を控える。根の Transform と Model の previous を current へ揃える
        void SnapshotObjects();

        //! 永続 object id を 1 個割り当ててカウンタを進める
        [[nodiscard]] std::uint32_t AllocateObjectId() noexcept { return m_nextObjectId++; }

        //! 次に割り当てる永続 id。保存がファイルへ書き戻すために読む
        [[nodiscard]] std::uint32_t NextObjectId() const noexcept { return m_nextObjectId; }

        //! 抱えている配置物の数
        [[nodiscard]] std::size_t ObjectCount() const noexcept { return m_objects.size(); }

        //! index 番目の配置物。範囲外は nullptr。所有は ObjectList が持ったまま外へ出さない
        [[nodiscard]] Actor* ObjectAt(std::size_t index) const noexcept;

        //! 範囲 for 用の反復子。辿ると生ポインタが出るので、所有の入れ物を外へ見せずに全配置物を回せる
        //! 添字が要る呼び出し側は ObjectCount / ObjectAt を使う
        //! std::vector<Actor*> を返す関数は作らない。毎回確保になる
        //! 配置物の生ポインタの配列もメンバに持たない。m_objects と食い違う
        class Iterator
        {
        public:
            [[nodiscard]] Actor* operator*() const noexcept { return m_slot->get(); }
            Iterator& operator++() noexcept
            {
                ++m_slot;
                return *this;
            }
            [[nodiscard]] bool operator!=(const Iterator& rhs) const noexcept { return m_slot != rhs.m_slot; }

        private:
            friend class ObjectList;
            explicit Iterator(const std::unique_ptr<Actor>* slot) noexcept : m_slot(slot) {}
            const std::unique_ptr<Actor>* m_slot = nullptr;
        };

        [[nodiscard]] Iterator begin() const noexcept { return Iterator{m_objects.data()}; }
        [[nodiscard]] Iterator end() const noexcept { return Iterator{m_objects.data() + m_objects.size()}; }

    private:
        // 1 体ずつ入れる口は開始の手前で止まる。開始まで済ませる Scene の湧かす口だけに開く
        friend class Scene;

        //! 組み上がった配置物を id を振らずに 1 体加える。実行時に湧く一時オブジェクト用で、保存も参照もされない
        //! Scene の湧かす口だけが呼ぶ。シーンへの付けと開始は呼び手の Scene が済ませる
        Actor* Append(std::unique_ptr<Actor> obj);

        //! 永続 id を振り、名前が既存と重なれば番号を付けて 1 体加える
        //! Scene の湧かす口だけが呼ぶ。シーンへの付けと開始は呼び手の Scene が済ませる
        Actor* AppendWithNewId(std::unique_ptr<Actor> obj, std::string name);

        //! 組み直さずに 1 体だけ入れる。名前は既存と重なれば番号を付ける。index が末尾より先なら末尾
        //! Scene の湧かす口だけが呼ぶ。シーンへの付けと親子の結び付けと開始は呼び手の Scene が済ませる
        Actor* InsertFromJson(std::unique_ptr<Actor> obj, const nlohmann::json& entry, std::size_t index);

        //! 並びが変わったので索引を捨てる。並びを変える箇所は必ず呼び、破棄した配置物を索引に残さない
        void MarkIndexDirty() noexcept;

        void ApplyIdentity(Actor& obj, const nlohmann::json& entry);

        std::vector<std::unique_ptr<Actor>> m_objects; // 配置物の単一所有リスト
        // 更新の予定 1 件。部品か、部品でない物のどちらか片方を持つ
        struct ScheduledTick
        {
            Actor* actor = nullptr;
            ITickable* ticker = nullptr;
        };
        // 部品でない物の登録 1 件
        struct TickerEntry
        {
            ITickable* ticker = nullptr;
            UpdatePhase phase = UpdatePhase::Triggers;
        };
        std::vector<ScheduledTick> m_scheduled; // ExecutePhase がその段で呼ぶ物を呼ぶ順に積む作業用の並び
        std::vector<TickerEntry> m_tickers;     // 部品でない物の登録。登録順
        std::unordered_map<std::uint32_t, Actor*>
            m_index;                      // 永続 id から配置物への索引。汚れていれば次に引く時に作り直す
        bool m_indexDirty = true;         // 索引が所有リストと食い違っているか
        std::uint32_t m_nextObjectId = 1; // 次に割り当てる永続 id。単調増加で欠番は再利用しない
        bool m_updating = false; // ExecutePhase の実行中か。入れ子の呼び出しの検知と、段の最中の解放を断るのに使う
    };

} // namespace NS::Obj
