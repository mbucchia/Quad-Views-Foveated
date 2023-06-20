using System;
using System.Collections;
using System.Collections.Generic;
using System.ComponentModel;
using System.Configuration;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Windows.Forms;

// Reference: https://www.c-sharpcorner.com/article/how-to-perform-custom-actions-and-upgrade-using-visual-studio-installer/
namespace SetupCustomActions
{
    [RunInstaller(true)]
    public partial class CustomActions : System.Configuration.Install.Installer
    {
        public CustomActions()
        {
        }

        private class WindowWrapper : IWin32Window
        {
            private readonly IntPtr hwnd;

            public IntPtr Handle
            {
                get { return hwnd; }
            }

            public WindowWrapper(IntPtr handle)
            {
                hwnd = handle;
            }
        }

        protected override void OnAfterInstall(IDictionary savedState)
        {
            var installPath = Path.GetDirectoryName(base.Context.Parameters["AssemblyPath"]);

            // https://stackoverflow.com/questions/6213498/custom-installer-in-net-showing-form-behind-installer
            var proc = Process.GetProcessesByName("msiexec").FirstOrDefault(p => p.MainWindowTitle == "OpenXR-Meta-Foveated");
            var owner = proc != null ? new WindowWrapper(proc.MainWindowHandle) : null;

            // We want to add our layer at the very beginning, so that any other layer like the OpenXR Toolkit layer is following us.
            // We delete all entries, create our own, and recreate all entries.

            Microsoft.Win32.RegistryKey key;
            {
                key = Microsoft.Win32.Registry.LocalMachine.CreateSubKey("SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
                var jsonName = "OpenXR-Meta-Foveated\\openxr-api-layer.json";
                var jsonPath = installPath + "\\openxr-api-layer.json";

                var existingValues = key.GetValueNames();

                ReOrderApiLayers(key, jsonName, jsonPath);

                // Issue the appropriate compatibility warnings.
                foreach (var value in existingValues)
                {
                    if (value.EndsWith("\\XR_APILAYER_MBUCCHIA_toolkit.json"))
                    {
                        MessageBox.Show(owner, "OpenXR Toolkit was detected on your system.\n\n" +
                            "OpenXR Toolkit is not compatible with applications such as DCS World or Pavlov VR when they use quad views rendering with this tool.\n" +
                            "Please make sure to disable OpenXR Toolkit for the applications that will use quad views rendering.",
                            "Warning", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    }
                }

                key.Close();
            }
            try
            {
                key = Microsoft.Win32.Registry.LocalMachine.CreateSubKey("SOFTWARE\\Khronos\\OpenXR\\1");
                var activeRuntime = (string)key.GetValue("ActiveRuntime", "");

                if (!activeRuntime.EndsWith("\\oculus_openxr_64.json"))
                {
                    MessageBox.Show(owner, "Oculus OpenXR was not detected as your active OpenXR runtime.\n\n" +
                        "Please make sure to set Oculus as your OpenXR runtime from the Oculus application in order to use the eye tracking capabilities of your device.",
                        "Warning", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                }
            }
            catch (Exception)
            {
            }
            finally
            {
                if (key != null)
                {
                    key.Close();
                }
            }

            base.OnAfterInstall(savedState);
        }

        private void ReOrderApiLayers(Microsoft.Win32.RegistryKey key, string jsonName, string jsonPath)
        {
            var existingValues = key.GetValueNames();
            var entriesValues = new Dictionary<string, object>();
            foreach (var value in existingValues)
            {
                entriesValues.Add(value, key.GetValue(value));
                key.DeleteValue(value);
            }

            key.SetValue(jsonPath, 0);
            foreach (var value in existingValues)
            {
                // Do not re-create our own key. We did it before this loop.
                if (value.EndsWith("\\" + jsonName))
                {
                    continue;
                }

                key.SetValue(value, entriesValues[value]);
            }
        }
    }
}
