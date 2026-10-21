#pragma once

#include "Editor/EditorAppearance.h"
#include "Editor/InspectorReflection.h"
#include "NSlib/Core/NonCopyable.h"
#include "NSlib/CoreObject/ClassObjs/HitReaction.h"

#include "NSlib/Core/Types.h"
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class LevelEditorController;

namespace NS
{
    class ActorList;
    class Material;
    class Mesh;
    class Object;
    namespace Gfx
    {
        class D3DRenderTarget;
        class Skybox;
        struct DrawItem;
    }
    namespace Editor
    {
        //! 選択物の名前・姿勢・部品の値を編集する。改名の適用は描画後に行う
        class InspectorPanel : public NonCopyable
        {
        public:
            InspectorPanel();
            ~InspectorPanel();
            //! インスペクターを 1 枚描く。名前・トランスフォーム・アクターの持ち物の編集は controller へ流す
            void Render(LevelEditorController& editor) noexcept;

        private:
            void DrawSceneSettings(LevelEditorController& editor);
            bool DrawSkyboxPreview(std::string_view path);
            void DrawSelectedAsset(LevelEditorController& editor, Object& asset);
            void DrawMaterialPreview(const Material& material);
            void DrawMeshPreview(const Mesh& mesh);
            void DrawAssetPreview(const Gfx::DrawItem& item);
            //! actorFileName の .actor の名前の束の欄を描き、直し終えた束を .actor へ書く。.actor が無ければ何も描かない
            void DrawHitReactionSets(LevelEditorController& editor, const std::string& actorFileName);

            std::unique_ptr<Gfx::D3DRenderTarget> m_assetPreview;
            std::unique_ptr<Gfx::D3DRenderTarget> m_environmentPreview;
            std::unique_ptr<Gfx::Skybox> m_skyboxPreview;
            std::string m_skyboxSource;
            std::string m_skyboxDraft;
            std::string m_skyboxPreviewPath;
            bool m_skyboxPreviewFailed = false;
            std::string m_previewMeshPath;
            float m_previewYaw = 0.45f;
            float m_previewPitch = -0.15f;
            float m_previewZoom = 1.0f;
            char m_nameBuffer[64]{};       // 最上部の名前欄
            uint32 m_nameId = 0;       // 名前欄が今映している対象。打っていない間は毎回入れ直す
            uint32 m_nameCommitId = 0; // このフレームに確定した名前の対象、0 なら無し
            uint32 m_nameEditId = 0;   // 名前を打ち始めた対象。0 なら打っていない
            char m_fieldFilter[64]{};      // 欄の検索欄。空でない間は名前に含む欄だけを並べる。選択を変えても残す
            EditorAppearance m_appearance; // 欄の行の地の色。シーン設定の下で直す
            // 束の欄が直している写し。直し終えるまでは .actor の文書が変わっても読み直さない
            std::string m_hitSetsActorFile;
            nlohmann::json m_hitSetsSource; // 写しを読んだ時の .actor の hitReactions
            std::vector<HitReactionSet> m_hitSets;
            bool m_hitSetsEditing = false; // 値を変えて、まだ .actor へ書いていない
        };

        //! ActorRef の欄の参照先の候補。PlacedActors の並びと番号で、アクター一覧と同じ表示名を出す
        [[nodiscard]] std::vector<ObjectRefOption> MakeObjectRefOptions(const ActorList& objects);
    } // namespace Editor
} // namespace NS
