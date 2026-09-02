#pragma once

#include "Runtime/Core/Math.h"
#include "Runtime/Graphics/DrawItem.h"

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

        //! ソート優先度。距離が同じときは小さい方から先に描く
        int sortPriority = 0;

        //! 半透明オブジェクトとして扱う場合はtrue
        bool transparent = false;

        //! @brief 見えると判定された時に呼ばれ、DrawItem を out へ積むコールバック
        void (*collect)(void* owner, const RenderContext& context, std::vector<DrawItem>& out) = nullptr;

        void* owner = nullptr;
    };

    //! @brief 描画物の一覧を持ち、視錐台カリングで絞ってから描画コマンドを集めて出す
    class RenderScene
    {
    public:
        //! @brief 描画オブジェクトを登録し、識別用のハンドルを返す
        [[nodiscard]] RenderHandle Register(const RenderProxyDesc& desc);

        //! @brief 指定されたハンドルの登録を解除する。無効なハンドルの場合は無視される
        void Unregister(RenderHandle handle) noexcept;

        //! @brief 登録済みの境界とソート情報を差し替える
        void Update(RenderHandle handle,
                    const NS::Core::AABB& bounds,
                    const NS::Core::Vector3& sortCenter,
                    int sortPriority,
                    bool transparent) noexcept;

        //! @brief 視錐台の外を捨て、残った描画物を描く
        //! @param[in] context 描画コンテキスト
        //! @param[in] transparent true なら半透明、false なら不透明を描く
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