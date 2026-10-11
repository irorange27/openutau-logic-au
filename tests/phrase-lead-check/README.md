# DiffSinger phrase timing regression

Apply `patches/openutau-diffsinger-phrase-lead-0.1.572.1-alpha.patch` to the matching OpenUtau checkout and build OpenUtau.Core for .NET 10. Run:

```sh
dotnet run --project tests/phrase-lead-check -c Release \
  -p:OpenUtauCore=/absolute/path/OpenUtau.Core.dll -- /absolute/path/voicebank
```

Without a voicebank argument, this checks 37 position budget/order cases. With a bank argument it also runs the real Japanese phonemizer and duration models over 54,000 combinations: 24 consonants, five previous vowels, five following vowels, two tempos, three previous note lengths, three following note lengths, and five positive gaps. It verifies onset order, vowel anchor preservation, and the rest-dependent lead budget. It disables tensor caching for the test process.

A small consonant lead before the vowel remains intentional. Across a positive gap, overlap into the preceding note is limited to its last 30% and at most 70 ms. Zero-gap connected notes keep their existing timing. The voicebank's phrase-onset duration patch independently caps consonant predictions by type and caps total cluster lead to 16 frames. That part travels with the voicebank; the rest-dependent limit requires the host patch because the duration model's inputs do not contain the actual rest length.

These checks establish timing bounds, not perceptual quality for every possible score. Listen to representative rendered phrases before publishing.
