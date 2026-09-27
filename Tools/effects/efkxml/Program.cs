// 編集ソフトの中核 (EffekseerCore.dll、1.80.7) を画面なしで呼び、エフェクトの中身を XML で見る道具
// 編集ソフトの bin の場所は --tool-bin で受ける。efkbuild.py が環境変数 NS_EFFEKSEER_TOOL から渡す
//
//   efkxml --tool-bin <bin> dump  <入力 .efkefc|.efkproj> <出力 .xml>
//       入力を編集ソフトと同じ読み方で開き、既定から動いた値だけを XML に書き出す
//
//   efkxml --tool-bin <bin> check <定義 .efkproj> <書き出した .efkefc>
//       1. 定義に書いた値を持つ要素が、編集ソフトに読まれた後も同じ値で残っているか
//          残っていない葉は、綴りの誤りで読み飛ばされたか、既定と同じ値を書いたかのどちらか。どちらも失敗にする
//       2. 定義を開いて書き出した XML と、.efkefc を開いて書き出した XML が一字一句同じか
//          同じなら、.efkefc の編集用の中身は定義どおり
//       失敗は終了コード 1
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Reflection;
using System.Text;
using System.Xml;

internal static class Program
{
    private static string s_toolBin = "";

    private static int Main(string[] args)
    {
        Console.OutputEncoding = new UTF8Encoding(false);
        if (args.Length != 5 || args[0] != "--tool-bin")
        {
            Console.Error.WriteLine("使い方: efkxml --tool-bin <編集ソフトの bin> dump <入力 .efkefc|.efkproj>"
                                    + " <出力 .xml>");
            Console.Error.WriteLine("        efkxml --tool-bin <編集ソフトの bin> check <定義 .efkproj>"
                                    + " <書き出した .efkefc>");
            return 2;
        }
        s_toolBin = Path.GetFullPath(args[1]);
        if (!File.Exists(Path.Combine(s_toolBin, "EffekseerCore.dll")))
        {
            Console.Error.WriteLine("編集ソフトの bin に EffekseerCore.dll が無い: " + s_toolBin);
            return 2;
        }
        AppDomain.CurrentDomain.AssemblyResolve += (sender, e) =>
        {
            string candidate = Path.Combine(s_toolBin, new AssemblyName(e.Name).Name + ".dll");
            if (File.Exists(candidate))
            {
                return Assembly.LoadFrom(candidate);
            }
            return null;
        };

        string command = args[2];
        if (command == "dump")
        {
            return Dump(Path.GetFullPath(args[3]), Path.GetFullPath(args[4]));
        }
        if (command == "check")
        {
            return Check(Path.GetFullPath(args[3]), Path.GetFullPath(args[4]));
        }
        Console.Error.WriteLine("efkxml の命令が分からない: " + command + " (dump か check を渡す)");
        return 2;
    }

    private static bool s_initialized = false;

    private static void InitializeCore()
    {
        if (s_initialized)
        {
            return;
        }
        // 編集ソフトの起動と同じく、言語の表を先に読む。無いと Initialize が落ちる
        // 末尾の区切りまで含めた道を渡す。区切りの無い道は試していない
        Effekseer.LanguageTable.CreateTableFromDirectory(s_toolBin + Path.DirectorySeparatorChar);
        Effekseer.Core.Initialize("ja");
        s_initialized = true;
    }

    // 編集ソフトで開き直し、既定から動いた値だけの XML にする。開けなければ null
    private static XmlDocument LoadAsXml(string path)
    {
        InitializeCore();
        if (!Effekseer.Core.LoadFrom(path))
        {
            Console.Error.WriteLine("編集ソフトの中核が開けなかった: " + path);
            return null;
        }
        return Effekseer.Core.SaveAsXmlDocument(Effekseer.Core.Root);
    }

    private static string ToText(XmlDocument document)
    {
        XmlWriterSettings settings = new XmlWriterSettings
        {
            Indent = true,
            Encoding = new UTF8Encoding(false),
            NewLineChars = "\n",
        };
        using (MemoryStream stream = new MemoryStream())
        {
            using (XmlWriter writer = XmlWriter.Create(stream, settings))
            {
                document.Save(writer);
            }
            return Encoding.UTF8.GetString(stream.ToArray());
        }
    }

    private static int Dump(string input, string output)
    {
        XmlDocument document = LoadAsXml(input);
        if (document == null)
        {
            return 1;
        }
        File.WriteAllText(output, ToText(document), new UTF8Encoding(false));
        Console.WriteLine("書き出した: " + output);
        return 0;
    }

    private static int Check(string source, string compiled)
    {
        XmlDocument sourceRaw = new XmlDocument();
        sourceRaw.Load(source);

        XmlDocument sourceLoaded = LoadAsXml(source);
        if (sourceLoaded == null)
        {
            return 1;
        }
        string sourceText = ToText(sourceLoaded);

        XmlDocument compiledLoaded = LoadAsXml(compiled);
        if (compiledLoaded == null)
        {
            return 1;
        }
        string compiledText = ToText(compiledLoaded);

        int failures = 0;

        Dictionary<string, string> loadedLeaves = CollectLeaves(sourceLoaded.DocumentElement);
        Dictionary<string, string> writtenLeaves = CollectLeaves(sourceRaw.DocumentElement);
        int checkedLeaves = 0;
        foreach (KeyValuePair<string, string> written in writtenLeaves)
        {
            // IsLoop は編集ソフトの再生を繰り返すかのフラグ。開くと常に True になり、実行側にも渡らない
            if (written.Key == "EffekseerProject/IsLoop[0]")
            {
                continue;
            }
            ++checkedLeaves;
            if (!loadedLeaves.TryGetValue(written.Key, out string loadedValue))
            {
                Console.WriteLine("失敗: 定義の葉が読まれた後に無い (綴りの誤りか、既定と同じ値): "
                                  + written.Key + " = " + written.Value);
                ++failures;
                continue;
            }
            if (!SameValue(written.Value, loadedValue))
            {
                Console.WriteLine("失敗: 定義の葉の値が変わった: " + written.Key + " 定義 " + written.Value
                                  + " → 読まれた値 " + loadedValue);
                ++failures;
            }
        }
        Console.WriteLine("定義の葉 " + checkedLeaves + " 個を照合した");

        if (sourceText != compiledText)
        {
            Console.WriteLine("失敗: 定義を開いた XML と .efkefc を開いた XML が食い違う");
            string[] a = sourceText.Split('\n');
            string[] b = compiledText.Split('\n');
            int shown = 0;
            for (int i = 0; i < Math.Max(a.Length, b.Length) && shown < 20; ++i)
            {
                string left = LineOrNone(a, i);
                string right = LineOrNone(b, i);
                if (left != right)
                {
                    Console.WriteLine("  " + (i + 1) + " 行目: 定義 [" + left.Trim() + "] / efkefc ["
                                      + right.Trim() + "]");
                    ++shown;
                }
            }
            ++failures;
        }
        else
        {
            Console.WriteLine("定義を開いた XML と .efkefc を開いた XML は一致 ("
                              + sourceText.Split('\n').Length + " 行)");
        }

        if (failures > 0)
        {
            Console.WriteLine("結果: 失敗 " + failures + " 件");
            return 1;
        }
        Console.WriteLine("結果: 合格");
        return 0;
    }

    private static string LineOrNone(string[] lines, int index)
    {
        if (index < lines.Length)
        {
            return lines[index];
        }
        return "(無し)";
    }

    // 子の要素を持たない要素を、「親からの道/名前[同名の何番目か]」から値への表に集める
    private static Dictionary<string, string> CollectLeaves(XmlElement root)
    {
        Dictionary<string, string> leaves = new Dictionary<string, string>();
        Walk(root, root.Name, leaves);
        return leaves;
    }

    private static void Walk(XmlElement element, string path, Dictionary<string, string> leaves)
    {
        Dictionary<string, int> seen = new Dictionary<string, int>();
        bool hasChildElement = false;
        foreach (XmlNode child in element.ChildNodes)
        {
            if (child.NodeType != XmlNodeType.Element)
            {
                continue;
            }
            XmlElement childElement = (XmlElement)child;
            hasChildElement = true;
            seen.TryGetValue(childElement.Name, out int index);
            seen[childElement.Name] = index + 1;
            Walk(childElement, path + "/" + childElement.Name + "[" + index + "]", leaves);
        }
        if (!hasChildElement && element.InnerText.Length > 0)
        {
            leaves[path] = element.InnerText.Trim();
        }
    }

    private static bool SameValue(string written, string loaded)
    {
        if (written == loaded)
        {
            return true;
        }
        bool writtenIsNumber = double.TryParse(written, NumberStyles.Float, CultureInfo.InvariantCulture, out double a);
        bool loadedIsNumber = double.TryParse(loaded, NumberStyles.Float, CultureInfo.InvariantCulture, out double b);
        if (writtenIsNumber && loadedIsNumber)
        {
            // 編集ソフトは float で持つので、書いた 10 進の値と最後の桁がずれることがある
            return Math.Abs(a - b) <= 1e-5 * Math.Max(1.0, Math.Abs(a));
        }
        return string.Equals(written, loaded, StringComparison.OrdinalIgnoreCase);
    }
}
