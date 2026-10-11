using Mono.Cecil;
using Mono.Cecil.Cil;
if (args.Length != 3) throw new ArgumentException("Expected original core, built core, output core");
var resolver = new DefaultAssemblyResolver();
resolver.AddSearchDirectory(Path.GetDirectoryName(args[0]));
resolver.AddSearchDirectory(Path.GetDirectoryName(args[1]));
using var original = ModuleDefinition.ReadModule(args[0], new ReaderParameters { AssemblyResolver = resolver });

var preserved = AllTypes(original.Types).SelectMany(t => t.Methods).ToDictionary(m => m.FullName, Describe);
using var built = ModuleDefinition.ReadModule(args[1], new ReaderParameters { AssemblyResolver = resolver });
var typeName = "OpenUtau.Core.DiffSinger.DiffSingerBasePhonemizer";
var destType = original.GetType(typeName); var sourceType = built.GetType(typeName);
var names = new[] { "SetUp", "ConstrainPhraseLeadToRest" };
foreach (var name in names) {
    if (destType.Methods.Any(m => m.Name == name)) throw new Exception("Already patched: " + name);
}
var methods = sourceType.Methods.Where(m => names.Contains(m.Name)).ToArray();
var methodMap = new Dictionary<MethodDefinition, MethodDefinition>();
foreach (var m in methods) {
    var copy = new MethodDefinition(m.Name, m.Attributes, original.ImportReference(m.ReturnType));
    foreach (var p in m.Parameters) copy.Parameters.Add(new ParameterDefinition(p.Name, p.Attributes, ImportType(p.ParameterType)));
    destType.Methods.Add(copy); methodMap[m] = copy;
}
TypeReference ImportType(TypeReference t) => original.GetType(t.FullName) ?? original.ImportReference(t);
MethodReference ImportMethod(MethodReference m) {
    var local = original.GetType(m.DeclaringType.FullName);
    if (local != null) {
        var found = local.Methods.FirstOrDefault(x => x.FullName == m.FullName);
        if (found != null) return found;
    }
    return original.ImportReference(m);
}
foreach (var m in methods) {
    var copy = methodMap[m]; copy.Body.InitLocals = m.Body.InitLocals;
    var variables = new Dictionary<VariableDefinition, VariableDefinition>();
    foreach (var v in m.Body.Variables) { var nv = new VariableDefinition(ImportType(v.VariableType)); copy.Body.Variables.Add(nv); variables[v] = nv; }
    var instructions = new Dictionary<Instruction, Instruction>();
    foreach (var i in m.Body.Instructions) { var ni = Instruction.Create(OpCodes.Nop); ni.OpCode = i.OpCode; copy.Body.Instructions.Add(ni); instructions[i] = ni; }
    foreach (var i in m.Body.Instructions) {
        instructions[i].Operand = i.Operand switch {
            Instruction target => instructions[target], Instruction[] targets => targets.Select(x => instructions[x]).ToArray(),
            VariableDefinition v => variables[v], ParameterDefinition p => copy.Parameters[p.Index],
            MethodReference mr => ImportMethod(mr),
            FieldReference f => original.GetType(f.DeclaringType.FullName)?.Fields.FirstOrDefault(x => x.FullName == f.FullName) ?? original.ImportReference(f),
            TypeReference t => ImportType(t), _ => i.Operand
        };
    }
    foreach (var handler in m.Body.ExceptionHandlers) {
        Instruction Map(Instruction x) => x == null ? null : instructions[x];
        copy.Body.ExceptionHandlers.Add(new ExceptionHandler(handler.HandlerType) {
            TryStart = Map(handler.TryStart), TryEnd = Map(handler.TryEnd),
            HandlerStart = Map(handler.HandlerStart), HandlerEnd = Map(handler.HandlerEnd),
            FilterStart = Map(handler.FilterStart), CatchType = handler.CatchType == null ? null : ImportType(handler.CatchType)
        });
    }
}
original.Attributes = ModuleAttributes.ILOnly;
original.Architecture = TargetArchitecture.I386;
original.Mvid = new Guid(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(args[0]).Concat(File.ReadAllBytes(args[1])).ToArray())[..16]);
original.Write(args[2]);
Console.WriteLine("Inserted only phrase timing methods into the existing core");

using var check = ModuleDefinition.ReadModule(args[2], new ReaderParameters { AssemblyResolver = resolver });
var outputMethods = AllTypes(check.Types).SelectMany(t => t.Methods).ToDictionary(m => m.FullName, Describe);
foreach (var pair in preserved) {
    if (!outputMethods.TryGetValue(pair.Key, out var value) || value != pair.Value)
        throw new InvalidOperationException("Existing method changed: " + pair.Key);
}
Console.WriteLine($"Verified {preserved.Count} existing method bodies are unchanged");
static IEnumerable<TypeDefinition> AllTypes(IEnumerable<TypeDefinition> roots) {
    foreach (var type in roots) { yield return type; foreach (var nested in AllTypes(type.NestedTypes)) yield return nested; }
}
static string Describe(MethodDefinition m) {
    if (!m.HasBody) return "no-body";
    var body = m.Body; var code = body.Instructions;
    int Index(Instruction i) => i == null ? -1 : code.IndexOf(i);
    string Operand(object o) => o switch {
        Instruction i => "branch:" + Index(i), Instruction[] targets => string.Join(",", targets.Select(Index)),
        VariableDefinition v => "local:" + v.Index, ParameterDefinition p => "arg:" + p.Index,
        MemberReference r => r.FullName, _ => o?.ToString() ?? ""
    };
    return body.InitLocals + "|" + string.Join(";", body.Variables.Select(v => v.VariableType.FullName)) + "|" +
        string.Join(";", code.Select(i => i.OpCode + ":" + Operand(i.Operand))) + "|" +
        string.Join(";", body.ExceptionHandlers.Select(h => $"{h.HandlerType}:{Index(h.TryStart)}:{Index(h.TryEnd)}:{Index(h.HandlerStart)}:{Index(h.HandlerEnd)}:{Index(h.FilterStart)}:{h.CatchType?.FullName}"));
}
