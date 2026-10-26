#pragma once

#include "NSlib/Core/NonCopyable.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/ActorRef.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/CoreObject/UpdatePhase.h"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace NS
{
    class Scene;
    //! アクターの JSON 1 件からアクターを組むファクトリ。組めない JSON には nullptr を返し、RebuildFromJson が読み飛ばす
    using ActorFactoryFn = std::function<std::unique_ptr<SceneActor>(const nlohmann::json&)>;

    //! 解放せずに一覧から外したアクター。持つ側は捨てる前に OnEndPlay を呼ぶ
    struct DetachedActor
    {
        std::unique_ptr<SceneActor> actor; // 居なかった時と、外すのを断った時は空
        std::size_t index = 0;             // 外す前の並びの位置
        bool wasAppeared = false;          // 外す前に出ていたか。戻す時に出す物を決める
    };

    //! アクターを所有し、永続 id から参照を解決する一覧。個体の構築は渡された生成関数が行う
    class ActorList : public NonCopyable
    {
    public:
        ActorList();
        ~ActorList();

        //! objectId のアクターの並びの位置。居なければ ActorCount()
        [[nodiscard]] std::size_t IndexOfActorId(uint32 objectId) const noexcept;

        //! obj の名前を変える。他のアクターと重なれば番号を付ける。名前を書けるのはシーンのアクターを持つここだけ
        void RenameActor(SceneActor* obj, std::string_view name);

        //! アクターの OnEndPlay を逆順に呼んでから所有物を空へ戻す。scene の OnShutdown と RebuildFromJson の冒頭が呼ぶ
        //! 段の更新の最中は断る。その段で後に呼ぶ予定のアクターを解放しない
        void Clear();

        //! objectId 一致のアクターを破棄して所有リストから外す。居なければ何もしない
        //! 当たり箱もここで揃えるので、組み直さずに 1 体だけ消せる
        //! 0 は未採番を表す値なので何もしない。段の更新の最中は断る
        void RemoveByActorId(uint32 objectId);

        //! @brief SceneActor::Destroy を呼ばれたアクターを破棄して所有リストから外す
        //! @details 段の更新の最中は断るので、段を回し終えた後に呼ぶ
        void RemoveDestroyed();

        //! objectId 一致のアクターを返す。居なければ nullptr。選択・編集の実体索引
        //! 0 は未採番を表す値なので常に nullptr。索引から引くので、毎フレーム引いても全アクターを辿らない
        [[nodiscard]] SceneActor* FindByActorId(uint32 objectId) noexcept;

        //! ActorRef の指すアクターを返す。未設定と該当なしは nullptr
        //! 並びが変わるたびに索引を捨てるので、破棄した相手を指す参照は必ず nullptr になる
        //! アクターは保存する項目の ActorRef から ResolveActorRefs で相手を取得して持つ。一覧の出入りのたびに取得し直させる
        [[nodiscard]] SceneActor* FindActor(ActorRef ref) noexcept;

        //! 出ている Actor の collision を今の姿と形へ揃える。変わっていない collision は何もしない
        //! 世界が止まっていて Actor の Update が回らない歩に、編集で動いた当たりを揃える
        void SyncCollisions();

        //! 全アクターの補間の前の値を控える。根の Transform と Model の previous を current へ揃える
        void SnapshotActors();

        //! 永続 object id を 1 個割り当ててカウンタを進める
        [[nodiscard]] uint32 AllocateActorId() noexcept { return m_nextActorId++; }

        //! 次に割り当てる永続 id。保存がファイルへ書き戻すために読む
        [[nodiscard]] uint32 NextActorId() const noexcept { return m_nextActorId; }

        //! 抱えているアクターの数
        [[nodiscard]] std::size_t ActorCount() const noexcept { return m_actors.size(); }

        //! index 番目のアクター。範囲外は nullptr。所有は ActorList が持ったまま外へ出さない
        [[nodiscard]] SceneActor* ActorAt(std::size_t index) const noexcept;

        //! 所有を渡さず、アクターを生ポインタで辿る反復子
        class Iterator
        {
        public:
            [[nodiscard]] SceneActor* operator*() const noexcept { return m_slot->get(); }
            Iterator& operator++() noexcept
            {
                ++m_slot;
                return *this;
            }
            [[nodiscard]] bool operator!=(const Iterator& rhs) const noexcept { return m_slot != rhs.m_slot; }

        private:
            friend class ActorList;
            explicit Iterator(const std::unique_ptr<SceneActor>* slot) noexcept : m_slot(slot) {}
            const std::unique_ptr<SceneActor>* m_slot = nullptr;
        };

        [[nodiscard]] Iterator begin() const noexcept { return Iterator{m_actors.data()}; }
        [[nodiscard]] Iterator end() const noexcept { return Iterator{m_actors.data() + m_actors.size()}; }

    private:
        // 入れる口は開始の手前で止まるので、開始まで済ませる Scene だけに開く
        // 段を回す口も Scene だけに開く。同じ段の管理役を Scene が続けて回す
        friend class Scene;

        //! scene の objects からアクターを組み直す。既存のアクターは先に空へ戻す
        //! 出ているアクターの当たりは開始の時に入る。broadphase の張り直しは呼出側がする
        //! factory は CreateActorOfClass で作った個体を返す。factory が空なら物を組まない。
        //! 段の更新の最中は断る
        void RebuildFromJson(const nlohmann::json& scene, Scene& owner, const ActorFactoryFn& factory);

        //! 段の更新を始める。EndPhase まで解放の口を断る。再入は禁止
        void BeginPhase() noexcept;
        //! 段の更新を終える。Scene が同じ段の管理役を回し終えてから呼ぶ
        void EndPhase() noexcept;
        //! 有効な SceneActor を配置順に更新する。BeginPhase と EndPhase の間に呼ぶ
        //! RenderPrep は PrepareRender、他は対応する段の Update
        //! 途中で無効になった個体は呼ばない
        void ExecutePhase(UpdatePhase phase);

        //! 永続 id を振り、名前が既存と重なれば番号を付けて 1 体加える
        //! シーンへの付けと開始は呼び手が済ませる
        SceneActor* AppendWithNewId(std::unique_ptr<SceneActor> obj, std::string name);

        //! 組み直さずに 1 体だけ入れる。名前は既存と重なれば番号を付ける。index が末尾より先なら末尾
        //! シーンへの接続と開始は呼び手が済ませる
        SceneActor* InsertFromJson(std::unique_ptr<SceneActor> obj, const nlohmann::json& entry, std::size_t index);

        //! objectId のアクターを Disappear してから、解放せずに一覧から外す。OnEndPlay は呼ばない
        //! 居なければ空を返す。段の更新の最中は断る
        [[nodiscard]] DetachedActor DetachByActorId(uint32 objectId);

        //! DetachByActorId で外したアクターを外す前の位置へ戻し、出ていた物は Appear する。OnStart は呼び直さない
        SceneActor* Reattach(DetachedActor detached);

        //! 並びが変わったので索引を捨てる。並びを変える箇所は必ず呼び、破棄したアクターを索引に残さない
        void MarkIndexDirty() noexcept;
        //! 一覧に入れた後と外した後に呼び、全部のアクターに ActorRef の指す相手を取得し直させる
        //! 外した相手を指したまま次のフレームへ持ち越さない
        void ResolveActorRefs();

        //! 頭の欄の id・.actor の名前・名前・出現を obj へ写す。名前は他のアクターと重なれば番号を付ける
        void ApplyIdentity(SceneActor* obj, const ActorHeader& header);

        std::vector<std::unique_ptr<SceneActor>> m_actors; // アクターの単一所有リスト
        std::vector<SceneActor*> m_scheduled; // ExecutePhase がその段で呼ぶ SceneActor を呼ぶ順に積む作業用の並び
        std::unordered_map<uint32, SceneActor*>
            m_index;                   // 永続 id からアクターへの索引。汚れていれば次に引く時に作り直す
        bool m_indexDirty = true;      // 索引が所有リストと食い違っているか
        uint32 m_nextActorId = 1; // 次に割り当てる永続 id。単調増加で欠番は再利用しない
        bool m_updating = false;       // BeginPhase から EndPhase の間か。入れ子の検知と、段の最中の解放を断るのに使う
    };

} // namespace NS
