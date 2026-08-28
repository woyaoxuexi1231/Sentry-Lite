using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace SentryLite.LhmBridge;

internal static class Program
{
    private const uint AttachParentProcess = 0xFFFFFFFFu;

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AttachConsole(uint dwProcessId);

    [DllImport("kernel32.dll", SetLastError = true)]
    private static extern bool AllocConsole();

    [STAThread]
    public static int Main(string[] args)
    {
        if (args.Any(a => string.Equals(a, "--probe-api", StringComparison.OrdinalIgnoreCase)))
        {
            ApplicationConfiguration.Initialize();
            return RunProbeApi();
        }

        if (args.Any(a => string.Equals(a, "--probe", StringComparison.OrdinalIgnoreCase)))
        {
            ApplicationConfiguration.Initialize();
            if (!AttachConsole(AttachParentProcess))
                AllocConsole();
            return RunProbe();
        }

        return 0;
    }

    private static int RunProbeApi()
    {
        try
        {
            var outPath = Path.Combine(AppContext.BaseDirectory, "probe-api.txt");
            var lines = new List<string> { "Sentry-Lite-lhm probe-api (CpuTempApi in-process path)" };

            if (CpuTempApi.InitNative() != 0)
            {
                lines.Add("InitNative => failed");
                File.WriteAllLines(outPath, lines);
                return 1;
            }
            lines.Add("InitNative => ok");

            unsafe
            {
                float t = 0;
                for (var i = 0; i < 30; i++)
                {
                    var ok = CpuTempApi.ReadNative((IntPtr)(&t));
                    lines.Add($"ReadNative[{i}] => ok={ok} t={t:F1}");
                    if (ok != 0 && !float.IsNaN(t) && t > 0 && t <= 125)
                        break;
                    Thread.Sleep(500);
                }
            }

            CpuTempApi.ShutdownNative();
            File.WriteAllLines(outPath, lines);
            return lines.Any(l => l.Contains("ok=1") && !l.Contains("t=0.0")) ? 0 : 1;
        }
        catch (Exception ex)
        {
            var outPath = Path.Combine(AppContext.BaseDirectory, "probe-api.txt");
            File.WriteAllText(outPath, ex.ToString());
            return 1;
        }
    }

    private static int RunProbe()
    {
        var outPath = Path.Combine(AppContext.BaseDirectory, "probe-out.txt");
        using var hw = new LiteMonitorComputer();
        try
        {
            var lines = new List<string> { "Sentry-Lite-lhm probe (LiteMonitor-style LHM init)" };
            hw.Open();
            hw.UpdateCpu();

            foreach (var (name, value) in hw.CpuTemperatureSensors())
            {
                var v = value.HasValue ? value.Value.ToString("F1") : "(null)";
                lines.Add($"  {name}: {v} C");
            }

            var picked = hw.GetCpuTemp();
            lines.Add($"GetCpuTemp => {(picked.HasValue ? picked.Value.ToString("F1") : "null")} C");
            File.WriteAllLines(outPath, lines);
            return picked.HasValue ? 0 : 1;
        }
        catch (Exception ex)
        {
            File.WriteAllText(outPath, ex.ToString());
            return 1;
        }
    }
}
