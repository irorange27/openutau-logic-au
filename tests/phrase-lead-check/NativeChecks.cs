using OpenUtau.Core.Util;

using OpenUtau.Api;
using OpenUtau.Classic;
using OpenUtau.Core;
using OpenUtau.Core.DiffSinger;
using OpenUtau.Core.Format;
using OpenUtau.Core.Ustx;
using System.Text;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

static class NativeChecks {
    public static void Run(string bankPath) {
        var appDir = "/Applications/OpenUtau.app/Contents/MacOS";
        AssemblyLoadContext.Default.Resolving += (_, name) => {
            var path = Path.Combine(appDir, name.Name + ".dll");
            return File.Exists(path) ? AssemblyLoadContext.Default.LoadFromAssemblyPath(path) : null;
        };
        var ort = AssemblyLoadContext.Default.LoadFromAssemblyPath(Path.Combine(appDir, "Microsoft.ML.OnnxRuntime.dll"));
        NativeLibrary.SetDllImportResolver(ort, (name, _, _) => {
            var path = Path.Combine(appDir, name.EndsWith(".dylib") ? name : "lib" + name + ".dylib");
            return File.Exists(path) ? NativeLibrary.Load(path) : IntPtr.Zero;
        });
        Encoding.RegisterProvider(CodePagesEncodingProvider.Instance);

        var bank = new Voicebank { File = Path.Combine(bankPath, "character.txt"), BasePath = Path.GetDirectoryName(bankPath) };
        VoicebankLoader.LoadVoicebank(bank);
        var singer = new DiffSingerSinger(bank); singer.EnsureLoaded();
        var ph = new DiffSingerJapanesePhonemizer(); ph.SetSinger(singer);
        Preferences.Default.DiffSingerTensorCache = false;
        int cases = 0;
        foreach (var consonant in new[] { "b", "by", "ch", "cl", "d", "f", "g", "h", "hy", "j", "k", "ky", "m", "n", "p", "r", "ry", "s", "sh", "t", "ts", "w", "y", "z" })
        foreach (var previousVowel in new[] { "a", "e", "i", "o", "u" })
        foreach (var followingVowel in new[] { "a", "e", "i", "o", "u" })
        foreach (var nextDuration in new[] { 30, 120, 1200 })
        foreach (var bpm in new[] { 120.0, 193.0 })
        foreach (var duration in new[] { 30, 120, 480 })
        foreach (var gap in new[] { 1, 15, 60, 120, 480 }) {
            var project = Ustx.Create(); project.tempos.Clear(); project.tempos.Add(new UTempo(0, bpm));
            project.timeAxis.BuildSegments(project); ph.SetTiming(project.timeAxis);
            var prev = new Phonemizer.Note { lyric = previousVowel, phoneticHint = previousVowel, tone = 58, position = 960, duration = duration };
            var next = new Phonemizer.Note { lyric = "ka", phoneticHint = consonant + " " + followingVowel, tone = 58, position = 960 + duration + gap, duration = nextDuration };
            var groups = new[] { new[] { prev }, new[] { next } };
            ph.SetUp(groups, project, project.tracks[0]);
            var result = ph.Process(groups[1], prev, null, null, null, Array.Empty<Phonemizer.Note>());
            double endMs = project.timeAxis.TickPosToMsPos(prev.position + duration);
            double beginMs = project.timeAxis.TickPosToMsPos(prev.position);
            int overlap = prev.position + duration - project.timeAxis.MsPosToTickPos(endMs - Math.Min(70, (endMs - beginMs) * .3));
            int bound = -gap - Math.Max(0, overlap);
            if (result.phonemes.Length != 2 || result.phonemes[0].position < bound || result.phonemes[0].position > 0 || result.phonemes[1].position != 0)
                throw new Exception($"Unexpected {consonant}: gap={gap} duration={duration} bpm={bpm}");
            cases++;
        }
        Console.WriteLine($"{cases} real-model phrase/rest cases passed");
    }
}
