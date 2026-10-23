# 只读诊断：仅记录被测进程前台期间的 Esc/Backspace，不截获、改写或阻止输入。
# 专用线程只在快捷键专项期间安装低级观察钩子，结束时必须卸载。
if (-not ('YeViewerRecordingAudit' -as [type])) {
Add-Type @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Threading;
public static class YeViewerRecordingAudit {
    delegate IntPtr Hook(int code, UIntPtr message, IntPtr data);
    [StructLayout(LayoutKind.Sequential)] struct Key { public uint Vk, Scan, Flags, Time; public UIntPtr Extra; }
    [StructLayout(LayoutKind.Sequential)] struct Point { public int X,Y; }
    [StructLayout(LayoutKind.Sequential)] struct Message { public IntPtr Window; public uint Id; public UIntPtr W; public IntPtr L; public uint Time; public Point Pt; public uint Private; }
    [DllImport("user32.dll", SetLastError=true)] static extern IntPtr SetWindowsHookEx(int id,Hook callback,IntPtr module,uint thread);
    [DllImport("user32.dll")] static extern bool UnhookWindowsHookEx(IntPtr hook);
    [DllImport("user32.dll")] static extern IntPtr CallNextHookEx(IntPtr hook,int code,UIntPtr message,IntPtr data);
    [DllImport("user32.dll")] static extern int GetMessage(out Message message,IntPtr window,uint min,uint max);
    [DllImport("user32.dll")] static extern bool PostThreadMessage(uint thread,uint message,UIntPtr w,IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr window,out uint process);
    [DllImport("kernel32.dll")] static extern uint GetCurrentThreadId();
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode)] static extern IntPtr GetModuleHandle(string name);
    static readonly object gate=new object(); static readonly List<string> records=new List<string>();
    static Thread worker; static uint owner,thread; static Hook callback; static IntPtr hook;
    static ManualResetEventSlim ready; static int error;
    public static void Start(uint process) {
        if(worker!=null)throw new InvalidOperationException("Input audit already running");
        owner=process;error=0;lock(gate)records.Clear();ready=new ManualResetEventSlim(false);
        worker=new Thread(Pump);worker.IsBackground=true;worker.Start();
        if(!ready.Wait(3000)||error!=0){Stop();throw new InvalidOperationException("Input audit failed: "+error);}
    }
    static void Pump() {
        thread=GetCurrentThreadId();callback=Observe;
        // 先建线程消息队列，保证退出通知不会丢失。
        Message message;PeekMessage(out message,IntPtr.Zero,0,0,0);
        hook=SetWindowsHookEx(13,callback,GetModuleHandle(null),0);
        if(hook==IntPtr.Zero)error=Marshal.GetLastWin32Error();ready.Set();
        try {if(hook!=IntPtr.Zero)while(GetMessage(out message,IntPtr.Zero,0,0)>0){} }
        finally {if(hook!=IntPtr.Zero)UnhookWindowsHookEx(hook);hook=IntPtr.Zero;}
    }
    [DllImport("user32.dll")] static extern bool PeekMessage(out Message message,IntPtr window,uint min,uint max,uint remove);
    static IntPtr Observe(int code,UIntPtr message,IntPtr data) {
        if(code>=0) {
            var key=Marshal.PtrToStructure<Key>(data);uint process;
            IntPtr foreground=GetForegroundWindow();GetWindowThreadProcessId(foreground,out process);
            if(process==owner&&(key.Vk==8||key.Vk==27))lock(gate) {
                records.Add("key="+key.Vk+" message="+message.ToUInt64()+" injected="+((key.Flags&16)!=0)+
                    " flags="+key.Flags+" extra="+key.Extra.ToUInt64()+" time="+key.Time+" foreground="+foreground);
            }
        }
        return CallNextHookEx(hook,code,message,data);
    }
    public static string Stop() {
        if(worker!=null){PostThreadMessage(thread,0x12,UIntPtr.Zero,IntPtr.Zero);if(!worker.Join(3000))throw new InvalidOperationException("Input audit did not stop");worker=null;ready.Dispose();}
        lock(gate)return string.Join("\n",records);
    }
}
'@
}
