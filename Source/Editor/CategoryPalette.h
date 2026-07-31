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
    //! @brief 配置するオブジェクト（ブラシ）を選択するツールバーを管理するクラス。
    //! @details 現在選択されているブラシの状態を保持し、入力による切り替えやUIの描画処理を担う。
    class CategoryPalette : public NS::Core::NonCopyable
    {
    public:
        static constexpr std::size_t k_SlotCount = k_PaletteSlotCount;

        CategoryPalette() noexcept;
        ~CategoryPalette() noexcept = default;

        //! @brief 入力を受け取り、アクティブなスロット（ブラシ）を切り替える
        //! @note UI側がキーボード入力を要求している場合は、誤操作を防ぐためショートカット入力は無視される
        void TickInput(NS::Platform::Input* input, NS::UI::ImGuiContext* imgui) noexcept;

        //! @brief ツールバーのUIを描画する（UI機能が無効な環境では何もしない）
        //! @param viewRect Scene ビューのスクリーン矩形。ツールバーはこの矩形の外へ出ないよう毎フレーム位置を固定する
        void Render(const NS::Editor::ViewRect& viewRect) noexcept;

        //! 現在アクティブなスロット番号を取得する
        [[nodiscard]] std::size_t ActiveSlot() const noexcept { return m_activeSlot; }

        //! 選択中のブラシの元となるデータ（配置用テンプレート）を取得する
        [[nodiscard]] const NS::Object::ObjectData& CurrentTemplate() const noexcept { return m_current.prototype; }

        //! 選択中のブラシの表示名を取得する
        [[nodiscard]] const char* CurrentTemplateName() const noexcept { return m_current.name; }

        //! 選択中のブラシが回転可能なオブジェクトかどうかを返す
        [[nodiscard]] bool CurrentIsRotatable() const noexcept { return m_current.rotatable; }

        //! @brief 配置プレビュー用のスロープ角度を返す
        //! @note 角度を持たない形状（キューブなど）の場合は負の値が返る
        [[nodiscard]] float CurrentSlopeAngleDegrees() const noexcept;

        //! アクティブなスロットを指定した番号に変更する
        void SetActiveSlot(std::size_t slot) noexcept;

        //! 同じスロットが再度選択された際の、形状のバリエーション切り替え処理を行う
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