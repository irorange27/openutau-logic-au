#pragma once

/*
 * The JSON payloads of PROTOCOL.md §6, mirroring `DawMessages.cs`. nlohmann/json stays
 * inside messages.cpp: it is a very large header and nothing else needs it.
 *
 * Parsers tolerate unknown and missing fields (§10) and never throw; they return false only
 * when the payload is not the shape its kind requires.
 */

#include <string>
#include <vector>

namespace bridge {

/// The protocol version this build speaks, carried by the discovery file and echoed in the
/// init response (PROTOCOL.md §4). 1.3 adds the MIDI-input family (§6.3).
inline constexpr const char *kApiVersion = "1.3";

/// Control-plane kinds. Mirrors `DawMessageKind`.
namespace kind {
inline constexpr const char *kInit = "init";
inline constexpr const char *kUpdateUstx = "updateUstx";
inline constexpr const char *kUpdatePartLayout = "updatePartLayout";
inline constexpr const char *kGetAudio = "getAudio";
inline constexpr const char *kUpdateTracks = "updateTracks";
inline constexpr const char *kUpdateProjectInfo = "updateProjectInfo";
// v1.3 MIDI input (PROTOCOL.md §6.3).
inline constexpr const char *kRecordStart = "recordStart";
inline constexpr const char *kRecordStop = "recordStop";
inline constexpr const char *kPing = "ping";
inline constexpr const char *kPlaybackStarted = "playbackStarted";
inline constexpr const char *kPlayhead = "playhead";
inline constexpr const char *kBpm = "bpm";
// v1.3 MIDI input: the batch of notes sent while a session is open.
inline constexpr const char *kMidiNotes = "midiNotes";
}  // namespace kind

/// One entry of `updatePartLayout`. Windows are absolute milliseconds on OpenUtau's
/// timeline, not the DAW's — a differing tempo misaligns musically until tempo sync lands.
struct PartLayout {
    int trackNo = 0;
    double startMs = 0.0;
    double endMs = 0.0;
    std::string audioHash;
};

/// One entry of `updateTracks`, retained for wire compatibility. The bridge's default output is
/// pre-fader, so these mixer values are informational here; the DAW owns gain, pan, mute and solo.
struct TrackInfo {
    std::string name;
    /// Decibels; 0 is unity. Convert with the fader law in fader.h, not 10^(dB/20).
    double volume = 0.0;
    /// -100..+100; 0 is centre.
    double pan = 0.0;
    /// The effective mute, with solo already resolved against the rest of the project.
    bool muted = false;
    /// v1.2: singer display name, empty when the track has none yet. Informational.
    std::string singer;
    /// v1.2: render engine key (CLASSIC, DIFFSINGER, ...), empty when unusable. Informational.
    std::string engine;
};

/// An `updateProjectInfo` notification payload (v1.1): what an info window shows about the
/// project. The name is the file stem; an unsaved project reports `saved` false and an empty
/// name.
struct ProjectInfo {
    std::string name;
    bool saved = false;
};

/// A `recordStart` request payload (v1.3, §6.3): the track and the window to record into.
/// `startMs`/`endMs` are absolute milliseconds on the shared timeline — the same coordinate
/// `playhead`'s position and a part's `startMs` use — so notes outside the window can be
/// dropped before they are ever sent.
struct RecordStartRequest {
    int trackNo = 0;
    double startMs = 0.0;
    double endMs = 0.0;
};

/// A `recordStop` request payload (v1.3, §6.3). `commit` false means the user cancelled and
/// whatever was recorded should be discarded rather than sent.
struct RecordStopRequest {
    std::string sessionId;
    bool commit = true;
};

/// One note of a `midiNotes` notification (v1.3, §6.3). `tone` is a MIDI note number;
/// times are absolute milliseconds on the shared timeline.
struct DawNote {
    int tone = 60;
    double startMs = 0.0;
    double durationMs = 0.0;
    /// 0-127, or -1 when the source had no velocity and the receiver's default should stand.
    int velocity = -1;
};

/// A `response:<uuid>` envelope, as read when a peer refuses one of our requests.
struct Envelope {
    bool success = false;
    std::string error;
};

/// `{ "ustx": "<document>" }`, shared by `init` and `updateUstx`.
bool ParseUstxPayload(const std::string &json, std::string *ustx);
bool ParsePartLayoutRequest(const std::string &json, std::vector<PartLayout> *parts);
bool ParseTracksNotification(const std::string &json, std::vector<TrackInfo> *tracks);
bool ParseProjectInfoNotification(const std::string &json, ProjectInfo *info);
bool ParseRecordStartRequest(const std::string &json, RecordStartRequest *request);
bool ParseRecordStopRequest(const std::string &json, RecordStopRequest *request);
bool ParseEnvelope(const std::string &json, Envelope *envelope);

/// Complete `{ "success": …, "data": …, "error": … }` envelopes, ready to send.
std::string BuildInitResponseEnvelope();
std::string BuildPartLayoutResponseEnvelope(const std::vector<std::string> &missingAudios);
std::string BuildRecordStartResponseEnvelope(const std::string &sessionId);
std::string BuildRecordStopResponseEnvelope(int noteCount);
std::string BuildFailEnvelope(const std::string &error);

std::string BuildGetAudioPayload(const std::string &hash);

/// The batch of recorded notes (§6.3). Sent as one message rather than one per note so a
/// chord does not cost a message per voice.
std::string BuildMidiNotesPayload(const std::string &sessionId, const std::vector<DawNote> &notes);

/// The DAW transport position, one-way towards OpenUtau (v1.1). `positionMs` is absolute
/// milliseconds on the shared timeline — the same coordinate a part's `startMs` uses.
std::string BuildPlayheadPayload(double positionMs, bool playing);

/// The DAW project's tempo (v1.1).
std::string BuildBpmPayload(double bpm);

/// `{}` — the payload of the kinds that carry no fields.
std::string BuildEmptyPayload();

/// The discovery file body (§4). Lives here so JSON has exactly one translation unit.
std::string BuildDiscoveryJson(int port, const std::string &name);

}  // namespace bridge
