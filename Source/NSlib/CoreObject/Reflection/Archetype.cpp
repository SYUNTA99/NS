#include "NSlib/CoreObject/Reflection/Archetype.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/CoreObject/ClassObjs/Transform.h"
#include "NSlib/CoreObject/Object.h"
#include "NSlib/CoreObject/Reflection/ObjectBuilder.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Reflection/TypeRegistry.h"
#include "NSlib/CoreObject/SceneActor.h"
#include "NSlib/Windows/Filesystem.h"

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace NS
{
    namespace
    {
        // 種類の既定値 1 つの上限。部品の値だけなので、シーンのファイルよりずっと小さい
        constexpr std::size_t k_MaxArchetypeFileBytes = 1024u * 1024u;

        // 種類の既定値に持たせない物を落とした写し。参照の欄と id は個体の物
        [[nodiscard]] nlohmann::json Sanitize(std::string_view className, const nlohmann::json& archetype)
        {
            nlohmann::json out = nlohmann::json::object();
            SetObjectJsonClass(out, className);
            nlohmann::json& parts = ObjectJsonClassObjs(out);
            const nlohmann::json& source = ObjectJsonClassObjs(archetype);
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

        // 種類の既定値のファイルが持つのはクラス名と持ち物の鍵だけ。ほかの鍵は打ち間違いか古い形で、
        // そのまま読むと値が黙って既定に戻るので、最初に見つけた 1 つを返す。無ければ nullptr
        [[nodiscard]] const std::string* FindUnknownArchetypeKey(const nlohmann::json& root)
        {
            for (nlohmann::json::const_iterator it = root.begin(); it != root.end(); ++it)
            {
                if (it.key() != "class" && it.key() != "classObjs")
                {
                    return &it.key();
                }
            }
            return nullptr;
        }

        // comp の欄 fieldName の値を JSON で読む。無ければ nullopt
        [[nodiscard]] std::optional<nlohmann::json> FieldValue(const Object& comp, std::string_view fieldName)
        {
            const nlohmann::json fields = SerializeClassObjFields(comp);
            const nlohmann::json::const_iterator it = fields.find(std::string{fieldName});
            if (it == fields.end())
            {
                return std::nullopt;
            }
            return std::optional<nlohmann::json>{std::in_place, *it};
        }
    } // namespace

    ArchetypeLibrary& ArchetypeLibrary::Get()
    {
        static ArchetypeLibrary instance;
        return instance;
    }

    ArchetypeLibrary::ArchetypeLibrary() = default;
    ArchetypeLibrary::~ArchetypeLibrary() = default;

    const std::string& ArchetypeLibrary::Directory()
    {
        if (m_directory.empty())
        {
            m_directory = OS::FileSystem::Combine(
                OS::FileSystem::Combine(OS::FileSystem::ContentRoot(), "Assets"), "Archetypes");
        }
        return m_directory;
    }

    void ArchetypeLibrary::SetDirectory(std::string directory)
    {
        m_directory = std::move(directory);
        Reload();
    }

    void ArchetypeLibrary::EnsureLoaded()
    {
        if (!m_loaded)
        {
            Reload();
        }
    }

    void ArchetypeLibrary::Reload()
    {
        m_archetypes.clear();
        m_baselines.clear();
        m_loaded = true;

        const std::string& directory = Directory();
        if (!OS::FileSystem::IsDirectory(directory))
        {
            // 種類の既定値が 1 つも無いプロジェクトもある。全てコードの既定値で組む
            return;
        }
        for (const std::string& path : OS::FileSystem::ListFiles(directory, ".json"))
        {
            const std::optional<std::string> text = OS::FileSystem::ReadAllText(path);
            if (!text.has_value())
            {
                continue;
            }
            if (text->size() > k_MaxArchetypeFileBytes)
            {
                NS_LOG_ERROR(
                    Scene, "種類の既定値 {} が上限 ({} byte) を超えるので読まない", path, k_MaxArchetypeFileBytes);
                continue;
            }
            const nlohmann::json root = nlohmann::json::parse(*text, nullptr, false);
            if (root.is_discarded() || !root.is_object())
            {
                NS_LOG_ERROR(Scene, "種類の既定値 {} の JSON を読めない", path);
                continue;
            }
            if (const std::string* unknown = FindUnknownArchetypeKey(root))
            {
                NS_LOG_ERROR(Scene, "種類の既定値 {} に知らない鍵 {} があるので読まない", path, *unknown);
                continue;
            }
            // クラス名はファイル名が正。中の class と食い違えばファイル名を使う
            const std::string className = OS::FileSystem::Stem(path);
            const std::string_view written = ObjectJsonClass(root);
            if (!written.empty() && written != className)
            {
                NS_LOG_WARN(Scene,
                            "種類の既定値 {} の class {} がファイル名と違うので、ファイル名のクラスとして読む",
                            path,
                            written);
            }
            if (TypeRegistry::Get().Find(className) == nullptr)
            {
                NS_LOG_WARN(Scene, "種類の既定値 {} のクラスが登録されていない", path);
            }
            m_archetypes[className] = Sanitize(className, root);
        }
    }

    const nlohmann::json* ArchetypeLibrary::Find(std::string_view className)
    {
        EnsureLoaded();
        const std::map<std::string, nlohmann::json, std::less<>>::const_iterator it = m_archetypes.find(className);
        if (it == m_archetypes.end())
        {
            return nullptr;
        }
        return &it->second;
    }

    void ArchetypeLibrary::Set(std::string_view className, nlohmann::json archetype)
    {
        EnsureLoaded();
        if (className.empty())
        {
            return;
        }
        m_archetypes[std::string{className}] = Sanitize(className, archetype);
        // 既定の 1 体は種類の既定値から作るので、変えたら作り直す
        m_baselines.erase(std::string{className});
    }

    void ArchetypeLibrary::Erase(std::string_view className)
    {
        EnsureLoaded();
        m_archetypes.erase(std::string{className});
        m_baselines.erase(std::string{className});
    }

    bool ArchetypeLibrary::Save(std::string_view className)
    {
        const nlohmann::json* archetype = Find(className);
        if (archetype == nullptr)
        {
            return false;
        }
        // シーンのファイルと同じく辞書順のキーで書く。同じ中身の 2 回の保存はバイト一致になる
        const std::string text = archetype->dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        const std::string path = OS::FileSystem::Combine(Directory(), std::string{className} + ".json");
        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        if (!OS::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size())))
        {
            NS_LOG_ERROR(Scene, "種類の既定値 {} を書けない", path);
            return false;
        }
        return true;
    }

    const SceneActor& ArchetypeLibrary::Baseline(std::string_view className)
    {
        EnsureLoaded();
        const std::map<std::string, std::unique_ptr<SceneActor>, std::less<>>::const_iterator it =
            m_baselines.find(className);
        if (it != m_baselines.end())
        {
            return *it->second;
        }
        std::unique_ptr<SceneActor> actor = CreateActorOfClass(className);
        ApplyArchetype(*actor);
        const SceneActor& result = *actor;
        m_baselines.emplace(std::string{className}, std::move(actor));
        return result;
    }

    void ApplyArchetype(SceneActor& actor)
    {
        const nlohmann::json* archetype = ArchetypeLibrary::Get().Find(actor.ClassName());
        if (archetype == nullptr)
        {
            return;
        }
        // 位置・回転・拡縮は個体の物なので、ファイルに書かれていても根の部品へは写さない
        nlohmann::json values = *archetype;
        ForEachReflectedClassObj(actor, [&](const ClassObjInfo& row, const Object* part) {
            if (part == &actor.Root())
            {
                ObjectJsonClassObjs(values).erase(row.key);
            }
        });
        ApplyObjectClassObjs(actor, values);
    }

    nlohmann::json ExpandObjectJson(const nlohmann::json& object)
    {
        const SceneActor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        nlohmann::json full = object;
        nlohmann::json& target = ObjectJsonClassObjs(full);
        target = nlohmann::json::object();
        ForEachReflectedClassObj(baseline, [&](const ClassObjInfo& field, const Object* classObj) {
            nlohmann::json fields = SerializeClassObjFields(*classObj);
            if (const nlohmann::json* source = ClassObjFields(object, field.key))
            {
                fields.update(*source);
            }
            target[field.key] = std::move(fields);
        });
        return full;
    }

    nlohmann::json DiffObjectJson(const nlohmann::json& object)
    {
        const SceneActor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        nlohmann::json out = object;
        ForEachReflectedClassObj(baseline, [&](const ClassObjInfo& member, const Object* classObj) {
            // 位置・回転・拡縮は個体の物なので、既定と同じ値でも差分から落とさない
            if (classObj == &baseline.Root())
            {
                return;
            }
            nlohmann::json* fields = ClassObjFields(out, member.key);
            if (fields == nullptr)
            {
                return;
            }
            const nlohmann::json defaults = SerializeClassObjFields(*classObj);
            for (nlohmann::json::iterator field = fields->begin(); field != fields->end();)
            {
                const nlohmann::json::const_iterator original = defaults.find(field.key());
                if (original != defaults.end() && *original == field.value())
                {
                    field = fields->erase(field);
                }
                else
                {
                    ++field;
                }
            }
        });
        return out;
    }

    namespace
    {
        // row が actor の表に無ければ nullptr。Baseline は登録の無いクラスでは素の SceneActor なので、
        // 行の関数を直に呼ばず、actor の表を回して同じ行を探す
        [[nodiscard]] const Object* FindRowPart(const SceneActor& actor, const ClassObjInfo& row)
        {
            const Object* found = nullptr;
            ForEachReflectedClassObj(actor, [&](const ClassObjInfo& field, const Object* part) {
                if (&field == &row)
                {
                    found = part;
                }
            });
            return found;
        }
    } // namespace

    const Object* FindBaselineClassObj(const SceneActor& actor, const ClassObjInfo& row)
    {
        const Object* live = FindRowPart(actor, row);
        if (live == nullptr)
        {
            return nullptr;
        }
        const SceneActor& baseline = ArchetypeLibrary::Get().Baseline(actor.ClassName());
        const Object* found = FindRowPart(baseline, row);
        if (found != nullptr && std::string_view{found->ClassName()} == live->ClassName())
        {
            return found;
        }
        return nullptr;
    }

    bool IsFieldOverridden(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName)
    {
        const Object* part = FindBaselineClassObj(actor, row);
        if (part == nullptr)
        {
            return false;
        }
        return FieldValue(*FindRowPart(actor, row), fieldName) != FieldValue(*part, fieldName);
    }

    bool IsArchetypeField(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName)
    {
        const Object* live = FindRowPart(actor, row);
        if (live == nullptr || live == &actor.Root())
        {
            return false;
        }
        const FieldDesc* field = FindField(live->GetReflection(), fieldName);
        if (field == nullptr)
        {
            return false;
        }
        return field->type != FieldType::ActorRef;
    }

    bool WriteFieldToArchetype(const SceneActor& actor, const ClassObjInfo& row, std::string_view fieldName)
    {
        if (!IsArchetypeField(actor, row, fieldName))
        {
            return false;
        }
        const std::optional<nlohmann::json> value = FieldValue(*FindRowPart(actor, row), fieldName);
        if (!value.has_value())
        {
            return false;
        }
        ArchetypeLibrary& library = ArchetypeLibrary::Get();
        nlohmann::json archetype = nlohmann::json::object();
        if (const nlohmann::json* current = library.Find(actor.ClassName()))
        {
            archetype = *current;
        }
        nlohmann::json& fields = ObjectJsonClassObjs(archetype)[std::string{row.key}];
        if (!fields.is_object())
        {
            fields = nlohmann::json::object();
        }
        fields[std::string{fieldName}] = *value;
        library.Set(actor.ClassName(), std::move(archetype));
        return true;
    }
} // namespace NS
