#include "cli/Diagnostic.h"

#include "util/StrUtil.h"
#include "util/Paths.h"
#include "util/ComInitializer.h"
#include "audio/CoreAudioDeviceLister.h"
#include "audio/ExclusiveModeStore.h"
#include "audio/ExclusiveModeManager.h"
#include "audio/AudioFormatStore.h"
#include "audio/FormatManager.h"
#include "core/WatchdogEngine.h"
#include "config/Config.h"
#include "logging/Logger.h"
#include "version.h"

#include <windows.h>
#include <audiopolicy.h>
#include <audioclient.h>
#include <vector>
#include <iostream>

namespace aw {

namespace {

const wchar_t* DeviceStateName(DWORD state) {
    switch (state) {
        case DEVICE_STATE_ACTIVE: return L"active";
        case DEVICE_STATE_DISABLED: return L"disabled";
        case DEVICE_STATE_NOTPRESENT: return L"not-present";
        case DEVICE_STATE_UNPLUGGED: return L"unplugged";
        default: return L"?";
    }
}

// Reads a REG_SZ value from HKLM under "...\Windows NT\CurrentVersion".
std::wstring ReadVersionValue(const wchar_t* name) {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                        L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                        0, KEY_READ, &key) != ERROR_SUCCESS) {
        return L"";
    }
    wchar_t buf[256] = {};
    DWORD size = sizeof(buf);
    DWORD type = 0;
    LONG r = ::RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &size);
    ::RegCloseKey(key);
    if (r == ERROR_SUCCESS && type == REG_SZ) return buf;
    return L"";
}

std::wstring FlowName(EndpointFlow flow) {
    return flow == EndpointFlow::Render ? L"Render " : L"Capture";
}

void PrintEndpoint(const EndpointInfo& e) {
    std::wcout << L"  " << FlowName(e.flow)
               << (e.isDefault ? L"  [default]" : L"           ")
               << L"  " << DeviceStateName(e.deviceState) << L"\n";
    std::wcout << L"       " << e.name << L"\n";
    std::wcout << L"       id = " << e.id << L"\n";
    if (e.exclusiveKnown) {
        std::wcout << L"       exclusive mode = "
                   << (e.exclusiveAllowed ? L"ALLOWED " : L"blocked ")
                   << L"(priority " << (e.exclusivePriority ? L"on" : L"off") << L")"
                   << (e.exclusiveAllowed ? L" <-- NOT COMPLIANT" : L"") << L"\n";
    } else {
        std::wcout << L"       exclusive mode = unknown (" << HrText(e.exclusiveReadHr) << L")\n";
    }
    if (e.deviceState != DEVICE_STATE_ACTIVE) return;
    AudioFormatStore formats;
    AudioFormat current;
    const HRESULT hr = formats.GetDeviceFormat(e.id, current);
    if (FAILED(hr)) {
        std::wcout << L"       default format = unknown (" << HrText(hr) << L")\n";
        return;
    }
    std::wcout << L"       default format = " << DescribeFormat(current)
               << (current.isFloat ? L" float" : L"") << L", " << current.channels << L" ch\n";
    FormatManager fm(formats);
    std::wcout << L"       supported      = " << fm.DescribeSupported(e.id, current) << L"\n";
}

void ListEndpointsWithState() {
    CoreAudioDeviceLister lister;
    ExclusiveModeStore store;
    ExclusiveModeManager exm(store);

    std::vector<EndpointInfo> endpoints;
    HRESULT hr = lister.Enumerate(endpoints);
    if (FAILED(hr)) {
        std::wcout << L"Enumeration failed: " << HrText(hr) << L"\n";
        return;
    }

    std::wcout << L"\nEndpoints (" << endpoints.size() << L")\n";
    std::wcout << L"----------------------------\n";
    for (EndpointInfo& e : endpoints) {
        exm.Inspect(e);
        PrintEndpoint(e);
    }
}

// Tries to open an exclusive-mode audio stream on the device to learn whether
// the OS currently allows it. This is the definitive behavioural check.
std::wstring ProbeExclusiveOnEndpoint(const std::wstring& id) {
    aw::ComPtr<IMMDeviceEnumerator> enumerator;
    HRESULT hr = ::CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                    __uuidof(IMMDeviceEnumerator), enumerator.putVoid());
    if (FAILED(hr)) return L"co-create failed (" + HrText(hr) + L")";

    aw::ComPtr<IMMDevice> device;
    hr = enumerator->GetDevice(id.c_str(), device.put());
    if (FAILED(hr)) return L"device lookup failed (" + HrText(hr) + L")";

    aw::ComPtr<IAudioClient> client;
    hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, client.putVoid());
    if (FAILED(hr)) return L"activation failed (" + HrText(hr) + L")";

    // Ask the endpoint for its native mixed-mode format; exclusive streams
    // must typically specify exactly that format.
    WAVEFORMATEX* mix = nullptr;
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) return L"GetMixFormat failed (" + HrText(hr) + L")";

    hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, 0, 100000ll, 0, mix, nullptr);
    ::CoTaskMemFree(mix);

    if (hr == S_OK) return L"exclusive ALLOWED (stream opened)";
    if (hr == AUDCLNT_E_EXCLUSIVE_MODE_NOT_ALLOWED) return L"exclusive BLOCKED (0x8889000E)";
    return L"other result: " + HrText(hr);
}

} // namespace

void PrintEnvironment() {
    const std::wstring product = ReadVersionValue(L"ProductName");
    const std::wstring display = ReadVersionValue(L"DisplayVersion");
    const std::wstring build = ReadVersionValue(L"CurrentBuildNumber");
    const std::wstring ed = ReadVersionValue(L"EditionID");

    std::wcout << L"OS                : " << (product.empty() ? L"Windows" : product)
               << (display.empty() ? L"" : L" " + display)
               << L" (build " << (build.empty() ? L"?" : build) << L")"
               << (ed.empty() ? L"" : L", " + ed) << L"\n";
    std::wcout << L"Architecture      : 64-bit (x64)\n";

    BOOL elevated = FALSE;
    HANDLE token = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &token)) {
        TOKEN_ELEVATION te{};
        DWORD cb = 0;
        if (::GetTokenInformation(token, TokenElevation, &te, sizeof(te), &cb)) elevated = te.TokenIsElevated;
        ::CloseHandle(token);
    }
    std::wcout << L"Elevated           : " << (elevated ? L"yes" : L"no") << L"\n";
    std::wcout << L"Config             : " << ConfigFilePath() << L"\n";
    std::wcout << L"Logs               : " << LogsDir() << L"\n";
    std::wcout << L"Version            : " << AWW_VERSION_WSTR << L"\n";
}

int RunDevicesCommand() {
    aw::ComInitializer com(COINIT_MULTITHREADED);
    if (!com.ok()) {
        std::wcout << L"COM initialization failed\n";
        return 2;
    }
    ListEndpointsWithState();
    return 0;
}

int RunDiagnoseCommand(bool probeExclusive) {
    aw::ComInitializer com(COINIT_MULTITHREADED);
    if (!com.ok()) {
        std::wcout << L"COM initialization failed\n";
        return 2;
    }

    PrintEnvironment();
    std::wcout << L"\n-- Endpoint inventory --\n";
    ListEndpointsWithState();

    // Find a viable device for the store test: prefer the default (active)
    // render endpoint, otherwise the first active one.
    std::vector<EndpointInfo> endpoints;
    CoreAudioDeviceLister lister;
    if (SUCCEEDED(lister.Enumerate(endpoints)) && !endpoints.empty()) {
        EndpointInfo* target = nullptr;
        for (auto& e : endpoints) {
            if (e.flow == EndpointFlow::Render && e.deviceState == DEVICE_STATE_ACTIVE) {
                target = &e;
                if (e.isDefault) break;
            }
        }
        if (!target) {
            for (auto& e : endpoints) {
                if (e.deviceState == DEVICE_STATE_ACTIVE) { target = &e; break; }
            }
        }
        if (!target) target = &endpoints[0];

        ExclusiveModeStore store;
        ExclusiveModeManager exm(store);
        exm.Inspect(*target);
        std::wcout << L"\n-- Store test on '" << target->name << L"' --\n";

        bool allow = false, priority = false;
        HRESULT hr = store.Read(target->id, allow, priority);
        std::wcout << L"  read  -> " << HrText(hr) << L" (allow=" << (allow ? 1 : 0)
                   << L", priority=" << (priority ? 1 : 0) << L")\n";
        hr = store.VerifyWritable(target->id);
        std::wcout << L"  write (same value) -> " << HrText(hr) << L"\n";

        if (probeExclusive) {
            std::wcout << L"  wasapi probe -> " << ProbeExclusiveOnEndpoint(target->id) << L"\n";
        }
    }

    std::wcout << L"\n-- Config --\n";
    Config cfg = LoadConfig(ConfigFilePath());
    std::wcout << L"  monitorPlayback      = " << (cfg.monitorPlayback ? L"true" : L"false") << L"\n";
    std::wcout << L"  monitorCapture       = " << (cfg.monitorCapture ? L"true" : L"false") << L"\n";
    std::wcout << L"  checkIntervalSeconds = " << cfg.checkIntervalSeconds << L"\n";
    std::wcout << L"  enforce              = " << (cfg.enforce ? L"true" : L"false") << L"\n";
    std::wcout << L"  exclusive protection = " << (cfg.exclusiveModeProtection ? L"ENABLED" : L"DISABLED") << L"\n";
    std::wcout << L"  format standardize   = "
               << (cfg.formatStandardization ? L"ENABLED (" + DescribeFormatTarget(cfg) + L")" : std::wstring(L"DISABLED"))
               << L"\n";

    return 0;
}

} // namespace aw