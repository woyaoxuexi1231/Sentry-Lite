using System.Runtime.InteropServices;

namespace SentryLite.LhmBridge;

// Delegates for hostfxr load_assembly_and_get_function_pointer (must be public, named).
public delegate int CpuTempInitDelegate();
public delegate int CpuTempReadDelegate(IntPtr celsiusPtr);
public delegate void CpuTempShutdownDelegate();

public static class CpuTempApi
{
    private static LiteMonitorComputer? _hw;
    private static readonly object Gate = new();

    public static int InitNative()
    {
        lock (Gate)
        {
            try
            {
                ShutdownNative();
                _hw = new LiteMonitorComputer();
                _hw.Open();
                return 0;
            }
            catch (Exception ex)
            {
                try { _hw?.Dispose(); } catch { }
                _hw = null;
                System.Diagnostics.Debug.WriteLine("InitNative: " + ex);
                return -1;
            }
        }
    }

    public static int ReadNative(IntPtr celsiusPtr)
    {
        lock (Gate)
        {
            try
            {
                if (_hw == null || celsiusPtr == IntPtr.Zero)
                    return 0;

                _hw.UpdateCpu();
                var picked = _hw.GetCpuTemp();
                unsafe
                {
                    var dst = (float*)celsiusPtr;
                    if (picked is > 0 and <= 125f)
                    {
                        *dst = picked.Value;
                        return 1;
                    }

                    *dst = float.NaN;
                }

                return 0;
            }
            catch
            {
                return 0;
            }
        }
    }

    public static void ShutdownNative()
    {
        lock (Gate)
        {
            if (_hw == null) return;
            try { _hw.Dispose(); } catch { }
            _hw = null;
        }
    }
}
