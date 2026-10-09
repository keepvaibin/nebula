using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;

namespace Nebula.Setup
{
    // Private fixture inputs are supplied by the caller; no game data is checked in.
    internal static class RelinkCacheIntegrationTests
    {
        static readonly string[] Modules = {"RMGE01_game.dll", "RMGE01_home_button.dll", "RMGE01_dsp.dll"};
        static void CopyModules(string from, string to)
        { Directory.CreateDirectory(to); foreach (string n in Modules) File.Copy(Path.Combine(from,n),Path.Combine(to,n)); }
        public static int Main(string[] args)
        {
            string build=args[0], installed=args[1], libraries=args[2], linker=args[3], scratch=args[4];
            if (Directory.Exists(scratch)) throw new Exception("Use a new test directory.");
            var contract=new Dictionary<string,object>{{"nativeAbi",23},{"rsoAbi",2},{"dspProbeVersion",1},{"dspCapabilities",3}};
            ModuleCompatibility.Validate(installed,contract);
            string a=Path.Combine(scratch,"a"), b=Path.Combine(scratch,"b"), c=Path.Combine(scratch,"c"), d=Path.Combine(scratch,"d");
            CopyModules(installed,a);
            RelinkCache.Capture(build,a,libraries,"fixture");
            CopyModules(a,b);
            if (!RelinkCache.Update(a,b,libraries,"fixture",delegate(IList<string> command){throw new Exception("Unchanged libraries invoked linker");},Console.WriteLine)) throw new Exception("Cache not found");
            foreach(string n in Modules) if(FileUtil.Sha256File(Path.Combine(a,n))!=FileUtil.Sha256File(Path.Combine(b,n))) throw new Exception("Unchanged module modified");
            string manifestPath=Path.Combine(b,"relink-cache","manifest.json");
            var manifest=Json.Load(manifestPath);Json.Obj(manifest,"libraries")["galaxy_ppc_float.lib"]=new string('0',64);Json.Save(manifestPath,manifest);
            CopyModules(b,c);int links=0;
            RelinkCache.Update(b,c,libraries,"fixture",delegate(IList<string> command){
                ++links;using(var runner=new ProcessRunner())return runner.Run(linker,command,scratch,null,Console.WriteLine,CancellationToken.None);
            },Console.WriteLine);
            if(links!=3)throw new Exception("Expected three linker calls and zero compiler calls");
            ModuleCompatibility.Validate(c,contract);
            foreach(string target in new[]{"game","home_button","dsp"})
                foreach(Dictionary<string,object> entry in Json.List(Json.Obj(manifest,"objects"),target))
                    if(FileUtil.Sha256File(RelinkCache.CheckedPath(Path.Combine(c,"relink-cache"),Json.Str(entry,"path")))!=Json.Str(entry,"sha256"))throw new Exception("Compiled object changed");
            CopyModules(c,d);
            var first=(Dictionary<string,object>)Json.List(Json.Obj(manifest,"objects"),"game")[0];
            File.AppendAllText(RelinkCache.CheckedPath(Path.Combine(c,"relink-cache"),Json.Str(first,"path")),"corrupt");
            bool rejected=false;
            try{RelinkCache.Update(c,d,libraries,"fixture",delegate(IList<string> command){throw new Exception("Corruption reached linker");},Console.WriteLine);}
            catch(InvalidDataException){rejected=true;}
            if(!rejected)throw new Exception("Corrupted object accepted");
            Console.WriteLine("PASS: real module ABI; original objects captured; unchanged libraries preserve DLL hashes; three successful links without compilation; objects unchanged; corrupt cache rejected before link.");
            return 0;
        }
    }
}
