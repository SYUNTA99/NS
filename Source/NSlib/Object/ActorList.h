#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/ObjectJson.h"
#include "NSlib/Object/Reflection/ActorRef.h"
#include "NSlib/Object/UpdatePhase.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NS::Phys
{
    class PhysicsScene;
} // namespace NS::Phys

namespace NS::Obj
{
    class Scene;
    //! 配置物の JSON 1 件から配置物を組むファクトリ。組めない JSON には nullptr を返し、RebuildFromJson が読み飛ばす
    using ObjectFactoryFn = std::function<std::unique_ptr<Actor>(const nlohmann::json&)>;

    //! 配置物を所有し、永続 id から参照を解決する一覧。個体の構築は渡された生成関数が行う
    class ActorList : public NS::NonCopyable
    {
    public:
        ActorList();
        ~ActorList();

        //! scene の objects から配置物を組み直す。既存の配置物は一時オブジェクトも含めて先に空へ戻す
        //! 当たりの同期は含まない。呼出側が続けて SyncPhysics を呼ぶ
        //! factory が空の起動前 / テストでは物を組まない。段の更新の最中は断る
        void RebuildFromJson(const nlohmann::json& scene, Scene& owner, const ObjectFactoryFn& factory);

        //! objectId の配置物の並びの位置。居なければ ObjectCount()
        [[nodiscard]] std::size_t IndexOfObjectId(std::uint32_t objectId) const noexcept;

        //! obj の名前を変える。他の配置物と重なれば番号を付ける。名前を書けるのはシーンの配置物を持つここだけ
        void RenameObject(Actor& obj, std::string_view name);

        //! 配置物の OnEndPlay を逆順に呼んでから所有物を空へ戻す。scene の OnShutdown と RebuildFromJson の冒頭が呼ぶ
        //! 段の更新の最中は断る。その段で後に呼ぶ予定の配置物を解放しない
        void Clear();

        //! objectId 一致の配置物を破棄して所有リストから外す。居なければ何もしない
        //! 当たり箱もここで揃えるので、組み直さずに 1 体だけ消せる
        //! 子は根として残る。親子の切り離しは Actor の破棄が行う
        //! 0 は未採番の印なので何もしない。段の更新の最中は断る
        void RemoveByObjectId(std::uint32_t objectId);

        //! @brief Actor::Destroy を呼ばれた配置物を破棄して所有リストから外す
        //! @details 段の更新の最中は断るので、段を回し終えた後に呼ぶ
        void RemoveDestroyed();

        //! objectId 一致の配置物を返す。居なければ nullptr。選択・編集の実体索引
        //! 0 は未採番の印なので常に nullptr。索引から引くので、毎フレーム引いても全配置物を辿らない
        [[nodiscard]] Actor* FindByObjectId(std::uint32_t objectId) noexcept;

        //! ActorRef の指す配置物を返す。未設定と該当なしは nullptr
        //! 並びが変わるたびに索引を捨てるので、破棄した相手を指す参照は必ず nullptr になる
        //! 別の配置物への参照はポインタで控えず、ActorRef で持って使うたびにここで引く
        [[nodiscard]] Actor* FindObject(ActorRef ref) noexcept;

        //! 稼働中の collision に持ち主の Scene の PhysicsScene へ body を入れさせ、physics の broadphase を張り直す
        //! 稼働していない collision は body を外す。既存 body は同じ id のまま shape と姿勢を更新する
        //! physics はこの並びを持つ Scene の物。collision は渡した物でなく、自分の持ち主の Scene へ入れる
        void SyncPhysics(NS::Phys::PhysicsScene& physics);

        //! 全配置物の補間の前の値を控える。根の Transform と Model の previous を current へ揃える
        void SnapshotObjects();

        //! 永続 object id を 1 個割り当ててカウンタを進める
        [[nodiscard]] std::uint32_t AllocateObjectId() noexcept { return m_nextObjectId++; }

        //! 次に割り当てる永続 id。保存がファイルへ書き戻すために読む
        [[nodiscard]] std::uint32_t NextObjectId() const noexcept { return m_nextObjectId; }

        //! 抱えている配置物の数
        [[nodiscard]] std::size_t ObjectCount() const noexcept { return m_objects.size(); }

        //! index 番目の配置物。範囲外は nullptr。所有は ActorList が持ったまま外へ出さない
        [[nodiscard]] Actor* ObjectAt(std::size_t index) const noexcept;

        //! 所有を渡さず、配置物を生ポインタで辿る反復子
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
            friend class ActorList;
            explicit Iterator(const std::unique_ptr<Actor>* slot) noexcept : m_slot(slot) {}
            const std::unique_ptr<Actor>* m_slot = nullptr;
        };

        [[nodiscard]] Iterator begin() const noexcept { return Iterator{m_objects.data()}; }
        [[nodiscard]] Iterator end() const noexcept { return Iterator{m_objects.data() + m_objects.size()}; }

    private:
        // 1 体ずつ入れる口は開始の手前で止まる。開始まで済ませる Scene の湧かす口だけに開く
        // 段を回す口も Scene だけに開く。同じ段の管理役を Scene が続けて回す
        friend class Scene;

        //! 段の更新を始める。EndPhase まで解放の口を断る。再入は禁止
        void BeginPhase() noexcept;
        //! 段の更新を終える。Scene が同じ段の管理役を回し終えてから呼ぶ
        void EndPhase() noexcept;
        //! 有効な Actor を配置順に更新する。BeginPhase と EndPhase の間に呼ぶ
        //! Input は ReadInput、RenderPrep は PrepareRender、他は対応する段の Update
        //! 途中で無効になった個体は呼ばない
        void ExecutePhase(UpdatePhase phase);

        //! 組み上がった配置物を id を振らずに 1 体加える
        //! Scene の湧かす口だけが呼ぶ。Init はここで済ませ、シーンへの付けと開始は呼び手が済ませる
        Actor* Append(std::unique_ptr<Actor> obj);

        //! 永続 id を振り、名前が既存と重なれば番号を付けて 1 体加える
        //! Scene の湧かす口だけが呼ぶ。Init はここで済ませ、シーンへの付けと開始は呼び手が済ませる
        Actor* AppendWithNewId(std::unique_ptr<Actor> obj, std::string name);

        //! 組み直さずに 1 体だけ入れる。名前は既存と重なれば番号を付ける。index が末尾より先なら末尾
        //! Scene の湧かす口だけが呼ぶ。Init はここで済ませ、シーンへの付けと親子と開始は呼び手が済ませる
        Actor* InsertFromJson(std::unique_ptr<Actor> obj, const nlohmann::json& entry, std::size_t index);

        //! 並びが変わったので索引を捨てる。並びを変える箇所は必ず呼び、破棄した配置物を索引に残さない
        void MarkIndexDirty() noexcept;

        void ApplyIdentity(Actor& obj, const nlohmann::json& entry);

        std::vector<std::unique_ptr<Actor>> m_objects; // 配置物の単一所有リスト
        std::vector<Actor*> m_scheduled;               // ExecutePhase がその段で呼ぶ Actor を呼ぶ順に積む作業用の並び
        std::unordered_map<std::uint32_t, Actor*>
            m_index;                      // 永続 id から配置物への索引。汚れていれば次に引く時に作り直す
        bool m_indexDirty = true;         // 索引が所有リストと食い違っているか
        std::uint32_t m_nextObjectId = 1; // 次に割り当てる永続 id。単調増加で欠番は再利用しない
        bool m_updating = false; // BeginPhase から EndPhase の間か。入れ子の検知と、段の最中の解放を断るのに使う
    };

} // namespace NS::Obj
