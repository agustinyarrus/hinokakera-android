# Rinde un texto blanco con sombra negra (estilo de los nombres de escenario de tex_sys24) en una PNG transparente.
# nombre-undead.png salió de (Windows, GDI+):
#   pwsh -File texto.ps1 -Texto "Lost Grave -Undead-" -Fuente "Times New Roman" -Tam 26 -EscalaX 1.05 -Grosor 0.7 -Salida nombre-undead.png
param([string]$Texto, [string]$Fuente, [single]$Tam, [string]$Salida, [int]$Ancho = 256, [int]$Alto = 32,
      [single]$Sombra = 1.5, [single]$Dy = 0, [single]$EscalaX = 1, [single]$Grosor = 0)
Add-Type -AssemblyName System.Drawing
$k = 4 # supermuestreo
$bmp = New-Object System.Drawing.Bitmap ($Ancho * $k), ($Alto * $k), ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::Transparent)
$estilo = [System.Drawing.FontStyle]::Bold -bor [System.Drawing.FontStyle]::Italic
$fam = New-Object System.Drawing.FontFamily $Fuente
$camino = New-Object System.Drawing.Drawing2D.GraphicsPath
$formato = [System.Drawing.StringFormat]::GenericTypographic
$camino.AddString($Texto, $fam, [int]$estilo, $Tam * $k, (New-Object System.Drawing.PointF 0, 0), $formato)
$mx = New-Object System.Drawing.Drawing2D.Matrix
$mx.Scale($EscalaX, 1)
$camino.Transform($mx)
$caja = $camino.GetBounds()
# centrado horizontal; vertical por la caja de la tinta (+ Dy)
$m = New-Object System.Drawing.Drawing2D.Matrix
$m.Translate((($Ancho * $k) - $caja.Width) / 2 - $caja.X, (($Alto * $k) - $caja.Height) / 2 - $caja.Y + $Dy * $k)
$camino.Transform($m)
$camS = $camino.Clone()
$ms = New-Object System.Drawing.Drawing2D.Matrix
$ms.Translate($Sombra * $k, $Sombra * $k)
$camS.Transform($ms)
$negro = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 0, 0, 0))
$g.FillPath($negro, $camS)
if ($Grosor -gt 0) { $g.DrawPath((New-Object System.Drawing.Pen ([System.Drawing.Color]::Black), ($Grosor * $k)), $camS) }
$g.FillPath([System.Drawing.Brushes]::White, $camino)
if ($Grosor -gt 0) { $g.DrawPath((New-Object System.Drawing.Pen ([System.Drawing.Color]::White), ($Grosor * $k)), $camino) }
$g.Dispose()
# reducir a 1x promediando bloques de k x k (con alfa)
$out = New-Object System.Drawing.Bitmap $Ancho, $Alto, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
for ($y = 0; $y -lt $Alto; $y++) {
  for ($x = 0; $x -lt $Ancho; $x++) {
    $a = 0; $r = 0; $gg = 0; $b = 0
    for ($j = 0; $j -lt $k; $j++) { for ($i = 0; $i -lt $k; $i++) {
      $c = $bmp.GetPixel($x * $k + $i, $y * $k + $j)
      $a += $c.A; $r += $c.R * $c.A; $gg += $c.G * $c.A; $b += $c.B * $c.A
    } }
    if ($a -gt 0) { $out.SetPixel($x, $y, [System.Drawing.Color]::FromArgb([int]($a / ($k * $k)), [int]($r / $a), [int]($gg / $a), [int]($b / $a))) }
  }
}
$out.Save($Salida, [System.Drawing.Imaging.ImageFormat]::Png)
"{0}: tinta {1:N0}x{2:N0} px" -f $Salida, ($caja.Width / $k), ($caja.Height / $k)
