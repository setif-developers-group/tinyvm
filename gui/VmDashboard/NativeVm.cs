using System.Runtime.InteropServices;
using System.Text;

namespace VmDashboard;

public enum VmStatus { Running = 0, Halted = 1, IllegalOpcode = 2, DivisionByZero = 3, MemoryFault = 4 }

/// <summary>Managed wrapper around the C engine (src/c/vm.c) using P/Invoke.</summary>
public sealed class VirtualMachine : IDisposable
{
    public const int RegisterCount = 16;
    public const int MemorySize = 65536;

    private const string Lib = "vm"; // vm.dll on Windows, libvm.so / libvm.dylib elsewhere

    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern IntPtr vm_create();
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void vm_destroy(IntPtr vm);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern void vm_reset(IntPtr vm);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int vm_load(IntPtr vm, byte[] data, UIntPtr size, uint addr);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int vm_step(IntPtr vm);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern uint vm_get_reg(IntPtr vm, int index);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern uint vm_get_pc(IntPtr vm);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern uint vm_get_flags(IntPtr vm);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int vm_get_status(IntPtr vm);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern ulong vm_get_cycles(IntPtr vm);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int vm_read_mem(IntPtr vm, uint addr, byte[] buffer, UIntPtr len);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern UIntPtr vm_take_output(IntPtr vm, byte[] buffer, UIntPtr capacity);
    [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)] private static extern int vm_disasm_at(IntPtr vm, uint addr, byte[] buffer, UIntPtr capacity);

    private IntPtr _handle;

    public VirtualMachine()
    {
        _handle = vm_create();
        if (_handle == IntPtr.Zero) throw new OutOfMemoryException("vm_create failed");
    }

    public uint Pc => vm_get_pc(_handle);
    public uint Flags => vm_get_flags(_handle);
    public ulong Cycles => vm_get_cycles(_handle);
    public VmStatus Status => (VmStatus)vm_get_status(_handle);
    public uint Register(int index) => vm_get_reg(_handle, index);

    public void Reset() => vm_reset(_handle);
    public VmStatus Step() { vm_step(_handle); return Status; }

    public bool Load(byte[] image) => vm_load(_handle, image, (UIntPtr)image.Length, 0) == 0;

    public byte[] ReadMemory(uint address, int length)
    {
        var buffer = new byte[length];
        return vm_read_mem(_handle, address, buffer, (UIntPtr)length) == 0 ? buffer : Array.Empty<byte>();
    }

    public string TakeOutput()
    {
        var buffer = new byte[4096];
        int n = (int)vm_take_output(_handle, buffer, (UIntPtr)buffer.Length);
        return Encoding.Latin1.GetString(buffer, 0, n);
    }

    public string DisassembleAt(uint address)
    {
        var buffer = new byte[64];
        vm_disasm_at(_handle, address, buffer, (UIntPtr)buffer.Length);
        int end = Array.IndexOf(buffer, (byte)0);
        return Encoding.ASCII.GetString(buffer, 0, end < 0 ? buffer.Length : end);
    }

    public void Dispose()
    {
        if (_handle != IntPtr.Zero) { vm_destroy(_handle); _handle = IntPtr.Zero; }
    }
}
