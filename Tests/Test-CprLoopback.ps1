# Exercise the production executable, including the socket and the startup charset.
param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$build = Join-Path $PSScriptRoot "../Lite/$Configuration/PCMan"
if (Get-Process -Name PCMan -ErrorAction SilentlyContinue) {
    throw 'Close PCMan before running this test. Its single-instance handler can redirect the connection.'
}
$tempRoot = [IO.Path]::GetTempPath()
$portable = Join-Path $tempRoot ('PCMan-Cpr-' + [guid]::NewGuid())
$ascii = [Text.Encoding]::ASCII
$esc = [char]27
if (-not ('CprTestWindow' -as [type])) {
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class CprTestWindow {
    [DllImport("user32.dll", CharSet=CharSet.Unicode)]
    static extern IntPtr FindWindow(string name, string title);
    [DllImport("user32.dll")]
    static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll")]
    static extern IntPtr SendMessageTimeout(IntPtr window, uint msg, UIntPtr cmd, IntPtr data,
        uint flags, uint timeout, out UIntPtr result);
    [DllImport("user32.dll")] static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] static extern IntPtr GetSubMenu(IntPtr menu, int index);
    [DllImport("user32.dll")] static extern uint GetMenuItemID(IntPtr menu, int index);
    [DllImport("user32.dll")] static extern uint GetMenuState(IntPtr menu, uint item, uint flags);
    static IntPtr Window(int process) {
        IntPtr window = FindWindow("PCMan", null);
        uint owner;
        GetWindowThreadProcessId(window, out owner);
        if (window == IntPtr.Zero || owner != process) throw new Exception("Missing test window");
        return window;
    }
    static bool CheckSubmenu(IntPtr window, IntPtr menu, int index, uint selected) {
        for (int i = 0; i < GetMenuItemCount(menu); i++) {
            if (GetMenuItemID(menu, i) == 33025) {
                UIntPtr result;
                if (SendMessageTimeout(window, 0x117, (UIntPtr)(ulong)menu.ToInt64(), (IntPtr)index, 2, 5000, out result) == IntPtr.Zero)
                    throw new Exception("Charset popup did not respond");
                for (uint id = 33025; id <= 33029; id++) {
                    uint state = GetMenuState(menu, id, 0);
                    if (state == uint.MaxValue || ((state & 8) != 0) != (id == selected))
                        throw new Exception("Incorrect menu state for " + id + ", expected selection " + selected);
                }
                return true;
            }
            IntPtr child = GetSubMenu(menu, i);
            if (child != IntPtr.Zero && CheckSubmenu(window, child, i, selected)) return true;
        }
        return false;
    }
    public static void CheckCharset(int process, uint selected) {
        IntPtr window = Window(process);
        if (!CheckSubmenu(window, GetMenu(window), 0, selected)) throw new Exception("Missing charset menu");
    }
    public static void SelectCharset(int process, uint command) {
        IntPtr window = Window(process);
        UIntPtr result;
        if (SendMessageTimeout(window, 0x111, new UIntPtr(command), IntPtr.Zero, 2, 5000, out result) == IntPtr.Zero)
            throw new Exception("Charset menu did not respond");
    }
}
'@
}
function Test-Report($socket, [byte[]]$request, [string]$expected) {
    $socket.Write($request, 0, $request.Length)
    foreach ($byte in $ascii.GetBytes($expected)) {
        $received = $socket.ReadByte()
        if ($received -ne $byte) { throw "Charset mode $mode expected byte $byte but received $received." }
    }
}
try {
    $config = New-Item -ItemType Directory -Path (Join-Path $portable 'Config') -Force
    foreach ($file in 'PCMan.exe','Portable','B2U','U2B','Config/Toolbar.bmp','Config/Icons.bmp') {
        Copy-Item -LiteralPath (Join-Path $build $file) -Destination (Join-Path $portable $file)
    }
    foreach ($mode in 0,1,2,3,4,5,255) {
        [IO.File]::WriteAllText((Join-Path $config.FullName 'Config.ini'),
            "[General]`r`nsaved_charset=$mode`r`nsave_session=0`r`n", $ascii)
        $app = Start-Process (Join-Path $portable 'PCMan.exe') -WorkingDirectory $portable -PassThru -WindowStyle Hidden
        try {
            if (!$app.WaitForInputIdle(10000)) { throw 'Startup timed out.' }
            $selected = if ($mode -in 1..5) { 33024 + $mode } else { 33025 }
            [CprTestWindow]::CheckCharset($app.Id, $selected)
            [CprTestWindow]::SelectCharset($app.Id, 33029)
            [CprTestWindow]::CheckCharset($app.Id, 33029)
            Write-Output "$Configuration startup charset $mode passed before connecting."
        } finally {
            if (!$app.HasExited) { $app.Kill(); $app.WaitForExit() }
            $app.Dispose()
        }
    }
    foreach ($mode in 2,5) {
        $utf8 = $mode -eq 5
        $column = if ($utf8) { 2 } else { 3 }
        [IO.File]::WriteAllText((Join-Path $config.FullName 'Config.ini'),
            "[General]`r`nsaved_charset=$mode`r`nsave_session=0`r`nmultiple_instance=0`r`n", $ascii)
        $listener = [Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback, 0)
        $app = $null
        $client = $null
        $second = $null
        $forwarder = $null
        try {
            $listener.Start()
            $port = $listener.LocalEndpoint.Port
            $accept = $listener.AcceptTcpClientAsync()
            $app = Start-Process (Join-Path $portable 'PCMan.exe') -ArgumentList "telnet://127.0.0.1:$port" -WorkingDirectory $portable -PassThru -WindowStyle Hidden
            if (!$accept.Wait(10000)) { throw "No loopback connection in charset mode $mode." }
            $client = $accept.Result
            $stream = $client.GetStream()
            $stream.ReadTimeout = 5000
            # The second report also checks for a stray terminator after the first.
            $request = [byte[]](@(13,196,161) + $ascii.GetBytes("${esc}[6n${esc}[?2026h${esc}[4;11H${esc}[?6n${esc}[6n"))
            Test-Report $stream $request "${esc}[1;${column}R${esc}[4;11R"
            # End the frame only after the live cursor reply arrived at the server.
            $endFrame = $ascii.GetBytes("${esc}[?2026l")
            $stream.Write($endFrame,0,$endFrame.Length)
            Write-Output "$Configuration charset mode $mode passed both wire reports (1,$column and 4,11)."
            $accept = $listener.AcceptTcpClientAsync()
            $forwarder = Start-Process (Join-Path $portable 'PCMan.exe') -ArgumentList "telnet://127.0.0.1:$port" -WorkingDirectory $portable -PassThru -WindowStyle Hidden
            if (!$accept.Wait(10000) -or !$forwarder.WaitForExit(5000)) { throw 'Second tab failed to open in the same process.' }
            $second = $accept.Result
            $otherStream = $second.GetStream()
            $otherStream.ReadTimeout = 5000
            $streams = @($stream, $otherStream)
            for ($i = 0; $i -lt $streams.Length; ++$i) {
                $row = $i + 2
                $prefix = [byte[]]($ascii.GetBytes("${esc}[${row};1H") + @(196) + $ascii.GetBytes("${esc}[6n"))
                Test-Report $streams[$i] $prefix "${esc}[${row};1R"
            }
            # Invoke the actual WM_COMMAND handler for the other encoding.
            $command = if ($utf8) { 33026 } else { 33029 }
            [CprTestWindow]::SelectCharset($app.Id, $command)
            [CprTestWindow]::CheckCharset($app.Id, $command)
            # The old UTF-8 prefix finishes as U+FFFD (two PTT cells);
            # the old Big5 prefix finishes as one legacy byte cell.
            $after = if ($utf8) { 5 } else { 3 }
            for ($i = 0; $i -lt $streams.Length; ++$i) {
                $row = $i + 2
                $probe = [byte[]](@(196,161) + $ascii.GetBytes("${esc}[6n"))
                Test-Report $streams[$i] $probe "${esc}[${row};${after}R"
            }
            Write-Output "$Configuration charset mode $mode switched with pending prefixes on both tabs (column $after)."
        } finally {
            if ($forwarder) {
                if (!$forwarder.HasExited) { $forwarder.Kill(); $forwarder.WaitForExit() }
                $forwarder.Dispose()
            }
            if ($second) { $second.Dispose() }
            if ($client) { $client.Dispose() }
            $listener.Stop()
            if ($app) {
                if (!$app.HasExited) { $app.Kill(); $app.WaitForExit() }
                $app.Dispose()
            }
        }
    }
} finally {
    if ((Split-Path $portable -Parent) -eq $tempRoot.TrimEnd('\') -and (Test-Path -LiteralPath $portable)) {
        Remove-Item -LiteralPath $portable -Recurse -Force
    }
}
