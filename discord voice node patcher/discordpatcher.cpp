#define _CRT_SECURE_NO_WARNINGS 1

#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <psapi.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <functiondiscoverykeys_devpkey.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <devguid.h>
#include <mmreg.h>
#include <tchar.h>
#include <initguid.h>
#include <cstdint>
#include <string>
#include <cstdlib>
DEFINE_GUID(CLSID_MMDeviceEnumerator, 0xbcde0395, 0xe52f, 0x467c, 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e);
DEFINE_GUID(IID_IMMDeviceEnumerator, 0xa95664d2, 0x9614, 0x4f35, 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6);
DEFINE_GUID(IID_IAudioClient, 0x1cb9ad4c, 0xdbfa, 0x4c32, 0xb1, 0x78, 0xc2, 0xf5, 0x68, 0xa7, 0x03, 0xb2);
DEFINE_GUID(IID_IAudioEndpointVolume, 0x5cdf2c82, 0x841e, 0x4546, 0x97, 0x22, 0x0c, 0xf7, 0x40, 0x78, 0x22, 0x9a);
DEFINE_GUID(IID_ISimpleAudioVolume, 0x87ce5498, 0x68d6, 0x44e5, 0x92, 0x15, 0x6d, 0xa4, 0x7e, 0xf8, 0x83, 0xd8);

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "cfgmgr32.lib")

static bool IsDryRun() {
    static int cached = -1;
    if (cached == -1) {
        const char* v = ::getenv("PATCH_DRY_RUN");
        cached = (v && v[0] == '1') ? 1 : 0;
    }
    return cached == 1;
}

class WASAPIVolumeBypasser {
private:
    IMMDeviceEnumerator* pEnumerator;
    IMMDevice* pDevice;
    IAudioClient* pAudioClient;
    IAudioEndpointVolume* pEndpointVolume;
    ISimpleAudioVolume* pSimpleVolume;
    bool initialized;

public:
    WASAPIVolumeBypasser() : pEnumerator(nullptr), pDevice(nullptr),
        pAudioClient(nullptr), pEndpointVolume(nullptr),
        pSimpleVolume(nullptr), initialized(false) {
    }

    ~WASAPIVolumeBypasser() {
        Cleanup();
    }

    HRESULT Initialize() {
        HRESULT hr = CoInitialize(nullptr);
        if (FAILED(hr)) return hr;

        hr = CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr, CLSCTX_ALL,
            IID_IMMDeviceEnumerator, (void**)&pEnumerator);
        if (FAILED(hr)) return hr;

        hr = pEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
        if (FAILED(hr)) return hr;

        hr = pDevice->Activate(IID_IAudioEndpointVolume, CLSCTX_ALL,
            nullptr, (void**)&pEndpointVolume);
        if (FAILED(hr)) return hr;

        hr = pDevice->Activate(IID_IAudioClient, CLSCTX_ALL,
            nullptr, (void**)&pAudioClient);
        if (FAILED(hr)) return hr;

        initialized = true;
        return S_OK;
    }

    HRESULT EnableExclusiveMode() {
        if (!initialized) return E_FAIL;

        WAVEFORMATEX* pWaveFormat = nullptr;
        HRESULT hr = pAudioClient->GetMixFormat(&pWaveFormat);
        if (FAILED(hr)) return hr;

        hr = pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_EXCLUSIVE,
            AUDCLNT_STREAMFLAGS_NOPERSIST,
            10000000,
            10000000,
            pWaveFormat,
            nullptr
        );

        CoTaskMemFree(pWaveFormat);
        return hr;
    }

    HRESULT EnableSharedModeBypass() {
        if (!initialized) return E_FAIL;

        WAVEFORMATEX* pWaveFormat = nullptr;
        HRESULT hr = pAudioClient->GetMixFormat(&pWaveFormat);
        if (FAILED(hr)) return hr;

        hr = pAudioClient->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_NOPERSIST,
            10000000,
            0,
            pWaveFormat,
            nullptr
        );

        CoTaskMemFree(pWaveFormat);
        if (FAILED(hr)) return hr;

        hr = pAudioClient->GetService(IID_ISimpleAudioVolume, (void**)&pSimpleVolume);
        return hr;
    }

    HRESULT SetRawVolume(float volume) {
        if (!pEndpointVolume) return E_FAIL;

        float minDb, maxDb, stepDb;
        HRESULT hr = pEndpointVolume->GetVolumeRange(&minDb, &maxDb, &stepDb);
        if (FAILED(hr)) return hr;

        float targetDb = minDb + (volume * (maxDb - minDb));
        return pEndpointVolume->SetMasterVolumeLevel(targetDb, nullptr);
    }

    HRESULT SetApplicationVolume(float volume) {
        if (!pSimpleVolume) return E_FAIL;
        return pSimpleVolume->SetMasterVolume(volume, nullptr);
    }

    void GetVolumeInfo() {
        if (!pEndpointVolume) return;

        float currentLevel, minDb, maxDb, stepDb;
        pEndpointVolume->GetMasterVolumeLevel(&currentLevel);
        pEndpointVolume->GetVolumeRange(&minDb, &maxDb, &stepDb);
    }

private:
    void Cleanup() {
        if (pSimpleVolume) pSimpleVolume->Release();
        if (pEndpointVolume) pEndpointVolume->Release();
        if (pAudioClient) pAudioClient->Release();
        if (pDevice) pDevice->Release();
        if (pEnumerator) pEnumerator->Release();
        CoUninitialize();
    }
};

uint32_t CreateAudioFrameStereoInstruction = 0xAD794;
uint32_t AudioEncoderOpusConfigSetChannelsInstruction = 0x302EA8;
uint32_t MonoDownmixerInstructions = 0x95B23;
uint32_t HighPassFilter_Process = 0x4A5022;
uint32_t EmulateStereoSuccess = 0x497504;
uint32_t EmulateBitrateModified = 0x497762;
uint32_t Emulate48Khz = 0x49761B;
uint32_t HighpassCutoffFilter = 0x8B4370;
uint32_t DcReject = 0x8B4550;
uint32_t downmix_func = 0x8B0BB0;
uint32_t AudioEncoderOpusConfig_IsOk = 0x30310C;
uint32_t AudioEncoderOpusImpl_EncodeImpl_Jump = 0x4998BA;
uint32_t AudioEncoderOpusImpl_EncodeImpl_Shellcode = 0x499A2F;
uint32_t CompressionInstruction = 0x4A0C28;
uint32_t AGCConfigFunction = 0x4A6F84;
uint32_t InputClippingInstruction = 0x04A70F2;
uint32_t AGCDigitalGain = 0x664584;
uint32_t AGCClippingAdjustmentInstruction = 0x4A66BE;
uint32_t AGCClippingAdjustmentInstruction2 = 0x663698;
uint32_t EchoCancellationInstruction = 0x8F5E24;
uint32_t Aec3DelayOverTimeInstruction = 0x336304;
uint32_t EchoCancellerJitterInstruction = 0x4ECC78;
uint32_t GainControllerAnalogInstruction = 0x3167A2;
uint32_t AudioProcessing_ProcessStream_AudioFrame = 0x319C60;
uint32_t AudioProcessing_ProcessStream_StreamConfig = 0x318630;
uint32_t NetEqDelayManager = 0xD8F730;
uint32_t MaxBitratePatch = 0x3A6D90;
uint32_t Limiter_Process = 0x311DFE;

// 49 89 D7 48 89 CE 48 8B 05 ? ? ? ? 48 31 E0 48 89 84 24 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 01 C7 41 - sinewave_emulator

extern "C" void dc_reject(const float* in, float* out, int* hp_mem, int len, int channels, int Fs);
extern "C" void __cdecl hp_cutoff(const float* input, int cutoff_Hz, float* output, int* hp_mem, int len, int channels, int sampleRate, int arch);

#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <shlobj.h>

void CleanShellRecent() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_RECENT, NULL, 0, path))) {
        WIN32_FIND_DATAW fd;
        wchar_t fullpath[MAX_PATH];
        wcscat_s(path, L"\\*");
        HANDLE hFind = FindFirstFileW(path, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                swprintf_s(fullpath, MAX_PATH, L"%s\\%s", path, fd.cFileName);
                DeleteFileW(fullpath);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }
}


void ClearRecentFiles() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_RECENT, NULL, 0, path))) {
        WIN32_FIND_DATAW fd;
        wchar_t fullpath[MAX_PATH];
        wcscat_s(path, L"\\*");
        HANDLE hFind = FindFirstFileW(path, &fd);
        if (hFind != INVALID_HANDLE_VALUE) {
            do {
                swprintf_s(fullpath, MAX_PATH, L"%s\\%s", path, fd.cFileName);
                DeleteFileW(fullpath);
            } while (FindNextFileW(hFind, &fd));
            FindClose(hFind);
        }
    }
}

BOOL InjectDLL(HANDLE hProcess, const char* dllPath) {
    size_t len = strlen(dllPath) + 1;
    LPVOID remoteStr = VirtualAllocEx(hProcess, NULL, len, MEM_COMMIT, PAGE_READWRITE);
    if (!remoteStr) return FALSE;
    if (!WriteProcessMemory(hProcess, remoteStr, dllPath, len, NULL)) {
        VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
        return FALSE;
    }
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0,
        (LPTHREAD_START_ROUTINE)LoadLibraryA, remoteStr, 0, NULL);
    if (!hThread) {
        VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
        return FALSE;
    }
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);
    VirtualFreeEx(hProcess, remoteStr, 0, MEM_RELEASE);
    return TRUE;
}

void MinimizeToTaskbar() {
    HWND hwnd = GetConsoleWindow();
    if (hwnd) ShowWindow(hwnd, SW_MINIMIZE);
}

std::vector<std::string> GetProcessNamesToInject() {
    return {
        "Taskmgr.exe", "ProcessHacker.exe", "ProcessHacker-2.39.exe", "procexp.exe", "procexp64.exe",
        "ProcessExplorer.exe", "ProcessHacker2.exe", "ProcessHacker3.exe"
    };
}

bool IsProcessNameMatch(const char* exeName, const std::vector<std::string>& names) {
    for (const auto& name : names) {
        if (_stricmp(exeName, name.c_str()) == 0)
            return true;
    }
    return false;
}

HANDLE GetProcessHandleByName(const std::string& processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return NULL;

    PROCESSENTRY32 pe;
    pe.dwSize = sizeof(PROCESSENTRY32);

    if (Process32First(snapshot, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, processName.c_str()) == 0) {
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD, FALSE, pe.th32ProcessID);
                CloseHandle(snapshot);
                return hProcess;
            }
        } while (Process32Next(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return NULL;  // Process not found
}


void ExternalWrite(HANDLE Process, void* Address, const char* source, uint32_t size) {
    if (IsDryRun()) {
        std::cout << "[DRY-RUN] Would write " << size << " bytes to address " << Address << std::endl;
        return;
    }
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    uintptr_t addr = reinterpret_cast<uintptr_t>(Address);
    uintptr_t base = addr & ~(static_cast<uintptr_t>(page) - 1);
    size_t end = (addr + size);
    size_t sizeToProtect = end - base;
    size_t protectSize = (sizeToProtect + page - 1) & ~(page - 1);
    DWORD oldProt = 0;
    VirtualProtectEx(Process, reinterpret_cast<void*>(base), protectSize, PAGE_EXECUTE_READWRITE, &oldProt);
    WriteProcessMemory(Process, Address, source, size, NULL);
    DWORD tmp = 0;
    VirtualProtectEx(Process, reinterpret_cast<void*>(base), protectSize, oldProt, &tmp);
}

void ExternalWrite(HANDLE Process, void* Address, uint8_t byte) {
    if (IsDryRun()) {
        std::cout << "[DRY-RUN] Would write 1 byte to address " << Address << std::endl;
        return;
    }
    SYSTEM_INFO si{};
    GetSystemInfo(&si);
    const size_t page = si.dwPageSize;
    uintptr_t addr = reinterpret_cast<uintptr_t>(Address);
    uintptr_t base = addr & ~(static_cast<uintptr_t>(page) - 1);
    size_t protectSize = page;
    DWORD oldProt = 0;
    VirtualProtectEx(Process, reinterpret_cast<void*>(base), protectSize, PAGE_EXECUTE_READWRITE, &oldProt);
    WriteProcessMemory(Process, Address, &byte, 1, NULL);
    DWORD tmp = 0;
    VirtualProtectEx(Process, reinterpret_cast<void*>(base), protectSize, oldProt, &tmp);
}

void DemoVolumeBypass() {
    WASAPIVolumeBypasser bypasser;
    if (SUCCEEDED(bypasser.Initialize())) {
        bypasser.EnableExclusiveMode();
        bypasser.SetRawVolume(338.3f);
    }
}

int main() {
    HMODULE VoiceEngine = {};
    HANDLE Discord = {};
    HANDLE Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (!Snapshot || Snapshot == INVALID_HANDLE_VALUE)
    {
        std::cout << "Failed to create snapshot" << std::endl;
        return 0;
    }
    if (IsDryRun()) {
        std::cout << "[DRY-RUN] PATCH_DRY_RUN=1 -> no memory will be modified" << std::endl;
    }
    // Find the Discord process
    PROCESSENTRY32 ProcessEntry = {};
    ProcessEntry.dwSize = sizeof(ProcessEntry);
    if (Process32First(Snapshot, &ProcessEntry))
    {
        do
        {
            if (!strcmp(ProcessEntry.szExeFile, "Discord.exe"))
            {
                HANDLE Process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_CREATE_THREAD, false, ProcessEntry.th32ProcessID);
                if (!Process || Process == INVALID_HANDLE_VALUE)
                {
                    std::cout << "Failed to obtain Discord.exe process access, disable any Permission Blockers or run this as admin" << std::endl;
                    continue;
                }
                DWORD BytesUsed = 0;
                HMODULE Modules[1024] = {};
                if (!EnumProcessModules(Process, Modules, sizeof(Modules), &BytesUsed))
                {
                    std::cout << "Failed to obtain query access to the target process" << std::endl;
                    continue;
                }
                for (uint32_t i = 0; i < BytesUsed / sizeof(Modules[0]); i++)
                {
                    char ModuleName[MAX_PATH] = {};
                    if (!GetModuleBaseNameA(Process, Modules[i], ModuleName, sizeof(ModuleName)))
                    {
                        std::cout << "Could not get the module name" << std::endl;
                        continue;
                    }
                    if (!strcmp(ModuleName, "discord_voice.node"))
                    {
                        VoiceEngine = Modules[i];
                        Discord = Process;
                        goto exit_from_loop;
                    }
                }
            }
        } while (Process32Next(Snapshot, &ProcessEntry));
    }
    std::cout << "Discord not found..." << std::endl;
    system("pause");
    return 0;

exit_from_loop:
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + EmulateStereoSuccess), "\xBD\x01\x00\x00\x00\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 17);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + CreateAudioFrameStereoInstruction), "\x4D\x89\xC5\x90", 4);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AudioEncoderOpusConfigSetChannelsInstruction), 2);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + MonoDownmixerInstructions), "\x90\x90\x90\x90\x90\x90\x90\x90\x90\xE9", 10);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + EmulateBitrateModified), "\x48\xC7\xC5\x00\xD0\x07\x00\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90\x90", 29);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + HighPassFilter_Process), "\x48\xB8\x10\x9E\xD8\xCF\x08\x02\x00\x00\xC3", 11);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + HighpassCutoffFilter), (const char*)hp_cutoff, 0x100);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + DcReject), (const char*)dc_reject, 0x1B6);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + downmix_func), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + Emulate48Khz), "\x90\x90\x90", 3);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AudioEncoderOpusConfig_IsOk), "\x48\xC7\xC0\x01\x00\x00\x00\xC3", 8);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AudioEncoderOpusImpl_EncodeImpl_Jump), "\x90\xE9", 2);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AudioEncoderOpusImpl_EncodeImpl_Shellcode), "\x48\xC7\x47\x20\x00\xD0\x07\x00\xE9\x84\xFE\xFF\xFF", 13);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + CompressionInstruction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AGCConfigFunction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + InputClippingInstruction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AGCDigitalGain), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AGCClippingAdjustmentInstruction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AGCClippingAdjustmentInstruction2), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + EchoCancellationInstruction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + Aec3DelayOverTimeInstruction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + EchoCancellerJitterInstruction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + GainControllerAnalogInstruction), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AudioProcessing_ProcessStream_AudioFrame), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + AudioProcessing_ProcessStream_StreamConfig), "\xC3", 1);
    ExternalWrite(Discord, (void*)((uintptr_t)VoiceEngine + NetEqDelayManager), "\xC3", 1);


    std::cout << "Ia la muie acum esti loud jaka jaka icey baka." << std::endl;
    system("pause");

    return 0;
}
