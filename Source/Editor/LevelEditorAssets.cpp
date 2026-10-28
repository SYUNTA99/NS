#include "Editor/LevelEditorController.h"
#include "NSlib/Core/Assert.h"
#include "NSlib/Core/Types.h"
#include "NSlib/CoreObject/Reflection/ReflectedClassObjs.h"

#include "Editor/AssetPicker.h"
#include "Editor/Undo/ActorSnapshotCommand.h"
#include "Game/Level/LaunchEffectSettings.h"
#include "Game/Player/ChargeEffectSettings.h"
#include "Game/Player/ImpactEffectSettings.h"
#include "NSlib/App/Application.h"
#include "NSlib/Core/Logger.h"
#include "NSlib/Core/Windows/Filesystem.h"
#include "NSlib/CoreObject/ActorInitInfo.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Asset/AssetManager.h"
#include "NSlib/CoreObject/Asset/EffectAsset.h"
#include "NSlib/CoreObject/Asset/EffectSet.h"
#include "NSlib/CoreObject/Asset/Material.h"
#include "NSlib/CoreObject/Asset/Mesh.h"
#include "NSlib/CoreObject/Asset/Texture.h"
#include "NSlib/CoreObject/ClassObjs/Effect.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"
#include "NSlib/CoreObject/ClassObjs/HitSensorsObj.h"
#include "NSlib/CoreObject/ClassObjs/Model.h"
#include "NSlib/CoreObject/Reflection/ActorFile.h"
#include "NSlib/CoreObject/Reflection/ClassObjEntry.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"
#include "NSlib/CoreObject/Reflection/ReflectionJson.h"
#include "NSlib/CoreObject/Scene/Scene.h"
#include "NSlib/CoreObject/Scene/SceneJson.h"
#include "NSlib/Gfx/D3DTexture.h"

#include <charconv>
#include <optional>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace
{
    // 部品の欄の値を JSON で読む。欄が無ければ値なしを返す
    nlohmann::json FieldJson(const NS::Object& comp, std::string_view fieldName)
    {
        const nlohmann::json fields = NS::SerializeClassObjFields(&comp);
        const nlohmann::json::const_iterator it = fields.find(std::string{fieldName});
        if (it == fields.end())
        {
            return nullptr;
        }
        return *it;
    }

    // name の .actor を document に差し替えてファイルへ書き、name を指すアクターと name の派生を指すアクターの全部へ、
    // 全部の値を写し直す。シーンで上書きした項目はアクターの値が残る
    // 直し終えた時・取り消し・やり直しの全部がこの 1 本を通る
    void WriteActorFile(NS::Scene& scene, const std::string& name, const nlohmann::json& document)
    {
        NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
        if (!library.Set(name, document))
        {
            return;
        }
        if (!library.Save(name))
        {
            NS_LOG_WARN(App, ".actor {} をファイルへ書けなかった。今の起動の間だけ効く", name);
        }
        for (NS::SceneActor* actor : scene.Actors())
        {
            const nlohmann::json* actorFile = library.Find(actor->ActorFileName());
            if (actorFile == nullptr || (actor->ActorFileName() != name && NS::ActorFileBase(*actorFile) != name))
            {
                continue;
            }
            NS::ApplyActorFile(actor);
            actor->ReloadAssets();
            // 束とセンサーの値はアクターを作る時に .actor から読むので、読み直させて次の当たりに効かせる
            if (NS::HitSensorsObj* sensors = actor->HitSensorsObjClassObj())
            {
                sensors->ReloadValues(actor);
            }
            if (NS::HitReaction* reaction = actor->HitReactionClassObj())
            {
                reaction->ReloadSets(actor);
            }
        }
    }

    // baseName の .actor から作る、name の派生の .actor の文書。何も書かず、baseName が派生ならその元を指す
    // name が空・ファイルの名前に使えない字を含む・既にある時と、baseName が読まれていない時は WARN を出して
    // std::nullopt
    std::optional<nlohmann::json> NewDerivedActorFile(std::string_view baseName, std::string_view name)
    {
        NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
        if (name.empty() || name.find_first_of("\\/:*?\"<>|.") != std::string_view::npos)
        {
            NS_LOG_WARN(App, "派生の .actor の名前 \"{}\" はファイルの名前に使えないので作らない", name);
            return std::nullopt;
        }
        if (library.Find(name) != nullptr)
        {
            NS_LOG_WARN(App, ".actor {} は既にあるので派生を作らない", name);
            return std::nullopt;
        }
        const nlohmann::json* baseFile = library.Find(baseName);
        if (baseFile == nullptr)
        {
            NS_LOG_WARN(App, ".actor {} が読まれていないので派生 {} を作らない", baseName, name);
            return std::nullopt;
        }
        // 派生は元を 1 つだけ受け継ぎ、部品は元と同じなので、派生から作る時もその元から作り、派生の値は写さない
        std::string_view origin = baseName;
        if (!NS::ActorFileBase(*baseFile).empty())
        {
            origin = NS::ActorFileBase(*baseFile);
        }
        nlohmann::json derived = nlohmann::json::object();
        NS::SetActorFileClass(derived, NS::ActorFileClass(*baseFile));
        NS::SetActorFileBase(derived, origin);
        NS::ActorJsonClassObjs(derived) = nlohmann::json::object();
        return derived;
    }

    // name の .actor を document で足してファイルへ書く。読めない文書か書けなければ足さずに false
    bool AddActorFile(std::string_view name, const nlohmann::json& document)
    {
        NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
        // 読めない訳は Set が ERROR で出している
        if (!library.Set(name, document))
        {
            return false;
        }
        if (!library.Save(name))
        {
            NS_LOG_WARN(App, ".actor {} をファイルへ書けなかったので足さない", name);
            library.Erase(name);
            return false;
        }
        return true;
    }

    // nlohmann::json::diff が作る 1 つの操作 (add・remove・replace) を document に当てられるか
    // add は親が要り、配列の親なら位置が末尾以下。remove と replace は当てる所が要る
    bool CanApplyPatchOperation(const nlohmann::json& document, const nlohmann::json& operation)
    {
        const nlohmann::json::json_pointer path{operation.at("path").get<std::string>()};
        if (operation.at("op") != "add")
        {
            return document.contains(path);
        }
        if (path.empty())
        {
            return true;
        }
        const nlohmann::json::json_pointer parentPath = path.parent_pointer();
        if (!document.contains(parentPath))
        {
            return false;
        }
        const nlohmann::json& parent = document[parentPath];
        if (parent.is_object())
        {
            return true;
        }
        if (!parent.is_array())
        {
            return false;
        }
        const std::string& last = path.back();
        if (last == "-")
        {
            return true;
        }
        std::size_t index = 0;
        const std::from_chars_result parsed = std::from_chars(last.data(), last.data() + last.size(), index);
        return parsed.ec == std::errc{} && parsed.ptr == last.data() + last.size() && index <= parent.size();
    }

    // .actor の 1 欄の書き換え。アクターでないので IActorSnapshotApplier を使わず、シーンと前後の差分を自分で持つ
    // 戻す・やり直す時は、その時の文書に差分だけを当てる。動作の窓は取り消しの履歴を通さずに文書を書くので、
    // 文書の全部を戻すと窓の直しが消える
    class ActorFileCommand final : public NS::Editor::ICommand
    {
    public:
        ActorFileCommand(NS::Scene& scene, std::string name, const nlohmann::json& before, const nlohmann::json& after)
            : m_scene(scene), m_name(std::move(name)), m_redo(nlohmann::json::diff(before, after)),
              m_undo(nlohmann::json::diff(after, before))
        {
            m_bytes = sizeof(*this) + m_name.size() + m_redo.dump().size() + m_undo.dump().size();
        }

        void Do(NS::Editor::IActorSnapshotApplier&) noexcept override { Apply(m_redo); }
        void Undo(NS::Editor::IActorSnapshotApplier&) noexcept override { Apply(m_undo); }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return m_bytes; }

    private:
        void Apply(const nlohmann::json& patch) noexcept
        {
            const nlohmann::json* current = NS::ActorFileLibrary::Get().Find(m_name);
            if (current == nullptr)
            {
                NS_LOG_WARN(App, ".actor {} が読まれていないので、取り消しかやり直しを当てない", m_name);
                return;
            }
            nlohmann::json document = *current;
            // 例外を切ってあるので、当てられない差分を patch に渡すと止まる。1 つずつ確かめて当てる
            for (const nlohmann::json& operation : patch)
            {
                if (!CanApplyPatchOperation(document, operation))
                {
                    // 間に別の書き換えが同じ所を消した。書かずに今の文書を残す
                    NS_LOG_WARN(App, ".actor {} の取り消しかやり直しを当てられなかった: {}", m_name, operation.dump());
                    return;
                }
                document = document.patch(nlohmann::json::array({operation}));
            }
            WriteActorFile(m_scene, m_name, document);
        }

        NS::Scene& m_scene;
        std::string m_name;
        nlohmann::json m_redo;
        nlohmann::json m_undo;
        std::size_t m_bytes = 0;
    };

    void WriteEffectSet(NS::Scene* scene, const std::string& id, const nlohmann::json& document)
    {
        NS::AssetManager& assets = NS::AssetManager::Get();
        NS::EffectSet* set = assets.LoadEffectSet(id);
        if (!set->IsValid() || !set->Replace(set->Path(), document))
        {
            return;
        }
        if (!assets.SaveEffectSet(*set))
        {
            NS_LOG_ERROR(App, "演出セット {} を保存できない", id);
        }
        if (scene == nullptr)
        {
            return;
        }
        std::vector<const NS::EffectAsset*> references;
        for (const std::pair<const std::string, std::unique_ptr<NS::Object>>& section : set->Sections())
        {
            for (const NS::ReflectionInfo* info = section.second->GetReflection(); info != nullptr; info = info->base)
            {
                for (const NS::FieldDesc& field : std::span{info->fields, info->fieldCount})
                {
                    if (field.assetKind != NS::AssetKind::Effect)
                    {
                        continue;
                    }
                    std::string assetId;
                    field.get(section.second.get(), &assetId);
                    references.push_back(assets.LoadEffectAsset(assetId));
                }
            }
        }
        for (NS::SceneActor* actor : scene->Actors())
        {
            NS::Effect* effect = actor->GetEffect();
            if (effect == nullptr || effect->Definition() != set)
            {
                continue;
            }
            for (const NS::EffectAsset* asset : references)
            {
                effect->Preload(scene->GetEffectScene(), asset);
            }
        }
    }

    class EffectSetCommand final : public NS::Editor::ICommand
    {
    public:
        EffectSetCommand(NS::Scene* scene, std::string id, nlohmann::json before, nlohmann::json after)
            : m_scene(scene), m_id(std::move(id)), m_before(std::move(before)), m_after(std::move(after))
        {
            m_bytes = sizeof(*this) + m_id.size() + m_before.dump().size() + m_after.dump().size();
        }
        void Do(NS::Editor::IActorSnapshotApplier&) noexcept override { Apply(m_after); }
        void Undo(NS::Editor::IActorSnapshotApplier&) noexcept override { Apply(m_before); }
        [[nodiscard]] std::size_t EstimatedBytes() const noexcept override { return m_bytes; }

    private:
        void Apply(const nlohmann::json& document) { WriteEffectSet(m_scene, m_id, document); }
        NS::Scene* m_scene;
        std::string m_id;
        nlohmann::json m_before;
        nlohmann::json m_after;
        std::size_t m_bytes = 0;
    };

    [[nodiscard]] NS::Object* LoadSelectableAsset(std::string_view path)
    {
        if (NS::Editor::IsMaterialAssetPath(path))
        {
            return NS::AssetManager::Get().LoadMaterial(path);
        }
        if (NS::Editor::IsModelAssetPath(path))
        {
            return NS::AssetManager::Get().LoadMesh(path);
        }
        if (NS::Editor::IsTextureAssetPath(path))
        {
            const std::optional<std::string> resolved = NS::ResolveContentPath(std::string{path});
            if (resolved)
            {
                return NS::AssetManager::Get().LoadTexture(*resolved);
            }
        }
        if (NS::Editor::IsEffectAssetPath(path))
        {
            NS::Object* asset = NS::AssetManager::Get().LoadEffectFile(path);
            if (NS::EffectSet* set = NS::Cast<NS::EffectSet>(asset); set != nullptr && set->IsValid())
            {
                const nlohmann::json document = set->Document();
                if (document["settings"].contains(GL::Player::ChargeEffectSettings::StaticReflection()->typeName))
                {
                    set->Settings<GL::Player::ChargeEffectSettings>();
                }
                if (document["settings"].contains(GL::Player::ImpactEffectSettings::StaticReflection()->typeName))
                {
                    set->Settings<GL::Player::ImpactEffectSettings>();
                }
                if (document["settings"].contains(GL::Level::LaunchEffectSettings::StaticReflection()->typeName))
                {
                    set->Settings<GL::Level::LaunchEffectSettings>();
                }
            }
            return asset;
        }
        return nullptr;
    }

    [[nodiscard]] bool IsReadable(const NS::Object& asset)
    {
        if (const NS::EffectSet* set = NS::Cast<NS::EffectSet>(&asset))
        {
            return set->IsValid();
        }
        if (const NS::EffectAsset* effect = NS::Cast<NS::EffectAsset>(&asset))
        {
            return effect->IsValid();
        }
        if (const NS::Material* material = NS::Cast<NS::Material>(&asset))
        {
            return !material->IsEmpty();
        }
        if (const NS::Mesh* mesh = NS::Cast<NS::Mesh>(&asset))
        {
            return !mesh->IsEmpty();
        }
        if (const NS::Texture* texture = NS::Cast<NS::Texture>(&asset))
        {
            return texture->D3D()->IsValid() && !texture->D3D()->IsUsingFallback();
        }
        return false;
    }
} // namespace

void LevelEditorController::MirrorPlayEditToBaseline(const NS::SceneActor* actor,
                                                     const NS::ClassObjInfo& row,
                                                     std::string_view fieldName)
{
    NS_ASSERT(App, actor != nullptr, "MirrorPlayEditToBaseline: アクターが null");
    // 写すのはプレイ中だけ。編集モードで写すと次のプレイ突入の捕捉と二重管理になる
    if (m_mode != Mode::Play || !m_playBaseline)
    {
        return;
    }
    const std::size_t index = NS::FindActorIndexById(*m_playBaseline, actor->Id());
    if (index == NS::k_NoActorIndex)
    {
        return;
    }
    const NS::Object* part = row.get(actor);
    if (part == nullptr)
    {
        return;
    }
    // .actor へ書く欄はプレイを始めた時の JSON
    // が持たない。写すと編集へ戻る時にシーンに置いたアクターの値として捨てられる
    const NS::FieldDesc* field = NS::FindField(part->GetReflection(), fieldName);
    if (field == nullptr || NS::FieldDestinationOf(actor, *field) != NS::FieldDestination::Scene)
    {
        return;
    }
    nlohmann::json& baselineParts = NS::ActorJsonClassObjs(NS::SceneJsonActors(*m_playBaseline)[index]);
    // 上書きの一覧から外した項目は、JSON からも消す。残すと編集へ戻る時に上書きとして読み直される
    if (field->type != NS::FieldType::ActorRef && !actor->IsSceneOverride(row.key, fieldName))
    {
        const nlohmann::json::iterator baselinePart = baselineParts.find(row.key);
        if (baselinePart != baselineParts.end())
        {
            baselinePart->erase(std::string{fieldName});
        }
        return;
    }
    const nlohmann::json fields = NS::SerializeClassObjFields(part);
    const nlohmann::json::const_iterator value = fields.find(std::string{fieldName});
    if (value == fields.end())
    {
        return;
    }
    baselineParts[std::string{row.key}][std::string{fieldName}] = *value;
}

const nlohmann::json& LevelEditorController::PlayBaseline() const
{
    NS_ASSERT(App, m_playBaseline.has_value(), "PlayBaseline: プレイ中でないので、プレイを始めた時の JSON が無い");
    return *m_playBaseline;
}

void LevelEditorController::NoteFieldEdit(const NS::SceneActor* actor,
                                          const NS::ClassObjInfo& row,
                                          std::string_view fieldName)
{
    NS_ASSERT(App, actor != nullptr, "NoteFieldEdit: アクターが null");
    const NS::Object* part = row.get(actor);
    if (part == nullptr)
    {
        return;
    }
    const NS::FieldDesc* field = NS::FindField(part->GetReflection(), fieldName);
    if (field == nullptr)
    {
        return;
    }
    if (NS::FieldDestinationOf(actor, *field) == NS::FieldDestination::Scene)
    {
        // 保存は上書きの一覧の項目だけを書くので、直したら一覧に足す。参照の欄はいつも書く
        NS::SceneActor* edited = nullptr;
        if (m_scene != nullptr)
        {
            edited = m_scene->Actors().FindByActorId(actor->Id());
        }
        if (edited != nullptr && field->type != NS::FieldType::ActorRef)
        {
            edited->AddSceneOverride(row.key, fieldName);
        }
        MirrorPlayEditToBaseline(actor, row, fieldName);
        return;
    }
    for (const ActorFileEdit& edit : m_actorFileEdits)
    {
        if (edit.actorId == actor->Id() && edit.row == &row && edit.fieldName == fieldName)
        {
            return;
        }
    }
    m_actorFileEdits.push_back(ActorFileEdit{actor->Id(), &row, std::string{fieldName}});
}

void LevelEditorController::NoteActorFileFieldEdit(const NS::ClassObjInfo& row, std::string_view fieldName)
{
    if (m_actorFileDraft == nullptr)
    {
        return;
    }
    const NS::Object* part = row.get(m_actorFileDraft.get());
    if (part == nullptr)
    {
        return;
    }
    const NS::FieldDesc* field = NS::FindField(part->GetReflection(), fieldName);
    if (field == nullptr || field->type == NS::FieldType::ActorRef)
    {
        return;
    }
    m_assetEdited = true;
    for (const ActorFileEdit& edit : m_actorFileEdits)
    {
        if (edit.actorId == NS::k_NoActorId && edit.row == &row && edit.fieldName == fieldName)
        {
            return;
        }
    }
    m_actorFileEdits.push_back(ActorFileEdit{NS::k_NoActorId, &row, std::string{fieldName}});
}

void LevelEditorController::CommitActorFileEdits()
{
    std::vector<ActorFileEdit> edits = std::move(m_actorFileEdits);
    m_actorFileEdits.clear();
    if (m_scene == nullptr)
    {
        return;
    }
    NS::ActorFileLibrary& library = NS::ActorFileLibrary::Get();
    for (const ActorFileEdit& edit : edits)
    {
        // 選んだ .actor の 1 体は作り直さずに読む。作り直すと直した値が文書の値へ戻る
        const NS::SceneActor* actor = m_actorFileDraft.get();
        if (edit.actorId != NS::k_NoActorId)
        {
            actor = m_scene->Actors().FindByActorId(edit.actorId);
        }
        if (actor == nullptr)
        {
            continue;
        }
        const NS::Object* part = edit.row->get(actor);
        if (part == nullptr)
        {
            continue;
        }
        const NS::FieldDesc* field = NS::FindField(part->GetReflection(), edit.fieldName);
        if (field == nullptr)
        {
            continue;
        }
        // 共有の項目は派生を指すアクターでも元へ書く。派生へ書くと、派生を読む時に書けない項目として断られる
        // 選んだ .actor の 1 体はシーンに居ないので、グローバルの項目はその .actor 自身へ書く
        NS::FieldDestination destination = NS::FieldDestinationOf(actor, *field);
        if (destination == NS::FieldDestination::Scene)
        {
            destination = NS::FieldDestination::DerivedActorFile;
        }
        const std::string name = NS::ActorFileToWrite(actor, destination);
        const nlohmann::json* current = library.Find(name);
        if (current == nullptr)
        {
            NS_LOG_WARN(App, ".actor {} が読まれていないので、欄 {} を書かない", name, edit.fieldName);
            continue;
        }
        const nlohmann::json before = *current;
        nlohmann::json after = before;
        NS::ActorJsonClassObjs(after)[std::string{edit.row->key}][edit.fieldName] = FieldJson(*part, edit.fieldName);
        if (after == before)
        {
            continue;
        }
        // .actor は配置の姿でないので、プレイ中に直した物も積む。編集へ戻った後に戻せる
        m_editor.Undo().Push(std::make_unique<ActorFileCommand>(*m_scene, name, before, after), m_applier);
    }
}

void LevelEditorController::WriteActorFileHitReactions(std::string_view actorFileName,
                                                       const std::vector<NS::HitReactionSet>& sets)
{
    PushActorFileChange(
        actorFileName, [&sets](nlohmann::json& document) { NS::WriteHitReactionSets(document, sets); }, "束");
}

void LevelEditorController::WriteActorFileHitSensors(std::string_view actorFileName, const NS::HitSensorsObj& sensors)
{
    PushActorFileChange(
        actorFileName, [&sensors](nlohmann::json& document) { sensors.WriteValues(document); }, "センサーの値");
}

void LevelEditorController::PushActorFileChange(std::string_view actorFileName,
                                                const std::function<void(nlohmann::json&)>& change,
                                                std::string_view what)
{
    if (m_scene == nullptr)
    {
        return;
    }
    const std::string name{actorFileName};
    const nlohmann::json* current = NS::ActorFileLibrary::Get().Find(name);
    if (current == nullptr)
    {
        NS_LOG_WARN(App, ".actor {} が読まれていないので、{}を書かない", name, what);
        return;
    }
    nlohmann::json before = *current;
    nlohmann::json after = before;
    change(after);
    if (after == before)
    {
        return;
    }
    // .actor は配置の姿でないので、プレイ中に直した物も積む。編集へ戻った後に戻せる
    m_editor.Undo().Push(std::make_unique<ActorFileCommand>(*m_scene, name, before, after), m_applier);
}

bool LevelEditorController::CreateDerivedActorFile(std::string_view baseName, std::string_view name)
{
    const std::optional<nlohmann::json> document = NewDerivedActorFile(baseName, name);
    if (!document.has_value())
    {
        return false;
    }
    return AddActorFile(name, *document);
}

bool LevelEditorController::DeriveActorFileFromActor(NS::uint32 actorId, std::string_view name)
{
    if (!IsSceneEditable())
    {
        NS_LOG_WARN(App, "編集モードでないので、アクターから派生 {} を作らない", name);
        return false;
    }
    const NS::SceneActor* actor = m_scene->Actors().FindByActorId(actorId);
    if (actor == nullptr)
    {
        NS_LOG_WARN(App, "アクター {} が居ないので派生 {} を作らない", actorId, name);
        return false;
    }
    std::optional<nlohmann::json> document = NewDerivedActorFile(actor->ActorFileName(), name);
    std::optional<nlohmann::json> before = m_applier.CaptureActor(actorId);
    if (!document.has_value() || !before.has_value())
    {
        return false;
    }
    // 保存の JSON はシーンの上書きと参照の欄だけを持つ。派生に書ける項目を移し、残りはシーンに残す
    nlohmann::json after = *before;
    NS::ForEachReflectedClassObj(actor, [&](const NS::ClassObjInfo& row, const NS::Object* part) {
        nlohmann::json* fields = NS::ClassObjFields(after, row.key);
        if (fields == nullptr)
        {
            return;
        }
        for (nlohmann::json::iterator entry = fields->begin(); entry != fields->end();)
        {
            const NS::FieldDesc* field = NS::FindField(part->GetReflection(), entry.key());
            if (field == nullptr || !NS::CanWriteField(NS::FieldDestination::DerivedActorFile, *field))
            {
                ++entry;
                continue;
            }
            NS::ActorJsonClassObjs(*document)[std::string{row.key}][entry.key()] = entry.value();
            entry = fields->erase(entry);
        }
    });
    if (!AddActorFile(name, *document))
    {
        return false;
    }
    NS::SetActorJsonFile(after, name);
    m_editor.Undo().Push(
        std::make_unique<NS::Editor::ActorSnapshotCommand>(actorId, std::move(before), std::move(after)), m_applier);
    RefreshGizmoSelectables();
    ResolveSelectionFromId();
    return true;
}

void LevelEditorController::FinishFieldWrite(NS::SceneActor* actor, NS::FieldType writtenType)
{
    NS_ASSERT(App, actor != nullptr, "FinishFieldWrite: アクターが null");
    // 部品は ActorRef の指す相手を取得して持つので、欄を書き換えた時に取得し直す
    if (writtenType == NS::FieldType::ActorRef)
    {
        actor->ResolveActorRefs();
        return;
    }
    // 素材の欄は道を書くだけで、描くメッシュの差し替え・描くマテリアル・メッシュの当たりの形は読み直しで作る
    // 素材でない欄で読み直すと、ドラッグの間じゅう骨の差し直しとメッシュの当たりの作り直しが毎回走る
    if (writtenType != NS::FieldType::Asset)
    {
        return;
    }
    actor->ReloadAssets();
}

bool LevelEditorController::ApplyMaterialToSelected(std::string_view matPath)
{
    NS::Application* app = NS::Application::Get();
    if (!IsSceneEditable() || !GizmoToolActive() || app == nullptr)
    {
        return false;
    }

    if (m_selectedActorId == NS::k_NoActorId)
    {
        return false;
    }
    NS::SceneActor* go = SelectedActor();
    if (go == nullptr)
    {
        return false;
    }

    NS::Model* mesh = go->ModelClassObj();
    if (mesh == nullptr)
    {
        return false;
    }

    // .mat パスは ContentRoot 相対で持つ。材質の正データは欄の名前なので、名前を書いて欄から読み直す
    const std::optional<std::string> stored =
        NS::OS::FileSystem::RelativeUnder(NS::OS::FileSystem::ContentRoot(), matPath);
    if (!stored)
    {
        NS_LOG_ERROR(App, "マテリアル {} が ContentRoot の外にあるので付けない", matPath);
        return false;
    }
    const NS::Material* material = app->Assets().LoadMaterial(*stored);
    if (material->IsEmpty())
    {
        return false;
    }

    // 差替前を忠実に写す。matRef を実体へ書き込み、差替後との差分を 取り消し 履歴へ積む
    std::optional<nlohmann::json> before = m_applier.CaptureActor(m_selectedActorId);

    mesh->SetMaterialRef(*stored);
    FinishFieldWrite(go, NS::FieldType::Asset);

    std::optional<nlohmann::json> after = m_applier.CaptureActor(m_selectedActorId);
    if (before && after && *before != *after)
    {
        m_editor.Undo().Record(
            std::make_unique<NS::Editor::ActorSnapshotCommand>(m_selectedActorId, std::move(before), std::move(after)));
    }
    return true;
}

void LevelEditorController::SelectAsset(std::string_view assetPath)
{
    std::optional<std::string> stored = NS::OS::FileSystem::RelativeUnder(NS::OS::FileSystem::ContentRoot(), assetPath);
    if (!stored)
    {
        NS_LOG_ERROR(App, "素材 {} が ContentRoot の外にあるので選ばない", assetPath);
        return;
    }

    ClearAssetSelection();
    SelectActorById(NS::k_NoActorId);

    // .actor は名前で引く。読めなかった .actor は持っていないので選ばない
    if (NS::Editor::IsActorFileAssetPath(*stored))
    {
        if (NS::ActorFileLibrary::Get().Find(NS::OS::FileSystem::Stem(*stored)) == nullptr)
        {
            NS_LOG_WARN(App, ".actor {} を読めていないので選ばない", *stored);
            return;
        }
        m_selectedAssetPath = std::move(*stored);
        return;
    }
    const NS::Object* asset = LoadSelectableAsset(*stored);
    if (asset == nullptr || !IsReadable(*asset))
    {
        NS_LOG_WARN(App, "素材 {} を読めないので選ばない", *stored);
        return;
    }
    m_selectedAssetPath = std::move(*stored);
}

bool LevelEditorController::IsAssetSelected(std::string_view assetPath) const
{
    if (m_selectedAssetPath.empty())
    {
        return false;
    }
    return NS::OS::FileSystem::RelativeUnder(NS::OS::FileSystem::ContentRoot(), assetPath) == m_selectedAssetPath;
}

NS::Object* LevelEditorController::SelectedAsset()
{
    if (m_selectedAssetPath.empty())
    {
        return nullptr;
    }
    if (NS::Editor::IsActorFileAssetPath(m_selectedAssetPath))
    {
        return SelectedActorFile();
    }
    return LoadSelectableAsset(m_selectedAssetPath);
}

void LevelEditorController::ClearAssetSelection()
{
    CommitAssetEdit();
    m_selectedAssetPath.clear();
    m_actorFileDraft.reset();
}

NS::SceneActor* LevelEditorController::SelectedActorFile()
{
    const std::string name = NS::OS::FileSystem::Stem(m_selectedAssetPath);
    const nlohmann::json* document = NS::ActorFileLibrary::Get().Find(name);
    if (document == nullptr)
    {
        m_actorFileDraft.reset();
        return nullptr;
    }
    if (m_actorFileDraft == nullptr || m_actorFileDraft->ActorFileName() != name || m_actorFileDraftSource != *document)
    {
        m_actorFileDraftSource = *document;
        const NS::ActorInitInfo info{.actorFile = name};
        m_actorFileDraft = NS::CreateActorOfClass(NS::ActorFileClass(m_actorFileDraftSource), info);
    }
    return m_actorFileDraft.get();
}

void LevelEditorController::MarkAssetEdited() noexcept
{
    m_assetEdited = true;
}

void LevelEditorController::MarkEffectSetEdited(const nlohmann::json& before)
{
    if (!m_assetEdited)
    {
        m_effectSetEditBefore = before;
    }
    m_assetEdited = true;
}

void LevelEditorController::CommitAssetEdit()
{
    if (!m_assetEdited)
    {
        return;
    }
    m_assetEdited = false;

    if (NS::Editor::IsActorFileAssetPath(m_selectedAssetPath))
    {
        CommitActorFileEdits();
        // 直した 1 体は書いた文書と同じ値だが、作った時の文書と比べると取り消しで元の文書へ戻った事を見落とす。
        // 次に引いた時に今の文書から作り直させる
        m_actorFileDraftSource = nlohmann::json{};
        return;
    }
    if (NS::EffectSet* set = NS::Cast<NS::EffectSet>(SelectedAsset()))
    {
        const nlohmann::json after = set->Document();
        if (m_effectSetEditBefore.is_object() && m_effectSetEditBefore != after)
        {
            m_editor.Undo().Record(
                std::make_unique<EffectSetCommand>(m_scene, set->Id(), m_effectSetEditBefore, after));
        }
        m_effectSetEditBefore = nlohmann::json{};
        WriteEffectSet(m_scene, set->Id(), after);
        return;
    }
    NS::Material* material = NS::Cast<NS::Material>(SelectedAsset());
    if (material == nullptr)
    {
        return;
    }
    NS::AssetManager& assets = NS::AssetManager::Get();
    // 共有のマテリアルは書く先が無いので、絵も作り直さない。Inspector は見るだけにしている
    if (!assets.MakeMaterialFileJson(*material))
    {
        return;
    }
    // 作り直せなくても値は .mat に残す。作り直しの失敗は Rebuild が報告する
    (void)assets.RebuildMaterial(*material);
    const std::optional<std::string> path = NS::ResolveContentPath(m_selectedAssetPath);
    if (!path)
    {
        return;
    }
    (void)NS::Editor::WriteMaterialFile(assets, *material, *path);
}

namespace NS
{
    namespace Editor
    {
        bool WriteMaterialFile(const AssetManager& assets, const Material& material, std::string_view filePath)
        {
            const std::optional<std::string> text = assets.MakeMaterialFileJson(material);
            if (!text)
            {
                NS_LOG_WARN(
                    App, "マテリアル {} は .mat から読んだ物でないか、書けない値を持つので書かない", material.Path());
                return false;
            }
            const std::span<const std::byte> bytes = std::as_bytes(std::span<const char>{text->data(), text->size()});
            if (!OS::FileSystem::ReplaceAllBytes(filePath, bytes))
            {
                NS_LOG_WARN(App, "マテリアル {} を {} に書けないので前の .mat を残す", material.Path(), filePath);
                return false;
            }
            return true;
        }
    } // namespace Editor
} // namespace NS
