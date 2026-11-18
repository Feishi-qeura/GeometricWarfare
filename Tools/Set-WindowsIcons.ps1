param([Parameter(Mandatory)] [string]$GameRoot)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$icoPath = Join-Path $root 'Build\Windows\Application.ico'
Add-Type -TypeDefinition @'
using System;
using System.IO;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
public static class GWSetIcons {
    struct Group {public int Id; public ushort Language;}
    delegate bool NameProc(IntPtr module, IntPtr type, IntPtr name, IntPtr data);
    delegate bool LangProc(IntPtr module, IntPtr type, IntPtr name, ushort language, IntPtr data);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr LoadLibraryEx(string file, IntPtr reserved, uint flags);
    [DllImport("kernel32.dll")] static extern bool FreeLibrary(IntPtr module);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern bool EnumResourceNames(IntPtr module, IntPtr type, NameProc callback, IntPtr data);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode)] static extern bool EnumResourceLanguages(IntPtr module, IntPtr type, IntPtr name, LangProc callback, IntPtr data);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern IntPtr BeginUpdateResource(string file, bool deleteExisting);
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)] static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name, ushort language, byte[] data, uint length);
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool EndUpdateResource(IntPtr update, bool discard);
    [DllImport("shell32.dll", CharSet=CharSet.Unicode)] static extern void SHChangeNotify(uint change, uint flags, string path, IntPtr extra);
    static void Write(IntPtr update,int type,int id,ushort language,byte[] data) {
        if(!UpdateResource(update,(IntPtr)type,(IntPtr)id,language,data,(uint)data.Length)) throw new Win32Exception(Marshal.GetLastWin32Error());
    }
    public static int Set(string path,string icoPath) {
        byte[] ico=File.ReadAllBytes(icoPath);
        int count=BitConverter.ToUInt16(ico,4);
        if(BitConverter.ToUInt16(ico,0)!=0 || BitConverter.ToUInt16(ico,2)!=1 || count<1 || count>256) throw new Exception("Invalid ICO");
        var frames=new List<byte[]>();
        var groupStream=new MemoryStream();
        using(var writer=new BinaryWriter(groupStream)) {
            writer.Write(ico,0,6);
            for(int i=0;i<count;i++) {
                int entry=6+16*i;
                int length=checked((int)BitConverter.ToUInt32(ico,entry+8));
                int offset=checked((int)BitConverter.ToUInt32(ico,entry+12));
                var frame=new byte[length];
                Array.Copy(ico,offset,frame,0,length);
                frames.Add(frame);
                writer.Write(ico,entry,12);
                writer.Write((ushort)(i+1));
            }
        }
        byte[] normalizedGroup=groupStream.ToArray();
        var groups=new List<Group>();
        var module=LoadLibraryEx(path,IntPtr.Zero,2);
        if(module==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        try {
            NameProc onName=(m,t,n,d)=> {
                if(((ulong)n.ToInt64()>>16)!=0) throw new Exception("Unexpected named icon group");
                LangProc onLanguage=(lm,lt,ln,lang,ld)=> {groups.Add(new Group {Id=n.ToInt32(),Language=lang}); return true;};
                EnumResourceLanguages(m,t,n,onLanguage,IntPtr.Zero);
                return true;
            };
            EnumResourceNames(module,(IntPtr)14,onName,IntPtr.Zero);
        } finally {FreeLibrary(module);}
        if(groups.Count==0) throw new Exception("EXE has no icon groups: "+path);
        var update=BeginUpdateResource(path,false);
        if(update==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
        bool committed=false;
        try {
            var languages=new HashSet<ushort>();
            foreach(var group in groups) {
                Write(update,14,group.Id,group.Language,normalizedGroup);
                languages.Add(group.Language);
            }
            foreach(ushort language in languages)
                for(int i=0;i<count;i++) Write(update,3,i+1,language,frames[i]);
            if(!EndUpdateResource(update,false)) throw new Win32Exception(Marshal.GetLastWin32Error());
            committed=true;
        } finally {if(!committed) EndUpdateResource(update,true);}
        // Invalidate this executable's Explorer icon entry without clearing unrelated user caches.
        SHChangeNotify(0x00002000,0x0005,path,IntPtr.Zero);
        return groups.Count;
    }
}
'@
foreach($relative in @('GeometricWarfare.exe','GeometricWarfare\Binaries\Win64\GeometricWarfare.exe')) {
    $exe=(Resolve-Path -LiteralPath (Join-Path $GameRoot $relative)).Path
    $groups=[GWSetIcons]::Set($exe,$icoPath)
    Write-Output "Updated all $groups icon group(s): $exe"
}
