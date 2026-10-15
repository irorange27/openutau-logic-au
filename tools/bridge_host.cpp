/*
 * The bridge outside a DAW: a Session, a fake transport and a fake audio thread, so OpenUtau's
 * real client can be run against the real plugin code without a host in between.
 *
 * Both sides of this protocol are otherwise only ever tested against a stand-in — the plugin's
 * tests speak to a FakeOpenUtau, OpenUtau's speak to a DawTestPlugin — and two implementations
 * that each agree with their own fake can still disagree with each other. This is what closes
 * that: start it, connect OpenUtau's DAW dialog to it, and watch the peak meter come alive as
 * parts arrive.
 *
 * Not part of the shipped plugin. Build it with -DBRIDGE_BUILD_HOST=ON.
 */

#include "clock.h"
#include "session.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace {

std::atomic<bool> g_stop{false};

void OnInterrupt(int) {
    g_stop.store(true);  // Async-signal-safe: a lock-free store and nothing else.
}

struct Options {
    double sampleRate = 48000.0;  // Not the wire rate, so a real run exercises the resampler.
    int trackNo = 0;
    uint32_t blockFrames = 512;
    double loopSeconds = 0.0;  // 0 plays forward forever.
    std::string directory;     // Empty means the production discovery directory.
    /// Play a note pattern into the session, the way a DAW sends MIDI while recording (§6.3).
    /// Zero means no notes, which is what every earlier run did.
    int noteCount = 0;
    /// Seconds between the starts of consecutive notes in that pattern.
    double noteSpacingSeconds = 0.5;
    /// How long each note is held.
    double noteLengthSeconds = 0.25;
};

/// Returns false when the arguments do not make sense, having said why.
bool ParseOptions(int argc, char **argv, Options *options) {
    for (int i = 1; i < argc; i++) {
        std::string flag = argv[i];
        bool hasValue = i + 1 < argc;
        if (flag == "--help" || flag == "-h") {
            return false;
        }
        if (flag == "--rate" && hasValue) {
            options->sampleRate = std::atof(argv[++i]);
        } else if (flag == "--track" && hasValue) {
            options->trackNo = std::atoi(argv[++i]);
        } else if (flag == "--block" && hasValue) {
            options->blockFrames = static_cast<uint32_t>(std::atoi(argv[++i]));
        } else if (flag == "--loop" && hasValue) {
            options->loopSeconds = std::atof(argv[++i]);
        } else if (flag == "--dir" && hasValue) {
            options->directory = argv[++i];
        } else if (flag == "--notes" && hasValue) {
            options->noteCount = std::atoi(argv[++i]);
        } else if (flag == "--note-spacing" && hasValue) {
            options->noteSpacingSeconds = std::atof(argv[++i]);
        } else if (flag == "--note-length" && hasValue) {
            options->noteLengthSeconds = std::atof(argv[++i]);
        } else {
            std::printf("Unrecognised argument: %s\n", flag.c_str());
            return false;
        }
    }
    if (options->sampleRate < 8000.0 || options->blockFrames == 0) {
        std::printf("A sample rate of at least 8000 and a non-zero block size are needed.\n");
        return false;
    }
    if (options->noteCount < 0) {
        std::printf("--notes cannot be negative.\n");
        return false;
    }
    return true;
}

void Usage() {
    std::printf(
        "Runs the bridge's session outside a DAW so OpenUtau can connect to it.\n\n"
        "  --rate <hz>      host sample rate to convert to (default 48000)\n"
        "  --track <n>      the OpenUtau track to play, zero-based (default 0)\n"
        "  --block <n>      frames per block (default 512)\n"
        "  --loop <sec>     restart the transport every <sec> seconds (default: play forward)\n"
        "  --notes <n>      send <n> ascending notes into the session (default 0, none)\n"
        "  --note-spacing <sec>  seconds between note starts (default 0.5)\n"
        "  --note-length <sec>   seconds each note is held (default 0.25)\n"
        "  --dir <path>     discovery directory (default: the one OpenUtau scans)\n");
}

}  // namespace

int main(int argc, char **argv) {
    Options options;
    if (!ParseOptions(argc, argv, &options)) {
        Usage();
        return 1;
    }

    std::signal(SIGINT, OnInterrupt);
    std::signal(SIGTERM, OnInterrupt);

    // Constructed with the directory rather than reassigned into: a Session owns a thread and a
    // listening socket, so it is neither copyable nor movable.
    std::filesystem::path directory = options.directory.empty()
                                          ? bridge::DiscoveryDirectory()
                                          : std::filesystem::path(options.directory);
    bridge::Session session(std::move(directory));
    // This runs outside a DAW, so there is no host sandbox rewriting TMPDIR and no relay to
    // dial: listening directly is both possible and what makes the advertised port reachable.
    session.ListenDirectly();
    session.SetHostSampleRate(options.sampleRate);
    session.SetTrackNo(options.trackNo);
    if (!session.Start()) {
        std::printf("Could not listen or advertise; nothing to connect to.\n");
        return 1;
    }
    std::printf("Listening on port %d at %.0f Hz, playing track %d. Ctrl-C to stop.\n",
                session.Port(), options.sampleRate, options.trackNo + 1);

    std::vector<float> left(options.blockFrames, 0.0f);
    std::vector<float> right(options.blockFrames, 0.0f);
    // A transport that is playing from the first block, which is also the edge that makes the
    // session send playbackStarted. The host's own tempo is reported once it is known.
    session.NoteTransport(true, 0.0, true, false, 0.0);

    const int64_t startMs = bridge::NowMs();
    // Monotonic, for pacing; the position it renders at is this folded into the loop, so a part
    // placed anywhere in the loop comes round again however late something connects.
    const int64_t loopFrames =
        static_cast<int64_t>(std::llround(options.loopSeconds * options.sampleRate));
    int64_t rendered = 0;
    int64_t reportedSecond = -1;
    int64_t lastPosition = 0;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;
    // Which pass through the --notes pattern each note was last sent on, so a note goes out
    // once per pass even though the transport position passes its start on every block. -1
    // means "not yet", and the pattern's own pass number is what marks it done.
    std::vector<long long> sentOn(static_cast<size_t>(std::max(0, options.noteCount)), -1);
    std::vector<long long> sentOff(static_cast<size_t>(std::max(0, options.noteCount)), -1);
    while (!g_stop.load()) {
        int64_t position = loopFrames > 0 ? rendered % loopFrames : rendered;
        if (position < lastPosition) {
            // The transport jumped back to the top of the loop, which a DAW reports as a fresh
            // start — and a start is what makes the session send playbackStarted, so looping is
            // also how that notification gets exercised.
            session.NoteTransport(false, 0.0, false, false, 0.0);
            session.NoteTransport(true, 0.0, true, false, 0.0);
        }
        // The position feeds the v1.1 playhead notifications as well as the renderer.
        session.NoteTransport(true, static_cast<double>(position) / options.sampleRate, true,
                              false, 0.0);

        // §6.3: play a note pattern into the session, which is what a DAW does while the user
        // records. Notes are delivered at their absolute timeline position, exactly as
        // ApplyEvents would deliver a host's.
        //
        // The pattern repeats for as long as the process runs rather than being played once.
        // A session opens whenever the other side asks, which in any real run is long after
        // startup - a one-shot pattern would have finished before there was anyone to record it,
        // and the recording would look empty for reasons that have nothing to do with the
        // protocol. (It did, before this looped.)
        if (options.noteCount > 0) {
            const double nowMs = static_cast<double>(position) * 1000.0 / options.sampleRate;
            const double spacingMs = options.noteSpacingSeconds * 1000.0;
            const double lengthMs = options.noteLengthSeconds * 1000.0;
            const double cycleMs = spacingMs * options.noteCount;
            // Which pass through the pattern this block is in. Each pass sends every note again,
            // so a receiver that listened continuously would see duplicates - which is the
            // behaviour §6.3 exists to make the user opt into, and worth reproducing here.
            const long long pass = static_cast<long long>(nowMs / cycleMs);
            for (int n = 0; n < options.noteCount; n++) {
                const double onMs = pass * cycleMs + n * spacingMs;
                const double offMs = onMs + lengthMs;
                // Ascending from middle C, so a batch is easy to eyeball at the far end.
                const int key = 60 + (n % 12);
                if (sentOn[n] != pass && nowMs >= onMs) {
                    session.ObserveNote(key, 100, true, onMs);
                    sentOn[n] = pass;
                }
                if (sentOff[n] != pass && nowMs >= offMs) {
                    session.ObserveNote(key, 0, false, offMs);
                    sentOff[n] = pass;
                }
            }
        }

        lastPosition = position;
        session.Render(position, left.size(), left.data(), right.data());
        for (size_t i = 0; i < left.size(); i++) {
            peakLeft = std::max(peakLeft, std::fabs(left[i]));
            peakRight = std::max(peakRight, std::fabs(right[i]));
        }
        rendered += static_cast<int64_t>(left.size());

        int64_t second = position / static_cast<int64_t>(options.sampleRate);
        if (second != reportedSecond) {
            // Labelled with the second that just finished, which is the one the peak covers.
            if (reportedSecond >= 0) {
                std::printf("t=%3llds  peak L %.4f  R %.4f%s\n",
                            static_cast<long long>(reportedSecond), peakLeft, peakRight,
                            session.IsConnected() ? "  [OpenUtau connected]" : "");
                std::fflush(stdout);
            }
            reportedSecond = second;
            peakLeft = 0.0f;
            peakRight = 0.0f;
        }

        // Paced against the wall clock, because a session that renders as fast as it can would
        // reach the end of the project before OpenUtau had finished sending it.
        int64_t dueMs = startMs + rendered * 1000 / static_cast<int64_t>(options.sampleRate);
        int64_t waitMs = dueMs - bridge::NowMs();
        if (waitMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(waitMs));
        }
    }

    std::printf("Stopping.\n");
    session.NoteTransport(false, 0.0, false, false, 0.0);
    session.Stop();
    return 0;
}
