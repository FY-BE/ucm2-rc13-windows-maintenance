[CmdletBinding()]
param(
    [string]$SourceLogo =
        (Join-Path $PSScriptRoot '..\assets\qizhen-sensing-logo.jpg'),

    [string]$OutputPng =
        (Join-Path $PSScriptRoot '..\assets\qizhen-ucm-icon.png'),

    [string]$OutputIco =
        (Join-Path $PSScriptRoot '..\assets\qizhen-ucm.ico')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

Add-Type -AssemblyName System.Drawing

function New-RoundedRectanglePath {
    param(
        [Drawing.RectangleF]$Rectangle,
        [float]$Radius
    )

    $diameter = 2.0 * $Radius
    $path = [Drawing.Drawing2D.GraphicsPath]::new()
    $path.AddArc($Rectangle.X, $Rectangle.Y, $diameter, $diameter, 180, 90)
    $path.AddArc($Rectangle.Right - $diameter, $Rectangle.Y,
        $diameter, $diameter, 270, 90)
    $path.AddArc($Rectangle.Right - $diameter,
        $Rectangle.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($Rectangle.X, $Rectangle.Bottom - $diameter,
        $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    return $path
}

function Write-MultiSizeIcon {
    param(
        [Drawing.Bitmap]$Source,
        [string]$Path
    )

    $sizes = @(16, 20, 24, 32, 40, 48, 64, 96, 128, 256)
    $frames = [Collections.Generic.List[byte[]]]::new()
    foreach ($size in $sizes) {
        $frame = [Drawing.Bitmap]::new(
            $size, $size, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $graphics = [Drawing.Graphics]::FromImage($frame)
        try {
            $graphics.Clear([Drawing.Color]::Transparent)
            $graphics.CompositingMode =
                [Drawing.Drawing2D.CompositingMode]::SourceOver
            $graphics.CompositingQuality =
                [Drawing.Drawing2D.CompositingQuality]::HighQuality
            $graphics.InterpolationMode =
                [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $graphics.PixelOffsetMode =
                [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
            $graphics.SmoothingMode =
                [Drawing.Drawing2D.SmoothingMode]::HighQuality
            $graphics.DrawImage($Source, 0, 0, $size, $size)
        }
        finally {
            $graphics.Dispose()
        }

        $stream = [IO.MemoryStream]::new()
        try {
            $frame.Save($stream, [Drawing.Imaging.ImageFormat]::Png)
            $frames.Add($stream.ToArray())
        }
        finally {
            $stream.Dispose()
            $frame.Dispose()
        }
    }

    $outputDirectory = Split-Path -Parent $Path
    if (-not (Test-Path -LiteralPath $outputDirectory)) {
        New-Item -ItemType Directory -Path $outputDirectory | Out-Null
    }
    $file = [IO.File]::Open($Path, [IO.FileMode]::Create,
        [IO.FileAccess]::Write, [IO.FileShare]::None)
    $writer = [IO.BinaryWriter]::new($file)
    try {
        $writer.Write([uint16]0)
        $writer.Write([uint16]1)
        $writer.Write([uint16]$sizes.Count)
        $offset = 6 + (16 * $sizes.Count)
        for ($index = 0; $index -lt $sizes.Count; ++$index) {
            $size = $sizes[$index]
            $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
            $writer.Write([byte]$(if ($size -eq 256) { 0 } else { $size }))
            $writer.Write([byte]0)
            $writer.Write([byte]0)
            $writer.Write([uint16]1)
            $writer.Write([uint16]32)
            $writer.Write([uint32]$frames[$index].Length)
            $writer.Write([uint32]$offset)
            $offset += $frames[$index].Length
        }
        foreach ($frameBytes in $frames) {
            $writer.Write($frameBytes)
        }
    }
    finally {
        $writer.Dispose()
        $file.Dispose()
    }
}

$sourcePath = [IO.Path]::GetFullPath($SourceLogo)
$pngPath = [IO.Path]::GetFullPath($OutputPng)
$icoPath = [IO.Path]::GetFullPath($OutputIco)
if (-not (Test-Path -LiteralPath $sourcePath)) {
    throw "Brand source logo is missing: $sourcePath"
}

$original = [Drawing.Bitmap]::FromFile($sourcePath)
try {
    if ($original.Width -lt $original.Height) {
        throw "Brand source must contain the square QZ mark on its left side."
    }
    $cropSize = $original.Height
    $mark = [Drawing.Bitmap]::new(
        $cropSize, $cropSize,
        [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [Drawing.Graphics]::FromImage($mark)
    try {
        $graphics.DrawImage($original,
            [Drawing.Rectangle]::new(0, 0, $cropSize, $cropSize),
            [Drawing.Rectangle]::new(0, 0, $cropSize, $cropSize),
            [Drawing.GraphicsUnit]::Pixel)
    }
    finally {
        $graphics.Dispose()
    }

    # The supplied JPEG has a near-white checkerboard baked into it. Remove
    # only neutral bright pixels; the official blue and grey mark is retained.
    for ($y = 0; $y -lt $mark.Height; ++$y) {
        for ($x = 0; $x -lt $mark.Width; ++$x) {
            $pixel = $mark.GetPixel($x, $y)
            $maximum = [Math]::Max($pixel.R,
                [Math]::Max($pixel.G, $pixel.B))
            $minimum = [Math]::Min($pixel.R,
                [Math]::Min($pixel.G, $pixel.B))
            $luma = (0.2126 * $pixel.R) + (0.7152 * $pixel.G) +
                (0.0722 * $pixel.B)
            if (($maximum - $minimum) -le 18 -and $luma -ge 202) {
                $alpha = [int][Math]::Round(
                    255.0 * [Math]::Max(0.0,
                        [Math]::Min(1.0, (242.0 - $luma) / 40.0)))
                $mark.SetPixel($x, $y,
                    [Drawing.Color]::FromArgb(
                        $alpha, $pixel.R, $pixel.G, $pixel.B))
            }
        }
    }

    $canvas = [Drawing.Bitmap]::new(
        1024, 1024, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $canvasGraphics = [Drawing.Graphics]::FromImage($canvas)
    try {
        $canvasGraphics.Clear([Drawing.Color]::Transparent)
        $canvasGraphics.SmoothingMode =
            [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $canvasGraphics.CompositingQuality =
            [Drawing.Drawing2D.CompositingQuality]::HighQuality
        $canvasGraphics.InterpolationMode =
            [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $canvasGraphics.PixelOffsetMode =
            [Drawing.Drawing2D.PixelOffsetMode]::HighQuality

        $cardRectangle = [Drawing.RectangleF]::new(32, 32, 960, 960)
        $cardPath = New-RoundedRectanglePath -Rectangle $cardRectangle -Radius 206
        try {
            $cardBrush = [Drawing.SolidBrush]::new(
                [Drawing.Color]::FromArgb(255, 248, 251, 255))
            $cardPen = [Drawing.Pen]::new(
                [Drawing.Color]::FromArgb(255, 218, 229, 241), 7)
            try {
                $canvasGraphics.FillPath($cardBrush, $cardPath)
                $canvasGraphics.DrawPath($cardPen, $cardPath)
            }
            finally {
                $cardBrush.Dispose()
                $cardPen.Dispose()
            }
        }
        finally {
            $cardPath.Dispose()
        }

        $canvasGraphics.DrawImage($mark,
            [Drawing.Rectangle]::new(112, 112, 800, 800),
            [Drawing.Rectangle]::new(0, 0, $mark.Width, $mark.Height),
            [Drawing.GraphicsUnit]::Pixel)
    }
    finally {
        $canvasGraphics.Dispose()
        $mark.Dispose()
    }

    $pngDirectory = Split-Path -Parent $pngPath
    if (-not (Test-Path -LiteralPath $pngDirectory)) {
        New-Item -ItemType Directory -Path $pngDirectory | Out-Null
    }
    $canvas.Save($pngPath, [Drawing.Imaging.ImageFormat]::Png)
    Write-MultiSizeIcon -Source $canvas -Path $icoPath
    $canvas.Dispose()
}
finally {
    $original.Dispose()
}

$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$pngHash = (Get-FileHash -LiteralPath $pngPath -Algorithm SHA256).Hash
$icoHash = (Get-FileHash -LiteralPath $icoPath -Algorithm SHA256).Hash
Write-Output "Source SHA256: $sourceHash"
Write-Output "PNG: $pngPath"
Write-Output "PNG SHA256: $pngHash"
Write-Output "ICO: $icoPath"
Write-Output "ICO SHA256: $icoHash"
