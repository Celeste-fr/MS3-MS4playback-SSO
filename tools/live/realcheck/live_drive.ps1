# live_drive.ps1: drive Ableton Live 12.4.6 on the test VM's desktop, unattended (tools/live/realcheck; docs/TOOLS.md
# section 5). Runs in the interactive session (scheduled task claude-rc-gui, started by vm_run.ps1), never over ssh.
#
#   live_drive.ps1 -job <job.json> -out <result.json>
#
# job.json: { "set": "<.als>", "exportDir": "<folder>", "exportBase": "L", "renderBars": 30 (0: keep Live's),
#             "saveAs": "<.als>" or "", "loadTimeoutS": 1200, "exportTimeoutS": 3600, "shots": "<folder>",
#             "settleS": 20 }
# Steps: (1) refuse if a Live already runs (the owner's or another job's); (2) start Live with the set; (3) wait until
# it has loaded: the main window titled with the set's name answers, the process' CPU stays under 0.6 of a core (Live
# idles at about 0.3 on the VM) and its memory still (under 0.2 % a second) for settleS (Kontakt loads its samples after the set opens; an export before that renders silence);
# dialogs on the way are handled by their look (Dialogs below); (4) File > Export Audio/Video: Rendered Track = All
# Individual Tracks, Render Start 1.1.1 (Live's own), Render Length = renderBars, the toggles checked by their colour
# (Normalize, Convert to Mono, Render as Loop, Include Return and Main Effects, MP3 off; PCM on), Export, the file name
# typed into Windows' Save dialog; waits until the progress window is gone and every file stopped growing; (5) File >
# Save Live Set As (saveAs); (6) File > Quit, waiting for the process to end (killing only the Live it started, after
# 180 s). Each step's result, timings, the dialogs met and screenshots (on a failure, and of the export dialog) go to
# result.json. The screen is the VM's 1024 x 768; positions below are relative to the windows Live reports, measured
# on Live 12.4.6 (2026-10-22).
param([string]$job, [string]$out)
$ErrorActionPreference = 'Stop'
$J = Get-Content $job -Raw | ConvertFrom-Json
$R = [ordered]@{ started = (Get-Date -Format o); steps = @(); dialogs = @(); verdict = 'RUNNING' }
# (written to a temporary file and moved: vm_run.ps1 reads the file while it is written; a failed write is tried again)
function Save { $t = $R | ConvertTo-Json -Depth 6
  for ($i = 0; $i -lt 20; $i++) { try { Set-Content -Encoding utf8 -Path "$out.tmp" -Value $t -ErrorAction Stop
      Move-Item -Force "$out.tmp" $out -ErrorAction Stop; return } catch { Start-Sleep -Milliseconds 250 } } }
function Step($name, $ok, $detail) { $R.steps += [ordered]@{ step = $name; ok = $ok; detail = $detail; at = (Get-Date -Format o) }; Save }

Add-Type @"
using System; using System.Runtime.InteropServices; using System.Collections.Generic;
public class U {
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(int f, int dx, int dy, int d, int e);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern void keybd_event(byte k, byte s, int f, int e);
  [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint m, IntPtr w, IntPtr l, uint f, uint t, out IntPtr r);
  public delegate bool EP(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern bool EnumWindows(EP f, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint p);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h, System.Text.StringBuilder s, int n);
  public struct RECT { public int L, T, R, B; }
  public static List<string> Windows(uint pid) { var res = new List<string>();
    EnumWindows((h, x) => { uint p; GetWindowThreadProcessId(h, out p); RECT r;
      if ((pid == 0 || p == pid) && IsWindowVisible(h) && GetWindowRect(h, out r)) { var t = new System.Text.StringBuilder(256); GetWindowText(h, t, 256);
        var c = new System.Text.StringBuilder(256); GetClassName(h, c, 256);
        res.Add(h.ToInt64() + "|" + r.L + "|" + r.T + "|" + r.R + "|" + r.B + "|" + p + "|" + c + "|" + t); }
      return true; }, IntPtr.Zero); return res; }
}
"@
[U]::SetProcessDPIAware() | Out-Null
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$LIVE = 'C:\ProgramData\Ableton\Live 12 Trial\Program\Ableton Live 12 Trial.exe'
$shotN = 0
function Shot([string]$what) {
  if (-not $J.shots) { return '' }
  New-Item -ItemType Directory -Force $J.shots | Out-Null
  $script:shotN++; $f = Join-Path $J.shots ('{0:D2}-{1}.png' -f $script:shotN, $what)
  $b = [System.Windows.Forms.SystemInformation]::VirtualScreen
  $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
  $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose(); return $f }
function Pixel([int]$x, [int]$y) { $bmp = New-Object System.Drawing.Bitmap 1, 1
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($x, $y, 0, 0, (New-Object System.Drawing.Size 1, 1))
  $c = $bmp.GetPixel(0, 0); $bmp.Dispose(); return $c }
function Click([int]$x, [int]$y) { [U]::SetCursorPos($x, $y) | Out-Null; Start-Sleep -Milliseconds 150; [U]::mouse_event(2,0,0,0,0); Start-Sleep -Milliseconds 80; [U]::mouse_event(4,0,0,0,0); Start-Sleep -Milliseconds 400 }
function Key([int[]]$mods, [int]$vk) { foreach ($m in $mods) { [U]::keybd_event($m,0,0,0); Start-Sleep -Milliseconds 30 }
  [U]::keybd_event($vk,0,0,0); Start-Sleep -Milliseconds 50; [U]::keybd_event($vk,0,2,0)
  $r = @($mods); [array]::Reverse($r); foreach ($m in $r) { Start-Sleep -Milliseconds 30; [U]::keybd_event($m,0,2,0) }; Start-Sleep -Milliseconds 300 }
function TypeText([string]$s) { Set-Clipboard -Value $s; Key @(0x11) 0x41; Key @(0x11) 0x56; Start-Sleep -Milliseconds 300 }
function Wins($procId) { [U]::Windows([uint32]$procId) | % { $a = $_.Split('|', 8)
  [pscustomobject]@{ H = [IntPtr][int64]$a[0]; L = [int]$a[1]; T = [int]$a[2]; R = [int]$a[3]; B = [int]$a[4]; W = [int]$a[3] - [int]$a[1]; Ht = [int]$a[4] - [int]$a[2]; Pid = [int]$a[5]; Class = $a[6]; Title = $a[7] } } }
function Busy($h) { $r = [IntPtr]::Zero; return ([U]::SendMessageTimeout($h, 0, [IntPtr]::Zero, [IntPtr]::Zero, 2, 300, [ref]$r) -eq [IntPtr]::Zero) }
function Front($h) { [U]::keybd_event(0x12,0,0,0); [U]::keybd_event(0x12,0,2,0); [U]::SetForegroundWindow($h) | Out-Null; Start-Sleep -Milliseconds 300 }
# other programs' windows over Live (the Trial's shop page in Edge, a terminal, a browser) are minimised, not closed
function HideOthers { foreach ($n in 'msedge', 'WindowsTerminal', 'chrome', 'claude', 'notepad') {
  Get-Process $n -ErrorAction SilentlyContinue | % { foreach ($w in (Wins $_.Id)) { [U]::ShowWindow($w.H, 6) | Out-Null } } } }
function Main($p) { Wins $p.Id | ? { $_.Title -like '*Ableton Live 12*' } | Select -First 1 }
function WaitWin($p, [scriptblock]$test, [int]$secs) { for ($i = 0; $i -lt $secs * 4; $i++) { $w = Wins $p.Id | ? $test | Select -First 1; if ($w) { return $w }; Start-Sleep -Milliseconds 250 }; return $null }

# Dialogs: Live draws its own (class "Ableton Live Window Class", no title). Told apart by a thumbnail of the
# dialog: 16 x 6 cells of mean brightness, matched against the known ones (measured on the VM, 2026-10-22). A known
# dialog is answered by its action; an unknown one: a screenshot, Escape, and the run is marked (detected, not
# guessed: an unknown dialog may be the owner's to answer).
$KNOWN = @(
  # "Saving and exporting were successfully activated. Time remaining: N days." [OK] [Purchase now]: OK (the window's
  # rect has a shadow: the OK button's centre at 0.375 of its width, 34 px over its bottom)
  @{ name = 'trial-activated'; w = 424; h = 155; tol = 6; action = 'click'; x = 0.375; y = -34 },
  # the splash ("12 Trial … Initializing the application") and its lower part, left behind while Live loads
  @{ name = 'splash'; w = 358; h = 358; tol = 4; action = 'ignore' },
  @{ name = 'splash-part'; w = 358; h = 175; tol = 4; action = 'ignore' }
)
$seen = @{}
function Thumb($d) { $bmp = New-Object System.Drawing.Bitmap $d.W, $d.Ht
  [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($d.L, $d.T, 0, 0, $bmp.Size)
  $cells = @(); for ($cy = 0; $cy -lt 6; $cy++) { for ($cx = 0; $cx -lt 16; $cx++) { $s = 0; $n = 0
    for ($y = [int]($cy * $d.Ht / 6); $y -lt [int](($cy + 1) * $d.Ht / 6); $y += 3) { for ($x = [int]($cx * $d.W / 16); $x -lt [int](($cx + 1) * $d.W / 16); $x += 3) {
      $c = $bmp.GetPixel($x, $y); $s += ($c.R + $c.G + $c.B) / 3; $n++ } }
    $cells += [int]($s / [Math]::Max($n, 1)) } }; $bmp.Dispose(); return ,$cells }
function HandleDialogs($p) {
  $m = Main $p
  $ds = Wins $p.Id | ? { $_.Class -eq 'Ableton Live Window Class' -and $_.Title -eq '' -and $_.W -gt 250 -and $_.Ht -gt 100 -and $_.W -lt 800 -and $_.Ht -lt 500 }
  $handled = 0
  foreach ($d in $ds) {
    $k = $KNOWN | ? { [Math]::Abs($_.w - $d.W) -le $_.tol -and [Math]::Abs($_.h - $d.Ht) -le $_.tol } | Select -First 1
    if ($k -and $k.action -eq 'ignore') { continue }
    # one answer per window and 8 s (an answer takes a moment; a dialog that stays after three is a failure)
    $hk = [string]$d.H
    if ($seen.ContainsKey($hk) -and ((Get-Date) - $seen[$hk].t).TotalSeconds -lt 8) { continue }
    if ($seen.ContainsKey($hk) -and $seen[$hk].n -ge 3) { Fail "a dialog ($($d.W)x$($d.Ht), '$(if ($k) { $k.name } else { 'unknown' })') stays after 3 answers"; exit 1 }
    $seen[$hk] = @{ t = (Get-Date); n = $(if ($seen.ContainsKey($hk)) { $seen[$hk].n + 1 } else { 1 }) }
    $th = Thumb $d
    $entry = [ordered]@{ at = (Get-Date -Format o); w = $d.W; h = $d.Ht; thumb = ($th -join ','); known = $(if ($k) { $k.name } else { '' }) }
    if ($k -and $k.action -eq 'click') {
      Click ($d.L + [int]($d.W * $k.x)) ($d.B + $k.y); $entry.action = 'clicked'
    } else {
      $entry.shot = Shot 'unknown-dialog'; Front $d.H; Key @() 0x1B; $entry.action = 'escape'; $R.unknownDialog = $true
    }
    $R.dialogs += $entry; Save; $handled++; Start-Sleep 2
  }
  return $handled
}

# a failure: the screen kept, the Live this run started stopped (the next run must find no Live)
function Fail($what) { $R.verdict = 'FAIL'; $R.failure = $what; $R.failShot = Shot 'fail'; Save
  if ($script:p -and -not $script:p.HasExited) { Stop-Process -Id $script:p.Id -Force -ErrorAction SilentlyContinue } }

$p = $null
if ($J.shots -and (Test-Path $J.shots)) { Remove-Item -Recurse -Force $J.shots }   # (an earlier attempt's)
try {
# ---- 1: no Live of anyone else's
$other = Get-Process | ? { $_.Path -eq $LIVE }
if ($other) { Fail "Live already runs (pid $($other.Id -join ' ')): not ours, left alone"; exit 1 }
Step 'preflight' $true "no Live running"

# ---- 2: start
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process $LIVE -ArgumentList ('"' + $J.set + '"') -PassThru
$R.livePid = $p.Id; Save
$name = [IO.Path]::GetFileNameWithoutExtension($J.set)

# ---- 3: loaded
$quietSince = $null; $lastCpu = $null; $lastMem = $null; $loaded = $false
while ($sw.Elapsed.TotalSeconds -lt $J.loadTimeoutS) {
  Start-Sleep 1
  if ($p.HasExited) { Fail "Live exited while loading (code $($p.ExitCode))"; exit 1 }
  $null = HandleDialogs $p
  $m = Main $p
  if (-not $m -or $m.Title -notlike "$name*") { $quietSince = $null; continue }
  $p.Refresh(); $cpu = $p.TotalProcessorTime.TotalSeconds; $mem = $p.WorkingSet64
  $busy = Busy $m.H
  if ($lastCpu -ne $null) {
    $rate = $cpu - $lastCpu; $grow = [Math]::Abs($mem - $lastMem) / [Math]::Max($lastMem, 1)
    # (Live's own idle load on the VM is about 0.3 of a core: its UI drawn in software; Kontakt's sample loading shows
    # as memory growth and CPU above that)
    if (-not $busy -and $rate -lt 0.6 -and $grow -lt 0.002) { if (-not $quietSince) { $quietSince = $sw.Elapsed.TotalSeconds } }
    else { $quietSince = $null }
    if ($quietSince -and $sw.Elapsed.TotalSeconds - $quietSince -ge $J.settleS) { $loaded = $true; break }
  }
  $lastCpu = $cpu; $lastMem = $mem
}
if (-not $loaded) { Fail "not loaded after $($J.loadTimeoutS) s"; exit 1 }
$R.loadSeconds = [Math]::Round($sw.Elapsed.TotalSeconds - $J.settleS, 1); $R.liveMemoryMB = [int]($p.WorkingSet64 / 1MB)
Step 'loaded' $true "title '$($m.Title)', $($R.loadSeconds) s, $($R.liveMemoryMB) MB"
HideOthers; [U]::ShowWindow($m.H, 3) | Out-Null; Front $m.H; Start-Sleep 2
$null = HandleDialogs $p

function Menu($p, [int]$dy) {
  $m = Main $p; Front $m.H; Click ($m.L + 25) ($m.T + 44); Start-Sleep 1
  $mn = WaitWin $p { $_.Class -eq '#32768' } 5
  if (-not $mn) { return $false }
  Click ($mn.L + 90) ($mn.T + $dy); return $true }

# ---- 4: export
if ($J.exportDir) {
  New-Item -ItemType Directory -Force $J.exportDir | Out-Null
  $ok = Menu $p 357   # File > Export Audio/Video…
  $dlg = WaitWin $p { $_.Title -eq 'Export Audio/Video' } 15
  if (-not $dlg) { Key @() 0x1B; Fail 'the Export Audio/Video dialog did not open'; exit 1 }
  $X = $dlg.L; $Y = $dlg.T
  # Rendered Track: the chooser, then its 2nd entry (Main, All Individual Tracks, Selected Tracks Only, tracks…)
  Click ($X + 223) ($Y + 77); Start-Sleep 1
  $pop = WaitWin $p { $_.Class -eq 'Ableton Live Window Class' -and $_.Title -eq '' -and $_.W -lt 300 -and $_.Ht -lt 600 -and $_.T -gt $Y } 5
  if (-not $pop) { Fail 'the Rendered Track list did not open'; exit 1 }
  Click ($pop.L + 50) ($pop.T + 2 + 15 + 7); Start-Sleep 1
  if ($J.renderBars -gt 0) {
    Click ($X + 231) ($Y + 121); foreach ($ch in ([string]$J.renderBars).ToCharArray()) { Key @() ([int]$ch) }; Key @() 0x0D; Start-Sleep 1
  }
  # toggles (x 253 from the dialog's left; y from its top): Off is grey, On is orange (R > 200, B < 100)
  $toggles = @(@{ n = 'Include Return and Main Effects'; y = 169; on = $false }, @{ n = 'Render as Loop'; y = 191; on = $false },
               @{ n = 'Convert to Mono'; y = 213; on = $false }, @{ n = 'Normalize'; y = 235; on = $false },
               @{ n = 'Create Analysis File'; y = 257; on = $false }, @{ n = 'Encode PCM'; y = 350; on = $true },
               @{ n = 'Encode MP3'; y = 464; on = $false })
  $fixed = @()
  foreach ($t in $toggles) { $c = Pixel ($X + 236) ($Y + $t.y); $isOn = ($c.R -gt 200 -and $c.B -lt 100)
    if ($isOn -ne $t.on) { Click ($X + 236) ($Y + $t.y); $fixed += $t.n; Start-Sleep -Milliseconds 500
      $c = Pixel ($X + 236) ($Y + $t.y); if ((($c.R -gt 200 -and $c.B -lt 100)) -ne $t.on) { Fail "export toggle '$($t.n)' could not be set"; exit 1 } } }
  $R.exportDialogShot = Shot 'export-dialog'
  $R.togglesChanged = $fixed
  Click ($X + 118) ($Y + 595)   # Export
  $sv = WaitWin $p { $_.Class -eq '#32770' -and $_.Title -like 'Save Audio File As*' } 15
  if (-not $sv) { Fail 'no Save Audio File As dialog after Export'; exit 1 }
  Front $sv.H; TypeText (Join-Path $J.exportDir ($J.exportBase + '.wav')); Key @() 0x0D
  $t0 = $sw.Elapsed.TotalSeconds; Start-Sleep 3
  $cf = WaitWin $p { $_.Class -eq '#32770' -and $_.Title -notlike 'Save Audio*' } 2
  if ($cf) { Fail "a Windows dialog after the file name: '$($cf.Title)'"; exit 1 }
  $stable = 0; $lastSizes = ''
  while ($sw.Elapsed.TotalSeconds - $t0 -lt $J.exportTimeoutS) {
    Start-Sleep 2
    if ($p.HasExited) { Fail 'Live exited while exporting'; exit 1 }
    $prog = Wins $p.Id | ? { $_.Title -like 'Export Audio*' -and $_.Title -ne 'Export Audio/Video' }
    $sizes = (Get-ChildItem $J.exportDir -Filter *.wav -ErrorAction SilentlyContinue | % { "$($_.Name)=$($_.Length)" }) -join ';'
    if (-not $prog -and $sizes -and $sizes -eq $lastSizes) { $stable++ } else { $stable = 0 }
    $lastSizes = $sizes
    if ($stable -ge 3) { break }
  }
  if ($stable -lt 3) { Fail "export not finished after $($J.exportTimeoutS) s"; exit 1 }
  $R.exportSeconds = [Math]::Round($sw.Elapsed.TotalSeconds - $t0, 1)
  $R.exported = @(Get-ChildItem $J.exportDir -Filter *.wav | % { $_.Name })
  Step 'export' $true "$($R.exported.Count) file(s) in $($R.exportSeconds) s"
  $null = HandleDialogs $p
}

# ---- 5: save as
if ($J.saveAs) {
  $ok = Menu $p 238   # File > Save Live Set As…
  $sv = WaitWin $p { $_.Class -eq '#32770' -and $_.Title -like 'Save Live Set As*' } 15
  if (-not $sv) { Fail 'no Save Live Set As dialog'; exit 1 }
  $before = Get-Date
  $dir = Split-Path $J.saveAs; $base = [IO.Path]::GetFileName($J.saveAs)
  New-Item -ItemType Directory -Force $dir | Out-Null   # (Windows' dialog: "Path does not exist" otherwise)
  Front $sv.H; TypeText $J.saveAs; Key @() 0x0D; Start-Sleep 2
  $cf = WaitWin $p { $_.Class -eq '#32770' } 2
  if ($cf) { Fail "a Windows dialog stays after the file name ('$($cf.Title)')"; exit 1 }
  $f = $null
  for ($i = 0; $i -lt 120; $i++) { Start-Sleep 1; $null = HandleDialogs $p
    $f = Get-ChildItem $dir -Recurse -Filter $base -ErrorAction SilentlyContinue | ? { $_.LastWriteTime -ge $before } | Select -First 1
    if ($f -and -not (Busy (Main $p).H)) { break } }
  if (-not $f) { Fail 'the set was not saved'; exit 1 }
  $R.savedSet = $f.FullName
  Step 'save' $true $f.FullName
}

# ---- 6: quit
$ok = Menu $p 410   # File > Quit
for ($i = 0; $i -lt 180 -and -not $p.HasExited; $i++) { Start-Sleep 1
  $d = Wins $p.Id | ? { $_.Class -eq 'Ableton Live Window Class' -and $_.Title -eq '' -and $_.W -gt 250 -and $_.Ht -gt 100 -and $_.W -lt 800 -and $_.Ht -lt 500 } | Select -First 1
  if ($d -and $i -gt 3) { $R.quitDialog = Shot 'quit-dialog'; break } }   # (a "save changes?" after Save As: not expected; the set is saved, so Live is stopped below)
for ($i = 0; $i -lt 60 -and -not $p.HasExited -and -not $R.quitDialog; $i++) { Start-Sleep 1 }
if (-not $p.HasExited) { Stop-Process -Id $p.Id -Force; Step 'quit' $false 'Live did not quit: its process stopped' }
else { Step 'quit' $true "exit $($p.ExitCode)" }
if ($R.verdict -eq 'RUNNING') { $R.verdict = $(if ($R.unknownDialog) { 'PASS (unknown dialog escaped)' } else { 'PASS' }) }
$R.finished = (Get-Date -Format o); Save
} catch {
  Fail "driver error: $($_.Exception.Message) (line $($_.InvocationInfo.ScriptLineNumber))"; exit 1
}
