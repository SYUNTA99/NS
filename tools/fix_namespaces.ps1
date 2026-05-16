$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = "C:/Users/nanat/Desktop/NS-ENGINE/source"
$files = Get-ChildItem -Path $root -Recurse -Include *.h, *.cpp | Where-Object {
    (Get-Content $_.FullName -Raw) -match 'namespace NS::'
}

$count = 0
foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw -Encoding UTF8

    # 3-level: namespace NS::A::B  ->  namespace NS { namespace A { namespace B
    # Opening (with newline + {)
    $content = $content -replace 'namespace NS::(\w+)::(\w+)\r?\n\{', 'namespace NS { namespace $1 { namespace $2 {'

    # 3-level closing comment: } // namespace NS::A::B  ->  }}} // namespace NS::A::B
    $content = $content -replace '^\} // namespace NS::(\w+)::(\w+)', '}}} // namespace NS::$1::$2'

    # 2-level: namespace NS::A  ->  namespace NS { namespace A
    # Opening (with newline + {)
    $content = $content -replace 'namespace NS::(\w+)\r?\n\{', 'namespace NS { namespace $1 {'

    # 2-level closing comment: } // namespace NS::A  ->  }} // namespace NS::A
    $content = $content -replace '^\} // namespace NS::(\w+)', '}} // namespace NS::$1'

    # 1-level inline: namespace NS::detail {  ->  namespace NS { namespace detail {
    $content = $content -replace 'namespace NS::(\w+) \{', 'namespace NS { namespace $1 {'

    # 1-level inline closing: } // namespace NS::detail  ->  }} // namespace NS::detail
    $content = $content -replace '^\} // namespace NS::(\w+)$', '}} // namespace NS::$1'

    [System.IO.File]::WriteAllText($file.FullName, $content, [System.Text.Encoding]::UTF8)
    $count++
    Write-Host "Fixed: $($file.FullName)"
}

Write-Host "`nDone: $count files updated."
