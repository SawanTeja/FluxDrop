$ErrorActionPreference = "Stop"
$WorkingDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $WorkingDir

Write-Host "==> [1/4] Creating dist directory..."
if (Test-Path "dist") { Remove-Item -Recurse -Force "dist" }

# Run deploy.sh via MSYS2 to bundle DLLs
Write-Host "==> [2/4] Running MSYS2 deploy.sh to gather DLLs + Qt plugins..."
$msysShell = "C:\msys64\msys2_shell.cmd"
if (!(Test-Path $msysShell)) {
    Write-Host "ERROR: MSYS2 not found at $msysShell" -ForegroundColor Red
    Write-Host "Please install MSYS2 from https://www.msys2.org/"
    exit 1
}

$deployScript = ($WorkingDir -replace '\\', '/') -replace '^C:', '/c'
$deployCmd = "bash `"$deployScript/deploy.sh`""

# Try UCRT64 first, fall back to MinGW64
$envName = "-ucrt64"
if (!(Test-Path "C:\msys64\ucrt64\bin\gcc.exe")) {
    $envName = "-mingw64"
}

Start-Process -FilePath $msysShell -ArgumentList $envName, "-defterm", "-no-start", "-c", "`"$deployCmd`"" -Wait -NoNewWindow

if (!(Test-Path "dist\fluxdrop_gui.exe")) {
    Write-Host "ERROR: deploy.sh failed — dist\fluxdrop_gui.exe not found" -ForegroundColor Red
    exit 1
}

Write-Host "==> [3/4] Compressing to app.zip..."
if (Test-Path "app.zip") { Remove-Item "app.zip" }
Compress-Archive -Path "dist\*" -DestinationPath "app.zip" -Force

$guid = [guid]::NewGuid().ToString().Substring(0, 8)
Write-Host "==> [4/4] Compiling FluxDrop.exe (self-extracting launcher)..."

$csCode = @"
using System;
using System.Diagnostics;
using System.IO;
using System.IO.Compression;
using System.Reflection;
using System.Windows.Forms;

namespace FluxDropBundler
{
    class Program
    {
        [STAThread]
        static void Main(string[] args)
        {
            string tempDir = Path.Combine(Path.GetTempPath(), "FluxDrop_Qt_$guid");
            string exePath = Path.Combine(tempDir, "fluxdrop_gui.exe");

            if (!File.Exists(exePath))
            {
                try {
                    if (Directory.Exists(tempDir)) { Directory.Delete(tempDir, true); }
                    Directory.CreateDirectory(tempDir);
                    using (Stream resStream = Assembly.GetExecutingAssembly().GetManifestResourceStream("app.zip"))
                    using (ZipArchive archive = new ZipArchive(resStream, ZipArchiveMode.Read))
                    {
                        archive.ExtractToDirectory(tempDir);
                    }
                }
                catch (Exception ex) {
                    MessageBox.Show("Failed to extract application: " + ex.Message, "FluxDrop Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
                    return;
                }
            }

            ProcessStartInfo psi = new ProcessStartInfo(exePath);
            psi.WorkingDirectory = tempDir;
            if (args.Length > 0) {
                psi.Arguments = string.Join(" ", args);
            }

            try {
                Process.Start(psi);
            } catch (Exception ex) {
                MessageBox.Show("Failed to launch application: " + ex.Message, "FluxDrop Error", MessageBoxButtons.OK, MessageBoxIcon.Error);
            }
        }
    }
}
"@
Set-Content -Path "Bundler.cs" -Value $csCode -Encoding UTF8

$csc = "C:\Windows\Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (!(Test-Path $csc)) { $csc = "C:\Windows\Microsoft.NET\Framework\v4.0.30319\csc.exe" }

if (!(Test-Path $csc)) {
    Write-Host "ERROR: C# compiler not found" -ForegroundColor Red
    Write-Host "The .NET Framework 4.x CSC compiler is required"
    exit 1
}

$cscArgs = @(
    "/target:winexe",
    "/out:FluxDrop.exe",
    "/resource:app.zip",
    "/win32icon:assets\fluxdroplogo.ico",
    "/reference:System.IO.Compression.dll",
    "/reference:System.IO.Compression.FileSystem.dll",
    "/reference:System.Windows.Forms.dll",
    "Bundler.cs"
)
Start-Process -FilePath $csc -ArgumentList $cscArgs -Wait -NoNewWindow

Write-Host "Cleaning up..."
if (Test-Path "dist") { Remove-Item -Recurse -Force "dist" }
if (Test-Path "app.zip") { Remove-Item "app.zip" }
if (Test-Path "Bundler.cs") { Remove-Item "Bundler.cs" }

if (Test-Path "FluxDrop.exe") {
    $size = (Get-Item "FluxDrop.exe").Length / 1MB
    Write-Host ""
    Write-Host "Done! FluxDrop.exe created ($([math]::Round($size, 1)) MB)" -ForegroundColor Green
} else {
    Write-Host "ERROR: FluxDrop.exe was not created" -ForegroundColor Red
    exit 1
}
