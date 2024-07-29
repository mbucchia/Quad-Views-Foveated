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
            var proc = Process.GetProcessesByName("msiexec").FirstOrDefault(p => p.MainWindowTitle == "OpenXR-Quad-Views-Foveated");
            var owner = proc != null ? new WindowWrapper(proc.MainWindowHandle) : null;

            Microsoft.Win32.RegistryKey key;
            {
                key = Microsoft.Win32.Registry.LocalMachine.CreateSubKey("SOFTWARE\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
                var jsonPath = installPath + "\\openxr-api-layer.json";

                var existingValues = key.GetValueNames();

                ReOrderApiLayers(key, jsonPath);

                // Issue the appropriate compatibility warnings.
                foreach (var value in existingValues)
                {
                    if (value.EndsWith("\\XR_APILAYER_MBUCCHIA_toolkit.json"))
                    {
                        MessageBox.Show(owner, "OpenXR Toolkit was detected on your system.\n\n" +
                            "OpenXR Toolkit is compatible with applications such as DCS World or Pavlov VR when they use quad views rendering with this tool.\n" +
                            "However you might need to clear all prior OpenXR Toolkit settings for these applications, by using OpenXR Toolkit Safe Mode then selecting Menu -> Restore Defaults in the application.",
                            "Warning", MessageBoxButtons.OK, MessageBoxIcon.Warning);
                    }
                }

                key.Close();
            }
            {
                key = Microsoft.Win32.Registry.LocalMachine.CreateSubKey("SOFTWARE\\WOW6432Node\\Khronos\\OpenXR\\1\\ApiLayers\\Implicit");
                var jsonPath = installPath + "\\openxr-api-layer-32.json";

                ReOrderApiLayers(key, jsonPath);

                key.Close();
            }

            base.OnAfterInstall(savedState);
        }

        private void ReOrderApiLayers(Microsoft.Win32.RegistryKey key, string jsonPath)
        {
            // We want to add our layer near the very beginning, so that any other layer like the OpenXR Toolkit layer are following us.
            // However, the layer seem to confuse XRNeckSafer and OpenXR-MotionCompensation, so we want to add ourselves after them.
            // We delete all entries, re-create the XRNeckSafer and OpenXR-MotionCompensation ones if needed, then create our own, and re-create the remaining entries.

            List<String> existingValues = new List<String>(key.GetValueNames());
            var entriesValues = new Dictionary<string, object>();
            foreach (var value in existingValues)
            {
                entriesValues.Add(value, key.GetValue(value));
                key.DeleteValue(value);
            }

            void PullToFront(string endsWith)
            {
                var index = existingValues.FindIndex(entry => entry.EndsWith(endsWith));
                if (index > 0)
                {
                    existingValues.Insert(0, existingValues[index]);
                    existingValues.RemoveAt(1 + index);
                }
            }

            // Start at the beginning.
            existingValues.Insert(0, jsonPath);
            entriesValues.Remove(jsonPath);
            entriesValues.Add(jsonPath, 0);

            // Do not re-create keys for previous versions of our layer.
            existingValues.RemoveAll(entry => entry.EndsWith("OpenXR-Meta-Foveated\\openxr-api-layer.json"));

            // XRNeckSafer must always be first. OXRMC can be next.
            // We pull them in the opposite order.
            PullToFront("\\XR_APILAYER_NOVENDOR_motion_compensation.json");
            PullToFront("\\XR_APILAYER_NOVENDOR_XRNeckSafer.json");

            // Now we create the keys in the desired order.
            foreach (var value in existingValues)
            {
                key.SetValue(value, entriesValues[value]);
            }
        }
    }
}
