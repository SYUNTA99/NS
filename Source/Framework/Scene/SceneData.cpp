#include "Framework/Scene/SceneData.h"

#include "Framework/Scene/detail/crc32.h"

#include <span>
#include <type_traits>
#include <unordered_set>

namespace NS::Scene
{
    namespace
    {
        /// POD 値を std::byte span として view し CRC32 に流す helper
        template <typename T> std::uint32_t UpdateWith(std::uint32_t crc, const T& value) noexcept
        {
            static_assert(std::is_trivially_copyable_v<T>, "UpdateWith expects trivially copyable type");
            const auto* raw = reinterpret_cast<const std::byte*>(&value);
            return detail::Crc32Update(crc, std::span<const std::byte>(raw, sizeof(T)));
        }

        /// 長さ prefix + 中身バイトで文字列を hash する
        std::uint32_t UpdateWithString(std::uint32_t crc, const std::string& text) noexcept
        {
            crc = UpdateWith(crc, static_cast<std::uint64_t>(text.size()));
            if (!text.empty())
            {
                const auto* raw = reinterpret_cast<const std::byte*>(text.data());
                crc = detail::Crc32Update(crc, std::span<const std::byte>(raw, text.size()));
            }
            return crc;
        }

        /// FieldValue を「名前 + variant tag + 値」の順で hash する。 順序固定で決定的
        /// case の数値は FieldValue::value の変種宣言順に対応する
        /// 変種を増減・並べ替えるときは FieldValue::operator== の switch と必ず一緒に直すこと
        std::uint32_t UpdateWithFieldValue(std::uint32_t crc, const FieldValue& field) noexcept
        {
            crc = UpdateWithString(crc, field.name);
            crc = UpdateWith(crc, static_cast<std::uint8_t>(field.value.index()));
            switch (field.value.index())
            {
            case 0:
                return UpdateWith(crc, std::get<float>(field.value));
            case 1:
                return UpdateWith(crc, std::get<int>(field.value));
            case 2:
                return UpdateWith(crc, std::get<bool>(field.value));
            case 3:
                return UpdateWith(crc, std::get<NS::Math::Vector3>(field.value));
            case 4:
                return UpdateWithString(crc, std::get<std::string>(field.value));
            case 5:
                return UpdateWith(crc, std::get<ObjectRef>(field.value).id);
            default:
                // valueless_by_exception 等の想定外 index。 tag は hash 済なので値は足さない
                return crc;
            }
        }

        /// ObjectData のスカラ部を宣言順で hash し、 続けて components を hash する
        /// component の field は名前順に hash するため、 名前昇順の save→load 正準化を跨いでも CRC が安定する
        std::uint32_t UpdateWithObject(std::uint32_t crc, const ObjectData& object) noexcept
        {
            crc = UpdateWith(crc, object.objectId);
            crc = UpdateWith(crc, object.positionX);
            crc = UpdateWith(crc, object.positionY);
            crc = UpdateWith(crc, object.positionZ);
            crc = UpdateWith(crc, object.rotationX);
            crc = UpdateWith(crc, object.rotationY);
            crc = UpdateWith(crc, object.rotationZ);
            crc = UpdateWith(crc, object.rotationW);
            crc = UpdateWith(crc, object.scaleX);
            crc = UpdateWith(crc, object.scaleY);
            crc = UpdateWith(crc, object.scaleZ);
            crc = UpdateWith(crc, object.materialIndex);

            for (const auto& component : object.components)
            {
                crc = UpdateWithString(crc, component.typeName);
                const std::size_t fieldCount = component.fields.size();
                crc = UpdateWith(crc, static_cast<std::uint64_t>(fieldCount));
                // JSON は field を名前順に正準化するので CRC も名前順で hash する。 field 名は component 内で一意なので
                // heap を使わず「直前より大きい最小名」を順に選んで安定させる。 noexcept で非確保
                const std::string* previousName = nullptr;
                for (std::size_t emitted = 0; emitted < fieldCount; ++emitted)
                {
                    const FieldValue* next = nullptr;
                    for (const auto& field : component.fields)
                    {
                        const bool afterPrevious = previousName == nullptr || field.name > *previousName;
                        if (afterPrevious && (next == nullptr || field.name < next->name))
                            next = &field;
                    }
                    if (next == nullptr)
                        break; // 想定外の重複名は安全側で打ち切る
                    crc = UpdateWithFieldValue(crc, *next);
                    previousName = &next->name;
                }
            }
            return crc;
        }
    } // namespace

    bool FieldValue::operator==(const FieldValue& other) const noexcept
    {
        if (name != other.name || value.index() != other.value.index())
        {
            return false;
        }
        switch (value.index())
        {
        case 0:
            return std::get<float>(value) == std::get<float>(other.value);
        case 1:
            return std::get<int>(value) == std::get<int>(other.value);
        case 2:
            return std::get<bool>(value) == std::get<bool>(other.value);
        case 3:
        {
            const auto& a = std::get<NS::Math::Vector3>(value);
            const auto& b = std::get<NS::Math::Vector3>(other.value);
            return a.x == b.x && a.y == b.y && a.z == b.z;
        }
        case 4:
            return std::get<std::string>(value) == std::get<std::string>(other.value);
        case 5:
            return std::get<ObjectRef>(value) == std::get<ObjectRef>(other.value);
        default:
            // 両者 index 一致を確認済なので、 valueless 同士など想定外 index は等しくないとみなす
            return false;
        }
    }

    std::uint32_t SceneData::ComputeCrc32() const noexcept
    {
        std::uint32_t crc = detail::kCrc32Init;

        // objects の論理 size を先に hash しておくと「append したら CRC 必ず変わる」 を保証できる
        const std::uint64_t objectCount = static_cast<std::uint64_t>(objects.size());
        crc = UpdateWith(crc, objectCount);
        for (const auto& object : objects)
        {
            crc = UpdateWithObject(crc, object);
        }

        const std::uint64_t materialCount = static_cast<std::uint64_t>(materialPaths.size());
        crc = UpdateWith(crc, materialCount);
        for (const auto& materialPath : materialPaths)
        {
            crc = UpdateWithString(crc, materialPath);
        }

        // 環境は見た目を確定する永続データなので、 変化が dirty 検知に必ず出るよう field 単位で hash する
        crc = UpdateWith(crc, environment.lightDirection);
        crc = UpdateWith(crc, environment.lightColor);
        crc = UpdateWith(crc, environment.ambientColor);
        crc = UpdateWithString(crc, environment.skyboxCubemapPath);

        // nextObjectId は意図して hash しない。採番カウンタは undo で巻き戻さないため、入れると
        // 「置いて undo しただけで dirty」が恒久化する。カウンタだけが進んだ状態は保存しなくても
        // 未保存 object への参照が残らず整合が壊れないので、内容の変化検知からは外す

        return detail::Crc32Finalize(crc);
    }

    std::size_t FindObjectIndexById(const SceneData& scene, std::uint32_t id) noexcept
    {
        if (id == kNoObjectId)
            return kNoObjectIndex;
        for (std::size_t i = 0; i < scene.objects.size(); ++i)
        {
            if (scene.objects[i].objectId == id)
                return i;
        }
        return kNoObjectIndex;
    }

    std::uint32_t AllocateObjectId(SceneData& scene) noexcept
    {
        return scene.nextObjectId++;
    }

    void EnsureUniqueObjectIds(SceneData& scene)
    {
        // 先にカウンタを既存最大 id の先へ進め、これから振る id が既存と衝突しないようにする
        for (const ObjectData& object : scene.objects)
        {
            if (object.objectId >= scene.nextObjectId)
                scene.nextObjectId = object.objectId + 1;
        }

        // 未割当は手編集ファイルの object、重複は手編集や複製バグの防波堤。先勝ちで後続へ新 id を振る
        std::unordered_set<std::uint32_t> seen;
        seen.reserve(scene.objects.size());
        for (ObjectData& object : scene.objects)
        {
            if (object.objectId == 0 || !seen.insert(object.objectId).second)
            {
                object.objectId = scene.nextObjectId++;
                seen.insert(object.objectId);
            }
        }
    }

    std::size_t PruneDanglingObjectRefs(SceneData& scene)
    {
        std::unordered_set<std::uint32_t> validIds;
        validIds.reserve(scene.objects.size());
        for (const ObjectData& object : scene.objects)
            validIds.insert(object.objectId);

        std::size_t prunedCount = 0;
        for (ObjectData& object : scene.objects)
        {
            for (ComponentData& component : object.components)
            {
                for (FieldValue& field : component.fields)
                {
                    auto* ref = std::get_if<ObjectRef>(&field.value);
                    if (ref == nullptr || !ref->IsSet() || validIds.contains(ref->id))
                        continue;
                    *ref = ObjectRef{};
                    ++prunedCount;
                }
            }
        }
        return prunedCount;
    }

    std::size_t EstimatedHeapBytes(const ComponentData& component) noexcept
    {
        std::size_t bytes = component.typeName.size();
        for (const FieldValue& field : component.fields)
            bytes += sizeof(FieldValue) + field.name.size();
        return bytes;
    }

    std::size_t EstimatedHeapBytes(const ObjectData& object) noexcept
    {
        std::size_t bytes = object.components.size() * sizeof(ComponentData);
        for (const ComponentData& component : object.components)
            bytes += EstimatedHeapBytes(component);
        return bytes;
    }

    const ComponentData* FindComponentData(const ObjectData& object, std::string_view typeName) noexcept
    {
        for (const ComponentData& component : object.components)
            if (component.typeName == typeName)
                return &component;
        return nullptr;
    }

    const FieldValue* FindField(const ComponentData& component, std::string_view name) noexcept
    {
        for (const FieldValue& field : component.fields)
            if (field.name == name)
                return &field;
        return nullptr;
    }

} // namespace NS::Scene
