#pragma once

#include <string>
#include <vector>

// Decodes any file FMOD can open (mp3 / ogg / wav ...) to mono float PCM.
// Must be called on the main thread (uses GD's FMOD system).
bool decodeSongToMono(std::string const& path, std::vector<float>& out, int& sampleRate, std::string& error);

// Resolves the audio file of a level's song: custom (Newgrounds / Music Library) or main track.
// Returns an empty string and fills `error` if the song is not available locally.
std::string resolveSongPath(int songID, int audioTrack, std::string& error);

// Human readable name of the song.
std::string songDisplayName(int songID, int audioTrack);
