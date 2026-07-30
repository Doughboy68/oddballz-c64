# peek.ps1 -Prg <path> -Start <addr> -End <addr> [-WarmupMs <n>]
#
# Reads C64 memory out of a running VICE via the binary monitor over TCP.
# This is the C64 analogue of poking at window.oddApp in the JS version: it lets
# a test assert on real engine state (e.g. ballMap contents after a drop) rather
# than on pixels.
#
# Binary monitor wire format (VICE 3.x, API v2):
#   request : STX(0x02) ver(0x02) bodyLen(u32le) reqId(u32le) cmdType(u8) body...
#   response: STX(0x02) ver(0x02) bodyLen(u32le) respType(u8) errCode(u8)
#             reqId(u32le) body...
#   MEM_GET (0x01) body: sideEffects(u8) start(u16le) end(u16le)
#                        memspace(u8) bankId(u16le)
#   MEM_GET response body: length(u16le) then the bytes.

param(
    [Parameter(Mandatory = $true)][string]$Prg,
    [Parameter(Mandatory = $true)][int]$Start,
    [Parameter(Mandatory = $true)][int]$End,
    [int]$WarmupMs = 6000,
    [int]$Port = 6502,
    [string[]]$Extra = @()      # e.g. -Extra '-ntsc' to check the other machine
)

$ErrorActionPreference = 'Stop'

$exe = "C:\Users\Brian\AppData\Local\Microsoft\WinGet\Packages\VICE-Team.VICE.GTK3_Microsoft.Winget.Source_8wekyb3d8bbwe\GTK3VICE-3.10-win64\bin\x64sc.exe"
$Prg = (Resolve-Path $Prg).Path

$vice = Start-Process -FilePath $exe -PassThru -ArgumentList (@($Extra) + @(
    '-autostart', "`"$Prg`"",
    '-binarymonitor',
    '-binarymonitoraddress', "ip4://127.0.0.1:$Port",
    '-warp', '-autostart-warp',
    '-sounddev', 'dummy'
))

try {
    Start-Sleep -Milliseconds $WarmupMs

    $client = New-Object System.Net.Sockets.TcpClient
    $client.Connect('127.0.0.1', $Port)
    $ns = $client.GetStream()
    $ns.ReadTimeout = 10000

    # --- build MEM_GET request ---
    $body = [byte[]]@(
        0x00,                                            # no side effects
        ($Start -band 0xFF), (($Start -shr 8) -band 0xFF),
        ($End   -band 0xFF), (($End   -shr 8) -band 0xFF),
        0x00,                                            # memspace: main
        0x00, 0x00                                       # bank 0
    )
    $req = [System.Collections.Generic.List[byte]]::new()
    $req.AddRange([byte[]]@(0x02, 0x02))
    $req.AddRange([System.BitConverter]::GetBytes([uint32]$body.Length))
    $req.AddRange([System.BitConverter]::GetBytes([uint32]1))   # request id
    $req.Add(0x01)                                             # MEM_GET
    $req.AddRange($body)
    $ns.Write($req.ToArray(), 0, $req.Count)
    $ns.Flush()

    function Read-Exactly([System.IO.Stream]$s, [int]$n) {
        $buf = New-Object byte[] $n
        $got = 0
        while ($got -lt $n) {
            $r = $s.Read($buf, $got, $n - $got)
            if ($r -le 0) { throw "socket closed after $got/$n bytes" }
            $got += $r
        }
        return $buf
    }

    # VICE may push unsolicited events first; loop until our reqId comes back.
    for ($attempt = 0; $attempt -lt 20; $attempt++) {
        $hdr = Read-Exactly $ns 12
        if ($hdr[0] -ne 0x02) { throw "bad STX 0x$('{0:X2}' -f $hdr[0])" }
        $bodyLen  = [System.BitConverter]::ToUInt32($hdr, 2)
        $respType = $hdr[6]
        $errCode  = $hdr[7]
        $reqId    = [System.BitConverter]::ToUInt32($hdr, 8)
        $payload  = if ($bodyLen -gt 0) { Read-Exactly $ns ([int]$bodyLen) } else { [byte[]]@() }

        if ($reqId -eq 1 -and $respType -eq 0x01) {
            if ($errCode -ne 0) { throw "MEM_GET error code $errCode" }
            $count = [System.BitConverter]::ToUInt16($payload, 0)
            $mem = $payload[2..(1 + $count)]
            Write-Output ("addr  \$" + ('{0:X4}' -f $Start) + "-\$" + ('{0:X4}' -f $End) + "  ($count bytes)")
            Write-Output (($mem | ForEach-Object { '{0:X2}' -f $_ }) -join ' ')
            return
        }
    }
    throw "no MEM_GET response after 20 frames"
}
finally {
    if (-not $vice.HasExited) { $vice.Kill() }
}
