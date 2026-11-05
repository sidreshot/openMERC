# openMERC Android: package the arm64-v8a build into a debug APK without Gradle.
# usage: build-apk.ps1 [-Sdk <android sdk>] [-Build <native build root>] [-Out <apk>]
# The native trees are built by the CMake Android toolchain first (engine: libmain.so + ref_gles3.so, game, player,
# modules/sound; SDL3 and OpenAL Soft as shared libraries in <build>\prefix\lib). The SDL Java sources come from the
# SDL3 source tree of the same version as libSDL3.so.
param(
	[string]$Sdk = "C:\Users\User0\src\deps\android",
	[string]$Build = "C:\Users\User0\src\build-android-arm64",
	[string]$SdlSrc = "C:\Users\User0\src\deps\android\src\SDL3-3.4.18",
	[string]$Out = "C:\Users\User0\src\build-android-arm64\openMERC-debug.apk"
)
$ErrorActionPreference = "Continue" # the tools print notes on stderr; their exit codes are checked
$here = Split-Path $MyInvocation.MyCommand.Path
$bt = "$Sdk\build-tools\34.0.0"
$jar = "$Sdk\platforms\android-34\android.jar"
$jdk = (Get-ChildItem "$Sdk\jdk" -Directory | Select-Object -First 1).FullName
$ndk = (Get-ChildItem "$Sdk\ndk" -Directory | Select-Object -First 1).FullName
$w = "$Build\apk"

if (Test-Path $w) { Remove-Item -Recurse -Force $w }
New-Item -ItemType Directory -Force "$w\classes", "$w\dex", "$w\root\lib\arm64-v8a" | Out-Null

# Java: SDL's activity and ours
$java = @(Get-ChildItem "$SdlSrc\android-project\app\src\main\java" -Recurse -Filter *.java) + @(Get-ChildItem "$here\java" -Recurse -Filter *.java)
& "$jdk\bin\javac.exe" -nowarn -source 8 -target 8 -bootclasspath $jar -classpath $jar -d "$w\classes" @($java | ForEach-Object { $_.FullName }) 2>&1 | Where-Object { $_ -notmatch "warning" }
if ($LASTEXITCODE) { throw "javac failed" }
$classes = Get-ChildItem "$w\classes" -Recurse -Filter *.class | ForEach-Object { $_.FullName }
$env:JAVA_HOME = $jdk
& "$bt\d8.bat" --release --min-api 26 --lib $jar --output "$w\dex" @classes
if ($LASTEXITCODE) { throw "d8 failed" }

# manifest -> base apk
& "$bt\aapt2.exe" compile --dir "$here\res" -o "$w\res.zip"
if ($LASTEXITCODE) { throw "aapt2 compile failed" }
& "$bt\aapt2.exe" link -o "$w\base.apk" -I $jar --manifest "$here\AndroidManifest.xml" -R "$w\res.zip" --auto-add-overlay --min-sdk-version 26 --target-sdk-version 34
if ($LASTEXITCODE) { throw "aapt2 failed" }

# native libraries: the engine, its modules (lib<name>.so, Sys_LoadLibrary finds them by that name), SDL3,
# OpenAL Soft and the C++ runtime
$libs = @{
	"libmain.so" = "$Build\engine\release\libmain.so";
	"libref_gles3.so" = "$Build\engine\release\ref_gles3.so";
	"libgamex86.so" = "$Build\game\gamex86.so";
	"libplayer.so" = "$Build\player\player.so";
	"libOMsnd.so" = "$Build\sound\OMsnd.so";
	"libSDL3.so" = "$Build\prefix\lib\libSDL3.so";
	"libopenal.so" = "$Build\prefix\lib\libopenal.so";
	"libc++_shared.so" = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\sysroot\usr\lib\aarch64-linux-android\libc++_shared.so"
}
$strip = "$ndk\toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-strip.exe"
foreach ($k in $libs.Keys) {
	if (!(Test-Path $libs[$k])) { throw "missing $($libs[$k])" }
	Copy-Item $libs[$k] "$w\root\lib\arm64-v8a\$k" -Force
	& $strip --strip-unneeded "$w\root\lib\arm64-v8a\$k"
}
Copy-Item "$w\dex\classes.dex" "$w\root\classes.dex"

# add dex + libs to the apk (zip), align, sign with a debug key
Add-Type -AssemblyName System.IO.Compression.FileSystem
$zip = [System.IO.Compression.ZipFile]::Open("$w\base.apk", "Update")
foreach ($f in Get-ChildItem "$w\root" -Recurse -File) {
	$rel = $f.FullName.Substring("$w\root\".Length).Replace("\", "/")
	[System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip, $f.FullName, $rel) | Out-Null
}
$zip.Dispose()
& "$bt\zipalign.exe" -f -p 4 "$w\base.apk" "$w\aligned.apk"
if ($LASTEXITCODE) { throw "zipalign failed" }
$ks = "$Build\debug.keystore"
if (!(Test-Path $ks)) {
	& "$jdk\bin\keytool.exe" -genkeypair -keystore $ks -storepass android -keypass android -alias androiddebugkey -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=openMERC Debug,O=openMERC,C=US" 2>&1 | Out-Null
}
& "$bt\apksigner.bat" sign --ks $ks --ks-pass pass:android --key-pass pass:android --out $Out "$w\aligned.apk"
if ($LASTEXITCODE) { throw "apksigner failed" }
"APK: $Out ($([math]::Round((Get-Item $Out).Length / 1MB, 1)) MB)"
