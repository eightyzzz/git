using System;
using System.Collections.Generic;
using Vanara.PInvoke;

class WpdEnum
{
    static void Main()
    {
        var mgr = new PortableDeviceApi.PortableDeviceManager();
        PortableDeviceApi.IPortableDeviceManager im =
            (PortableDeviceApi.IPortableDeviceManager)mgr;
        uint count = 0;
        im.GetDevices(null, ref count);
        Console.WriteLine("GetDevices count={0}", count);
        if (count == 0) return;
        var devices = new string[count];
        im.GetDevices(devices, ref count);
        foreach (var id in devices)
        {
            if (string.IsNullOrEmpty(id)) continue;
            Console.WriteLine("device id: " + id);
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
                var all = new List<string>();
                Collect(content, "DEVICE", all);
                foreach (var oid in all)
                {
                    Console.WriteLine("=== " + oid + " ===");
                    DumpProps(content, oid);
                }
                ipd.Close();
            }
            catch (Exception ex)
            {
                Console.WriteLine("  open failed: " + ex.Message);
            }
        }
    }

    static void Collect(PortableDeviceApi.IPortableDeviceContent content, string parent, List<string> all)
    {
        try
        {
            var e = content.EnumObjects(0, parent, null);
            var buf = new string[256];
            uint fetched = 0;
            e.Next(256, buf, out fetched);
            Console.WriteLine("  enum under " + parent + ": fetched=" + fetched);
            for (int i = 0; i < (int)fetched && all.Count < 64; i++)
            {
                if (string.IsNullOrEmpty(buf[i]) || buf[i] == parent) continue;
                all.Add(buf[i]);
                Console.WriteLine("  obj: [" + buf[i] + "]");
                if (all.Count < 64) Collect(content, buf[i], all);
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("  collect failed: " + ex.Message);
        }
    }

    static void DumpProps(PortableDeviceApi.IPortableDeviceContent content, string oid)
    {
        PortableDeviceApi.IPortableDeviceProperties props = content.Properties();
        PortableDeviceApi.IPortableDeviceKeyCollection keys = null;
        try
        {
            keys = props.GetSupportedProperties(oid);
            Console.WriteLine("  GetSupportedProperties OK, keys=" + keys.GetCount());
            for (uint i = 0; i < keys.GetCount(); i++)
            {
                try
                {
                    Ole32.PROPERTYKEY key = keys.GetAt(i);
                    Console.WriteLine("      key: " + KeyName(key));
                }
                catch (Exception ex)
                {
                    Console.WriteLine("      key getat err: " + ex.Message);
                }
            }
        }
        catch (Exception ex)
        {
            Console.WriteLine("  GetSupportedProperties FAILED: " + ex.Message);
        }
        if (keys != null)
        {
            try
            {
                PortableDeviceApi.IPortableDeviceValues values = props.GetValues(oid, keys);
                Console.WriteLine("  GetValues OK, values=" + values.GetCount());
            }
            catch (Exception ex)
            {
                Console.WriteLine("  GetValues FAILED: " + ex.Message);
            }
        }
    }

    static string KeyName(Ole32.PROPERTYKEY k)
    {
        if (k.fmtid == new Guid("EF6B490D-5CD8-437A-AFFC-DA8B7124C3E1"))
        {
            switch (k.pid)
            {
                case 1: return "WPD_OBJECT_NAME";
                case 2: return "WPD_OBJECT_PERSISTENT_UNIQUE_ID";
                case 3: return "WPD_OBJECT_PARENT_ID";
                case 4: return "WPD_OBJECT_FORMAT";
                case 5: return "WPD_OBJECT_CONTENT_TYPE";
                case 6: return "WPD_OBJECT_IS_DRM_PROTECTED";
                case 7: return "WPD_OBJECT_CAN_DELETE";
                case 8: return "WPD_OBJECT_SIZE";
                case 10: return "WPD_OBJECT_DATE_CREATED";
                case 11: return "WPD_OBJECT_DATE_MODIFIED";
                case 12: return "WPD_OBJECT_ISHIDDEN";
                case 13: return "WPD_OBJECT_ISSYSTEM";
                case 14: return "WPD_OBJECT_ORIGINAL_FILE_NAME";
                case 15: return "WPD_OBJECT_KEYWORDS";
                case 16: return "WPD_OBJECT_DATE_AUTHORED";
                case 19: return "WPD_OBJECT_REFERENCES";
                default: return "WPD_OBJECT_PID_" + k.pid;
            }
        }
        if (k.fmtid == new Guid("8D74735C-1D1F-4C48-9BC2-4A0B2A2C4BF0"))
            return "WPD_STORAGE_PID_" + k.pid;
        return k.fmtid.ToString("B") + ":" + k.pid;
    }
}
