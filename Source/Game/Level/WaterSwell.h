#pragma once

#include <array>

namespace NS
{
    class Material;
} // namespace NS

namespace GL
{
    namespace Level
    {
        //! うねりの波 1 本。水のシェーダー (Shaders/water.hlsli) の swellWaveA〜C と同じ並び
        struct WaterSwellWave
        {
            float angle = 0.0f;      //!< うねりの向きから回す角度 (ラジアン)
            float wavelength = 0.0f; //!< 波長 (m)。0 以下の波は足さない
            float weight = 0.0f;     //!< うねりの高さに掛ける割合
        };

        //! 水面のうねりの値。水のマテリアル (.mat) の params から読む。全部 0 なら平らな水面
        struct WaterSwell
        {
            float height = 0.0f;     //!< swellHeight
            float speed = 0.0f;      //!< swellSpeed
            float gravity = 0.0f;    //!< swellGravity
            float directionX = 0.0f; //!< swellDirection の x
            float directionZ = 0.0f; //!< swellDirection の y (ワールドの z)
            std::array<WaterSwellWave, 3> waves{};
        };

        //! material の .mat の params から、うねりの値を読む
        //! .mat に無い名前は 0 として読む。シェーダーの初期値は見ないので、水のマテリアルは名前を全部書く
        //! (出荷の water.mat が全部書いている事はテストが見る)
        //! @param[in] material 水面のマテリアル。nullptr と中身の空のマテリアルは平らな水面
        [[nodiscard]] WaterSwell ReadWaterSwell(const NS::Material* material);

        //! 水面の基準の高さからの、うねりの高さ (m)。シェーダーの WaterSwell の height と同じ式
        //! シェーダーは頂点を水平にも寄せるが、その分は見ない。出荷の値で高さの差は数 cm
        //! @param[in] x 調べる位置のワールドの x
        //! @param[in] z 調べる位置のワールドの z
        //! @param[in] timeSeconds シーンの経過秒 (SceneRenderer::SceneTimeSeconds)
        [[nodiscard]] float WaterSwellHeight(const WaterSwell& swell, float x, float z, float timeSeconds) noexcept;
    } // namespace Level
} // namespace GL
