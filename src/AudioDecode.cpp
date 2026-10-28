#include "AudioDecode.hpp"

#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>

#include <cstring>
#include <filesystem>

using namespace geode::prelude;

bool decodeSongToMono(std::string const& path, std::vector<float>& out, int& sampleRate, std::string& error) {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system) {
        error = "FMOD is not available";
        return false;
    }
    FMOD::Sound* sound = nullptr;
    auto res = engine->m_system->createSound(path.c_str(), FMOD_OPENONLY | FMOD_ACCURATETIME, nullptr, &sound);
    if (res != FMOD_OK || !sound) {
        error = fmt::format("FMOD could not open the song (error {})", int(res));
        return false;
    }

    FMOD_SOUND_TYPE type;
    FMOD_SOUND_FORMAT format;
    int channels = 0, bits = 0;
    float freq = 44100;
    int priority = 0;
    sound->getFormat(&type, &format, &channels, &bits);
    sound->getDefaults(&freq, &priority);
    if (channels <= 0) channels = 1;
    sampleRate = int(freq);

    int bytesPerSample = 2;
    switch (format) {
        case FMOD_SOUND_FORMAT_PCM8: bytesPerSample = 1; break;
        case FMOD_SOUND_FORMAT_PCM16: bytesPerSample = 2; break;
        case FMOD_SOUND_FORMAT_PCM24: bytesPerSample = 3; break;
        case FMOD_SOUND_FORMAT_PCM32:
        case FMOD_SOUND_FORMAT_PCMFLOAT: bytesPerSample = 4; break;
        default:
            sound->release();
            error = "Unsupported audio sample format";
            return false;
    }

    // cap at 10 minutes
    size_t maxFrames = size_t(sampleRate) * 600;
    std::vector<unsigned char> buf(1 << 16);
    size_t frameBytes = size_t(bytesPerSample * channels);
    std::vector<unsigned char> carry;
    out.clear();

    while (out.size() < maxFrames) {
        unsigned int read = 0;
        auto r = sound->readData(buf.data(), unsigned(buf.size()), &read);
        if (read > 0) {
            carry.insert(carry.end(), buf.begin(), buf.begin() + read);
            size_t frames = carry.size() / frameBytes;
            for (size_t f = 0; f < frames; f++) {
                unsigned char const* p = carry.data() + f * frameBytes;
                float sum = 0;
                for (int c = 0; c < channels; c++, p += bytesPerSample) {
                    float s = 0;
                    switch (format) {
                        case FMOD_SOUND_FORMAT_PCM8: s = (float(p[0]) - 128.f) / 128.f; break;
                        case FMOD_SOUND_FORMAT_PCM16: s = float(int16_t(p[0] | (p[1] << 8))) / 32768.f; break;
                        case FMOD_SOUND_FORMAT_PCM24: {
                            int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);
                            s = float(v >> 8) / 8388608.f;
                            break;
                        }
                        case FMOD_SOUND_FORMAT_PCM32: {
                            int32_t v;
                            std::memcpy(&v, p, 4);
                            s = float(double(v) / 2147483648.0);
                            break;
                        }
                        default: std::memcpy(&s, p, 4); break;
                    }
                    sum += s;
                }
                out.push_back(sum / float(channels));
            }
            carry.erase(carry.begin(), carry.begin() + long(frames * frameBytes));
        }
        if (r != FMOD_OK || read == 0) break;
    }
    sound->release();
    if (out.size() < size_t(sampleRate) * 3) {
        error = "Song is too short or could not be decoded";
        return false;
    }
    return true;
}

std::string resolveSongPath(int songID, int audioTrack, std::string& error) {
    if (songID > 0) {
        auto mdm = MusicDownloadManager::sharedState();
        if (!mdm->isSongDownloaded(songID)) {
            error = fmt::format("Song {} is not downloaded yet", songID);
            return "";
        }
        std::string path = mdm->pathForSong(songID);
        std::error_code ec;
        if (!std::filesystem::exists(path, ec)) {
            error = fmt::format("Song file not found: {}", path);
            return "";
        }
        return path;
    }
    std::string file = LevelTools::getAudioFileName(audioTrack);
    std::string full = CCFileUtils::sharedFileUtils()->fullPathForFilename(file.c_str(), false);
    std::error_code ec;
    if (full.empty() || !std::filesystem::exists(full, ec)) {
        error = fmt::format("Main level song not found: {}", file);
        return "";
    }
    return full;
}

std::string songDisplayName(int songID, int audioTrack) {
    if (songID > 0) {
        if (auto info = MusicDownloadManager::sharedState()->getSongInfoObject(songID)) {
            return fmt::format("{} - {} ({})", std::string(info->m_artistName), std::string(info->m_songName), songID);
        }
        return fmt::format("Song ID {}", songID);
    }
    return fmt::format("{} (main track {})", LevelTools::getAudioTitle(audioTrack), audioTrack + 1);
}
