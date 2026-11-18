param([Parameter(Mandatory)] [string]$GameRoot)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class GWIconResources {
    delegate bool NameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr data);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll")] static extern IntPtr FindResource(IntPtr module, IntPtr name, IntPtr type);
    [DllImport("kernel32.dll")] static extern IntPtr LoadResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll")] static extern IntPtr LockResource(IntPtr data);
    [DllImport("kernel32.dll")] static extern uint SizeofResource(IntPtr module, IntPtr resource);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern bool EnumResourceNames(IntPtr module, IntPtr type, NameProc callback, IntPtr data);
    public static int[] Groups(string path) {
        var module=LoadLibraryEx(path, IntPtr.Zero, 2);
        if(module==IntPtr.Zero) throw new Exception("Cannot read EXE resources: "+path);
        var ids=new List<int>();
        try {
            NameProc callback=(m,t,n,d)=> {ids.Add(n.ToInt32()); return true;};
            EnumResourceNames(module,(IntPtr)14,callback,IntPtr.Zero);
            if(ids.Count==0) throw new Exception("EXE has no icon groups: "+path);
            return ids.ToArray();
        } finally {FreeLibrary(module);}
    }
    public static byte[] Read(string path, int type, int id) {
        var module=LoadLibraryEx(path, IntPtr.Zero, 2);
        if(module==IntPtr.Zero) throw new Exception("Cannot read EXE resources: "+path);
        try {
            var resource=FindResource(module, (IntPtr)id, (IntPtr)type);
            if(resource==IntPtr.Zero) throw new Exception("Missing resource "+type+":"+id);
            var bytes=new byte[SizeofResource(module, resource)];
            Marshal.Copy(LockResource(LoadResource(module,resource)),bytes,0,bytes.Length);
            return bytes;
        } finally { FreeLibrary(module); }
    }
}
'@
$ico = [IO.File]::ReadAllBytes((Join-Path $root 'Build\Windows\Application.ico'))
$count = [BitConverter]::ToUInt16($ico, 4)
foreach ($entry in @(@('GeometricWarfare.exe',101), @('GeometricWarfare\Binaries\Win64\GeometricWarfare.exe',123))) {
    $exe = Join-Path $GameRoot $entry[0]
    foreach ($groupId in [GWIconResources]::Groups($exe)) {
    $group = [GWIconResources]::Read($exe, 14, $groupId)
    if ([BitConverter]::ToUInt16($group,4) -ne $count) { throw "Wrong icon size count in $exe (group $groupId)" }
    for ($i=0; $i -lt $count; $i++) {
        $id = [BitConverter]::ToUInt16($group, 6+14*$i+12)
        $actual = [GWIconResources]::Read($exe, 3, $id)
        $length = [BitConverter]::ToUInt32($ico, 6+16*$i+8)
        $offset = [BitConverter]::ToUInt32($ico, 6+16*$i+12)
        $expected = [byte[]]::new($length)
        [Array]::Copy($ico, $offset, $expected, 0, $length)
        if ([Convert]::ToBase64String($actual) -cne [Convert]::ToBase64String($expected)) { throw "Embedded icon differs in $exe (group $groupId, frame $i)" }
    }
    Write-Output "PASS: $exe group $groupId embeds all $count game icon sizes (16-256 px)."
    # Export the actual embedded 256 px frame for visual inspection.
    $preview = Join-Path $root ('Saved\SteamFix\icon-' + $groupId + '.png')
    [IO.File]::WriteAllBytes($preview, $actual)
    }
}
