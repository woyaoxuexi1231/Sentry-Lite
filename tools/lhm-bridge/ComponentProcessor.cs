// Copied from LiteMonitor src/System/HardwareServices/ComponentProcessor.cs (GetCpuTemp path).
using LibreHardwareMonitor.Hardware;

namespace SentryLite.LhmBridge;

internal sealed class ComponentProcessor
{
    private readonly Computer _computer;
    private List<ISensor>? _cpuTempSensorsCache;

    public ComponentProcessor(Computer computer) => _computer = computer;

    public void ClearCache() => _cpuTempSensorsCache = null;

    public float? GetCpuTemp()
    {
        if (_cpuTempSensorsCache == null)
        {
            var cpu = _computer.Hardware.FirstOrDefault(h => h.HardwareType == HardwareType.Cpu);
            if (cpu != null)
            {
                _cpuTempSensorsCache = new List<ISensor>();
                foreach (var s in cpu.Sensors)
                {
                    if (s.SensorType == SensorType.Temperature &&
                        !Has(s.Name, "Distance") &&
                        !Has(s.Name, "Average") &&
                        !Has(s.Name, "Max"))
                    {
                        _cpuTempSensorsCache.Add(s);
                    }
                }
            }
        }

        if (_cpuTempSensorsCache is not { Count: > 0 }) return null;

        float maxTemp = -1000f;
        bool found = false;
        foreach (var s in _cpuTempSensorsCache)
        {
            if (s.Value.HasValue && s.Value.Value > 0f)
            {
                if (s.Value.Value > maxTemp)
                {
                    maxTemp = s.Value.Value;
                    found = true;
                }
            }
        }

        return found ? maxTemp : null;
    }

    private static bool Has(string source, string sub) =>
        source.Contains(sub, StringComparison.OrdinalIgnoreCase);
}
