#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ActorBuilder.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"

#include <cstddef>
#include <format>
#include <optional>
#include <set>
#include <span>
#include <utility>

namespace NS
{
    namespace
    {
        // .actor 1 つの上限。部品の値だけなので、シーンのファイルよりずっと小さい
        constexpr std::size_t k_MaxActorFileBytes = 1024u * 1024u;

        // .actor に持たせない参照の欄を落とした写し。参照はシーンの中の相手を指すので配置物の物
        // 頭の鍵は全部残し、Save で丸ごと書く
        [[nodiscard]] nlohmann::json Sanitize(const nlohmann::json& actorFile)
        {
            nlohmann::json out = actorFile;
            nlohmann::json& parts = ActorJsonClassObjs(out);
            parts = nlohmann::json::object();
            const nlohmann::json& source = ActorJsonClassObjs(actorFile);
            for (nlohmann::json::const_iterator entry = source.begin(); entry != source.end(); ++entry)
            {
                if (!entry.value().is_object())
                {
                    continue;
                }
                nlohmann::json& fields = parts[entry.key()];
                fields = nlohmann::json::object();
                for (nlohmann::json::const_iterator field = entry.value().begin(); field != entry.value().end();
                     ++field)
                {
                    if (!IsRefValue(field.value()))
                    {
                        fields[field.key()] = field.value();
                    }
                }
            }
            return out;
        }

        // クラス名を書く .actor の頭の鍵。配置物は .actor の名前だけを書き、この鍵を持たない
        constexpr const char* k_ClassKey = "class";
        // 名前の束と動作の並びの鍵。配置物と派生は持たず、元の .actor だけに書く。派生は元の物を使う
        constexpr const char* k_HitReactionsKey = "hitReactions";
        constexpr const char* k_ActionsKey = "actions";
        // 派生の .actor が元にする .actor の名前の鍵。元の .actor は持たない
        constexpr const char* k_BaseKey = "base";

        // .actor の頭の鍵の表はこの 1 か所。ほかの鍵は打ち間違いか古い形で、
        // そのまま読むと値が黙って既定に戻るので、最初に見つけた 1 つを返す。無ければ nullptr
        [[nodiscard]] const std::string* FindUnknownActorFileKey(const nlohmann::json& root)
        {
            for (nlohmann::json::const_iterator it = root.begin(); it != root.end(); ++it)
            {
                if (it.key() != k_ClassKey && it.key() != k_BaseKey && it.key() != "classObjs" &&
                    it.key() != k_HitReactionsKey && it.key() != k_ActionsKey)
                {
                    return &it.key();
                }
            }
            return nullptr;
        }

        [[nodiscard]] const nlohmann::json* FindTopKey(const nlohmann::json& actorFile, const char* key) noexcept
        {
            if (!actorFile.is_object())
            {
                return nullptr;
            }
            const nlohmann::json::const_iterator it = actorFile.find(key);
            if (it == actorFile.end())
            {
                return nullptr;
            }
            return &*it;
        }

        // 空の並びは鍵ごと消す。束も動作も持たない .actor の形を変えない
        void SetTopList(nlohmann::json& actorFile, const char* key, nlohmann::json list)
        {
            if (!actorFile.is_object())
            {
                actorFile = nlohmann::json::object();
            }
            if (list.is_array() && list.empty())
            {
                actorFile.erase(key);
                return;
            }
            actorFile[key] = std::move(list);
        }

        // root を .actor として持てない理由。持てるなら空
        // 外から読む Reload と文書を入れる Set が同じ決まりで断る。Set だけが通すと、Save で書いた物を次の起動で読めない
        [[nodiscard]] std::string FindActorFileProblem(const nlohmann::json& root)
        {
            if (!root.is_object())
            {
                return "JSON の object でない";
            }
            if (const std::string* unknown = FindUnknownActorFileKey(root))
            {
                return std::format("知らない鍵 {} がある", *unknown);
            }
            // 派生に書いた束と動作は効かないので、書いた理由が分からないまま残さない
            if (!ActorFileBase(root).empty())
            {
                for (const char* key : {k_HitReactionsKey, k_ActionsKey})
                {
                    if (root.contains(key))
                    {
                        return std::format("派生なのに {} がある。束と動作は元の .actor だけに書く", key);
                    }
                }
            }
            // クラスが分からないまま持つと、作る型が黙って変わる
            const std::string_view className = ActorFileClass(root);
            if (className.empty())
            {
                return "class が無い";
            }
            if (TypeRegistry::Get().Find(className) == nullptr)
            {
                return std::format("class {} が登録されていない", className);
            }
            return {};
        }

    } // namespace

    FieldDestination FieldDestinationOf(const SceneActor* actor, const FieldDesc& field) noexcept
    {
        NS_ASSERT(Scene, actor != nullptr, "FieldDestinationOf: アクターが null");
        if (field.type == FieldType::ActorRef || field.scope == FieldScope::Global)
        {
            return FieldDestination::Scene;
        }
        if (field.scope == FieldScope::Protected)
        {
            const nlohmann::json* actorFile = ActorFileLibrary::Get().Find(actor->ActorFileName());
            if (actorFile != nullptr && !ActorFileBase(*actorFile).empty())
            {
                return FieldDestination::DerivedActorFile;
            }
        }
        return FieldDestination::ActorFile;
    }

    std::string ActorFileToWrite(const SceneActor* actor, FieldDestination destination)
    {
        NS_ASSERT(Scene, actor != nullptr, "ActorFileToWrite: アクターが null");
        switch (destination)
        {
        case FieldDestination::ActorFile:
        {
            const nlohmann::json* actorFile = ActorFileLibrary::Get().Find(actor->ActorFileName());
            if (actorFile != nullptr && !ActorFileBase(*actorFile).empty())
            {
                return std::string{ActorFileBase(*actorFile)};
            }
            return actor->ActorFileName();
        }
        case FieldDestination::DerivedActorFile:
            return actor->ActorFileName();
        case FieldDestination::Scene:
            return {};
        }
        return {};
    }

    bool CanWriteField(FieldDestination layer, const FieldDesc& field) noexcept
    {
        const bool sceneOnly = field.type == FieldType::ActorRef;
        switch (layer)
        {
        case FieldDestination::ActorFile:
            return !sceneOnly;
        case FieldDestination::DerivedActorFile:
            return !sceneOnly && field.scope != FieldScope::Shared;
        case FieldDestination::Scene:
            return sceneOnly || field.scope == FieldScope::Global;
        }
        return false;
    }

    namespace
    {
        [[nodiscard]] const char* LayerName(FieldDestination layer) noexcept
        {
            switch (layer)
            {
            case FieldDestination::ActorFile:
                return "元の .actor";
            case FieldDestination::DerivedActorFile:
                return "派生の .actor";
            case FieldDestination::Scene:
                return "シーン";
            }
            return "";
        }
    } // namespace

    std::optional<nlohmann::json> MergeLayer(const nlohmann::json& base,
                                             const nlohmann::json& written,
                                             FieldDestination layer,
                                             const SceneActor* actor)
    {
        NS_ASSERT(Scene, actor != nullptr, "MergeLayer: 渡したアクターか部品が無い");
        nlohmann::json merged = base;
        nlohmann::json& target = ActorJsonClassObjs(merged);
        bool writable = true;
        std::set<std::string_view> partKeys;
        ForEachReflectedClassObj(actor, [&](const ClassObjInfo& row, const Object* part) {
            partKeys.insert(row.key);
            const nlohmann::json* fields = ClassObjFields(written, row.key);
            if (fields == nullptr)
            {
                return;
            }
            for (nlohmann::json::const_iterator entry = fields->begin(); entry != fields->end(); ++entry)
            {
                const FieldDesc* field = FindField(part->GetReflection(), entry.key());
                if (field == nullptr)
                {
                    NS_LOG_ERROR(Scene,
                                 "{} の {} の上書きの部品 {} に欄 {} が無い",
                                 actor->ActorFileName(),
                                 LayerName(layer),
                                 row.key,
                                 entry.key());
                    writable = false;
                    continue;
                }
                if (!CanWriteField(layer, *field))
                {
                    NS_LOG_ERROR(Scene,
                                 "{} の部品 {} の欄 {} は {} に書けない",
                                 actor->ActorFileName(),
                                 row.key,
                                 entry.key(),
                                 LayerName(layer));
                    writable = false;
                    continue;
                }
                target[row.key][entry.key()] = entry.value();
            }
        });
        const nlohmann::json& source = ActorJsonClassObjs(written);
        for (nlohmann::json::const_iterator entry = source.begin(); entry != source.end(); ++entry)
        {
            if (!partKeys.contains(entry.key()))
            {
                NS_LOG_ERROR(Scene,
                             "{} の {} の上書きに、アクターに無い部品 {} がある",
                             actor->ActorFileName(),
                             LayerName(layer),
                             entry.key());
                writable = false;
            }
            else if (!entry->is_object())
            {
                NS_LOG_ERROR(Scene,
                             "{} の {} の上書きの部品 {} が JSON の object でない",
                             actor->ActorFileName(),
                             LayerName(layer),
                             entry.key());
                writable = false;
            }
        }
        if (!writable)
        {
            return std::nullopt;
        }
        return merged;
    }

    void ReadSceneOverrides(SceneActor* actor, const nlohmann::json& actorJson)
    {
        NS_ASSERT(Scene, actor != nullptr, "ReadSceneOverrides: アクターが null");
        actor->ClearSceneOverrides();
        ForEachReflectedClassObj(actor, [&](const ClassObjInfo& row, const Object* part) {
            const nlohmann::json* fields = ClassObjFields(actorJson, row.key);
            if (fields == nullptr)
            {
                return;
            }
            for (nlohmann::json::const_iterator entry = fields->begin(); entry != fields->end(); ++entry)
            {
                const FieldDesc* field = FindField(part->GetReflection(), entry.key());
                if (field != nullptr && field->type != FieldType::ActorRef)
                {
                    actor->AddSceneOverride(row.key, entry.key());
                }
            }
        });
    }

    std::string_view ActorFileClass(const nlohmann::json& actorFile) noexcept
    {
        if (!actorFile.is_object())
        {
            return {};
        }
        const nlohmann::json::const_iterator it = actorFile.find(k_ClassKey);
        if (it == actorFile.end() || !it->is_string())
        {
            return {};
        }
        return it->get_ref<const std::string&>();
    }

    void SetActorFileClass(nlohmann::json& actorFile, std::string_view className)
    {
        if (!actorFile.is_object())
        {
            actorFile = nlohmann::json::object();
        }
        actorFile[k_ClassKey] = std::string{className};
    }

    std::string_view ActorFileBase(const nlohmann::json& actorFile) noexcept
    {
        const nlohmann::json* base = FindTopKey(actorFile, k_BaseKey);
        if (base == nullptr || !base->is_string())
        {
            return {};
        }
        return base->get_ref<const std::string&>();
    }

    void SetActorFileBase(nlohmann::json& actorFile, std::string_view baseName)
    {
        if (!actorFile.is_object())
        {
            actorFile = nlohmann::json::object();
        }
        actorFile[k_BaseKey] = std::string{baseName};
    }

    const nlohmann::json* ActorFileHitReactions(const nlohmann::json& actorFile) noexcept
    {
        return FindTopKey(actorFile, k_HitReactionsKey);
    }

    void SetActorFileHitReactions(nlohmann::json& actorFile, nlohmann::json list)
    {
        SetTopList(actorFile, k_HitReactionsKey, std::move(list));
    }

    const nlohmann::json* ActorFileActions(const nlohmann::json& actorFile) noexcept
    {
        return FindTopKey(actorFile, k_ActionsKey);
    }

    void SetActorFileActions(nlohmann::json& actorFile, nlohmann::json list)
    {
        SetTopList(actorFile, k_ActionsKey, std::move(list));
    }

    ActorFileLibrary& ActorFileLibrary::Get()
    {
        static ActorFileLibrary instance;
        return instance;
    }

    ActorFileLibrary::ActorFileLibrary() = default;
    ActorFileLibrary::~ActorFileLibrary() = default;

    const std::string& ActorFileLibrary::Directory()
    {
        if (m_directory.empty())
        {
            m_directory = OS::FileSystem::Combine(
                OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets"), "Actors");
        }
        return m_directory;
    }

    void ActorFileLibrary::SetDirectory(std::string directory)
    {
        m_directory = std::move(directory);
        Reload();
    }

    void ActorFileLibrary::EnsureLoaded()
    {
        if (!m_loaded)
        {
            Reload();
        }
    }

    std::optional<nlohmann::json> ReadActorFileAt(const std::string& path)
    {
        const std::optional<std::string> text = OS::FileSystem::ReadAllText(path);
        if (!text.has_value())
        {
            NS_LOG_ERROR(Scene, ".actor {} のファイルを開けないので読まない", path);
            return std::nullopt;
        }
        if (text->size() > k_MaxActorFileBytes)
        {
            NS_LOG_ERROR(Scene, ".actor {} が上限 ({} byte) を超えるので読まない", path, k_MaxActorFileBytes);
            return std::nullopt;
        }
        const nlohmann::json root = nlohmann::json::parse(*text, nullptr, false);
        if (root.is_discarded())
        {
            NS_LOG_ERROR(Scene, ".actor {} の JSON を読めない", path);
            return std::nullopt;
        }
        const std::string problem = FindActorFileProblem(root);
        if (!problem.empty())
        {
            NS_LOG_ERROR(Scene, ".actor {} を読まない。{}", path, problem);
            return std::nullopt;
        }
        return Sanitize(root);
    }

    bool WriteActorFileAt(const std::string& path, const nlohmann::json& actorFile)
    {
        // シーンのファイルと同じく辞書順のキーで書く。同じ中身の 2 回の保存はバイト一致になる
        const std::string text = actorFile.dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        // 書く途中で失敗しても前のファイルを残す。壊れると、それを指すアクターが全部組めなくなる
        if (!OS::FileSystem::ReplaceAllBytes(path, std::span<const std::byte>(raw, text.size())))
        {
            NS_LOG_ERROR(Scene, ".actor {} を書けない", path);
            return false;
        }
        return true;
    }

    void ActorFileLibrary::Reload()
    {
        m_actorFiles.clear();
        m_mergedValues.clear();
        m_loaded = true;

        const std::string& directory = Directory();
        if (!OS::FileSystem::IsDirectory(directory))
        {
            // .actor が 1 つも無い。.actor を指すアクターは作らない
            return;
        }
        for (const std::string& path : OS::FileSystem::ListFiles(directory, ".actor"))
        {
            std::optional<nlohmann::json> actorFile = ReadActorFileAt(path);
            if (!actorFile.has_value())
            {
                continue;
            }
            // 名前はファイル名、クラスは中の class
            m_actorFiles[OS::FileSystem::Stem(path)] = std::move(*actorFile);
        }
        RebuildValues();
        // 全部の値を作れなかった文書は読まなかった物とする。Save で書き戻すと、次の起動でも読めない
        for (std::map<std::string, nlohmann::json, std::less<>>::iterator it = m_actorFiles.begin();
             it != m_actorFiles.end();)
        {
            if (m_mergedValues.contains(it->first))
            {
                ++it;
                continue;
            }
            it = m_actorFiles.erase(it);
        }
    }

    void ActorFileLibrary::RebuildValues()
    {
        m_mergedValues.clear();
        // 派生は元の全部の値に重ねるので、元を先に作る
        for (const bool derived : {false, true})
        {
            for (const std::pair<const std::string, nlohmann::json>& entry : m_actorFiles)
            {
                if (ActorFileBase(entry.second).empty() == derived)
                {
                    continue;
                }
                std::optional<nlohmann::json> values = BuildValues(entry.first, entry.second);
                if (values.has_value())
                {
                    m_mergedValues.emplace(entry.first, std::move(*values));
                }
            }
        }
    }

    std::optional<nlohmann::json> ActorFileLibrary::BuildValues(std::string_view name, const nlohmann::json& actorFile)
    {
        // 項目の一覧は部品を作らないと分からないので、.actor を写さずに 1 体作って聞く。シーンへは入れない
        const std::unique_ptr<SceneActor> actor = CreateActorWithCodeValues(ActorFileClass(actorFile));
        if (actor == nullptr)
        {
            NS_LOG_ERROR(Scene, ".actor {} の class {} のアクターを作れないので読まない", name, ActorFileClass(actorFile));
            return std::nullopt;
        }
        nlohmann::json values = actorFile;
        const std::string_view baseName = ActorFileBase(actorFile);
        if (!baseName.empty())
        {
            const std::map<std::string, nlohmann::json, std::less<>>::const_iterator base =
                m_mergedValues.find(baseName);
            if (base == m_mergedValues.end() || !ActorFileBase(base->second).empty())
            {
                NS_LOG_ERROR(Scene, "派生の .actor {} の元 {} が、読んだ元の .actor に無いので読まない", name, baseName);
                return std::nullopt;
            }
            if (ActorFileClass(base->second) != ActorFileClass(actorFile))
            {
                NS_LOG_ERROR(Scene,
                             "派生の .actor {} の class {} が、元 {} の class {} と違うので読まない",
                             name,
                             ActorFileClass(actorFile),
                             baseName,
                             ActorFileClass(base->second));
                return std::nullopt;
            }
            std::optional<nlohmann::json> merged =
                MergeLayer(base->second, actorFile, FieldDestination::DerivedActorFile, actor.get());
            if (!merged.has_value())
            {
                NS_LOG_ERROR(Scene, "派生の .actor {} を元 {} に重ねられないので読まない", name, baseName);
                return std::nullopt;
            }
            values = std::move(*merged);
        }

        std::vector<std::string> missing;
        ForEachReflectedClassObj(actor.get(), [&](const ClassObjInfo& row, const Object* part) {
            const nlohmann::json* fields = ClassObjFields(values, row.key);
            const nlohmann::json code = SerializeClassObjFields(part);
            for (nlohmann::json::const_iterator field = code.begin(); field != code.end(); ++field)
            {
                const FieldDesc* desc = FindField(part->GetReflection(), field.key());
                if (desc == nullptr || desc->type == FieldType::ActorRef)
                {
                    continue;
                }
                if (fields == nullptr || !fields->contains(field.key()))
                {
                    missing.push_back(std::format("{}/{}", row.key, field.key()));
                }
            }
        });
        if (!missing.empty())
        {
            std::string items = missing.front();
            for (std::size_t i = 1; i < missing.size(); ++i)
            {
                items += "、" + missing[i];
            }
            NS_LOG_ERROR(Scene,
                         ".actor {} に登録した項目の行が {} 個無いので読まない。エディタを起動すると書き足す: {}",
                         name,
                         missing.size(),
                         items);
            return std::nullopt;
        }
        return values;
    }

    std::vector<std::string> ActorFileLibrary::Names()
    {
        EnsureLoaded();
        std::vector<std::string> names;
        names.reserve(m_actorFiles.size());
        for (const std::pair<const std::string, nlohmann::json>& entry : m_actorFiles)
        {
            names.push_back(entry.first);
        }
        return names;
    }

    const nlohmann::json* ActorFileLibrary::Find(std::string_view name)
    {
        EnsureLoaded();
        const std::map<std::string, nlohmann::json, std::less<>>::const_iterator it = m_actorFiles.find(name);
        if (it == m_actorFiles.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    const nlohmann::json* ActorFileLibrary::Values(std::string_view name)
    {
        EnsureLoaded();
        const std::map<std::string, nlohmann::json, std::less<>>::const_iterator it = m_mergedValues.find(name);
        if (it == m_mergedValues.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    std::string ActorFileLibrary::ClassNameOf(std::string_view name)
    {
        const nlohmann::json* actorFile = Find(name);
        if (actorFile == nullptr)
        {
            return {};
        }
        return std::string{ActorFileClass(*actorFile)};
    }

    bool ActorFileLibrary::Set(std::string_view name, nlohmann::json actorFile)
    {
        EnsureLoaded();
        if (name.empty())
        {
            return false;
        }
        const std::string problem = FindActorFileProblem(actorFile);
        if (!problem.empty())
        {
            NS_LOG_ERROR(Scene, ".actor {} の文書を入れない。{}", name, problem);
            return false;
        }
        const std::string key{name};
        std::optional<nlohmann::json> previous;
        if (const std::map<std::string, nlohmann::json, std::less<>>::const_iterator it = m_actorFiles.find(key);
            it != m_actorFiles.end())
        {
            previous = it->second;
        }
        m_actorFiles[key] = Sanitize(actorFile);
        RebuildValues();
        // 作れない訳は BuildValues が ERROR で出している
        if (!m_mergedValues.contains(key))
        {
            if (previous.has_value())
            {
                m_actorFiles[key] = std::move(*previous);
            }
            else
            {
                m_actorFiles.erase(key);
            }
            RebuildValues();
            return false;
        }
        return true;
    }

    void ActorFileLibrary::Erase(std::string_view name)
    {
        EnsureLoaded();
        m_actorFiles.erase(std::string{name});
        RebuildValues();
    }

    bool ActorFileLibrary::Delete(std::string_view name)
    {
        Erase(name);
        const std::string path = OS::FileSystem::Combine(Directory(), std::string{name} + ".actor");
        if (!OS::FileSystem::RemoveFile(path))
        {
            NS_LOG_ERROR(Scene, ".actor {} のファイルを消せない", path);
            return false;
        }
        return true;
    }

    bool ActorFileLibrary::Save(std::string_view name)
    {
        const nlohmann::json* actorFile = Find(name);
        if (actorFile == nullptr)
        {
            return false;
        }
        return WriteActorFileAt(OS::FileSystem::Combine(Directory(), std::string{name} + ".actor"), *actorFile);
    }

    void ApplyActorFile(SceneActor* actor)
    {
        NS_ASSERT(Scene, actor != nullptr, "ApplyActorFile: アクターが null");
        const nlohmann::json* actorFile = ActorFileLibrary::Get().Values(actor->ActorFileName());
        if (actorFile == nullptr)
        {
            return;
        }
        // シーンで上書きした項目はアクターの値が正なので写さない
        nlohmann::json values = *actorFile;
        ForEachReflectedClassObj(actor, [&](const ClassObjInfo& row, const Object*) {
            nlohmann::json* fields = ClassObjFields(values, row.key);
            if (fields == nullptr)
            {
                return;
            }
            for (nlohmann::json::iterator field = fields->begin(); field != fields->end();)
            {
                if (actor->IsSceneOverride(row.key, field.key()))
                {
                    field = fields->erase(field);
                }
                else
                {
                    ++field;
                }
            }
        });
        ApplyActorClassObjs(actor, values);
    }

    bool RevertSceneOverride(SceneActor* actor, const ClassObjInfo& row, std::string_view fieldName)
    {
        NS_ASSERT(Scene, actor != nullptr, "RevertSceneOverride: アクターが null");
        Object* part = row.get(actor);
        const nlohmann::json* values = ActorFileLibrary::Get().Values(actor->ActorFileName());
        const nlohmann::json* fields = nullptr;
        if (values != nullptr)
        {
            fields = ClassObjFields(*values, row.key);
        }
        const std::string name{fieldName};
        if (part == nullptr || fields == nullptr || !fields->contains(name))
        {
            NS_LOG_ERROR(Scene, "{} の全部の値に部品 {} の欄 {} が無いので戻せない", actor->ActorFileName(), row.key, name);
            return false;
        }
        actor->RemoveSceneOverride(row.key, fieldName);
        (void)ApplyJsonFields(part, nlohmann::json{{name, fields->at(name)}});
        return true;
    }
} // namespace NS
