// Mirror LiteMonitor HardwareMonitor CPU init (in-process LHM, same as LiteMonitor.exe).
using LibreHardwareMonitor.Hardware;

namespace SentryLite.LhmBridge;

internal sealed class LiteMonitorComputer : IDisposable
{
    private readonly Computer _computer = new();
    private readonly ComponentProcessor _processor;
    private readonly object _lock = new();

    public LiteMonitorComputer()
    {
        // LiteMonitor HardwareMonitor constructor — same flags.
        _computer.IsCpuEnabled = true;
        _computer.IsGpuEnabled = true;
        _computer.IsMemoryEnabled = true;
        _computer.IsNetworkEnabled = true;
        _computer.IsStorageEnabled = true;
        _computer.IsMotherboardEnabled = true;
        _computer.IsControllerEnabled = false;
        _computer.IsBatteryEnabled = true;
        _computer.IsPsuEnabled = false;

        _processor = new ComponentProcessor(_computer);
    }

    public void Open()
    {
        lock (_lock)
        {
            // Match LiteMonitor: Open only (no multi-second CPU warm-up on UI/startup path).
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

    public IReadOnlyList<(string Name, float? Value)> CpuTemperatureSensors()
    {
        lock (_lock)
        {
            var cpu = _computer.Hardware.FirstOrDefault(h => h.HardwareType == HardwareType.Cpu);
            if (cpu == null) return Array.Empty<(string, float?)>();
            return cpu.Sensors
                .Where(s => s.SensorType == SensorType.Temperature)
                .Select(s => (s.Name, s.Value))
                .ToList();
        }
    }

    public void Dispose()
    {
        lock (_lock)
        {
            try { _computer.Close(); } catch { }
        }
    }
}
