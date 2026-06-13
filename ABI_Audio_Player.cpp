#define NOMINMAX  // prevent windows.h from defining min/max macros

#ifndef _WIN32_WINNT
#  define _WIN32_WINNT 0x0602
#elif _WIN32_WINNT < 0x0602
#  undef  _WIN32_WINNT
#  define _WIN32_WINNT 0x0602
#endif

#include "ABI_PhysFS_Loader.h"
#include "ABI_Audio_Player.h"

#pragma comment(lib, "xaudio2.lib")

static IXAudio2*               s_xaudio2   = nullptr;
static IXAudio2MasteringVoice* s_master    = nullptr;
static std::once_flag          s_init_flag;

static void InitXAudio2() {
    if (FAILED(XAudio2Create(&s_xaudio2, 0, XAUDIO2_DEFAULT_PROCESSOR))) {
        std::cerr << "[ABI_Audio] XAudio2Create failed\n";
        return;
    }
    if (FAILED(s_xaudio2->CreateMasteringVoice(&s_master))) {
        std::cerr << "[ABI_Audio] CreateMasteringVoice failed\n";
        s_xaudio2->Release();
        s_xaudio2 = nullptr;
    }
}

struct StreamCallback final : public IXAudio2VoiceCallback {
    HANDLE hEvent;
    StreamCallback()  : hEvent(CreateEvent(nullptr, FALSE, FALSE, nullptr)) {}
    ~StreamCallback() { CloseHandle(hEvent); }

    void STDMETHODCALLTYPE OnBufferEnd(void*)                 override { SetEvent(hEvent); }
    void STDMETHODCALLTYPE OnStreamEnd()                      override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassStart(UINT32) override {}
    void STDMETHODCALLTYPE OnVoiceProcessingPassEnd()         override {}
    void STDMETHODCALLTYPE OnBufferStart(void*)               override {}
    void STDMETHODCALLTYPE OnLoopEnd(void*)                   override {}
    void STDMETHODCALLTYPE OnVoiceError(void*, HRESULT)       override {}
};

struct WavInfo {
    WAVEFORMATEX    wfx;
    std::streampos  data_offset;
    std::streamsize data_bytes;
};

static bool ParseWavHeader(std::ifstream& f, WavInfo& out) {
    char     id[4];
    uint32_t u32;

    auto read4   = [&](char* b)     -> bool { f.read(b, 4);                           return f.gcount() == 4; };
    auto readU32 = [&](uint32_t& v) -> bool { f.read(reinterpret_cast<char*>(&v), 4); return f.gcount() == 4; };

    if (!read4(id)   || std::memcmp(id, "RIFF", 4) != 0) return false;
    if (!readU32(u32))                                    return false;
    if (!read4(id)   || std::memcmp(id, "WAVE", 4) != 0) return false;

    bool got_fmt = false, got_data = false;
    while (!f.fail() && !got_data) {
        char     chunk[4];
        uint32_t sz = 0;
        if (!read4(chunk) || !readU32(sz)) break;

        if (!got_fmt && std::memcmp(chunk, "fmt ", 4) == 0) {
            uint32_t to_read = std::min(sz, (uint32_t)sizeof(WAVEFORMATEX));
            f.read(reinterpret_cast<char*>(&out.wfx), to_read);
            if (sz > to_read) f.seekg(sz - to_read, std::ios::cur);
            got_fmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            out.data_offset = f.tellg();
            out.data_bytes  = static_cast<std::streamsize>(sz);
            got_data = true;
        } else {
            f.seekg(sz, std::ios::cur);
        }
    }
    return got_fmt && got_data;
}

static bool ParseWavHeaderMem(const uint8_t* data, size_t total,
                               WAVEFORMATEX& wfx_out,
                               size_t& data_offset_out, size_t& data_size_out) {
    size_t pos = 0;

    auto read4   = [&](char* b)     -> bool { if (pos + 4 > total) return false; std::memcpy(b, data + pos, 4); pos += 4; return true; };
    auto readU32 = [&](uint32_t& v) -> bool { if (pos + 4 > total) return false; std::memcpy(&v, data + pos, 4); pos += 4; return true; };

    char id[4]; uint32_t u32;
    if (!read4(id)   || std::memcmp(id, "RIFF", 4) != 0) return false;
    if (!readU32(u32))                                    return false;
    if (!read4(id)   || std::memcmp(id, "WAVE", 4) != 0) return false;

    bool got_fmt = false, got_data = false;
    while (pos + 8 <= total && !got_data) {
        char chunk[4]; uint32_t sz = 0;
        if (!read4(chunk) || !readU32(sz)) break;

        if (!got_fmt && std::memcmp(chunk, "fmt ", 4) == 0) {
            uint32_t to_read = std::min(sz, (uint32_t)sizeof(WAVEFORMATEX));
            if (pos + to_read > total) return false;
            std::memcpy(&wfx_out, data + pos, to_read);
            pos += sz;
            got_fmt = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            data_offset_out = pos;
            data_size_out   = std::min((size_t)sz, total - pos);
            got_data = true;
        } else {
            pos += sz;
        }
    }
    return got_fmt && got_data;
}

static constexpr int    NBUFS       = 3;      // max XAudio2 buffers in-flight
static constexpr size_t CHUNK_BYTES = 65536;  // 64 KB per buffer

template<typename ShouldStop>
static bool StreamWavOnce(const std::string& path, ShouldStop shouldStop) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[ABI_Audio] Cannot open: " << path << "\n";
        return false;
    }

    WavInfo info{};
    if (!ParseWavHeader(file, info)) {
        std::cerr << "[ABI_Audio] Bad WAV header: " << path << "\n";
        return false;
    }
    file.seekg(info.data_offset);

    StreamCallback       cb;
    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(s_xaudio2->CreateSourceVoice(&voice, &info.wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &cb))) {
        std::cerr << "[ABI_Audio] CreateSourceVoice failed\n";
        return false;
    }
    voice->Start(0);

    std::vector<std::vector<uint8_t>> ring(NBUFS, std::vector<uint8_t>(CHUNK_BYTES));
    int             slot      = 0;
    std::streamsize remaining = info.data_bytes;
    bool            cut       = false;

    while (remaining > 0 && !cut) {
        XAUDIO2_VOICE_STATE st;
        while (true) {
            if (shouldStop()) { cut = true; break; }
            voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (st.BuffersQueued < NBUFS) break;
            WaitForSingleObject(cb.hEvent, 50);
        }
        if (cut) break;

        size_t to_read = static_cast<size_t>(std::min((std::streamsize)CHUNK_BYTES, remaining));
        auto&  buf     = ring[slot % NBUFS];
        file.read(reinterpret_cast<char*>(buf.data()), (std::streamsize)to_read);
        size_t got = static_cast<size_t>(file.gcount());
        if (got == 0) break;

        XAUDIO2_BUFFER xbuf{};
        xbuf.AudioBytes = (UINT32)got;
        xbuf.pAudioData = buf.data();
        voice->SubmitSourceBuffer(&xbuf);
        remaining -= (std::streamsize)got;
        ++slot;
    }

    if (!cut) {
        XAUDIO2_VOICE_STATE st;
        while (true) {
            if (shouldStop()) break;
            voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
            if (st.BuffersQueued == 0) break;
            WaitForSingleObject(cb.hEvent, 50);
        }
    }

    voice->Stop();
    voice->DestroyVoice();
    return !cut;
}

template<typename ShouldStop>
static bool StreamWavFromMemory(const uint8_t* data, size_t size, ShouldStop shouldStop) {
    WAVEFORMATEX wfx{};
    size_t data_offset = 0, data_size = 0;
    if (!ParseWavHeaderMem(data, size, wfx, data_offset, data_size)) {
        std::cerr << "[ABI_Audio] Bad WAV header (PhysFS)\n";
        return false;
    }

    StreamCallback       cb;
    IXAudio2SourceVoice* voice = nullptr;
    if (FAILED(s_xaudio2->CreateSourceVoice(&voice, &wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, &cb))) {
        std::cerr << "[ABI_Audio] CreateSourceVoice failed\n";
        return false;
    }

    XAUDIO2_BUFFER xbuf{};
    xbuf.AudioBytes = (UINT32)data_size;
    xbuf.pAudioData = data + data_offset;
    voice->Start(0);
    voice->SubmitSourceBuffer(&xbuf);

    bool cut = false;
    XAUDIO2_VOICE_STATE st;
    while (true) {
        if (shouldStop()) { cut = true; break; }
        voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) break;
        WaitForSingleObject(cb.hEvent, 50);
    }

    voice->Stop();
    voice->DestroyVoice();
    return !cut;
}

static std::mutex                                  s_map_mutex;
static std::unordered_map<int, std::atomic<bool>*> s_instances;
static std::atomic<int>                            s_next_id{0};
static int                                         s_interrupt_id = -1;  // guarded by s_map_mutex

static void PlayThread(const char* path, int id, bool interruptable) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    std::atomic<bool> stop{false};

    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_instances[id] = &stop;
        if (interruptable) s_interrupt_id = id;
    }

    if (s_xaudio2) {
        auto shouldStop = [&] { return stop.load(); };

        std::ifstream probe(path, std::ios::binary);
        if (probe.is_open()) {
            probe.close();
            StreamWavOnce(path, shouldStop);
        } else {
            PHYSFS_sint64 phys_size = 0;
            void* raw = ABI_PhysFS_Get_File_Contents(path, &phys_size);
            if (raw && phys_size > 0) {
                std::vector<uint8_t> buf(static_cast<const uint8_t*>(raw),
                                         static_cast<const uint8_t*>(raw) + (size_t)phys_size);
                free(raw);
                StreamWavFromMemory(buf.data(), buf.size(), shouldStop);
            } else {
                if (raw) free(raw);
                std::cerr << "[ABI_Audio] File not found on disk or in PhysFS: " << path << "\n";
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        s_instances.erase(id);
        if (interruptable && s_interrupt_id == id)
            s_interrupt_id = -1;
    }

    CoUninitialize();
}

void ABI_Audio_Init() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::call_once(s_init_flag, InitXAudio2);
    CoUninitialize();
}

void ABI_Audio_Play(const char* filename, bool interruptable) {
    int id = s_next_id.fetch_add(1);

    if (interruptable) {
        std::lock_guard<std::mutex> lock(s_map_mutex);
        if (s_interrupt_id != -1) {
            auto it = s_instances.find(s_interrupt_id);
            if (it != s_instances.end())
                it->second->store(true);
        }
    }

    std::thread(PlayThread, filename, id, interruptable).detach();
}

void ABI_Audio_Stop_Interruptable() {
    std::lock_guard<std::mutex> lock(s_map_mutex);
    if (s_interrupt_id != -1) {
        auto it = s_instances.find(s_interrupt_id);
        if (it != s_instances.end())
            it->second->store(true);
    }
}

void ABI_Audio_Stop_Not_Interruptable() {
    std::lock_guard<std::mutex> lock(s_map_mutex);
    for (auto& kv : s_instances) {
        if (kv.first != s_interrupt_id && kv.second)
            kv.second->store(true);
    }
}


void ABI_Audio_Stop_All() {
    std::lock_guard<std::mutex> lock(s_map_mutex);
    for (auto& kv : s_instances) {
        if (kv.second)
            kv.second->store(true);
    }
}

void ABI_Audio_Set_Volume(float volume) {
    if (s_master) {
        s_master->SetVolume(volume);
    }
}