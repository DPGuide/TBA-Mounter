#define _WIN32_WINNT 0x0601 
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
#include <shlwapi.h>
#include <thread>
#include <string>
#include <vector>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <algorithm>
#include <commctrl.h>
#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "shell32.lib")
namespace fs = std::filesystem;
#define SAFE_RELEASE(punk) if ((punk) != NULL) { (punk)->Release(); (punk) = NULL; }
#pragma pack(push, 1)
struct WavHeader {
    char riff[4];           
    uint32_t overallSize;   
    char wave[4];           
    char fmtChunkId[4];     
    uint32_t fmtChunkSize;  
    uint16_t audioFormat;   
    uint16_t numChannels;   
    uint32_t sampleRate;    
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample; 
};
#pragma pack(pop)
HWND hMainWnd, hPromptEdit, hDurationEdit, hBpmEdit, hPlayBtn;
HWND hLyricsEdit, hVoiceBtn, hMixBtn;
HWND hBeatSlider, hVoiceSlider;
HWND hStyleCombo;
HWND hProgressBar;
HWND hVoiceCombo, hSpeakerCombo;
std::string selectedStyle = "[Rap]";
float beatGain = 0.7f;
float voiceGain = 1.0f;
std::string currentStatusText = "STATUS: BEREIT"; 
std::string exeDir = "";
char globalModelPath[512] = "";
std::atomic<bool> isRunning(false);
std::atomic<bool> stopRequested(false);
std::vector<float> LoadAudioForWASAPI(const std::string& filepath, int& outSampleRate) {
    std::vector<float> audioData;
    std::ifstream file(filepath, std::ios::binary);
    if (!file.is_open()) return audioData;
    char riff[4];
    file.read(riff, 4);
    if (strncmp(riff, "RIFF", 4) != 0) return audioData;
    uint32_t overallSize;
    file.read((char*)&overallSize, 4);
    char wave[4];
    file.read(wave, 4);
    if (strncmp(wave, "WAVE", 4) != 0) return audioData;
    uint16_t audioFormat = 0, numChannels = 0, bitsPerSample = 0;
    outSampleRate = 0;
    char chunkId[4];
    uint32_t chunkSize;
    while (file.read(chunkId, 4)) {
        if (!file.read((char*)&chunkSize, 4)) break;
        if (strncmp(chunkId, "fmt ", 4) == 0) {
            file.read((char*)&audioFormat, 2);
            file.read((char*)&numChannels, 2);
            file.read((char*)&outSampleRate, 4);
            uint32_t byteRate; file.read((char*)&byteRate, 4);
            uint16_t blockAlign; file.read((char*)&blockAlign, 2);
            file.read((char*)&bitsPerSample, 2);
            if (chunkSize > 16) {
                file.seekg(chunkSize - 16, std::ios::cur); 
            }
        }
        else if (strncmp(chunkId, "data", 4) == 0) {
            int numSamples = chunkSize / (bitsPerSample / 8);
            audioData.resize(numSamples);
            if (audioFormat == 3 && bitsPerSample == 32) {
                file.read((char*)audioData.data(), chunkSize);
            }
            else if (audioFormat == 1 && bitsPerSample == 16) {
                std::vector<int16_t> temp16(numSamples);
                file.read((char*)temp16.data(), chunkSize);
                for (int i = 0; i < numSamples; i++) {
                    audioData[i] = temp16[i] / 32768.0f; 
                }
            }
            if (chunkSize % 2 != 0) file.seekg(1, std::ios::cur);
            break;
        }
        else {
            file.seekg(chunkSize, std::ios::cur);
            if (chunkSize % 2 != 0) file.seekg(1, std::ios::cur);
        }
    }
    file.close();
    return audioData;
}
void PlayAudioWASAPI(const std::vector<float>& pcmData, int sampleRate) {
    HRESULT hr;
    IMMDeviceEnumerator *pEnumerator = NULL;
    IMMDevice *pDevice = NULL;
    IAudioClient *pAudioClient = NULL;
    IAudioRenderClient *pRenderClient = NULL;
    CoInitialize(NULL);
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&pEnumerator);
    hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pAudioClient);
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = sampleRate; 
    wfx.wBitsPerSample = 32;
    wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    hr = pAudioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, &wfx, NULL);
    hr = pAudioClient->GetService(__uuidof(IAudioRenderClient), (void**)&pRenderClient);
    UINT32 bufferFrameCount;
    hr = pAudioClient->GetBufferSize(&bufferFrameCount);
    hr = pAudioClient->Start();
    currentStatusText = "STATUS: SPIELE AUDIO (WASAPI)...";
    InvalidateRect(hMainWnd, nullptr, true);
    UINT32 framesAvailable;
    UINT32 dataOffset = 0;
    while (dataOffset < pcmData.size() && isRunning) {
        Sleep(10); 
        UINT32 numFramesPadding;
        hr = pAudioClient->GetCurrentPadding(&numFramesPadding);
        framesAvailable = bufferFrameCount - numFramesPadding;
        if (framesAvailable > 0) {
            BYTE *pData;
            UINT32 framesToWrite = std::min(framesAvailable, (UINT32)(pcmData.size() - dataOffset));
            hr = pRenderClient->GetBuffer(framesToWrite, &pData);
            memcpy(pData, &pcmData[dataOffset], framesToWrite * sizeof(float));
            hr = pRenderClient->ReleaseBuffer(framesToWrite, 0);
            dataOffset += framesToWrite;
        }
    }
    Sleep(500); 
    pAudioClient->Stop();
    SAFE_RELEASE(pEnumerator);
    SAFE_RELEASE(pDevice);
    SAFE_RELEASE(pAudioClient);
    SAFE_RELEASE(pRenderClient);
    CoUninitialize();
    currentStatusText = "STATUS: BEREIT";
    isRunning = false;
    InvalidateRect(hMainWnd, nullptr, true);
}
void GenerateAudioLoop(std::string prompt, std::string duration, std::string bpm) {
    isRunning = true;
    stopRequested = false;
    currentStatusText = "STATUS: GENERIERE AUDIO (BITTE WARTEN)...";
    InvalidateRect(hMainWnd, nullptr, true);
    std::string scriptPath = exeDir + "\\beats\\beat_generator.py"; 
    std::string outPath = exeDir + "\\output_beat.wav";
    std::string finalPrompt = prompt + ", " + bpm + " bpm";
    std::string cmdLine = "cmd /c \"python \"" + scriptPath + "\" --prompt \"" + finalPrompt + "\" --duration " + duration + " --output \"" + outPath + "\"\"";
    STARTUPINFOA si = { sizeof(si) }; 
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, exeDir.c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess); 
        CloseHandle(pi.hThread);
        currentStatusText = "STATUS: AUDIO FERTIG!";
    } else {
        currentStatusText = "FEHLER: Python-Skript nicht gefunden!";
    }
    isRunning = false;
    InvalidateRect(hMainWnd, nullptr, true);
}
void GenerateVoiceLoop(std::string text, std::string style, std::string speaker) {
    isRunning = true;
    stopRequested = false;
    std::thread statusThread([&]() {
        int dots = 0;
        while (isRunning) {
            std::string sweat = "";
            for(int i=0; i < dots; i++) sweat += ".";
            currentStatusText = "KI SCHWITZT BEIM SINGEN... " + style + " " + sweat + " :))";
            InvalidateRect(hMainWnd, nullptr, true);
            dots = (dots + 1) % 4;
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    });
    statusThread.detach();
    std::string scriptPath = exeDir + "\\lyrik\\voice_generator.py"; 
    std::string outPath = exeDir + "\\output_voice.wav";
    std::string cmdLine = "cmd /c \"chcp 65001 >nul & python \"" + scriptPath + 
                          "\" --text \"" + text + "\" --style \"" + style + 
                          "\" --voice \"" + speaker + "\" --output \"" + outPath + "\"\"";
    STARTUPINFOA si = { sizeof(si) }; 
    PROCESS_INFORMATION pi = { 0 };
    if (CreateProcessA(NULL, (LPSTR)cmdLine.c_str(), NULL, NULL, FALSE, CREATE_NO_WINDOW, NULL, exeDir.c_str(), &si, &pi)) {
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess); 
        CloseHandle(pi.hThread);
    }
    isRunning = false;
    currentStatusText = "STATUS: PERFORMANCE FERTIG! :p";
    InvalidateRect(hMainWnd, nullptr, true);
}
void MixAudioFiles(std::string beatPath, std::string voicePath, std::string outPath) {
    int beatRate = 0, voiceRate = 0;
    float bVol = (float)SendMessage(hBeatSlider, TBM_GETPOS, 0, 0) / 100.0f;
    float vVol = (float)SendMessage(hVoiceSlider, TBM_GETPOS, 0, 0) / 100.0f;
    std::vector<float> beatData = LoadAudioForWASAPI(beatPath, beatRate);
    std::vector<float> voiceData = LoadAudioForWASAPI(voicePath, voiceRate);
    if (beatData.empty() || voiceData.empty()) {
        MessageBoxA(hMainWnd, "Beat oder Stimme fehlt! Erst generieren.", "Mixer", MB_ICONWARNING);
        return;
    }
    size_t mixLength = std::max(beatData.size(), voiceData.size());
    std::vector<float> mixedData(mixLength, 0.0f);

    for (size_t i = 0; i < mixLength; i++) {
        float bS = (i < beatData.size()) ? beatData[i] : 0.0f;
        float vS = (i < voiceData.size()) ? voiceData[i] : 0.0f;
        mixedData[i] = (bS * bVol) + (vS * vVol);
        if (mixedData[i] > 1.0f) mixedData[i] = 1.0f;
        if (mixedData[i] < -1.0f) mixedData[i] = -1.0f;
    }
    std::ofstream outFile(outPath, std::ios::binary);
    WavHeader header = {};
    memcpy(header.riff, "RIFF", 4);
    header.overallSize = (uint32_t)(sizeof(WavHeader) + (mixedData.size() * 4) - 8);
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmtChunkId, "fmt ", 4);
    header.fmtChunkSize = 16;
    header.audioFormat = 3;
    header.numChannels = 1;
    header.sampleRate = beatRate;
    header.byteRate = beatRate * 4;
    header.blockAlign = 4;
    header.bitsPerSample = 32;
    outFile.write((char*)&header, sizeof(WavHeader));
    outFile.write("data", 4);
    uint32_t dSize = (uint32_t)(mixedData.size() * 4);
    outFile.write((char*)&dSize, 4);
    outFile.write((char*)mixedData.data(), dSize);
    outFile.close();
    currentStatusText = "STATUS: MIX FERTIG! (final_mix.wav)";
    InvalidateRect(hMainWnd, nullptr, true);
}
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE: {
    hMainWnd = hwnd;
    int yOffset = 10;
    // --- KANAL 1: INSTRUMENTAL (BEAT) ---
    CreateWindowA("STATIC", "KANAL 1: BEAT PROMPT (MusicGen):", WS_VISIBLE | WS_CHILD, 20, yOffset, 250, 20, hwnd, nullptr, nullptr, nullptr);
    hPromptEdit = CreateWindowA("EDIT", "Aggressive modern trap beat, fast rolling hi-hats, sliding heavy 808 bass, dark bell melody, club banger", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | WS_VSCROLL, 20, yOffset + 20, 480, 45, hwnd, nullptr, nullptr, nullptr);
    yOffset += 70;
    int xPos = 20;
    CreateWindowA("STATIC", "DAUER (Sek):", WS_VISIBLE | WS_CHILD, xPos, yOffset + 3, 90, 20, hwnd, nullptr, nullptr, nullptr);
    hDurationEdit = CreateWindowA("EDIT", "10", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER, xPos + 95, yOffset, 50, 25, hwnd, nullptr, nullptr, nullptr);
    xPos += 170;
    CreateWindowA("STATIC", "BPM:", WS_VISIBLE | WS_CHILD, xPos, yOffset + 3, 40, 20, hwnd, nullptr, nullptr, nullptr);
    hBpmEdit = CreateWindowA("EDIT", "120", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_NUMBER, xPos + 45, yOffset, 50, 25, hwnd, nullptr, nullptr, nullptr);
    yOffset += 40;
    CreateWindowA("BUTTON", "1. BEAT GENERIEREN", WS_VISIBLE | WS_CHILD, 20, yOffset, 230, 35, hwnd, (HMENU)10, nullptr, nullptr);
    // --- KANAL 2: SPRACHE (LYRICS) ---
    yOffset += 50;
    CreateWindowA("STATIC", "KANAL 2: LYRICS / TEXT (Bark AI):", WS_VISIBLE | WS_CHILD, 20, yOffset, 250, 20, hwnd, nullptr, nullptr, nullptr);
    hLyricsEdit = CreateWindowA("EDIT", "Rock rock, rock da house yoah, set it up and bring it on.", WS_VISIBLE | WS_CHILD | WS_BORDER | ES_MULTILINE | WS_VSCROLL, 20, yOffset + 20, 480, 45, hwnd, nullptr, nullptr, nullptr);
    yOffset += 75; 
    // 1. Vocal Style Menü erstellen
    CreateWindowA("STATIC", "VOCAL STYLE:", WS_VISIBLE | WS_CHILD, 20, yOffset + 3, 100, 20, hwnd, NULL, NULL, NULL);
    hStyleCombo = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 130, yOffset, 150, 200, hwnd, NULL, NULL, NULL);
    
    const char* styles[] = { "[Rock]", "[Rap]", "[Opera]", "[Soul]", "[Funk]", "[Speech]" };
    for (int i = 0; i < 6; i++) {
        SendMessageA(hStyleCombo, CB_ADDSTRING, 0, (LPARAM)styles[i]);
    } // <--- HIER muss die Schleife für die Styles enden!

    // 2. Jetzt erst den Offset erhöhen für das NÄCHSTE Menü
    yOffset += 40;
    CreateWindowA("STATIC", "SPEAKER ID:", WS_VISIBLE | WS_CHILD, 20, yOffset + 3, 100, 20, hwnd, NULL, NULL, NULL);
    hSpeakerCombo = CreateWindowA("COMBOBOX", "", WS_VISIBLE | WS_CHILD | CBS_DROPDOWNLIST | WS_VSCROLL, 130, yOffset, 200, 200, hwnd, NULL, NULL, NULL);

    // 3. Die Speaker-Strings hinzufügen
    const char* speakers[] = { 
    "None", 
    "v2/en_speaker_6 (M-Rap)", 
    "v2/en_speaker_9 (F-System)",
	"v2/ko_speaker_3 (F-modern)",
	"v2/ko_speaker_0 (F-soft)",
	"v2/zh_speaker_4 (F-Profes.)",
	"v2/ja_speaker_4 (F-Anime)",
	"v2/ja_speaker_1 (M-DeepCalm)",
	"v2/fr_speaker_9 (F-Melodie)",
	"v2/es_speaker_8 (F-Ener.Flow)",
    "v2/en_speaker_2 (M-Rough)", 
    "v2/en_speaker_1 (M-Neutral)",
    "v2/en_speaker_0 (F-Soft)" 
};

for (int i = 0; i < 12; i++) {
    SendMessageA(hSpeakerCombo, CB_ADDSTRING, 0, (LPARAM)speakers[i]);
    }
    SendMessageA(hSpeakerCombo, CB_SETCURSEL, 0, 0);

    // 4. Weiter zum Button
    yOffset += 40;
    hVoiceBtn = CreateWindowA("BUTTON", "2. PERFORMANCE GENERIEREN", WS_VISIBLE | WS_CHILD, 20, yOffset, 230, 35, hwnd, (HMENU)13, nullptr, nullptr);
    // --- KONTROLLZENTRUM (MIX & PLAY) ---
    yOffset += 55;
    hMixBtn = CreateWindowA("BUTTON", "3. BEAT + STIMME MIXEN", WS_VISIBLE | WS_CHILD, 20, yOffset, 230, 40, hwnd, (HMENU)14, nullptr, nullptr);
    hPlayBtn = CreateWindowA("BUTTON", "PLAY MIX", WS_VISIBLE | WS_CHILD, 260, yOffset, 110, 40, hwnd, (HMENU)12, nullptr, nullptr);
    CreateWindowA("BUTTON", "STOP", WS_VISIBLE | WS_CHILD, 380, yOffset, 110, 40, hwnd, (HMENU)11, nullptr, nullptr);
    // --- VOLUME SLIDER ---
    yOffset += 55;
    CreateWindowA("STATIC", "BEAT VOL:", WS_VISIBLE | WS_CHILD, 20, yOffset + 5, 80, 20, hwnd, NULL, NULL, NULL);
    hBeatSlider = CreateWindowA(TRACKBAR_CLASS, "BeatVolume", WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS, 100, yOffset, 200, 30, hwnd, NULL, NULL, NULL);
    yOffset += 35;
    CreateWindowA("STATIC", "VOICE VOL:", WS_VISIBLE | WS_CHILD, 20, yOffset + 5, 80, 20, hwnd, NULL, NULL, NULL);
    hVoiceSlider = CreateWindowA(TRACKBAR_CLASS, "VoiceVolume", WS_VISIBLE | WS_CHILD | TBS_AUTOTICKS, 100, yOffset, 200, 30, hwnd, NULL, NULL, NULL);
    SendMessage(hBeatSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
    SendMessage(hBeatSlider, TBM_SETPOS, TRUE, 70);
    SendMessage(hVoiceSlider, TBM_SETRANGE, TRUE, MAKELONG(0, 200));
    SendMessage(hVoiceSlider, TBM_SETPOS, TRUE, 100);
    yOffset += 45;
    hProgressBar = CreateWindowEx(0, PROGRESS_CLASS, NULL, 
                                WS_CHILD | WS_VISIBLE | PBS_MARQUEE, 
                                20, yOffset, 480, 15, 
                                hwnd, (HMENU)100, NULL, NULL);
    SendMessage(hProgressBar, PBM_SETMARQUEE, (WPARAM)FALSE, (LPARAM)0);
    
} break;
        case WM_COMMAND: {
            int id = LOWORD(wp);
            if (id == 1) { 
                OPENFILENAMEA ofn = {0}; 
                ofn.lStructSize = sizeof(ofn); 
                ofn.hwndOwner = hwnd;
                ofn.lpstrFilter = "AI Audio Models (*.bin; *.onnx)\0*.bin;*.onnx\0All Files\0*.*\0"; 
                ofn.lpstrFile = globalModelPath; 
                ofn.nMaxFile = 512; 
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameA(&ofn)) {
                    currentStatusText = "MODELL GELADEN: " + fs::path(globalModelPath).filename().string();
                    InvalidateRect(hwnd, NULL, TRUE);
                }
            }
            if (id == 10 && !isRunning) {
                char pBuf[1024], dBuf[20], bBuf[20];
                GetWindowTextA(hPromptEdit, pBuf, 1024);
                GetWindowTextA(hDurationEdit, dBuf, 20);
                GetWindowTextA(hBpmEdit, bBuf, 20);
                std::thread(GenerateAudioLoop, std::string(pBuf), std::string(dBuf), std::string(bBuf)).detach();
            }
            if (id == 11) {
                stopRequested = true;
                isRunning = false; 
                currentStatusText = "STATUS: ABGEBROCHEN.";
                InvalidateRect(hMainWnd, nullptr, true);
            }
            if (id == 12 && !isRunning) {
                std::string wavPath = exeDir + "\\final_mix.wav"; 
                int sampleRate = 0;
                currentStatusText = "STATUS: LADE WAV IN DEN RAM...";
                InvalidateRect(hMainWnd, nullptr, true);
                std::vector<float> myAudioData = LoadAudioForWASAPI(wavPath, sampleRate);
                if (!myAudioData.empty() && sampleRate > 0) {
                    isRunning = true;
                    std::thread([myAudioData, sampleRate]() {
                        PlayAudioWASAPI(myAudioData, sampleRate);
                    }).detach();
                }
            }
            if (id == 13 && !isRunning) {
				char lBuf[2048], sBuf[50], vBuf[100];
				GetWindowTextA(hLyricsEdit, lBuf, 2048);
    
				// Style auslesen
				int selStyle = SendMessage(hStyleCombo, CB_GETCURSEL, 0, 0);
				SendMessageA(hStyleCombo, CB_GETLBTEXT, selStyle, (LPARAM)sBuf);
    
				// Speaker auslesen
				int selSpeak = SendMessage(hSpeakerCombo, CB_GETCURSEL, 0, 0);
				SendMessageA(hSpeakerCombo, CB_GETLBTEXT, selSpeak, (LPARAM)vBuf);
    
				// Wir putzen den String (nur die ID schicken, nicht den Text in Klammern)
				std::string speakerArg = vBuf;
				if (speakerArg.find(" ") != std::string::npos) {
					speakerArg = speakerArg.substr(0, speakerArg.find(" "));
				}
				if (speakerArg == "None") speakerArg = "";

				std::thread(GenerateVoiceLoop, std::string(lBuf), std::string(sBuf), speakerArg).detach();
			}
            if (id == 14 && !isRunning) {
				std::string bPath = exeDir + "\\output_beat.wav";
				std::string vPath = exeDir + "\\output_voice.wav";
				std::string mPath = exeDir + "\\final_mix.wav";
				MixAudioFiles(bPath, vPath, mPath);
			}
        } break;
        case WM_PAINT: {
            PAINTSTRUCT ps; HDC hdc = BeginPaint(hwnd, &ps);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 0));
            RECT r = {20, 530, 500, 560}; 
            DrawTextA(hdc, currentStatusText.c_str(), -1, &r, DT_LEFT);
            EndPaint(hwnd, &ps);
        } break;
        case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
int WINAPI WinMain(HINSTANCE hI, HINSTANCE hP, LPSTR lpC, int nS) {
    char path[MAX_PATH]; GetModuleFileNameA(NULL, path, MAX_PATH);
    PathRemoveFileSpecA(path); exeDir = path;
    WNDCLASSA wc = {0}; 
    wc.lpfnWndProc = WndProc; 
    wc.hInstance = hI;
    wc.lpszClassName = "AI_AUDIO"; 
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); 
    RegisterClassA(&wc);
    CreateWindowA("AI_AUDIO", "AI - AUDIO CENTER", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 530, 600, nullptr, nullptr, hI, nullptr);
    MSG m; 
    while (GetMessage(&m, nullptr, 0, 0)) { 
        TranslateMessage(&m); 
        DispatchMessage(&m); 
    }
    return 0;
}