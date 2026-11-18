param([Parameter(Mandatory)] [string]$GameRoot)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$output = Join-Path $root 'Saved\IconInspection'
New-Item -ItemType Directory -Force -Path $output | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class GWInspectIcons {
    delegate bool NameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr data);
    delegate bool LangProc(IntPtr module, IntPtr type, IntPtr name, ushort language, IntPtr data);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern bool EnumResourceNames(IntPtr module, IntPtr type, NameProc callback, IntPtr data);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern bool EnumResourceLanguages(IntPtr module, IntPtr type, IntPtr name, LangProc callback, IntPtr data);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)] public static extern uint ExtractIconEx(string file, int index, IntPtr[] large, IntPtr[] small, uint count);
    [DllImport("user32.dll")] public static extern bool DestroyIcon(IntPtr icon);
    public static string[] Groups(string path) {
        var results=new List<string>();
        var module=LoadLibraryEx(path, IntPtr.Zero, 2);
        try {
            NameProc onName=(m,t,n,d)=> {
                string name=((ulong)n.ToInt64()>>16)==0 ? n.ToInt64().ToString() : Marshal.PtrToStringUni(n);
                LangProc onLanguage=(lm,lt,ln,lang,ld)=> { results.Add(name+":"+lang); return true; };
                EnumResourceLanguages(m,t,n,onLanguage,IntPtr.Zero);
                return true;
            };
            EnumResourceNames(module,(IntPtr)14,onName,IntPtr.Zero);
            return results.ToArray();
        } finally {FreeLibrary(module);}
    }
}
'@
foreach ($entry in @(@('GeometricWarfare.exe','launcher'), @('GeometricWarfare\Binaries\Win64\GeometricWarfare.exe','game'))) {
    $exe=Join-Path $GameRoot $entry[0]
    Write-Output ($entry[1] + ' icon groups/languages: ' + ([GWInspectIcons]::Groups($exe) -join ', '))
    $large=[IntPtr[]]::new(1); $small=[IntPtr[]]::new(1)
    $found=[GWInspectIcons]::ExtractIconEx($exe,0,$large,$small,1)
    if ($found -eq 0) {throw "Shell cannot extract the primary icon: $exe"}
    foreach($size in @('large','small')) {
        $handle=if ($size -eq 'large') {$large[0]} else {$small[0]}
        $icon=[Drawing.Icon]::FromHandle($handle)
        $bitmap=$icon.ToBitmap()
        try {$bitmap.Save((Join-Path $output ($entry[1]+'-'+$size+'.png')),[Drawing.Imaging.ImageFormat]::Png)}
        finally {$bitmap.Dispose(); $icon.Dispose(); [GWInspectIcons]::DestroyIcon($handle) | Out-Null}
    }
}
