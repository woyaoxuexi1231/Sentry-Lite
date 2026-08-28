// CPU-only LHM host — only what Sentry-Lite needs for package temperature.
using LibreHardwareMonitor.Hardware;

namespace SentryLite.LhmBridge;

internal sealed class LiteMonitorComputer : IDisposable
{
    private readonly Computer _computer = new();
    private readonly ComponentProcessor _processor;
    private readonly object _lock = new();

    public LiteMonitorComputer()
    {
        // Only CPU — enabling GPU/storage/network/etc. pulls large sensor trees into the process.
        _computer.IsCpuEnabled = true;
        _computer.IsGpuEnabled = false;
        _computer.IsMemoryEnabled = false;
        _computer.IsNetworkEnabled = false;
        _computer.IsStorageEnabled = false;
        _computer.IsMotherboardEnabled = false;
        _computer.IsControllerEnabled = false;
        _computer.IsBatteryEnabled = false;
        _computer.IsPsuEnabled = false;

        _processor = new ComponentProcessor(_computer);
    }

    public void Open()
    {
        lock (_lock)
        {
            _computer.Open();
            UpdateCpu();
        }
    }

    public void UpdateCpu()
    {
        lock (_lock)
        {
            foreach (var hw in _computer.Hardware)
            {
                if (hw.HardwareType == HardwareType.Cpu)
                    hw.Update();
            }
        }
    }

    public float? GetCpuTemp() => _processor.GetCpuTemp();

    public void Dispose()
    {
        lock (_lock)
        {
            try { _computer.Close(); } catch { }
        }
    }
}
