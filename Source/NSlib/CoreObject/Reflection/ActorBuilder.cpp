#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace NS
{
    nlohmann::json MakeActorHeaderJson(const SceneActor* obj, uint32 id)
    {
        NS_ASSERT(Scene, obj != nullptr, "MakeActorHeaderJson: 書くアクターが無い");
        nlohmann::json object = nlohmann::json::object();
        WriteActorHeader(
            object,
            ActorHeader{.id = id, .actorFile = obj->ActorFileName(), .name = obj->Name(), .active = obj->IsAppeared()});
        return object;
    }

    void ApplyActorClassObjs(SceneActor* obj, const nlohmann::json& actorJson)
    {
        NS_ASSERT(Scene, obj != nullptr, "ApplyActorClassObjs: 値を書くアクターが無い");
        const nlohmann::json& parts = ActorJsonClassObjs(actorJson);
        std::vector<std::string_view> knownKeys;
        ForEachReflectedClassObj(obj, [&](const ClassObjInfo& field, Object* classObj) {
            knownKeys.push_back(field.key);
            const nlohmann::json::const_iterator entry = parts.find(field.key);
            if (entry != parts.end())
            {
                ApplyJsonFields(classObj, entry.value());
            }
        });
        for (nlohmann::json::const_iterator entry = parts.begin(); entry != parts.end(); ++entry)
        {
            if (std::find(knownKeys.begin(), knownKeys.end(), entry.key()) == knownKeys.end())
            {
                NS_LOG_WARN(Scene, "{} の部品 {} はクラスに無いので読み飛ばす", obj->ClassName(), entry.key());
            }
        }
    }

    namespace
    {
        // class が正。登録の無い class は素の SceneActor で組むので、データの誤りとして知らせる
        void WarnIfUnregistered(std::string_view className)
        {
            if (className.empty())
            {
                return;
            }
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(className);
            if (entry == nullptr || entry->create == nullptr)
            {
                NS_LOG_WARN(Scene, "ActorFromJson: 未登録クラス {} を素の SceneActor で組む", className);
            }
        }
    } // namespace

    std::unique_ptr<SceneActor> ActorFromJson(const nlohmann::json& actorJson, Scene& scene)
    {
        // クラスは配置物の指す .actor の中の class だけが持つ。.actor が無ければ作るクラスが決まらない
        const std::string_view actorFile = ActorJsonFile(actorJson);
        const std::string className = ActorFileLibrary::Get().ClassNameOf(actorFile);
        if (className.empty())
        {
            NS_LOG_ERROR(Scene, "配置物 {} の .actor {} が見つからないので作らない", ActorJsonId(actorJson), actorFile);
            return nullptr;
        }
        WarnIfUnregistered(className);
        // シーンの上書きは書いてよい項目だけを持つ。書いてよくない項目を捨てて作ると、保存で黙って消える。
        // 確かめるのは本物を作る前で、シーンへ入れない一時のアクターで行う。本物の部品は OnInit でシーンの仕組みを
        // 受け取るので、作ったアクターは必ず一覧に入れて OnEndPlay まで通す。
        // 未登録のクラスは一時のアクターを作れないが、.actor の値も読まれないので CreateActorOfClass が断る
        const std::unique_ptr<SceneActor> probe = CreateActorWithCodeValues(className, actorFile);
        if (probe != nullptr &&
            !MergeLayer(nlohmann::json::object(), actorJson, FieldDestination::Scene, probe.get()).has_value())
        {
            return nullptr;
        }
        const ActorInitInfo info{.placement = &actorJson, .scene = &scene, .actorFile = actorFile};
        return CreateActorOfClass(className, info);
    }

    nlohmann::json ActorToJson(const SceneActor* obj)
    {
        NS_ASSERT(Scene, obj != nullptr, "ActorToJson: 書き出すアクターが無い");
        nlohmann::json object = MakeActorHeaderJson(obj, obj->Id());
        nlohmann::json& parts = ActorJsonClassObjs(object);
        // 値は .actor と比べない。シーンに書くのは参照の欄と上書きの一覧の項目で、ほかは .actor の値を使う
        ForEachReflectedClassObj(obj, [&](const ClassObjInfo& row, const Object* classObj) {
            nlohmann::json fields = SerializeClassObjFields(classObj);
            for (nlohmann::json::iterator entry = fields.begin(); entry != fields.end();)
            {
                const FieldDesc* field = FindField(classObj->GetReflection(), entry.key());
                const bool reference = field != nullptr && field->type == FieldType::ActorRef;
                if (reference || obj->IsSceneOverride(row.key, entry.key()))
                {
                    ++entry;
                    continue;
                }
                entry = fields.erase(entry);
            }
            parts[row.key] = std::move(fields);
        });
        return object;
    }
} // namespace NS
