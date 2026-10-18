# Test only synthetic loopback fixture output and lifecycle; never PCMan or PTT.
$ErrorActionPreference = 'Stop'
$checks = 0
function Assert-Check([bool]$Condition, [string]$Why) {
    ++$script:checks
    if (-not $Condition) { throw "FAIL: $Why" }
}
$fixture = Join-Path $PSScriptRoot 'Start-TerminalBoundaryFixture.ps1'
foreach ($mode in @('skip-resize', 'resize', 'quit')) {
    $reservation = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
    $reservation.Start()
    $port = $reservation.LocalEndpoint.Port
    $reservation.Stop()
    $runner = [powershell]::Create()
    $client = [Net.Sockets.TcpClient]::new()
    $capture = [IO.MemoryStream]::new()
    try {
        [void]$runner.AddScript({
            param($FixturePath, $FixturePort, $Mode)
            function Read-Host([string]$Prompt) {
                if ($Mode -eq 'quit') { return 'Q' }
                if ($Mode -eq 'resize' -and $Prompt.StartsWith('Enter new height')) { return '32' }
                return ''
            }
            & $FixturePath -Port $FixturePort -Width 80 -Rows 24
        }).AddArgument($fixture).AddArgument($port).AddArgument($mode)
        $running = $runner.BeginInvoke()
        $deadline = [DateTime]::UtcNow.AddSeconds(12)
        while (-not $client.Connected) {
            if ([DateTime]::UtcNow -ge $deadline -or $running.IsCompleted) { throw 'Fixture did not listen in time.' }
            try { $client.Connect([Net.IPAddress]::Loopback, $port) }
            catch { $client.Dispose(); $client = [Net.Sockets.TcpClient]::new(); Start-Sleep -Milliseconds 20 }
        }
        $stream = $client.GetStream()
        $buffer = New-Object byte[] 4096
        while (-not $running.IsCompleted -or $stream.DataAvailable) {
            if ([DateTime]::UtcNow -ge $deadline) { throw 'Fixture did not finish in time.' }
            if ($stream.DataAvailable) {
                $count = $stream.Read($buffer, 0, $buffer.Length)
                $capture.Write($buffer, 0, $count)
            } else { Start-Sleep -Milliseconds 10 }
        }
        $output = ($runner.EndInvoke($running) | Out-String)
        Assert-Check (-not $runner.HadErrors) 'fixture completes without exceptions'
        $bytes = [Text.Encoding]::ASCII.GetString($capture.ToArray())
        $esc = [string][char]27
        Assert-Check ($bytes.Contains("${esc}[?7l")) 'OFF command is actually sent'
        Assert-Check ($bytes.Contains('ABCDEFG')) 'right-edge OFF sample is complete'
        Assert-Check (-not $output.Contains('ABCDEFG')) 'terminal payload is not printed to console'
        Assert-Check (-not $output.Contains('PASS:')) 'script does not claim automatic GUI acceptance'
        if ($mode -ne 'quit') {
            Assert-Check ($bytes.Contains("${esc}[?7hABCD${esc}[31mEFG")) 'ON and color sample sent in order'
            Assert-Check ($bytes.Contains("${esc}[2;22rHOME")) 'DECSTBM sample has no extra CUP before HOME'
            Assert-Check ($bytes.Contains("${esc}[24;20H`nOUTSIDE")) 'below-region LF sample is sent'
            Assert-Check ($bytes.Contains("${esc}[rRESET HOME")) 'reset sample does not mask home behavior'
            if ($mode -eq 'resize') {
                Assert-Check ($bytes.Contains("${esc}[32;1H`r`nRESIZE BOTTOM")) 'resize uses requested new height'
                Assert-Check (-not $output.Contains('SKIP E:')) 'resize path is not reported as skipped'
            } else { Assert-Check ($output.Contains('SKIP E:')) 'blank resize answer is explicitly skipped'
            }
        } else { Assert-Check (-not $bytes.Contains("${esc}[?7h")) 'quit stops before following steps' }
    } finally {
        $client.Dispose(); $capture.Dispose(); $runner.Stop(); $runner.Dispose()
    }
    $probe = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, $port)
    try { $probe.Start(); Assert-Check $true 'listener released after completion or quit' }
    finally { $probe.Stop() }
}
Write-Output "PASS: $checks terminal boundary fixture checks (not GUI acceptance)"
