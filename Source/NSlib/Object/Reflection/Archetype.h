#pragma once

// 種類ごとの既定値 (アーキタイプ)
// 値は 3 段で重なる。コードの既定値 < 種類の既定値 (1 クラスに JSON 1 つ) < 個体の上書き
// 種類の既定値は、クラスのコンストラクタが積まない部品を足すこともできる。個体のデータは値だけを上書きし、部品は足せない
// 保存・プレイ開始時の凍結・undo の控えは、種類の既定値と違う欄だけを書く

#include "NSlib/Object/ObjectJson.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace NS::Obj
{
    class Actor;
    class Component;

    //! @brief クラス名から種類の既定値を引く置き場
    //! @details 1 クラスにつき Directory()/<クラス名>.json を 1 つ持つ。形は配置物の JSON から id と位置を除いた物
    //! {"class": クラス名, "parts": {部品名: {欄の名前: 値}, ...}}
    //! 参照の欄 (ActorRef) はシーンの中の相手を指すので、読む時に落として種類の既定値には持たせない
    //! 初めて引いた時に Directory() の *.json を全て読む
    class ArchetypeLibrary
    {
    public:
        [[nodiscard]] static ArchetypeLibrary& Get();

        ~ArchetypeLibrary();

        //! 読み書きするディレクトリ。既定は ContentRoot/Assets/Archetypes
        [[nodiscard]] const std::string& Directory();

        //! ディレクトリを差し替えて読み直す。試しは一時ディレクトリへ向ける
        void SetDirectory(std::string directory);

        //! Directory() の *.json を全て読み直す。読めないファイルはログを出して飛ばす
        void Reload();

        //! className の種類の既定値。無ければ nullptr
        [[nodiscard]] const nlohmann::json* Find(std::string_view className);

        //! className の種類の既定値を差し替える。ファイルへは書かない。参照の欄は落とす
        void Set(std::string_view className, nlohmann::json archetype);

        //! className の種類の既定値を消す。ファイルは消さない
        void Erase(std::string_view className);

        //! className の種類の既定値を Directory() へ書く。無いか書けなければ false
        [[nodiscard]] bool Save(std::string_view className);

        //! @brief className のクラスをコードの既定値で作り、種類の既定値を当てた 1 体
        //! @details 個体の上書きが無い時の姿。保存の差分と、インスペクタの上書きの印が比べる相手
        //! シーンには入れないので、開始も更新も描画も走らない。種類の既定値を変えると作り直すので、参照は持ち越さない
        [[nodiscard]] const Actor& Baseline(std::string_view className);

    private:
        ArchetypeLibrary();
        // まだ読んでいなければ Directory() を読む
        void EnsureLoaded();

        std::string m_directory;                                                // 空なら既定のディレクトリ
        std::map<std::string, nlohmann::json, std::less<>> m_archetypes;        // クラス名から種類の既定値
        std::map<std::string, std::unique_ptr<Actor>, std::less<>> m_baselines; // クラス名から既定の 1 体
        bool m_loaded = false;
    };

    //! actor のクラスの種類の既定値を当てる。足す部品を作り、欄の値を写す
    void ApplyArchetype(Actor& actor);

    //! @brief 個体の上書きだけを持つ配置物の JSON を、全欄を持つ姿へ広げる
    //! @details 部品の並びと欄は Baseline に揃え、個体の上書きと id・名前・有効を重ねる。個体にしか無い部品は落とす
    //! 実体を作り直さずに値を書き戻す経路 (undo の適用) が、上書きの無い欄も種類の既定値へ戻すために使う
    [[nodiscard]] nlohmann::json ExpandObjectJson(const nlohmann::json& object);

    //! @brief 全欄を持つ配置物の JSON から、種類の既定値と同じ欄を除く
    //! @details 欄が全部既定と同じ部品も、空の件として残す。位置・回転・拡縮の部品 Transform は個体の物なので除かない
    [[nodiscard]] nlohmann::json DiffObjectJson(const nlohmann::json& object);

    //! comp に対応する、持ち主のクラスの Baseline の部品。持ち主が無いか対応が無ければ nullptr
    //! 種類の既定値を変えると指す先が作り直されるので、そのフレームの間だけ使う
    [[nodiscard]] const Component* FindBaselinePart(const Component& comp);

    //! 部品 comp の欄 fieldName が、持ち主のクラスの種類の既定値と違うか
    [[nodiscard]] bool IsFieldOverridden(const Component& comp, std::string_view fieldName);

    //! 種類の既定値へ上げてよい欄か。参照の欄と位置・回転・拡縮は個体の物なので上げられない
    //! 持ち主の無い部品も、部品名が引けないので上げられない
    [[nodiscard]] bool IsArchetypeField(const Component& comp, std::string_view fieldName);

    //! @brief 部品 comp の欄 fieldName の今の値を、持ち主のクラスの種類の既定値へ書く
    //! @details ファイルへは書かない。上げられない欄と持ち主の無い部品は何もせず false
    [[nodiscard]] bool WriteFieldToArchetype(const Component& comp, std::string_view fieldName);
} // namespace NS::Obj
