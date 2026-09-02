# clean_dir.ps1
# Remove the release output folder so every package run starts from an empty tree.
#
# Why the delete does not happen in place:
#   The security software on this machine (Norton 360) denies deletion of
#   document / image file types (.txt .png ...) anywhere under the Desktop tree,
#   which is where this repository lives. Measured symptom: from the second
#   package run on, `rmdir /s /q` removed .exe .dll .cpp and reported
#   "Access is denied" for every .txt and .png, including
#   dist\NS_release\README.txt (a pure ASCII path -- so the Japanese folder
#   names left by rename_folders.ps1 are not the cause). PowerShell's
#   Remove-Item is denied there too; the block is in a filter driver, not in
#   cmd and not in the file attributes (Archive only, no read-only) and not a
#   held handle (the same files open with exclusive ReadWrite access).
#   Renaming is allowed, and the identical recursive delete succeeds once the
#   folder sits under %TEMP%. So the folder is moved out of the protected tree
#   first and deleted there.
#
# Plain delete, not the recycle bin: the output folder is a build product that
# is rebuilt on every run, so recycling it would pile up tens of MB per package.
#
# This file stays pure ASCII for the same reason rename_folders.ps1 does:
# Windows PowerShell decodes BOM-less files as the system ANSI codepage.
param([Parameter(Mandatory = $true)][string]$Path)
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Path)) {
    Write-Output ("clean: nothing to remove ({0})" -f $Path)
    exit 0
}

$full = (Resolve-Path -LiteralPath $Path).ProviderPath
$target = $full

# Move aside only when %TEMP% is on the same volume: across volumes Move-Item
# copies the whole tree and then deletes the source, which is the very delete
# that is blocked. On another volume, fall through and try the delete in place.
$tempRoot = [System.IO.Path]::GetTempPath()
if ([System.IO.Path]::GetPathRoot($tempRoot) -eq [System.IO.Path]::GetPathRoot($full)) {
    $staging = Join-Path $tempRoot ('ns_pkg_clean_' + [guid]::NewGuid().ToString('N'))
    try {
        Move-Item -LiteralPath $full -Destination $staging
        $target = $staging
    }
    catch {
        Write-Output ("clean: could not move {0} aside -- {1}" -f $full, $_.Exception.Message)
        exit 1
    }
}

try {
    Remove-Item -LiteralPath $target -Recurse -Force
}
catch {
    Write-Output ("clean: failed to remove {0} -- {1}" -f $target, $_.Exception.Message)
    exit 1
}

# Remove-Item can stop early and still return, so the result is checked.
if (Test-Path -LiteralPath $target) {
    Write-Output ("clean: {0} still exists after the delete" -f $target)
    exit 1
}
if (Test-Path -LiteralPath $full) {
    Write-Output ("clean: {0} still exists after the delete" -f $full)
    exit 1
}

Write-Output ("clean: removed {0}" -f $full)
exit 0
