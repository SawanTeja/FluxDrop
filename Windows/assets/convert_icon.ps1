# Convert fluxdroplogo.png to fluxdroplogo.ico (256x256 PNG-in-ICO)
# Run from the assets/ directory or adjust paths as needed.

Add-Type -AssemblyName System.Drawing

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$pngPath = Join-Path $scriptDir "fluxdroplogo.png"
$icoPath = Join-Path $scriptDir "fluxdroplogo.ico"

$img = [System.Drawing.Image]::FromFile($pngPath)
$bmp = New-Object System.Drawing.Bitmap(256, 256)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$g.DrawImage($img, 0, 0, 256, 256)
$g.Dispose()

$ms = New-Object System.IO.MemoryStream
$bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
$pngBytes = $ms.ToArray()

$fs = New-Object System.IO.FileStream($icoPath, [System.IO.FileMode]::Create)
$bw = New-Object System.IO.BinaryWriter($fs)

# ICO header
$bw.Write([uint16]0)
$bw.Write([uint16]1)
$bw.Write([uint16]1)

# Directory entry
$bw.Write([byte]0)
$bw.Write([byte]0)
$bw.Write([byte]0)
$bw.Write([byte]0)
$bw.Write([uint16]1)
$bw.Write([uint16]32)
$bw.Write([uint32]$pngBytes.Length)
$bw.Write([uint32]22)

# Data
$bw.Write($pngBytes)

$bw.Close()
$fs.Close()
$ms.Close()
$bmp.Dispose()
$img.Dispose()

Write-Host "Created $icoPath"
