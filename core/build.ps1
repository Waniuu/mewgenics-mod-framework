# Build MewCore (version.dll proxy + mod loader + optional dev agent) with MSVC.
$vc = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$out = Join-Path $PSScriptRoot 'build'; New-Item -ItemType Directory -Force $out | Out-Null
$mh = Join-Path $PSScriptRoot 'minhook\src'
$src = @("$PSScriptRoot\core.c", "$mh\buffer.c", "$mh\hook.c", "$mh\trampoline.c", "$mh\hde\hde64.c") | % { "`"$_`"" }
Remove-Item "$out\version.dll" -EA SilentlyContinue
cmd /c "`"$vc`" >nul && cd /d `"$out`" && cl /nologo /LD /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS $($src -join ' ') /Fe:version.dll /link /DLL"
if (Test-Path "$out\version.dll") { "built $out\version.dll" } else { throw 'build failed' }
