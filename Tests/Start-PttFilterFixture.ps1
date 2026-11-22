# Local replay for the PTT display filter. No login or upstream connection.
param(
    [ValidateRange(1,65535)][int]$Port = 22318,
    [ValidateSet('Big5','UTF8')][string]$Encoding = 'Big5'
)
$ErrorActionPreference = 'Stop'
if ($Encoding -eq 'UTF8') { $codec = [Text.Encoding]::UTF8 }
else { $codec = [Text.Encoding]::GetEncoding(950) }
$esc = [char]27
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)

function New-Frame([char]$key) {
    $footer = ' 瀏覽 第 1/1 頁 (100%)  目前顯示: 第 01~23 行'
    $rows = @{
        1 = ' 作者  Alice (reader)                           看板  Test'
        2 = ' 標題  [分類] 含暴雷的標題'
        3 = ' 時間  Sun Nov 22 10:00:00 2026'
        4 = '──────────────────'
        5 = "${esc}[44m中文Ab${esc}[0m"
        7 = '推 Bob: 有暴雷                    11/22 10:00'
        8 = '→ Other: 照常顯示                 11/22 10:01'
        24 = $footer
    }
    switch ($key) {
        'l' {
            $rows = @{
                1='看板《Test》'; 3=' 編號   日 期 作 者       文 章 標 題'
                5='> 123 +爆 11/22 Alice    □ [分類] 平常的標題'
                6='  124 +   11/22 Alice2   □ [暴雷] 平常的標題'
                7='  125 +   11/22 Other    R: [分類] 含暴雷的標題'
                24=' 文章列表 '
            }
        }
        'e' { $rows[1]='編輯文章'; $rows[24]='編輯器'; $rows[5]='正文暴雷' }
        'm' { $rows[1]='寄件者 Alice'; $rows[24]=$footer; $rows[5]='正文暴雷' }
        'u' { $rows[1]='未辨識的畫面'; $rows[24]='其他畫面'; $rows[5]='正文暴雷' }
        { $_ -in 'w','n','p' } {
            $rows[1]=' 作者  Other (reader)                           看板  Another'
            $join = switch ($key) { 'w' { '' } 'n' { "`r`n" } 'p' { "${esc}[6;1H" } }
            $rows[5]=('A' * 78) + '暴' + $join + '雷'
        }
    }
    $frame = [Text.StringBuilder]::new("${esc}[?2026h${esc}[?7h${esc}[2J${esc}[H")
    foreach ($row in ($rows.Keys | Sort-Object)) {
        [void]$frame.Append("${esc}[${row};1H").Append($rows[$row])
    }
    [void]$frame.Append("${esc}[23;79H${esc}[?2026l")
    return $codec.GetBytes($frame.ToString())
}

try {
    $listener.Start()
    Write-Output "127.0.0.1:$Port ($Encoding) — a 文章、l 列表、e 編輯器、m 站內信、u 未知、w 自動折行、n 手動換行、p 游標定位、q 結束"
    $client = $listener.AcceptTcpClient()
    try {
        $stream = $client.GetStream()
        $key = 'a'
        do {
            if ($key -in 'a','l','e','m','u','w','n','p') {
                [byte[]]$frame = New-Frame $key
                $stream.Write($frame, 0, $frame.Length)
            }
            $byte = $stream.ReadByte()
            if ($byte -lt 0) { break }
            $key = [char]$byte
        } while ($key -ne 'q')
    } finally { $client.Dispose() }
} finally { $listener.Stop() }
