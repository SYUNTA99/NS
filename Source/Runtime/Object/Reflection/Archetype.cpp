#include "Runtime/Object/Reflection/Archetype.h"

#include "Runtime/Core/Logger.h"
#include "Runtime/Object/Actor.h"
#include "Runtime/Object/Component.h"
#include "Runtime/Object/Reflection/ComponentEntry.h"
#include "Runtime/Object/Reflection/ObjectBuilder.h"
#include "Runtime/Object/Reflection/Reflection.h"
#include "Runtime/Object/Reflection/ReflectionJson.h"
#include "Runtime/Object/Reflection/TypeRegistry.h"
#include "Runtime/Platform/Filesystem.h"

#include <cstddef>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace NS::Obj
{
    namespace
    {
        // 位置・回転・拡縮は個体の物。種類の既定値に持たせず、保存の差分でも落とさない
        constexpr std::string_view k_TransformTypeName = "TransformComponent";

        // 種類の既定値 1 つの上限。部品の値だけなので、シーンのファイルよりずっと小さい
        constexpr std::size_t k_MaxArchetypeFileBytes = 1024u * 1024u;

        // 参照の欄の値の形。ForEachRefValue と同じ見分け方
        [[nodiscard]] bool IsRefValue(const nlohmann::json& value) noexcept
        {
            return value.is_object() && value.contains("ref");
        }

        [[nodiscard]] std::string_view PartTypeName(const Component& part) noexcept
        {
            const ReflectionInfo* info = part.GetReflection();
            if (info != nullptr)
            {
                return std::string_view{info->typeName};
            }
            return {};
        }

        // 種類の既定値に持たせない物を落とした写し。参照の欄・id・位置と回転と拡縮は個体の物
        [[nodiscard]] nlohmann::json Sanitize(std::string_view className, const nlohmann::json& archetype)
        {
            nlohmann::json out = nlohmann::json::object();
            SetObjectJsonClass(out, className);
            nlohmann::json& parts = ObjectJsonParts(out);
            const nlohmann::json& source = ObjectJsonParts(archetype);
            for (nlohmann::json::const_iterator entry = source.begin(); entry != source.end(); ++entry)
            {
                if (entry.key() == "Transform" || !entry.value().is_object())
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
        [[nodiscard]] std::optional<nlohmann::json> FieldValue(const Component& comp, std::string_view fieldName)
        {
            const nlohmann::json serialized = SerializeComponent(comp);
            const nlohmann::json* fields = ComponentEntryFields(serialized);
            if (fields == nullptr)
            {
                return std::nullopt;
            }
            const nlohmann::json::const_iterator it = fields->find(std::string{fieldName});
            if (it == fields->end())
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
            m_directory = NS::Platform::FileSystem::Combine(
                NS::Platform::FileSystem::Combine(NS::Platform::FileSystem::ContentRoot(), "Assets"), "Archetypes");
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
        if (!NS::Platform::FileSystem::IsDirectory(directory))
        {
            // 種類の既定値が 1 つも無いプロジェクトもある。全てコードの既定値で組む
            return;
        }
        for (const std::string& path : NS::Platform::FileSystem::ListFiles(directory, ".json"))
        {
            const std::optional<std::string> text = NS::Platform::FileSystem::ReadAllText(path);
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
            const std::string className = NS::Platform::FileSystem::Stem(path);
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
        const std::map<std::string, std::unique_ptr<Actor>, std::less<>>::iterator it = m_baselines.find(className);
        if (it != m_baselines.end())
        {
            m_baselines.erase(it);
        }
    }

    void ArchetypeLibrary::Erase(std::string_view className)
    {
        EnsureLoaded();
        const std::map<std::string, nlohmann::json, std::less<>>::iterator found = m_archetypes.find(className);
        if (found != m_archetypes.end())
        {
            m_archetypes.erase(found);
        }
        const std::map<std::string, std::unique_ptr<Actor>, std::less<>>::iterator it = m_baselines.find(className);
        if (it != m_baselines.end())
        {
            m_baselines.erase(it);
        }
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
        const std::string path = NS::Platform::FileSystem::Combine(Directory(), std::string{className} + ".json");
        const std::byte* raw = reinterpret_cast<const std::byte*>(text.data());
        if (!NS::Platform::FileSystem::WriteAllBytes(path, std::span<const std::byte>(raw, text.size())))
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

    std::unique_ptr<Actor> CreateActorOfClass(std::string_view className)
    {
        if (!className.empty())
        {
            const TypeRegistry::Entry* entry = TypeRegistry::Get().Find(className);
            if (entry != nullptr && entry->create != nullptr)
            {
                return entry->create();
            }
        }
        return std::make_unique<Actor>();
    }

    void ApplyArchetype(Actor& actor)
    {
        const nlohmann::json* archetype = ArchetypeLibrary::Get().Find(actor.ClassName());
        if (archetype == nullptr)
        {
            return;
        }
        // 種類の既定値は部品を足せる。どの部品を持つかも種類で決める
        ApplyObjectParts(actor, *archetype, PartCreation::Allow);
    }

    nlohmann::json ExpandObjectJson(const nlohmann::json& object)
    {
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        nlohmann::json full = object;
        nlohmann::json& target = ObjectJsonParts(full);
        target = nlohmann::json::object();
        baseline.ForEachPart([&object, &target](std::string_view name, Component& part) {
            nlohmann::json fields = SerializePartFields(part);
            if (const nlohmann::json* source = PartFields(object, name))
            {
                fields.update(*source);
            }
            target[std::string{name}] = std::move(fields);
        });
        return full;
    }

    nlohmann::json DiffObjectJson(const nlohmann::json& object)
    {
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        nlohmann::json out = object;
        baseline.ForEachPart([&out](std::string_view name, Component& part) {
            if (name == "Transform")
            {
                return;
            }
            nlohmann::json* fields = PartFields(out, name);
            if (fields == nullptr)
            {
                return;
            }
            const nlohmann::json defaults = SerializePartFields(part);
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

    const Component* FindBaselinePart(const Component& comp)
    {
        const Actor* owner = comp.Owner();
        if (owner == nullptr)
        {
            return nullptr;
        }
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(owner->ClassName());
        const std::string_view role = owner->PartName(comp);
        const Component* found = baseline.Part(role);
        if (found != nullptr && PartTypeName(*found) == PartTypeName(comp))
        {
            return found;
        }
        return nullptr;
    }

    bool IsFieldOverridden(const Component& comp, std::string_view fieldName)
    {
        const Component* part = FindBaselinePart(comp);
        if (part == nullptr)
        {
            return false;
        }
        return FieldValue(comp, fieldName) != FieldValue(*part, fieldName);
    }

    bool IsArchetypeField(const Component& comp, std::string_view fieldName)
    {
        if (PartTypeName(comp) == k_TransformTypeName)
        {
            return false;
        }
        const FieldDesc* field = FindField(comp.GetReflection(), fieldName);
        if (field == nullptr)
        {
            return false;
        }
        return field->type != FieldType::ActorRef && field->type != FieldType::ComponentRef;
    }

    bool WriteFieldToArchetype(const Component& comp, std::string_view fieldName)
    {
        const Actor* owner = comp.Owner();
        if (owner == nullptr || !IsArchetypeField(comp, fieldName))
        {
            return false;
        }
        const std::string_view role = owner->PartName(comp);
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
        nlohmann::json& fields = ObjectJsonParts(archetype)[std::string{role}];
        if (!fields.is_object())
        {
            fields = nlohmann::json::object();
        }
        fields[std::string{fieldName}] = *value;
        library.Set(owner->ClassName(), std::move(archetype));
        return true;
    }
} // namespace NS::Obj
