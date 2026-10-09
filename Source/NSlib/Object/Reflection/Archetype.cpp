#include "NSlib/Object/Reflection/Archetype.h"

#include "NSlib/Core/Logger.h"
#include "NSlib/Object/Actor.h"
#include "NSlib/Object/Reflection/ObjectBuilder.h"
#include "NSlib/Object/Reflection/Reflection.h"
#include "NSlib/Object/Reflection/ReflectionJson.h"
#include "NSlib/Object/Reflection/TypeRegistry.h"
#include "NSlib/Object/SubObject.h"
#include "NSlib/Object/SubObjects/TransformSubObject.h"
#include "NSlib/Windows/Filesystem.h"

#include <cstddef>
#include <optional>
#include <span>
#include <utility>

namespace NS::Obj
{
    namespace
    {
        // 種類の既定値 1 つの上限。部品の値だけなので、シーンのファイルよりずっと小さい
        constexpr std::size_t k_MaxArchetypeFileBytes = 1024u * 1024u;

        // 位置・回転・拡縮は個体の物。種類の既定値に持たせず、保存の差分でも落とさない
        // 保存側は JSON の件しか持たないので、型でなく保存の鍵の部品名で見分ける
        [[nodiscard]] bool IsInstanceOnlySubObj(std::string_view partName) noexcept
        {
            return partName == k_TransformSubObjName;
        }

        // 種類の既定値に持たせない物を落とした写し。参照の欄・id・位置と回転と拡縮は個体の物
        [[nodiscard]] nlohmann::json Sanitize(std::string_view className, const nlohmann::json& archetype)
        {
            nlohmann::json out = nlohmann::json::object();
            SetObjectJsonClass(out, className);
            nlohmann::json& parts = ObjectJsonSubObjs(out);
            const nlohmann::json& source = ObjectJsonSubObjs(archetype);
            for (nlohmann::json::const_iterator entry = source.begin(); entry != source.end(); ++entry)
            {
                if (IsInstanceOnlySubObj(entry.key()) || !entry.value().is_object())
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

        // comp の欄 fieldName の値を JSON で読む。無ければ nullopt
        [[nodiscard]] std::optional<nlohmann::json> FieldValue(const SubObject& comp, std::string_view fieldName)
        {
            const nlohmann::json fields = SerializeSubObjectFields(comp);
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
            m_directory = NS::OS::FileSystem::Combine(
                NS::OS::FileSystem::Combine(NS::OS::FileSystem::ContentRoot(), "Assets"), "Archetypes");
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
        if (!NS::OS::FileSystem::IsDirectory(directory))
        {
            // 種類の既定値が 1 つも無いプロジェクトもある。全てコードの既定値で組む
            return;
        }
        for (const std::string& path : NS::OS::FileSystem::ListFiles(directory, ".json"))
        {
            const std::optional<std::string> text = NS::OS::FileSystem::ReadAllText(path);
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
            // クラス名はファイル名が正。中の class と食い違えばファイル名を使う
            const std::string className = NS::OS::FileSystem::Stem(path);
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
        // シーンのファイルと同じく辞書順のキーで書く。同じ中身の 2 回の保存は byte 一致になる
        const std::string text = archetype->dump(2, ' ', false, nlohmann::json::error_handler_t::replace);
        const std::string path = NS::OS::FileSystem::Combine(Directory(), std::string{className} + ".json");
        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        if (!NS::OS::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size())))
        {
            NS_LOG_ERROR(Scene, "種類の既定値 {} を書けない", path);
            return false;
        }
        return true;
    }

    const Actor& ArchetypeLibrary::Baseline(std::string_view className)
    {
        EnsureLoaded();
        const std::map<std::string, std::unique_ptr<Actor>, std::less<>>::const_iterator it =
            m_baselines.find(className);
        if (it != m_baselines.end())
        {
            return *it->second;
        }
        std::unique_ptr<Actor> actor = CreateActorOfClass(className);
        ApplyArchetype(*actor);
        const Actor& result = *actor;
        m_baselines.emplace(std::string{className}, std::move(actor));
        return result;
    }

    void ApplyArchetype(Actor& actor)
    {
        const nlohmann::json* archetype = ArchetypeLibrary::Get().Find(actor.ClassName());
        if (archetype == nullptr)
        {
            return;
        }
        // 種類の既定値は部品を足せる。どの部品を持つかも種類で決める
        ApplyObjectSubObjs(actor, *archetype, SubObjCreation::Allow);
    }

    nlohmann::json ExpandObjectJson(const nlohmann::json& object)
    {
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        nlohmann::json full = object;
        nlohmann::json& target = ObjectJsonSubObjs(full);
        target = nlohmann::json::object();
        for (const SubObject* subObject : baseline.SubObjs())
        {
            nlohmann::json fields = SerializeSubObjFields(*subObject);
            if (const nlohmann::json* source = SubObjFields(object, subObject->Name()))
            {
                fields.update(*source);
            }
            target[subObject->Name()] = std::move(fields);
        }
        return full;
    }

    nlohmann::json DiffObjectJson(const nlohmann::json& object)
    {
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        nlohmann::json out = object;
        for (const SubObject* subObject : baseline.SubObjs())
        {
            if (IsInstanceOnlySubObj(subObject->Name()))
            {
                continue;
            }
            nlohmann::json* fields = SubObjFields(out, subObject->Name());
            if (fields == nullptr)
            {
                continue;
            }
            const nlohmann::json defaults = SerializeSubObjFields(*subObject);
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
        }
        return out;
    }

    const SubObject* FindBaselineSubObj(const SubObject& comp)
    {
        const Actor* owner = comp.Owner();
        if (owner == nullptr)
        {
            return nullptr;
        }
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(owner->ClassName());
        const SubObject* found = baseline.FindSubObj(comp.Name());
        if (found != nullptr && std::string_view{found->ClassName()} == comp.ClassName())
        {
            return found;
        }
        return nullptr;
    }

    bool IsFieldOverridden(const SubObject& comp, std::string_view fieldName)
    {
        const SubObject* part = FindBaselineSubObj(comp);
        if (part == nullptr)
        {
            return false;
        }
        return FieldValue(comp, fieldName) != FieldValue(*part, fieldName);
    }

    bool IsArchetypeField(const SubObject& comp, std::string_view fieldName)
    {
        // 持ち主の無い部品は種類を持たない
        if (comp.Owner() == nullptr || IsInstanceOnlySubObj(comp.Name()))
        {
            return false;
        }
        const FieldDesc* field = FindField(comp.GetReflection(), fieldName);
        if (field == nullptr)
        {
            return false;
        }
        return field->type != FieldType::ActorRef;
    }

    bool WriteFieldToArchetype(const SubObject& comp, std::string_view fieldName)
    {
        const Actor* owner = comp.Owner();
        if (owner == nullptr || !IsArchetypeField(comp, fieldName))
        {
            return false;
        }
        const std::string_view role = comp.Name();
        const std::optional<nlohmann::json> value = FieldValue(comp, fieldName);
        if (role.empty() || !value.has_value())
        {
            return false;
        }
        ArchetypeLibrary& library = ArchetypeLibrary::Get();
        nlohmann::json archetype = nlohmann::json::object();
        if (const nlohmann::json* current = library.Find(owner->ClassName()))
        {
            archetype = *current;
        }
        nlohmann::json& fields = ObjectJsonSubObjs(archetype)[std::string{role}];
        if (!fields.is_object())
        {
            fields = nlohmann::json::object();
        }
        fields[std::string{fieldName}] = *value;
        library.Set(owner->ClassName(), std::move(archetype));
        return true;
    }
} // namespace NS::Obj
