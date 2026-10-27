#pragma once

#include "NSlib/Core/Assert.h"
#include "NSlib/CoreObject/ClassObjs/StateMachine.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/IGet/IGetCamera.h"
#include "NSlib/CoreObject/IGet/IGetCollision.h"
#include "NSlib/CoreObject/IGet/IGetEffect.h"
#include "NSlib/CoreObject/IGet/IGetHitSensor.h"
#include "NSlib/CoreObject/IGet/IGetRenderer.h"
#include "NSlib/CoreObject/IGet/IGetSceneSystem.h"
#include "NSlib/CoreObject/IGet/IGetState.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/SensorDeclarations.h"
#include "NSlib/CoreObject/StateMachineOf.h"

#include "NSlib/Core/Types.h"
#include <concepts>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace NS
{
    class HitSensor;
    class Model;
    class Animation;
    class Shadow;
    class Collision;
    class EffectKeeper;
    class HitReaction;
    struct HitOwnerRun;
    class ActionTimeline;
    struct ActionTimelineSelf;
    class Message;
    class Scene;
    struct ActorInitInfo;

    //! 世界に置く物の基底。Transform と部品を持ち、MapPartsActor / PropActor / PlayerActor などが派生する
    //! 所属シーンを持ち、IGet〜 の問いを居るシーンへ繋ぐ。シーンに居ない間の問いは nullptr を返す
    class SceneActor : public Object,
                       public IGetCamera,
                       public IGetSceneSystem,
                       public IGetCollision,
                       public IGetEffect,
                       public IGetHitSensor,
                       public IGetRenderer,
                       public IGetState
    {
    public:
        SceneActor() noexcept;
        virtual ~SceneActor() noexcept;
        [[nodiscard]] uint32 Id() const noexcept { return m_id; }
        [[nodiscard]] const std::string& Name() const noexcept { return m_name; }
        //! 敷いている .actor の名前。型から作ったアクターはクラスと同じ名前で、その .actor が無くても変わらない。
        //! 同じクラスの別の .actor を指す控えを当てると、その名前に替わる
        [[nodiscard]] const std::string& ActorFileName() const noexcept { return m_actorFileName; }

        //! part の部品の欄 field をシーンで上書きしているか
        //! 一覧に入るのはグローバルの項目 (NS_GLOBAL_FIELD・NS_GLOBAL_ACCESSOR) だけ。参照の欄は一覧に入れず、保存でいつも書く
        [[nodiscard]] bool IsSceneOverride(std::string_view part, std::string_view field) const;
        //! 項目をシーンの上書きの一覧に足す。保存はこの一覧の項目を書く
        void AddSceneOverride(std::string_view part, std::string_view field);
        //! 項目をシーンの上書きの一覧から外す。値は変えないので、.actor の値へ戻すのは RevertSceneOverride
        void RemoveSceneOverride(std::string_view part, std::string_view field);
        //! シーンの上書きの一覧を空にする
        void ClearSceneOverrides() noexcept { m_sceneOverrides.clear(); }
        NS_REFLECT_DECLARE()

        //! 世界に出す。更新の段・当たりへ入れ、入れてある描く物を描き始め、センサーを調べる対象にする。出ていれば何もしない
        //! Destroy の後に呼ぶのは呼び手のバグで、NS_ASSERT が止める。出荷版では何もしない
        void Appear();
        //! 世界から外す。出ていなければ何もしない
        //! 解放はしないので Appear で戻せる。描く物とセンサーは入れたまま、描かず調べない
        void Disappear() noexcept;
        //! 世界から外し、段を回し終えた後に解放させる。2 回目からは何もしない
        //! その場では解放しないので、同じ段の中では指したまま読める。やり直しの知らせは
        //! 届かず戻らないので、戻す物は Disappear を使う
        void Destroy() noexcept;
        //! このアクターがシーンに出ているか
        [[nodiscard]] bool IsAppeared() const noexcept { return m_presence == Presence::Appeared; }
        //! Destroy を呼ばれて、段の後の解放を待っている場合 true、それ以外の場合は false
        [[nodiscard]] bool IsPendingDestroy() const noexcept { return m_presence == Presence::PendingDestroy; }

        //! 入るシーン。作る時の ActorInitInfo::scene で、OnInit の前から決まっている。シーンへ入らない物は nullptr
        [[nodiscard]] Scene* OwningScene() const noexcept { return m_scene; }

        [[nodiscard]] CameraManager* GetCameraManager() const noexcept override;
        [[nodiscard]] SceneSystemHolder* GetSceneSystemHolder() const noexcept override;
        [[nodiscard]] Phys::PhysicsScene* GetPhysicsScene() const noexcept override;
        [[nodiscard]] HitSensorDirector* GetHitSensorDirector() const noexcept override;
        [[nodiscard]] SceneRenderer* GetSceneRenderer() const noexcept override;
        // 出したエフェクトを記録する部品。持たなければ nullptr
        [[nodiscard]] EffectKeeper* GetEffectKeeper() const noexcept override { return m_effectKeeper.get(); }
        [[nodiscard]] Gfx::EffectScene* GetEffectScene() const noexcept override;

        //! 個体の位置・回転・大きさを持つ部品
        [[nodiscard]] Transform& Root() noexcept;
        [[nodiscard]] const Transform& Root() const noexcept;
        [[nodiscard]] IStateMachine* GetStateMachine() noexcept override;
        [[nodiscard]] const IStateMachine* GetStateMachine() const noexcept override;

        //! 重力の向き。シーンによらず真下
        [[nodiscard]] Vector3 GravityDirection() const noexcept;
        //! 重力の向きへ加速する。strength は m/s²、dt は秒
        //! 非有限値または dt が 0 以下なら変更しない
        void AddGravity(Vector3& velocity, float strength, float dt) const noexcept;

        [[nodiscard]] Model* ModelClassObj() noexcept { return m_model.get(); }
        [[nodiscard]] const Model* ModelClassObj() const noexcept { return m_model.get(); }
        [[nodiscard]] Animation* AnimationClassObj() noexcept { return m_animation.get(); }
        [[nodiscard]] const Animation* AnimationClassObj() const noexcept { return m_animation.get(); }
        [[nodiscard]] Shadow* ShadowClassObj() noexcept { return m_shadow.get(); }
        [[nodiscard]] const Shadow* ShadowClassObj() const noexcept { return m_shadow.get(); }
        [[nodiscard]] Collision* CollisionClassObj() noexcept { return m_collision.get(); }
        [[nodiscard]] const Collision* CollisionClassObj() const noexcept { return m_collision.get(); }
        //! その物の体の広がりのセンサー。持たなければ nullptr
        [[nodiscard]] HitSensor* BodySensorClassObj() noexcept { return m_bodySensor.get(); }
        [[nodiscard]] const HitSensor* BodySensorClassObj() const noexcept { return m_bodySensor.get(); }
        //! 体と別の広がりを持つ 2 つ目のセンサー。持たなければ nullptr
        //! 調べ役は体のセンサーと区別せずに扱う。今はこの枠を作る種類が無い
        [[nodiscard]] HitSensor* AttackSensorClassObj() noexcept { return m_attackSensor.get(); }
        [[nodiscard]] const HitSensor* AttackSensorClassObj() const noexcept { return m_attackSensor.get(); }
        [[nodiscard]] HitReaction* HitReactionClassObj() noexcept { return m_hitReaction.get(); }
        [[nodiscard]] const HitReaction* HitReactionClassObj() const noexcept { return m_hitReaction.get(); }
        [[nodiscard]] ActionTimeline* ActionTimelineClassObj() noexcept { return m_actionTimeline.get(); }
        [[nodiscard]] const ActionTimeline* ActionTimelineClassObj() const noexcept { return m_actionTimeline.get(); }
        //! 自分の ActionTimeline の Start と Advance に渡す、自分と自分の部品
        [[nodiscard]] ActionTimelineSelf ActionTimelineSelfOf() noexcept;

        //! 当たりの止め (ヒットストップ) の残りを、frames が残りより長い時だけ延ばす
        //! 止めの間は Update が状態機械の 1 歩と BodyStep を飛ばす。呼んだフレームの残りの段から効き、
        //! そのフレームも数える。束を始めたアクターが、HitReaction::Start の返した束の止めの長さで呼ぶ
        void RequestHitStop(int frames) noexcept;
        //! 当たりの止めの残りを frames に書き換える。短くもする
        //! 束と違う長さで止める当たり (貫通) が、束を始めた後に呼ぶ。事前条件: frames が 0 以上
        void OverrideHitStop(int frames) noexcept;
        //! 自分の HitReaction で名前の束を始め、束の止めの長さで RequestHitStop を呼ぶ
        //! 形・揺れ・震えは自分の Model に書く。束と違う長さで止める当たりは HitReaction::Start を直に呼ぶ
        //! @param[in] self 自分のセンサー。事前条件: nullptr でない
        //! @param[in] other 相手のセンサー。事前条件: nullptr でない
        //! @return 始めた場合 true。HitReaction が無い・その名前の束が無い場合 false
        bool StartHitReaction(std::string_view name,
                              const Vector3& position,
                              const HitSensor* self,
                              const HitSensor* other);
        //! StartHitReaction を呼び、この束が足した持ち主へ渡す項目の並びを返す
        [[nodiscard]] std::span<const HitOwnerRun> StartHitReactionRuns(std::string_view name,
                                                                        const Vector3& position,
                                                                        const HitSensor* self,
                                                                        const HitSensor* other);
        //! 当たりの止めの残りのフレーム数。止めていなければ 0
        [[nodiscard]] int HitStopFramesRemaining() const noexcept { return m_hitStopFrames; }
        //! 当たりの止めの間なら true
        [[nodiscard]] bool IsHitStopping() const noexcept { return m_hitStopFrames > 0; }

        //! 1 固定フレームぶん進める。順は基盤が決め、派生は変えられない
        //! シーンは出ているアクターを全部、物理の前の 1 か所で一覧の順に 1 回ずつ呼ぶ
        //! (止めている間は止め始めたアクターだけ)。中の順は次のとおりで、部品は基盤が挟む。
        //! - エフェクトの記録と、束が持ち主へ渡す項目を 1 フレーム進める
        //! - DecideStep を毎回 1 回
        //! - 止めの間でも SkipStateStep を呼んだフレームでもなければ、状態機械を 1 歩・動作のタイムライン・
        //!   AfterStateStep を 1 回ずつ
        //! - 止めの間でなければ BodyStep を 1 回 (状態機械の中で止めが始まったフレームは呼ばない)
        //! - 出ていれば当たりを体の姿へそろえ、Model の揺れと震えを進め、VisualStep を毎回 1 回
        //! - 止めの残りを 1 減らす
        void Update();
        virtual void PrepareRender();

        //! 全アクターの初期化後に一度呼ぶ。他のアクターを必要とする初期化を行う
        virtual void InitAfterPlacement() {}

        //! シーンに保存する項目の ActorRef から、指す相手を取得し直して部品へ渡す
        //! OnStart と、エディタがアクターの並びか ActorRef の欄を書き換えた後に呼ぶ。上書きは何回呼ばれてもよい形で書く
        virtual void ResolveActorRefs() {}

        //! 自分のセンサー self と、異なる持ち主のセンサー other が重なり始めた固定フレームに 1 回呼ばれる
        //! 応じるかは other の種類を見て決め、要るメッセージを送る。重なった組の両方の持ち主へ、
        //! 自分と相手を入れ替えて届く
        virtual void OnSensorEnter(HitSensor* self, HitSensor* other)
        {
            (void)self;
            (void)other;
        }
        //! 重なり始めた次の固定フレームから、重なっている間ずっと毎回呼ばれる
        virtual void OnSensorStay(HitSensor* self, HitSensor* other)
        {
            (void)self;
            (void)other;
        }
        //! OnSensorEnter を受けた組の重なりが終わった時に 1 回呼ばれる
        //! 形が離れた時と、どちらかのセンサーが調べる対象から外れた時 (Invalidate・持ち主の Disappear) に届く。
        //! 相手のセンサーが一覧から外れた時 (相手のアクターが終わった時) は、次の固定フレームに other を nullptr で届く。
        //! 自分のセンサーが一覧から外れた時 (自分のアクターが終わる時) は、SceneActor::OnEndPlay の中でその場で届く
        virtual void OnSensorExit(HitSensor* self, HitSensor* other)
        {
            (void)self;
            (void)other;
        }

        virtual void OnStart();
        //! Disappear で世界から外し、基底の部品 (Model・Shadow・当たり・センサー) をシーンの仕組みから外す
        //! 消えたままでも外し、当たりの body は壊す。入れていない部品は何もしないので、2 回呼ばれてもよい
        virtual void OnEndPlay();

        //! 部品の素材の欄を引き直し、続けて OnAssetsLoaded を呼ぶ
        //! 作る時は Init が欄を書いた直後に呼ぶ。エディタやシーンが欄を書き換えた後も、これを呼んで読み直す
        void ReloadAssets();

    protected:
        //! 知らせを受け取る。応じた場合 true、知らない知らせと応じなかった知らせは false
        //! 呼べるのは SendMsg と SendMsgToActor だけ。sender と receiver は知らせを運んだセンサーで、
        //! センサーを介さない知らせでは nullptr
        virtual bool ReceiveMsg(const Message& msg, HitSensor* sender, HitSensor* receiver)
        {
            (void)msg;
            (void)sender;
            (void)receiver;
            return false;
        }

        // 基底の部品の型を替える派生は、基底の OnInit を呼ばずに自分で作る
        //! 入るシーンはもう決まっているので、部品へシーンの仕組みを渡せる。id と名前はまだ付いていない
        virtual void OnInit() {}
        //! 素材の欄を引いた直後に呼ぶ。読んだ物から作る物を、部品を名指しで呼んで作る
        //! 作る時と ReloadAssets の両方で呼ぶので、何回呼ばれても同じ姿になるように書く。上書きする派生は基底も呼ぶ
        virtual void OnAssetsLoaded();

        //! 部品を member に作る。未登録・作成済みのメンバーや使用済みの保存キーは変更せず nullptr を返す
        //! センサーは作れない。NS_SENSOR か NS_FOLLOW_SENSOR で宣言し、基底が OnInit の前に作る
        template <class TBase, class T = TBase, class... TArgs>
            requires std::derived_from<T, TBase> && std::derived_from<T, Object>
        T* CreateClassObj(std::unique_ptr<TBase>& member, TArgs&&... args)
        {
            static_assert(!std::is_base_of_v<HitSensor, T>, "センサーは NS_SENSOR か NS_FOLLOW_SENSOR で宣言する");
            return EmplaceClassObj<TBase, T>(member, std::forward<TArgs>(args)...);
        }

        virtual void OnAppear();
        virtual void OnDisappear() noexcept;
        // 派生が上書きする更新の関数は次の 4 つだけ。呼ぶのは Update だけで、呼ぶ順と回数は Update の説明のとおり。
        // 状態機械を進めるのは基盤だけで、派生は進めない

        //! 世界と入力を見て、このフレームの値を決め、メッセージを送る。Update ごとに必ず 1 回、最初に呼ばれる
        //! 状態機械を進めないフレームは、ここで SkipStateStep を呼ぶ
        virtual void DecideStep() {}
        //! 状態機械を進めたフレームにだけ、状態機械の 1 歩と動作のタイムラインの後に 1 回呼ばれる
        //! 止めの間と SkipStateStep を呼んだフレームは呼ばれない
        virtual void AfterStateStep() {}
        //! 体を動かす。止めの間でなければ 1 回呼ばれる
        virtual void BodyStep() {}
        //! 動かした後の姿勢から見た目・演出・寿命を更新する。Update ごとに必ず 1 回、最後に呼ばれる
        virtual void VisualStep() {}
        //! このフレームだけ、状態機械の 1 歩と動作のタイムラインと AfterStateStep を飛ばす
        //! DecideStep の中で呼ぶ。Update の初めに戻るので、次のフレームへ持ち越さない
        void SkipStateStep() noexcept { m_skipStateStep = true; }
        //! 自分を持ち主に、TStates の先頭へ入る状態機械を一度だけ構築する
        //! 先頭の OnEnter は組んだ時でなく、最初に状態機械を進める Update の初めに走る。outMachine は組む前に設定する
        //! 機械は基底の部品 StateMachine が持ち、型付きポインタは非所有。既に構築済みなら変更せず false
        template <typename TOwner, typename... TStates>
        bool BuildStateMachine(TOwner* owner, StateMachineOf<TOwner>*& outMachine)
        {
            static_assert(std::is_base_of_v<SceneActor, TOwner>, "持ち主は SceneActor の派生");
            NS_ASSERT(Scene,
                      static_cast<const SceneActor*>(owner) == this,
                      "SceneActor::BuildStateMachine: 持ち主に自分以外の SceneActor を渡している");
            StateMachine* part = CreateClassObj(m_stateMachine);
            if (part == nullptr)
            {
                return false;
            }
            std::unique_ptr<StateMachineOf<TOwner>> machine = std::make_unique<StateMachineOf<TOwner>>();
            StateMachineOf<TOwner>* built = machine.get();
            part->Hold(std::move(machine));
            outMachine = built;
            built->template Build<TStates...>(owner);
            return true;
        }

    private:
        friend class ActorList;
        friend class SensorDeclarations;
        friend bool SendMsg(const Message& msg, HitSensor* receiver, HitSensor* sender);
        friend bool SendMsgToActor(const Message& msg, SceneActor* receiver);
        template <class T, class... TArgs> friend std::unique_ptr<T> CreateActor(TArgs&&... args);
        template <class T, class... TArgs>
        friend std::unique_ptr<T> CreateActorWithInfo(const ActorInitInfo& info, TArgs&&... args);
        friend std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className);
        friend std::unique_ptr<SceneActor> CreateActorOfClass(std::string_view className, const ActorInitInfo& info);
        friend std::unique_ptr<SceneActor> CreateActorWithCodeValues(std::string_view className,
                                                                     std::string_view actorFile);
        friend float GetFrameRate(const SceneActor* actor) noexcept;
        friend bool SetFrameRate(SceneActor* actor, float rate) noexcept;
        //! アクターを作る順。宣言したセンサー → 部品を作る (OnInit) → .actor の全部の値 → シーンの上書き → 素材
        //! 呼ぶのは作る関数だけで、作った直後に 1 回。private にして 2 回目を起こさない。
        //! info が nullptr のアクターも、シーンの上書きを飛ばすだけで同じ順を通る。
        //! 断らない。シーンの上書きが書いてよい項目だけを持つかは、作る前に ActorFromJson が確かめる
        void Init(const ActorInitInfo* info);
        //! 宣言したセンサーと部品を作るだけで、.actor も素材も読まない。項目の値はコンストラクタと OnInit が書いた物。
        //! actorFile は .actor の名前として持つだけで、空ならクラスと同じ名前
        void InitWithCodeValues(std::string_view actorFile);
        //! 補間の前の値として、根の Transform と Model の今の値を控える
        //! ActorList::SnapshotActors が固定ステップの頭で全ての SceneActor
        //! に呼ぶ。世界を止めている間も呼ばれる
        void SnapshotForInterpolation() noexcept;
        void SetId(uint32 id) noexcept { m_id = id; }
        void SetName(std::string name) noexcept { m_name = std::move(name); }
        // member を指すアクターの表の行。表に無ければ nullptr
        [[nodiscard]] const ClassObjInfo* FindReflectedMember(const void* member) const;
        // 別のメンバーが同じ保存キーで作成済みか。同じキーの部品が 2 つあると保存で片方が消える
        [[nodiscard]] bool IsClassObjKeyCreated(std::string_view key) const;
        void ReportInvalidClassObj(const ClassObjInfo* field) const;
        //! クラスが並べたセンサー。上書きは NS_SENSORS_BEGIN と NS_SENSORS_END が書く
        virtual void DeclareSensors(SensorDeclarations& sensors) { (void)sensors; }
        // 宣言したセンサーを作り、一覧に入れて種類と形を付ける。Init と InitWithCodeValues が OnInit の前に 1 回呼ぶ
        void CreateDeclaredSensors();
        void CreateShapeSensor(std::unique_ptr<HitSensor>& member, uint8 kind, HitSensorShape shape);
        void CreateFollowSensor(std::unique_ptr<HitSensor>& member,
                                uint8 kind,
                                std::function<SensorVolume(const Matrix& rootWorld)> volume);
        // 未登録・作成済みのメンバーや使用済みの保存キーは変更せず nullptr を返す
        template <class TBase, class T = TBase, class... TArgs>
            requires std::derived_from<T, TBase> && std::derived_from<T, Object>
        T* EmplaceClassObj(std::unique_ptr<TBase>& member, TArgs&&... args)
        {
            const ClassObjInfo* field = FindReflectedMember(&member);
            if (field == nullptr || member != nullptr || IsClassObjKeyCreated(field->key))
            {
                ReportInvalidClassObj(field);
                return nullptr;
            }
            std::unique_ptr<T> classObj = std::make_unique<T>(std::forward<TArgs>(args)...);
            T* raw = classObj.get();
            member = std::move(classObj);
            return raw;
        }
        // PendingDestroy からはどこへも移らない
        enum class Presence : uint8
        {
            Disappeared,
            Appeared,
            PendingDestroy,
        };
        uint32 m_id = 0;
        std::string m_name;
        std::string m_actorFileName; // 書くのは Init と、一覧へ入れる時と控えを当てる時の ActorList::ApplyIdentity
        struct SceneOverrideKey
        {
            std::string classObjKey;
            std::string fieldName;

            [[nodiscard]] bool operator<(const SceneOverrideKey& other) const noexcept
            {
                if (classObjKey != other.classObjKey)
                {
                    return classObjKey < other.classObjKey;
                }
                return fieldName < other.fieldName;
            }
        };
        std::set<SceneOverrideKey> m_sceneOverrides; // シーンで上書きしている (部品の鍵, 欄の名前)
        Scene* m_scene = nullptr;    // 入るシーン。書くのは Init だけ
        Presence m_presence = Presence::Disappeared;
        std::unique_ptr<Transform> m_transform;

    protected:
        std::unique_ptr<Model> m_model;
        std::unique_ptr<Animation> m_animation;
        std::unique_ptr<Shadow> m_shadow;
        std::unique_ptr<Collision> m_collision;
        std::unique_ptr<HitSensor> m_bodySensor;
        std::unique_ptr<HitSensor> m_attackSensor;
        std::unique_ptr<HitReaction> m_hitReaction;
        std::unique_ptr<EffectKeeper> m_effectKeeper;
        std::unique_ptr<ActionTimeline> m_actionTimeline;
        // 状態が借りる部品より先に壊すので、部品の最後に置く。組むのは BuildStateMachine だけ
        std::unique_ptr<StateMachine> m_stateMachine;

    private:
        float m_frameRate = 1.0f;     // 体の倍率。読み書きは GetFrameRate・SetFrameRate だけ
        int m_hitStopFrames = 0;      // 当たりの止めの残り。書くのは RequestHitStop・OverrideHitStop・Update だけ
        bool m_skipStateStep = false; // このフレームは状態機械を進めないか。書くのは SkipStateStep と Update の初めだけ
    };

    //! 体の倍率を返す。1 が等速
    //! 体の動きと見た目 (状態機械・自機の体・アニメ) だけに掛かる。シーン全体の止めと遅くは含めない
    [[nodiscard]] float GetFrameRate(const SceneActor* actor) noexcept;
    //! 体の倍率を書く
    //! @return 書いた場合 true、非有限か 0 以下で断って前の値を残した場合は false
    bool SetFrameRate(SceneActor* actor, float rate) noexcept;
    //! 体の 1 フレームの秒。固定フレームの秒に体の倍率を掛けた値
    [[nodiscard]] float GetFrameDelta(const SceneActor* actor) noexcept;

    //! T を作り、部品・クラスと同じ名前の .actor・素材まで済ませて返す
    //! Scene の中に作る時は Scene::SpawnActor を使う (中でこの関数を呼ぶ)。
    //! Scene に入れずに部品を触る試しは、この関数で作る
    template <class T, class... TArgs> [[nodiscard]] std::unique_ptr<T> CreateActor(TArgs&&... args)
    {
        static_assert(std::derived_from<T, SceneActor>, "T は SceneActor の派生でなければならない");
        std::unique_ptr<T> actor = std::make_unique<T>(std::forward<TArgs>(args)...);
        SceneActor& base = *actor;
        base.Init(nullptr);
        return actor;
    }

    //! T を作り、info で部品・.actor・配置の上書き・素材まで済ませて返す
    //! シーンへ入れる口 (Scene::SpawnActor) が呼ぶ。シーンの一覧に入る前に素材を読み終える
    template <class T, class... TArgs>
    [[nodiscard]] std::unique_ptr<T> CreateActorWithInfo(const ActorInitInfo& info, TArgs&&... args)
    {
        static_assert(std::derived_from<T, SceneActor>, "T は SceneActor の派生でなければならない");
        std::unique_ptr<T> actor = std::make_unique<T>(std::forward<TArgs>(args)...);
        SceneActor& base = *actor;
        base.Init(&info);
        return actor;
    }

} // namespace NS
