#pragma once

/// @file LevelObjects.h
/// @brief レベルのゲーム固有オブジェクト種別ヘルパ
///
/// @details 器は NS::Scene::SceneData で、ここはその中身を「プレイヤー / 追従カメラ /
/// cell ブラシ / 拾得」というこのゲームの語彙で判定・生成・走査するヘルパを集める
/// 種別は専用マーカーでなく components の構成そのもので判定する

#include "Framework/Scene/SceneData.h"

namespace NS::Game::Level
{

    /// 新規レベルでプレイヤーを置く既定の capsule 中心高さ。 床 block 上面 0.5 + capsule 半高 0.9 + 1cm
    inline constexpr float kDefaultPlayerSpawnY = 1.41f;

    /// プレイヤー実体か。 入力で動く能力そのものが種別の印なので、 専用マーカーを増やさず
    /// PlayerInputComponent の有無で判定する
    [[nodiscard]] bool IsPlayerObject(const NS::Scene::ObjectData& object) noexcept;

    /// objects からプレイヤー実体を探す。 最初の 1 件の添字、 無ければ kNoObjectIndex
    /// 複数居ても先頭を正とする。 読込の門が 1 体を保証し、 余分は読込時に警告済み
    [[nodiscard]] std::size_t FindPlayerObjectIndex(const NS::Scene::SceneData& level) noexcept;

    /// 指定 pose のプレイヤー実体 ObjectData を作る。 components は既定構成一式で、
    /// scale は capsule 当たり 0.4/0.9/0.4 に cube mesh の見た目を合わせる値
    [[nodiscard]] NS::Scene::ObjectData MakePlayerObject(const NS::Math::Vector3& position,
                                                         const NS::Math::Quaternion& rotation);

    /// 追従カメラ実体か。 ThirdPersonFollowComponent の有無で判定する
    [[nodiscard]] bool IsFollowCameraObject(const NS::Scene::ObjectData& object) noexcept;

    /// objects から追従カメラ実体を探す。 最初の 1 件の添字、 無ければ kNoObjectIndex
    [[nodiscard]] std::size_t FindFollowCameraObjectIndex(const NS::Scene::SceneData& level) noexcept;

    /// 追従カメラ実体 ObjectData を作る。 追従先の永続 id を Target 参照へ焼く。 0 は未設定
    /// pose は追従で毎フレーム決まるため Transform は既定のまま。 視覚と当たりは持たない
    [[nodiscard]] NS::Scene::ObjectData MakeFollowCameraObject(std::uint32_t targetObjectId);

    /// 配置物の cell 座標 = position を最近接整数へ丸めた値
    [[nodiscard]] std::int16_t ObjectCellX(const NS::Scene::ObjectData& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellY(const NS::Scene::ObjectData& object) noexcept;
    [[nodiscard]] std::int16_t ObjectCellZ(const NS::Scene::ObjectData& object) noexcept;

    /// cell ブラシが置換 / 削除できる配置物か。 プレイヤーとカメラは別経路で扱うため除く
    [[nodiscard]] bool IsCellBrushObject(const NS::Scene::ObjectData& object) noexcept;

    /// cell の x, y, z に一致する最初の cell ブラシ配置物の添字。 プレイヤー / カメラは除く。 無ければ kNoObjectIndex
    [[nodiscard]] std::size_t FindObjectAtCell(const NS::Scene::SceneData& level,
                                               std::int16_t x,
                                               std::int16_t y,
                                               std::int16_t z) noexcept;

    /// cell の x, y, z と rotationStep 0..3 から既定 solid の ObjectData を作る
    /// 既定 solid 一式すなわち cube 描画 + Box 当たりを component として積む
    [[nodiscard]] NS::Scene::ObjectData MakeCellObject(std::int16_t x,
                                                       std::int16_t y,
                                                       std::int16_t z,
                                                       std::uint8_t rotationStep);

    /// object の現在の 90° 回転 step を quaternion から最近接で復元する
    [[nodiscard]] std::uint8_t CellRotationStep(const NS::Scene::ObjectData& object) noexcept;

    /// object の回転を rotationStep に対応する Y 軸 yaw quaternion に設定する
    void SetCellRotationStep(NS::Scene::ObjectData& object, std::uint8_t rotationStep) noexcept;

    /// 拾得種別を返す。 PickupComponent が無ければ -1、 "Pickup Kind" 欠損は 0 でコイン既定
    /// 0=コイン / 1=ゴール。 Blocks の配置物の表示・固形判定と PlayMode のプレイ拾得判定が同じ契約を読む
    [[nodiscard]] int PickupKindOf(const NS::Scene::ObjectData& object) noexcept;

} // namespace NS::Game::Level
