namespace SentryLite.LhmBridge;

internal static class Program
{
    public static int Main(string[] args)
    {
        var outPath = Path.Combine(AppContext.BaseDirectory, "probe-api.txt");
        try
        {
            var lines = new List<string> { "Sentry-Lite-lhm-probe (CpuTempApi)" };
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
                for (var i = 0; i < 10; i++)
                {
                    var ok = CpuTempApi.ReadNative((IntPtr)(&t));
                    lines.Add($"ReadNative[{i}] => ok={ok} t={t:F1}");
                    if (ok != 0 && !float.IsNaN(t) && t > 0 && t <= 125)
                        break;
                    Thread.Sleep(200);
                }
            }

            CpuTempApi.ShutdownNative();
            File.WriteAllLines(outPath, lines);
            return lines.Any(l => l.Contains("ok=1")) ? 0 : 1;
        }
        catch (Exception ex)
        {
            File.WriteAllText(outPath, ex.ToString());
            return 1;
        }
    }
}
