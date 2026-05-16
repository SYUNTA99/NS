$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$root = "C:/Users/nanat/Desktop/NS-ENGINE/source"
$files = Get-ChildItem -Path $root -Recurse -Include *.h, *.cpp | Where-Object {
    (Get-Content $_.FullName -Raw) -match '\} // namespace NS::'
}

$count = 0
foreach ($file in $files) {
    $lines = Get-Content $file.FullName -Encoding UTF8
    $modified = $false

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]

        # 3-level: } // namespace NS::A::B  ->  }}} // namespace NS::A::B
        if ($line -match '^\} // namespace NS::(\w+)::(\w+)') {
            $lines[$i] = $line -replace '^\}', '}}}'
            $modified = $true
        }
        # 2-level: } // namespace NS::A  ->  }} // namespace NS::A
        elseif ($line -match '^\} // namespace NS::(\w+)') {
            $lines[$i] = $line -replace '^\}', '}}'
            $modified = $true
        }
    }

    if ($modified) {
        $lines | Set-Content $file.FullName -Encoding UTF8
        $count++
        Write-Host "Fixed closing: $($file.FullName)"
    }
}

Write-Host "`nDone: $count files fixed."
