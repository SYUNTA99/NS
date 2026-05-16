# PowerShell script to migrate STL includes to common headers
param(
    [string]$Path = "source/engine",
    [switch]$DryRun
)

$stlCommonIncludes = @(
    '<cstdint>', '<cstddef>', '<cstdlib>', '<cassert>', '<cstring>', '<cmath>',
    '<memory>', '<utility>', '<string>', '<string_view>',
    '<vector>', '<algorithm>', '<format>', '<source_location>'
)

$stlContainersIncludes = @(
    '<unordered_map>', '<unordered_set>', '<map>', '<set>',
    '<array>', '<deque>', '<queue>', '<span>', '<optional>', '<variant>'
)

$stlThreadingIncludes = @(
    '<atomic>', '<mutex>', '<shared_mutex>', '<thread>', '<condition_variable>', '<future>'
)

$stlMetaIncludes = @(
    '<type_traits>', '<typeindex>', '<concepts>', '<functional>', '<tuple>'
)

$files = Get-ChildItem -Path $Path -Recurse -Include "*.h","*.cpp" | Where-Object { $_.FullName -notmatch "\external\\" }

foreach ($file in $files) {
    $content = Get-Content $file.FullName -Raw -Encoding UTF8
    $modified = $false
    $needsStlCommon = $false
    $needsStlContainers = $false
    $needsStlThreading = $false
    $needsStlMeta = $false
    
    # Check what includes are present
    foreach ($inc in $stlCommonIncludes) {
        if ($content -match "#include\s*$([regex]::Escape($inc))") {
            $needsStlCommon = $true
            break
        }
    }
    foreach ($inc in $stlContainersIncludes) {
        if ($content -match "#include\s*$([regex]::Escape($inc))") {
            $needsStlContainers = $true
            break
        }
    }
    foreach ($inc in $stlThreadingIncludes) {
        if ($content -match "#include\s*$([regex]::Escape($inc))") {
            $needsStlThreading = $true
            break
        }
    }
    foreach ($inc in $stlMetaIncludes) {
        if ($content -match "#include\s*$([regex]::Escape($inc))") {
            $needsStlMeta = $true
            break
        }
    }
    
    if (-not ($needsStlCommon -or $needsStlContainers -or $needsStlThreading -or $needsStlMeta)) {
        continue
    }
    
    Write-Host "Processing: $($file.Name)"
    Write-Host "  Needs: stl_common=$needsStlCommon, stl_containers=$needsStlContainers, stl_threading=$needsStlThreading, stl_meta=$needsStlMeta"
}
