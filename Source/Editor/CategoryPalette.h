#pragma once

#include "Editor/GridMath.h"
#include "Editor/PaletteTemplates.h"
#include "Runtime/Core/NonCopyable.h"
#include "Runtime/Object/Scene/SceneData.h"

namespace NS::Platform
{
    class Input;
}
namespace NS::UI
{
    class ImGuiContext;
}

namespace NS::Editor
{
    //! @brief 置くブラシを選ぶツールバー
    //! @details 選択中のスロットを保持し、数字キーでの切り替えとツールバーの描画を行う
    class CategoryPalette : public NS::Core::NonCopyable
    {
    public:
        static constexpr std::size_t k_SlotCount = k_PaletteSlotCount;

        CategoryPalette() noexcept;
        ~CategoryPalette() noexcept = default;

        //! @brief 数字キーを見て、選択中のスロットを切り替える
        //! @note UI側がキーボード入力を要求している場合は、誤操作を防ぐためショートカット入力は無視される
        void TickInput(NS::Platform::Input* input, NS::UI::ImGuiContext* imgui) noexcept;

        //! @brief ツールバーを描く。UI を外したビルドでは何もしない
        //! @param[in] viewRect Scene
        //! ビューのスクリーン矩形。ツールバーはこの矩形の外へ出ないよう毎フレーム位置を固定する
        void Render(const NS::Editor::ViewRect& viewRect) noexcept;

        //! 現在アクティブなスロット番号を取得する
        [[nodiscard]] std::size_t ActiveSlot() const noexcept { return m_activeSlot; }

        //! 選択中のブラシのテンプレートを取得する
        [[nodiscard]] const NS::Object::ObjectData& CurrentTemplate() const noexcept { return m_current.prototype; }

        //! 選択中のブラシの表示名を取得する
        [[nodiscard]] const char* CurrentTemplateName() const noexcept { return m_current.name; }

        //! 選択中のブラシが回転可能なオブジェクトかどうかを返す
        [[nodiscard]] bool CurrentIsRotatable() const noexcept { return m_current.rotatable; }

        //! @brief 配置プレビュー用のスロープ角度を返す
        //! @note キューブのように角度を持たない形状では負の値が返る
        [[nodiscard]] float CurrentSlopeAngleDegrees() const noexcept;

        //! アクティブなスロットを指定した番号に変更する
        void SetActiveSlot(std::size_t slot) noexcept;

        //! 同じスロットをもう一度選んだ時に、形状のバリエーションを切り替える
        void CycleActiveVariant() noexcept;

    private:
        void RefreshCurrentTemplate() noexcept;

        std::size_t m_activeSlot = 0;
        PaletteTemplate m_current{};

        float m_toolbarX = 0.0f;        // ツールバー窓の左上X。Scene ビュー内で自前ドラッグする管理値
        float m_toolbarY = 0.0f;        // 同Y
        bool m_toolbarPlaced = false;   // 初回に Scene 上端中央へ置いたか
        bool m_toolbarDragging = false; // 余白ドラッグで移動中か
    };
} // namespace NS::Editor