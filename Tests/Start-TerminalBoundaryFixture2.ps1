# Synthetic loopback-only display fixture. No login, echo, credentials or payload logs.
param(
    [ValidateRange(1024,65535)][int]$Port = 22308,
    [ValidateSet(40,80,120,160)][int]$Width = 80,
    [ValidateSet(24,32,48,64)][int]$Rows = 24
)
$ErrorActionPreference = 'Stop'
$listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $Port)
$client = $null
$esc = [char]27
$encoding = [Text.Encoding]::GetEncoding(950)
function Send-Display([string]$Text, [bool]$Bytewise = $false) {
    $bytes = $encoding.GetBytes($Text)
    if ($Bytewise) {
        foreach ($byte in $bytes) { $script:stream.WriteByte($byte); $script:stream.Flush() }
    } else { $script:stream.Write($bytes,0,$bytes.Length); $script:stream.Flush() }
}
function Move-To([int]$Row, [int]$Column = 1) { Send-Display "${esc}[${Row};${Column}H" }
function Show-Grid([int]$Height) {
    Send-Display "${esc}[?7l${esc}[r${esc}[2J${esc}[H"
    for ($row = 1; $row -le $Height; ++$row) {
        Move-To $row
        Send-Display ('ROW {0:D2} - keep outside margins' -f $row)
    }
}
function Confirm-Step([string]$Prompt) {
    return ((Read-Host "$Prompt (Enter: continue; Q: quit)").Trim().ToUpperInvariant() -ne 'Q')
}
try {
    $listener.Start()
    Write-Output "Within 60 seconds, open a NEW PCMan tab: telnet://127.0.0.1:$Port"
    Write-Output "Set this connection to Big5/UAO and exactly ${Width}x${Rows}. Do not enable auto-login, saved credentials, macros or triggers."
    Write-Output 'All Enter/Q replies are entered HERE, not in PCMan. This script does not judge the display automatically.'
    $deadline = [DateTime]::UtcNow.AddSeconds(60)
    while (-not $listener.Pending()) {
        if ([DateTime]::UtcNow -ge $deadline) { throw 'No client connected within 60 seconds.' }
        Start-Sleep -Milliseconds 100
    }
    $client = $listener.AcceptTcpClient()
    $client.NoDelay = $true
    $script:stream = $client.GetStream()
    Send-Display "${esc}[?7l${esc}[r${esc}[2J${esc}[HDECAWM OFF - expect ABCG at right edge"
    Move-To 4; Send-Display 'NEXT ROW MUST STAY UNCHANGED'
    Move-To 3 ($Width - 3); Send-Display 'ABCDEFG' $true
    if (-not (Confirm-Step 'A: row 3 ends ABCG; row 4 unchanged?')) { return }

    Move-To 6 ($Width - 3)
    Send-Display "${esc}[?7hABCD${esc}[31mEFG${esc}[m" $true
    if (-not (Confirm-Step 'B: row 6 ends ABCD; row 7 begins red EFG (one wrap only)?')) { return }

    Show-Grid $Rows
    Move-To 5 10
    $bottom = $Rows - 2
    Send-Display "${esc}[2;${bottom}rHOME"
    if (-not (Confirm-Step 'C1: HOME is at row 1, column 1 without an extra cursor command?')) { return }
    Move-To $bottom
    Send-Display "`r`nNEW BOTTOM"
    if (-not (Confirm-Step "C2: only rows 2..$bottom scrolled; HOME and bottom two rows unchanged?")) { return }
    Move-To $Rows 20
    Send-Display "`nOUTSIDE"
    if (-not (Confirm-Step 'C3: OUTSIDE appears on last screen row; scrolling region unchanged?')) { return }

    Move-To 5 10
    Send-Display "${esc}[rRESET HOME"
    if (-not (Confirm-Step 'D: RESET HOME begins at row 1, column 1 after margins reset?')) { return }

    Show-Grid $Rows
    $top = $Rows - 2
    Send-Display "${esc}[${top};${Rows}r"
    Write-Output 'E: change the CURRENT CONNECTION height in PCMan to a different supported value (24..64). Keep the same width.'
    $newSize = (Read-Host 'Enter new height HERE after resizing; blank skips E; Q quits').Trim()
    if ($newSize.ToUpperInvariant() -eq 'Q') { return }
    if ($newSize -eq '') { Write-Output 'SKIP E: resize not verified.' }
    else {
        $newRows = 0
        if (-not [int]::TryParse($newSize, [ref]$newRows) -or $newRows -lt 24 -or $newRows -gt 64 -or $newRows -eq $Rows) {
            throw 'Resize height must be 24..64 and different from the original; resize check not performed.'
        }
        Move-To 1; Send-Display 'RESIZE TOP MUST SCROLL'
        Move-To $newRows; Send-Display "`r`nRESIZE BOTTOM"
        if (-not (Confirm-Step 'E: the top marker scrolled away, bottom says RESIZE BOTTOM, no stale partial region?')) { return }
    }
    Write-Output 'Manual fixture finished. Visual outcomes must be recorded separately; no automatic PASS is implied.'
    Write-Output 'VCOL is server layout: separately verify real PTT lists at 80/120/160 columns (no posting or mail deletion needed).'
} finally {
    if ($null -ne $client) { $client.Dispose() }
    $listener.Stop()
}
