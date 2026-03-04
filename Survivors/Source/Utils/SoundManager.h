#pragma once
#include <xaudio2.h>
#pragma comment(lib, "xaudio2.lib")
#include <map>
#include <string>
#include <fstream>
#include <wrl.h>

using namespace Microsoft::WRL;

// XAudio2 기반 사운드 매니저
class SoundManager
{
private:
    ComPtr<IXAudio2> pXAudio2;
    IXAudio2MasteringVoice* pMasterVoice = nullptr;

    struct SoundData
    {
        WAVEFORMATEX wfx;
        XAUDIO2_BUFFER buffer;
        BYTE* pData;
        IXAudio2SourceVoice* pVoice; // 사운드당 1개의 전용 채널 배정
    };

    std::map<std::string, SoundData> sounds;

public:
    void Initialize()
    {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR);
        pXAudio2->CreateMasteringVoice(&pMasterVoice);
    }

    // WAV 파일을 분석해서 메모리에 올리는 함수
    bool LoadWAV(const std::string& name, const char* filename)
    {
        std::ifstream file(filename, std::ios::binary);
        if (!file) return false;

        char chunkId[4];
        uint32_t chunkSize;
        file.read(chunkId, 4); file.read((char*)&chunkSize, 4); file.read(chunkId, 4); // RIFF, WAVE

        SoundData sd = {};
        ZeroMemory(&sd.wfx, sizeof(WAVEFORMATEX));
        ZeroMemory(&sd.buffer, sizeof(XAUDIO2_BUFFER));

        while (file.read(chunkId, 4))
        {
            file.read((char*)&chunkSize, 4);

            if (strncmp(chunkId, "fmt ", 4) == 0) 
            {
                file.read((char*)&sd.wfx, sizeof(WAVEFORMATEX));
                file.seekg(chunkSize - sizeof(WAVEFORMATEX), std::ios::cur);
            }
            else if (strncmp(chunkId, "data", 4) == 0) 
            {
                sd.pData = new BYTE[chunkSize];
                file.read((char*)sd.pData, chunkSize);
                sd.buffer.AudioBytes = chunkSize;
                sd.buffer.pAudioData = sd.pData;
                sd.buffer.Flags = XAUDIO2_END_OF_STREAM;
                break;
            }
            else
            {
                file.seekg(chunkSize, std::ios::cur);
            }
        }

        pXAudio2->CreateSourceVoice(&sd.pVoice, &sd.wfx);
        sounds[name] = sd;
        return true;
    }

    void Play(const std::string& name, bool loop = false, float volume = 1.0f)
    {
        if (sounds.find(name) == sounds.end()) return;

        SoundData& sd = sounds[name];
        sd.pVoice->Stop();
        sd.pVoice->FlushSourceBuffers();

        sd.buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
        sd.pVoice->SubmitSourceBuffer(&sd.buffer);

        // 재생 하기 전에 볼륨 설정
        sd.pVoice->SetVolume(volume);
        sd.pVoice->Start(0);
    }
};