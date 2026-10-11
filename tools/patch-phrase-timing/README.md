# Targeted phrase timing assembly patch

Build the matching OpenUtau Core after applying the maintained phrase-lead source patch. Then run:

```sh
dotnet run --project tools/patch-phrase-timing -c Release -- ORIGINAL_CORE BUILT_CORE OUTPUT_CORE
```

Only the new `DiffSingerBasePhonemizer.SetUp` override and `ConstrainPhraseLeadToRest` helper are inserted into the existing assembly. The assembly identity and existing API remain intact. ReadyToRun data is removed and the output uses portable managed IL. The tool reopens the output and verifies all existing method bodies are unchanged. Already patched inputs are rejected. Output module identity is derived from the two inputs.

Preserve the original app and sign the updated app bundle after replacing its Core. macOS may request folder access again after signing. Verify startup, voicebank discovery and actual rendering; the CLI timing test alone does not establish app compatibility.
