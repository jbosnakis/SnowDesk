# Finds the offset between the dumped icon mask and the real desktop in a clean screenshot.
param([string]$Mask, [string]$Shot, [int]$Width = 430)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing.Common, System.Drawing.Primitives, System.Linq, System.Collections, System.Console -TypeDefinition @'
using System; using System.IO; using System.Linq; using System.Collections.Generic; using System.Drawing;
public static class Align {
  public static void Run(string maskPath, string shotPath, int width) {
    var raw = File.ReadAllBytes(maskPath); int p = 0, nl = 0; while (nl < 3) if (raw[p++] == 10) nl++;
    var hdr = System.Text.Encoding.ASCII.GetString(raw, 0, p).Split('\n')[1].Split(' ');
    int w = int.Parse(hdr[0]), h = int.Parse(hdr[1]);
    var bmp = new Bitmap(shotPath); int cw = width / 2;
    var edge = new float[h, cw]; var gray = new float[h, cw];
    for (int y = 0; y < h; y++) for (int x = 0; x < cw; x++) { var c = bmp.GetPixel(x * 2, y * 2); gray[y, x] = c.R * .3f + c.G * .6f + c.B * .1f; }
    for (int y = 1; y < h; y++) for (int x = 1; x < cw; x++) edge[y, x] = Math.Abs(gray[y, x] - gray[y, x - 1]) + Math.Abs(gray[y, x] - gray[y - 1, x]);
    Func<int, int, bool> m = (y, x) => raw[p + y * w + x] == 255;
    var pts = new List<(int y, int x)>();
    for (int y = 1; y < h; y++) for (int x = 1; x < cw; x++) if (m(y, x) != m(y - 1, x) || m(y, x) != m(y, x - 1)) pts.Add((y, x));
    var res = new List<(double s, int dx, int dy)>();
    for (int dy = -10; dy <= 15; dy++) for (int dx = -10; dx <= 10; dx++) {
      double sum = 0; int n = 0;
      foreach (var q in pts) { int yy = q.y + dy, xx = q.x + dx; if (yy < 0 || yy >= h || xx < 0 || xx >= cw) continue; sum += edge[yy, xx]; n++; }
      res.Add((sum / Math.Max(n, 1), dx * 2, dy * 2));
    }
    foreach (var r in res.OrderByDescending(r => r.s).Take(6)) Console.WriteLine("score {0:F1}  dx={1}px dy={2}px", r.s, r.dx, r.dy);
  }
}
'@
[Align]::Run($Mask, $Shot, $Width)

