param([Parameter(Mandatory=$true)][string]$OutputPath)
$ErrorActionPreference = 'Stop'
$parts = @()
$selections = @{
    'Lite/TelnetConn.cpp' = @(
        'void CTelnetConn::OnText(', 'inline void CTelnetConn::ProcessAnsiEscapeSequence(',
        'inline void CTelnetConn::SetCurrentAttributes(', 'void CTelnetConn::GotoXY(',
        'void CTelnetConn::SetScrollRange(', 'void CTelnetConn::LineFeed()', 'void CTelnetConn::GoRight(',
        'char* CTelnetConn::ResizeLine(', 'void CTelnetConn::ReSizeBuffer(',
        'void CTelnetConn::CreateBuffer(', 'ArticleProgress::Progress CTelnetConn::GetArticleProgress(')
    'Lite/TelnetConn.h' = @(
        'inline LPBYTE CTelnetConn::GetLineAttr(const char *line)',
        'inline LPBYTE CTelnetConn::GetLineAttr(const int line)',
        'inline void CTelnetConn::InitNewLine(', 'inline char* CTelnetConn::AllocNewLine(',
        'inline int CTelnetConn::GetLineBufLen()', 'inline int CTelnetConn::GetLineBufLen(int ')
}
foreach ($file in @('Lite/TelnetConn.cpp', 'Lite/TelnetConn.h')) {
    $source = [Text.UTF8Encoding]::new($false, $true).GetString(
        [IO.File]::ReadAllBytes((Join-Path $PSScriptRoot "../$file")))
    foreach ($signature in $selections[$file]) {
        $start = $source.IndexOf($signature, [StringComparison]::Ordinal)
        if ($start -lt 0) { throw "Missing production method: $signature" }
        $body = $source.IndexOf('{', $start)
        $depth = 1
        $end = $body + 1
        while ($end -lt $source.Length -and $depth -gt 0) {
            if ($source[$end] -eq '{') { ++$depth }
            if ($source[$end] -eq '}') { --$depth }
            ++$end
        }
        if ($depth -ne 0) { throw "Unclosed production method: $signature" }
        $parts += $source.Substring($start, $end - $start)
    }
    if ($file.EndsWith('.h')) {
        $start = $source.IndexOf("`tvoid ClearAllFlags()", [StringComparison]::Ordinal)
        $end = $source.IndexOf("`r`n`t}", $start, [StringComparison]::Ordinal)
        if ($end -lt 0) { $end = $source.IndexOf("`n`t}", $start, [StringComparison]::Ordinal) }
        if ($start -lt 0 -or $end -lt 0) { throw 'Missing connection reset method' }
        $body = $source.IndexOf('{', $start)
        $parts += 'void CTelnetConn::ClearAllFlags() ' + $source.Substring($body, $end - $body) + "`r`n}"
    }
}
$fullPath = [IO.Path]::GetFullPath($OutputPath)
[void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($fullPath))
[IO.File]::WriteAllText($fullPath, ($parts -join "`r`n"), [Text.UTF8Encoding]::new($false))
