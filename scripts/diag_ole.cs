using System;
using System.Reflection;

class DiagOle
{
    static void Main()
    {
        string dir = Environment.GetEnvironmentVariable("TEMP") + @"\wpd_pkgs";
        string[] dlls =
        {
            dir + @"\system.memory\lib\net462\System.Memory.dll",
            dir + @"\system.runtime.compilerservices.unsafe\lib\net462\System.Runtime.CompilerServices.Unsafe.dll",
            dir + @"\microsoft.win32.registry\lib\net461\Microsoft.Win32.Registry.dll",
            dir + @"\system.security.accesscontrol\lib\net461\System.Security.AccessControl.dll",
            dir + @"\system.componentmodel.annotations\lib\net461\System.ComponentModel.Annotations.dll",
            dir + @"\vanara.pinvoke.rpc\lib\net48\Vanara.PInvoke.Rpc.dll",
            dir + @"\vanara.core\lib\net48\Vanara.Core.dll",
            dir + @"\vanara.pinvoke.ole\lib\net48\Vanara.PInvoke.Ole.dll"
        };
        foreach (var d in dlls)
        {
            try { Assembly.LoadFrom(d); }
            catch (Exception ex) { Console.WriteLine("load fail " + d + ": " + ex.Message); }
        }
        var asm = Assembly.LoadFrom(dlls[7]);
        try
        {
            var types = asm.GetTypes();
            Console.WriteLine("Ole types OK: " + types.Length);
            foreach (var t in types)
                if (t.Name == "PROPERTYKEY" || t.Name == "PROPVARIANT" || t.Name == "PROPVARIANT_UNMGD")
                    Console.WriteLine("  type: " + t.FullName);
        }
        catch (ReflectionTypeLoadException ex)
        {
            Console.WriteLine("LoaderExceptions: " + (ex.LoaderExceptions == null ? 0 : ex.LoaderExceptions.Length));
            if (ex.LoaderExceptions != null)
                foreach (var le in ex.LoaderExceptions)
                    if (le != null) Console.WriteLine("  " + le.Message);
        }
    }
}
