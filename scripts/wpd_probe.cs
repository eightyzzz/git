using System;
using System.Collections.Generic;
using System.Reflection;
using Vanara.PInvoke;

class WpdProbe
{
    static readonly Guid OBJECT = new Guid("EF6B490D-5CD8-437A-AFFC-DA8B7124C3E1");

    static void Main()
    {
        var mgr = new PortableDeviceApi.PortableDeviceManager();
        PortableDeviceApi.IPortableDeviceManager im =
            (PortableDeviceApi.IPortableDeviceManager)mgr;
        uint count = 0;
        im.GetDevices(null, ref count);
        if (count == 0) { Console.WriteLine("no devices"); return; }
        var devices = new string[count];
        im.GetDevices(devices, ref count);
        foreach (var id in devices)
        {
            if (string.IsNullOrEmpty(id)) continue;
            Console.WriteLine("device: " + id);
            var pd = new PortableDeviceApi.PortableDevice();
            PortableDeviceApi.IPortableDevice ipd =
                (PortableDeviceApi.IPortableDevice)pd;
            try
            {
                var ci = new PortableDeviceApi.PortableDeviceValues();
                PortableDeviceApi.IPortableDeviceValues ici =
                    (PortableDeviceApi.IPortableDeviceValues)ci;
                ipd.Open(id, ici);
                PortableDeviceApi.IPortableDeviceContent content = ipd.Content();
                var found = new List<string>();
                Collect(content, "DEVICE", found);
                foreach (var oid in found)
                {
                    if (!oid.StartsWith("o")) continue;
                    Console.WriteLine("=== " + oid + " ===");
                    Probe(content, oid);
                    break;
                }
                ipd.Close();
            }
            catch (Exception ex)
            {
                Console.WriteLine("  open failed: " + ex.Message);
            }
        }
    }

    static void Collect(PortableDeviceApi.IPortableDeviceContent content,
                        string parent, List<string> all)
    {
        try
        {
            var e = content.EnumObjects(0, parent, null);
            var buf = new string[256];
            uint fetched = 0;
            e.Next(256, buf, out fetched);
            for (int i = 0; i < (int)fetched && all.Count < 64; i++)
            {
                if (string.IsNullOrEmpty(buf[i]) || buf[i] == parent) continue;
                all.Add(buf[i]);
                if (all.Count < 64) Collect(content, buf[i], all);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("  collect failed: " + ex.Message);
        }
    }

    static void Probe(PortableDeviceApi.IPortableDeviceContent content, string oid)
    {
        PortableDeviceApi.IPortableDeviceProperties props = content.Properties();

        // GetSupportedProperties: works in the enum tool for storage but
        // fails for file objects. Show exactly what happens here.
        try
        {
            PortableDeviceApi.IPortableDeviceKeyCollection keys =
                props.GetSupportedProperties(oid);
            uint kn = keys.GetCount();
            Console.WriteLine("  GetSupportedProperties OK, keys=" + kn);
            for (uint i = 0; i < kn && i < 40; i++)
            {
                Ole32.PROPERTYKEY k = keys.GetAt(i);
                Console.WriteLine("    pid" + k.pid);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("  GetSupportedProperties FAILED: " + ex.Message);
        }

        TryGetValuesExplicitKeys(props, oid);
    }

    static void TryGetValuesExplicitKeys(
        PortableDeviceApi.IPortableDeviceProperties props, string oid)
    {
        try
        {
            var kc = new PortableDeviceApi.PortableDeviceKeyCollection();
            PortableDeviceApi.IPortableDeviceKeyCollection ik =
                (PortableDeviceApi.IPortableDeviceKeyCollection)kc;
            MethodInfo add = typeof(PortableDeviceApi.IPortableDeviceKeyCollection)
                .GetMethod("Add");
            if (add == null)
                Console.WriteLine("    !! Add method not found");
            foreach (int pid in new int[] { 1, 2, 3, 4, 6, 8, 14 })
            {
                Ole32.PROPERTYKEY k = new Ole32.PROPERTYKEY(OBJECT, (uint)pid);
                object[] args = new object[] { k };
                try { add.Invoke(ik, args); }
                catch (Exception ex) { Console.WriteLine("    add err: " + ex.Message); }
            }
            Console.WriteLine("    built key collection: " + ik.GetCount());
            PortableDeviceApi.IPortableDeviceValues vals = props.GetValues(oid, ik);
            Console.WriteLine("    GetValues(keys) OK, values=" + vals.GetCount());
        }
        catch (Exception ex)
        {
            Console.WriteLine("    GetValues(keys) FAILED: " + ex.Message);
        }
    }

}
