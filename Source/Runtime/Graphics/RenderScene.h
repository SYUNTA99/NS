#pragma once

#include "Runtime/Graphics/DrawItem.h"
#include "Runtime/Core/Math.h"

#include <cstdint>
#include <vector>

namespace NS::Graphics
{
    struct RenderContext;

    //! @brief 描画オブジェクトを一意に識別するための登録ハンドル
    struct RenderHandle
    {
        static constexpr std::uint32_t k_InvalidSlot = 0xffffffffu;

        std::uint32_t slot = k_InvalidSlot;
        std::uint32_t generation = 0;

        [[nodiscard]] bool IsValid() const noexcept { return slot != k_InvalidSlot; }
    };

    //! @brief 描画オブジェクトの登録に必要な情報をまとめた記述子
    struct RenderProxyDesc
    {
        //! カリング判定に用いるワールド空間のAABB
        NS::Core::AABB bounds{};

        //! 深度ソート用の中心座標
        NS::Core::Vector3 sortCenter{};

        //! ソート優先度（値が小さいほど手前に優先される）
        int sortPriority = 0;

        //! 半透明オブジェクトとして扱う場合はtrue
        bool transparent = false;

        //! @brief オブジェクトが可視状態と判定された際に、描画コマンド（DrawItem）を生成・追加するコールバック関数
        void (*collect)(void* owner, const RenderContext& context, std::vector<DrawItem>& out) = nullptr;

        void* owner = nullptr;
    };

    //! @brief 描画オブジェクトの登録管理と、視錐台カリングに基づく描画コマンドの収集・発行を行うクラス
    class RenderScene
    {
    public:
        //! @brief 描画オブジェクトを登録し、識別用のハンドルを返す
        [[nodiscard]] RenderHandle Register(const RenderProxyDesc& desc);

        //! @brief 指定されたハンドルの登録を解除する。無効なハンドルの場合は無視される
        void Unregister(RenderHandle handle) noexcept;

        //! @brief 登録済みオブジェクトの境界領域やソート情報を更新する
        void Update(RenderHandle handle,
                    const NS::Core::AABB& bounds,
                    const NS::Core::Vector3& sortCenter,
                    int sortPriority,
                    bool transparent) noexcept;

        //! @brief 視錐台カリングを行い、可視状態のオブジェクトの描画処理を実行する
        //! @param context 描画コンテキスト。
        //! @param transparent trueの場合は半透明オブジェクト、falseの場合は不透明オブジェクト群を処理する
        void DrawBucket(const RenderContext& context, bool transparent);

        //! @brief 現在登録されている有効なオブジェクトの総数を返す
        [[nodiscard]] std::size_t Count() const noexcept;

    private:
        struct Proxy
        {
            RenderProxyDesc desc{};
            std::uint32_t generation = 0;
            bool alive = false;
        };

        [[nodiscard]] bool IsLive(RenderHandle handle) const noexcept;

        std::vector<Proxy> m_proxies;
        std::vector<std::uint32_t> m_freeSlots;
        std::vector<std::uint32_t> m_visibleScratch;
        std::vector<DrawItem> m_drawScratch;
    };

} // namespace NS::Graphics