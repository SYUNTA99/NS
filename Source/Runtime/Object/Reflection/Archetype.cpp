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

        // 対応が無い印
        constexpr std::size_t k_NoIndex = static_cast<std::size_t>(-1);

        // 参照の欄の値の形。ForEachRefValue と同じ見分け方
        [[nodiscard]] bool IsRefValue(const nlohmann::json& value) noexcept
        {
            return value.is_object() && value.contains("ref");
        }

        [[nodiscard]] std::string_view PartTypeName(const Component& part) noexcept
        {
            const ReflectionInfo* info = part.GetReflection();
            return info != nullptr ? std::string_view{info->typeName} : std::string_view{};
        }

        // 種類の既定値に持たせない物を落とした写し。参照の欄・id・位置と回転と拡縮は個体の物
        [[nodiscard]] nlohmann::json Sanitize(std::string_view className, const nlohmann::json& archetype)
        {
            nlohmann::json out = nlohmann::json::object();
            SetObjectJsonClass(out, className);
            nlohmann::json components = nlohmann::json::array();
            for (const nlohmann::json& entry : ObjectJsonComponents(archetype))
            {
                const std::string_view typeName = ComponentEntryType(entry);
                if (typeName.empty() || typeName == k_TransformTypeName)
                {
                    continue;
                }
                nlohmann::json clean = MakeComponentEntry(typeName);
                SetComponentEntryName(clean, ComponentEntryName(entry));
                SetComponentEntryEnabled(clean, ComponentEntryEnabled(entry));
                if (const nlohmann::json* fields = ComponentEntryFields(entry))
                {
                    for (nlohmann::json::const_iterator it = fields->begin(); it != fields->end(); ++it)
                    {
                        if (!IsRefValue(it.value()))
                        {
                            clean["fields"][it.key()] = it.value();
                        }
                    }
                }
                components.push_back(std::move(clean));
            }
            out["components"] = std::move(components);
            return out;
        }

        // リフレクションを持つ部品を並び順に集める。JSON へ写るのはこれだけ
        [[nodiscard]] std::vector<const Component*> ReflectedParts(const Actor& actor)
        {
            std::vector<const Component*> parts;
            parts.reserve(actor.Components().size());
            for (const Component* part : actor.Components())
            {
                if (part != nullptr && part->GetReflection() != nullptr)
                {
                    parts.push_back(part);
                }
            }
            return parts;
        }

        // 部品と件を対応させる。先に名前と型が同じ組を結び、残りを型の同じ最初の件と結ぶ
        // MatchComponentEntry と同じ規則を、部品の側から引く形にした物
        // 戻り値は部品ごとの件の添字。対応が無ければ k_NoIndex
        [[nodiscard]] std::vector<std::size_t> MatchPartsToEntries(std::span<const Component* const> parts,
                                                                   const nlohmann::json& entries)
        {
            std::vector<std::size_t> assignment(parts.size(), k_NoIndex);
            std::vector<bool> taken(entries.size(), false);
            for (std::size_t p = 0; p < parts.size(); ++p)
            {
                for (std::size_t e = 0; e < entries.size(); ++e)
                {
                    if (!taken[e] && ComponentEntryName(entries[e]) == parts[p]->Name() &&
                        ComponentEntryType(entries[e]) == PartTypeName(*parts[p]))
                    {
                        assignment[p] = e;
                        taken[e] = true;
                        break;
                    }
                }
            }
            for (std::size_t p = 0; p < parts.size(); ++p)
            {
                if (assignment[p] != k_NoIndex)
                {
                    continue;
                }
                for (std::size_t e = 0; e < entries.size(); ++e)
                {
                    if (!taken[e] && ComponentEntryType(entries[e]) == PartTypeName(*parts[p]))
                    {
                        assignment[p] = e;
                        taken[e] = true;
                        break;
                    }
                }
            }
            return assignment;
        }

        // Baseline の部品を件にした物。名前と、止めてある時だけ有効の印を持つ。id は持たない
        [[nodiscard]] nlohmann::json BaselineEntry(const Component& part)
        {
            nlohmann::json entry = SerializeComponent(part);
            SetComponentEntryName(entry, part.Name());
            SetComponentEntryEnabled(entry, part.IsEnabled());
            return entry;
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

        [[nodiscard]] const FieldDesc* FindField(const Component& comp, std::string_view fieldName) noexcept
        {
            const ReflectionInfo* info = comp.GetReflection();
            if (info == nullptr)
            {
                return nullptr;
            }
            for (std::size_t i = 0; i < info->fieldCount; ++i)
            {
                if (fieldName == info->fields[i].name)
                {
                    return &info->fields[i];
                }
            }
            return nullptr;
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
                NS_LOG_ERROR(Scene, "種類の既定値 {} が上限 ({} byte) を超えるので読まない", path, k_MaxArchetypeFileBytes);
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
                NS_LOG_WARN(Scene, "種類の既定値 {} の class {} がファイル名と違うので、ファイル名のクラスとして読む", path, written);
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
        const std::map<std::string, std::unique_ptr<Actor>, std::less<>>::const_iterator it = m_baselines.find(className);
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
        ApplyObjectComponents(actor, *archetype, PartCreation::Allow);
    }

    nlohmann::json ExpandObjectJson(const nlohmann::json& object)
    {
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        const std::vector<const Component*> parts = ReflectedParts(baseline);

        nlohmann::json full = object;
        const nlohmann::json instance = ObjectJsonComponents(object);
        const std::vector<std::size_t> assignment = MatchPartsToEntries(parts, instance);

        nlohmann::json components = nlohmann::json::array();
        for (std::size_t p = 0; p < parts.size(); ++p)
        {
            nlohmann::json entry = BaselineEntry(*parts[p]);
            if (assignment[p] != k_NoIndex)
            {
                const nlohmann::json& source = instance[assignment[p]];
                SetComponentEntryId(entry, ComponentEntryId(source));
                SetComponentEntryEnabled(entry, ComponentEntryEnabled(source));
                if (const nlohmann::json* fields = ComponentEntryFields(source))
                {
                    for (nlohmann::json::const_iterator it = fields->begin(); it != fields->end(); ++it)
                    {
                        entry["fields"][it.key()] = it.value();
                    }
                }
            }
            components.push_back(std::move(entry));
        }
        ObjectJsonComponents(full) = std::move(components);
        return full;
    }

    nlohmann::json DiffObjectJson(const nlohmann::json& object)
    {
        const Actor& baseline = ArchetypeLibrary::Get().Baseline(ObjectJsonClass(object));
        const std::vector<const Component*> parts = ReflectedParts(baseline);

        nlohmann::json out = object;
        nlohmann::json& components = ObjectJsonComponents(out);
        const std::vector<std::size_t> assignment = MatchPartsToEntries(parts, components);
        for (std::size_t p = 0; p < parts.size(); ++p)
        {
            if (assignment[p] == k_NoIndex || PartTypeName(*parts[p]) == k_TransformTypeName)
            {
                continue;
            }
            nlohmann::json& entry = components[assignment[p]];
            const nlohmann::json::iterator fieldsIt = entry.find("fields");
            if (fieldsIt == entry.end() || !fieldsIt->is_object())
            {
                continue;
            }
            const nlohmann::json base = SerializeComponent(*parts[p]);
            const nlohmann::json* baseFields = ComponentEntryFields(base);
            if (baseFields == nullptr)
            {
                continue;
            }
            for (nlohmann::json::iterator it = fieldsIt->begin(); it != fieldsIt->end();)
            {
                const nlohmann::json::const_iterator baseIt = baseFields->find(it.key());
                if (baseIt != baseFields->end() && *baseIt == it.value())
                {
                    it = fieldsIt->erase(it);
                }
                else
                {
                    ++it;
                }
            }
            if (fieldsIt->empty())
            {
                entry.erase(fieldsIt);
            }
        }
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
        const std::string_view typeName = PartTypeName(comp);
        if (const Component* named = baseline.FindComponentByName(comp.Name()))
        {
            if (PartTypeName(*named) == typeName)
            {
                return named;
            }
        }
        for (const Component* part : baseline.Components())
        {
            if (part != nullptr && PartTypeName(*part) == typeName)
            {
                return part;
            }
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
        const FieldDesc* field = FindField(comp, fieldName);
        if (field == nullptr)
        {
            return false;
        }
        return field->type != FieldType::ObjectRef && field->type != FieldType::ComponentRef;
    }

    bool WriteFieldToArchetype(const Component& comp, std::string_view fieldName)
    {
        const Actor* owner = comp.Owner();
        if (owner == nullptr || !IsArchetypeField(comp, fieldName))
        {
            return false;
        }
        const std::string_view className = owner->ClassName();
        if (className.empty())
        {
            return false; // 素の Actor には種類が無い
        }
        const std::optional<nlohmann::json> value = FieldValue(comp, fieldName);
        if (!value.has_value())
        {
            return false;
        }

        ArchetypeLibrary& library = ArchetypeLibrary::Get();
        nlohmann::json archetype = nlohmann::json::object();
        if (const nlohmann::json* current = library.Find(className))
        {
            archetype = *current;
        }
        nlohmann::json& entries = ObjectJsonComponents(archetype);

        // 部品の件は名前と型で引く。無ければ値だけを持つ件を足す。コンストラクタが積む部品は件が無くても居る
        const std::string_view typeName = PartTypeName(comp);
        nlohmann::json* target = nullptr;
        for (nlohmann::json& entry : entries)
        {
            if (ComponentEntryType(entry) == typeName && ComponentEntryName(entry) == comp.Name())
            {
                target = &entry;
                break;
            }
        }
        if (target == nullptr)
        {
            nlohmann::json entry = MakeComponentEntry(typeName);
            SetComponentEntryName(entry, comp.Name());
            entries.push_back(std::move(entry));
            target = &entries.back();
        }
        (*target)["fields"][std::string{fieldName}] = *value;
        library.Set(className, std::move(archetype));
        return true;
    }
} // namespace NS::Obj
