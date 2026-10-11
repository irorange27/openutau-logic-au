using OpenUtau.Core.DiffSinger;
using System.Reflection;
using System.Runtime.Loader;
var coreDir = Path.GetDirectoryName(typeof(DiffSingerBasePhonemizer).Assembly.Location)!;
AssemblyLoadContext.Default.Resolving += (_, name) => {
    var path = Path.Combine(coreDir, name.Name + ".dll");
    return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
};
int cases = 0;
foreach (int budget in new[] { 1, 3, 12, 30, 60, 120, 480 }) {
    foreach (int lead in new[] { 1, 30, 120, 300, 960 }) {
        var phones = new List<Tuple<string, int>> {
            Tuple.Create("cl", -lead), Tuple.Create("k", -lead / 2),
            Tuple.Create("a", 0), Tuple.Create("N", 240)
        };
        DiffSingerBasePhonemizer.ConstrainPhraseLeadToRest(phones, budget);
        if (phones[0].Item2 < -budget || phones[1].Item2 < phones[0].Item2 ||
            phones[1].Item2 > 0 || phones[2].Item2 != 0 || phones[3].Item2 != 240)
            throw new Exception($"Invalid placement: budget={budget} lead={lead}");
        if (lead <= budget && phones[0].Item2 != -lead) throw new Exception("Unnecessary shift");
        cases++;
    }
}
foreach (int budget in new[] { 0, -1 }) {
    var phones = new List<Tuple<string, int>> { Tuple.Create("k", -120), Tuple.Create("a", 0) };
    DiffSingerBasePhonemizer.ConstrainPhraseLeadToRest(phones, budget);
    if (phones[0].Item2 != -120) throw new Exception("Connected note changed");
    cases++;
}
Console.WriteLine($"{cases} rest-boundary placement cases passed");
if (args.Length > 0) NativeChecks.Run(args[0]);
