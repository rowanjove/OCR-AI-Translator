param([Parameter(Mandatory = $true)][string[]]$Executable)
$ErrorActionPreference = 'Stop'

if (-not ('TranslatorShellIconRefresh' -as [type])) {
    # The .NET compiler inherits MSBuild's LIB paths, including optional ATL
    # paths absent from minimal Build Tools installs. It needs no native LIB.
    $savedLib = $env:LIB
    try {
    $env:LIB = $null
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TranslatorShellIconRefresh {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    public struct FileInfo {
        public IntPtr icon;
        public int index;
        public uint attributes;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string display;
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 80)] public string type;
    }
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern IntPtr SHGetFileInfoW(string path, uint attributes, ref FileInfo info, uint size, uint flags);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern void SHUpdateImageW(string path, int iconIndex, uint flags, int imageIndex);
    [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
    static extern void SHChangeNotify(int eventId, uint flags, string item1, IntPtr item2);
    public static void Refresh(string path) {
        var location = new FileInfo();
        var cached = new FileInfo();
        var size = (uint)Marshal.SizeOf(typeof(FileInfo));
        // These application EXEs contain a single normal file-backed icon group.
        if (SHGetFileInfoW(path, 0, ref location, size, 0x1000) == IntPtr.Zero ||
            SHGetFileInfoW(path, 0, ref cached, size, 0x4000 | 1) == IntPtr.Zero)
            throw new InvalidOperationException("Shell icon lookup failed: " + path);
        SHUpdateImageW(location.display, location.index, 0, cached.index);
        // PATHW | FLUSH: deliver this file's change to open Explorer views.
        SHChangeNotify(0x2000, 0x1005, path, IntPtr.Zero);
    }
}
'@
    } finally { $env:LIB = $savedLib }
}
foreach ($file in $Executable) {
    $resolved = (Resolve-Path -LiteralPath $file).ProviderPath
    if ([IO.Path]::GetExtension($resolved) -ne '.exe') { throw "Expected an EXE: $resolved" }
    [TranslatorShellIconRefresh]::Refresh($resolved)
    Write-Output "Shell icon refresh notification delivered: $resolved"
}
