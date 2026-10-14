using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Threading;
using System.Windows.Forms;

// Windows APIs come only from System32, so a DLL dropped next to Rug.exe (often the Downloads folder) is never loaded.
[assembly: DefaultDllImportSearchPaths(DllImportSearchPath.System32)]

namespace Rug;

internal static class Program
{
    private const string SingleInstanceName = @"Local\Rug.SingleInstance";
    private const string ShowSettingsName = @"Local\Rug.ShowSettings";

    [STAThread]
    private static void Main()
    {
        // rug_native.dll is loaded by its full path in the app folder and nowhere else.
        NativeLibrary.SetDllImportResolver(typeof(Program).Assembly, (name, _, _) =>
            name == "rug_native.dll" ? NativeLibrary.Load(Path.Combine(AppContext.BaseDirectory, name)) : IntPtr.Zero);

        using var mutex = new Mutex(true, SingleInstanceName, out bool first);
        using var showSettings = new EventWaitHandle(false, EventResetMode.AutoReset, ShowSettingsName);

        if (!first)
        {

            showSettings.Set();
            return;
        }

        Application.EnableVisualStyles();
        Application.SetCompatibleTextRenderingDefault(false);

        var settings = AppSettings.Load();
        var tray = new TrayContext(settings);

        new Thread(() =>
        {
            while (true)
            {
                showSettings.WaitOne();
                tray.ShowSettingsFromAnyThread();
            }
        }) { IsBackground = true, Name = "Rug show settings" }.Start();

        tray.ShowSettings();
        Application.Run(tray);
    }
}
