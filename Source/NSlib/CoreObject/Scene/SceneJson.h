#pragma once

#include "NSlib/Core/Math.h"
#include "NSlib/CoreObject/ActorJson.h"
#include "NSlib/CoreObject/Reflection/Reflection.h"

#pragma warning(push, 0)
#include "ThirdParty/nlohmann/json.hpp"
#pragma warning(pop)

#include "NSlib/Core/Types.h"
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_set>

// 保存・プレイ開始時の控えに使うシーン文書。参照はメモリ上で id、ファイル上で名前を使う
// キーを辞書順で書くため、同じ文書を保存した結果はバイト単位で一致する

namespace NS
{
    //! 既定の空文書を作る。nextObjectId は単調増加し、削除済み id を再利用しない
    [[nodiscard]] nlohmann::json MakeSceneJson();

    //! アクターの JSON の配列。無いか壊れていれば空の配列
    [[nodiscard]] const nlohmann::json& SceneJsonActors(const nlohmann::json& scene) noexcept;
    //! アクターの JSON の配列を返し、無ければ作る
    [[nodiscard]] nlohmann::json& SceneJsonActors(nlohmann::json& scene);

    //! 次に割り当てる永続 id。無ければ 1
    [[nodiscard]] uint32 SceneJsonNextActorId(const nlohmann::json& scene) noexcept;
    void SetSceneJsonNextActorId(nlohmann::json& scene, uint32 nextObjectId);

    //! 次に置くアクターの id。nextObjectId と、アクターの最大の id の次の大きい方
    //! 手編集で nextObjectId がアクターの id より小さいファイルもある。1 より小さくならない
    [[nodiscard]] uint32 NextFreeActorId(const nlohmann::json& scene) noexcept;

    //! シーンの environment の欄。リフレクションの欄の名前を JSON の鍵に使う
    struct SceneEnvironment
    {
        //! skybox cubemap のディレクトリまたは .dds の ContentRoot 配下相対パス。空文字なら skybox を描かない
        std::string skybox;
        //! コースの曲の名前。名前が空なら曲は無い
        SoundName music;
        //! 曲の音量 (0〜1)。曲が効果音より小さく鳴り、動きの音を埋もれさせないようにする。
        //! 0〜1 に収めるのは ClampMusicVolume で、ReadSceneEnvironment とエディタの SetSceneMusic が呼ぶ
        float musicVolume = 0.5f;

        NS_REFLECT_BEGIN(SceneEnvironment, void)
        NS_FIELD(skybox, "skybox", "スカイボックス")
        NS_FIELD(music, "music", "曲")
        NS_FIELD(musicVolume, "musicVolume", "曲の音量")
        NS_REFLECT_END_VALUE()
    };

    //! 曲の音量を 0〜1 に収める。非数と無限大は SceneEnvironment の既定の音量にする
    [[nodiscard]] float ClampMusicVolume(float volume) noexcept;

    //! environment を読む。無い欄と壊れた欄は SceneEnvironment の既定
    [[nodiscard]] SceneEnvironment ReadSceneEnvironment(const nlohmann::json& scene);
    //! environment を丸ごと書き換える
    void WriteSceneEnvironment(nlohmann::json& scene, const SceneEnvironment& environment);
    //! ReadSceneEnvironment の skybox
    [[nodiscard]] std::string SceneJsonSkybox(const nlohmann::json& scene);

    //! objects 配列で「該当無し」を表す添字
    inline constexpr std::size_t k_NoActorIndex = static_cast<std::size_t>(-1);

    //! 永続 id が id のアクターの添字。無ければ k_NoActorIndex、k_NoActorId は常に該当無し
    [[nodiscard]] std::size_t FindActorIndexById(const nlohmann::json& scene, uint32 id) noexcept;

    //! 未割当と重複には新 id を振り、nextObjectId を既存最大 id より先へ進める。重複は先勝ち
    void EnsureUniqueActorIds(nlohmann::json& scene);

    //! 未使用の名前を返す。空なら Object、重複したら _1、_2 と番号を付ける
    [[nodiscard]] std::string MakeUniqueActorName(std::string_view base, const std::unordered_set<std::string>& used);

    //! シーン文書をファイルの JSON 文字列へ直列化する。参照は名前へ直して書く
    [[nodiscard]] std::string SerializeSceneToJson(const nlohmann::json& scene);

    //! ファイルの JSON 文字列をシーン文書へ復元する
    //! parse 失敗・version 不一致・上限超過で false、outScene は空の文書へ戻す
    //! 成功時は id と名前の一意化、名前で書かれた参照の id への変換、宙に浮いた参照と辿れない親の除去まで済ませる
    [[nodiscard]] bool DeserializeSceneFromJson(nlohmann::json& outScene, std::string_view jsonText);

    //! シーン文書をファイルへ書く。要素数 / 出力 size が上限超過なら false + NS_LOG_ERROR
    [[nodiscard]] bool SaveSceneToJsonFile(const nlohmann::json& scene, std::string_view path) noexcept;

    //! ファイルをシーン文書へ読む。失敗時 false + NS_LOG_ERROR、outScene は空の文書へ戻す
    [[nodiscard]] bool LoadSceneFromJsonFile(nlohmann::json& outScene, std::string_view path) noexcept;
} // namespace NS
