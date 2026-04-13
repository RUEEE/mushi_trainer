
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include <sstream>

#include <tlhelp32.h>

struct GameContext
{
    HWND hwnd = nullptr;
    DWORD pid = 0;
    HANDLE hProcess = nullptr;
    uintptr_t base = 0;
    bool attached = false;
};

struct Selection
{
    int hotkey_vk = 0;
    std::wstring description;
    bool is_checkbox = false;
    bool checked = false;

    std::function<void()> OnCheck;
    std::function<void()> OnTick;
    std::function<void()> OnUnCheck;
};

GameContext g_game;
std::vector<Selection> g_selections;
bool g_prevKeyState[256] = {};
HWND g_mainWnd = nullptr;
HFONT g_font = nullptr;

const wchar_t* TARGET_WINDOW_NAME = L"Mushihimesama";
const wchar_t* TARGET_MODULE_NAME = L"dat12.bin";

template<typename T>
bool WriteMemory(HANDLE hProcess, uintptr_t address, const T& value)
{
    SIZE_T written = 0;
    return WriteProcessMemory(hProcess, reinterpret_cast<LPVOID>(address), &value, sizeof(T), &written)
        && written == sizeof(T);
}

HWND FindTargetWindowByTitle(const wchar_t* title)
{
    return FindWindowW(nullptr, title);
}

uintptr_t GetModuleBaseAddress(DWORD pid, const wchar_t* moduleName)
{
    uintptr_t result = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;
    
    MODULEENTRY32W me = {};
    me.dwSize = sizeof(me);
    std::wstring w;
    if (Module32FirstW(snapshot, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, moduleName) == 0)
            {
                result = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &me));
    }
    CloseHandle(snapshot);
    return result;
}

bool AttachGame()
{
    if (g_game.hProcess)
    {
        CloseHandle(g_game.hProcess);
        g_game.hProcess = nullptr;
    }

    g_game = {};

    HWND hwnd = FindTargetWindowByTitle(TARGET_WINDOW_NAME);
    if (!hwnd)
        return false;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid)
        return false;

    HANDLE hProcess = OpenProcess(PROCESS_VM_WRITE | PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!hProcess)
        return false;

    uintptr_t base = GetModuleBaseAddress(pid, TARGET_MODULE_NAME);
    if (!base)
    {
        CloseHandle(hProcess);
        return false;
    }

    g_game.hwnd = hwnd;
    g_game.pid = pid;
    g_game.hProcess = hProcess;
    g_game.base = base;
    g_game.attached = true;
    return true;
}

std::wstring VkToString(int vk)
{
    switch (vk)
    {
    case VK_F1: return L"F1";
    case VK_F2: return L"F2";
    case VK_F3: return L"F3";
    case VK_F4: return L"F4";
    case VK_F5: return L"F5";
    case VK_F6: return L"F6";
    case VK_F7: return L"F7";
    case VK_F8: return L"F8";
    case VK_F9: return L"F9";
    case VK_F10: return L"F10";
    case VK_F11: return L"F11";
    case VK_F12: return L"F12";
    default: return L"KEY";
    }
}

void TriggerSelection(Selection& sel)
{
    if (!g_game.attached || !g_game.hProcess || !g_game.base)
        return;

    if (sel.is_checkbox)
    {
        sel.checked = !sel.checked;
        if (sel.checked)
        {
            if (sel.OnCheck) sel.OnCheck();
        }
        else
        {
            if (sel.OnUnCheck) sel.OnUnCheck();
        }
    }
    else
    {
        if (sel.OnCheck) sel.OnCheck();
    }
}

void PollHotkeys()
{
    for (auto& sel : g_selections)
    {
        int vk = sel.hotkey_vk;
        bool downNow = (GetAsyncKeyState(vk) & 0x8000) != 0;
        bool downPrev = g_prevKeyState[vk];

        if (downNow && !downPrev)
        {
            TriggerSelection(sel);
            InvalidateRect(g_mainWnd, nullptr, TRUE);
        }

        g_prevKeyState[vk] = downNow;
    }
}

void TickSelections()
{
    if (!g_game.attached || !g_game.hProcess || !g_game.base)
        return;

    for (auto& sel : g_selections)
    {
        if (sel.is_checkbox && sel.checked)
        {
            if (sel.OnTick) sel.OnTick();
        }
    }
}

void BuildSelections()
{
    g_selections.clear();

    Selection s1;
    s1.hotkey_vk = VK_F1;
    s1.description = L"启动无敌";
    s1.is_checkbox = true;

    // +8F0FB, :似乎是丢b的丢出时的音效的播放，其中push esi是音效大小, push 53是音效类型，76是miss音效，7D是丢b音效
    // or word ptr[eax + 02], 01
    // push eax
    // push ecx
    // mov eax, [dat12.bin + 6C2AAC]
    // mov ecx, [eax + 00000478]
    // push 64
    // push 200
    // push 7D
    // push 05
    // call ecx
    // add esp, 10
    // +77C3: or word ptr [eax+02],01这个是设置子弹的dead flag

    s1.OnCheck = []() {
        char bytes[41] = {0x66, 0x83, 0x48, 0x02, 0x01, 0x90, 0x60, 0x8B, 0x05, 0xAC, 0x2A, 0xE1, 0x7A, 0x8B, 0x88, 0x78, 0x04, 0x00, 0x00, 0x6A, 0x64, 0x68, 0x00, 0x02, 0x00, 0x00, 0x6A, 0x7D, 0x6A, 0x05, 0xFF, 0xD1, 0x83, 0xC4, 0x10, 0x61, 0xE9, 0x8E, 0xF8, 0xFA, 0xFF};
        DWORD codecave = g_game.base + 0x57F11;
        for (int i = 0; i < 41; i++){
			DWORD oldProtect;
			VirtualProtectEx(g_game.hProcess, (LPVOID)(codecave + i),1, PAGE_EXECUTE_READWRITE, &oldProtect);
            WriteMemory(g_game.hProcess, codecave + i, bytes[i]);
			VirtualProtectEx(g_game.hProcess, (LPVOID)(codecave + i),1, oldProtect, &oldProtect);
        }
        char bytes2[5] = {0xE9,0x49,0x07,0x05,0x00};
        DWORD code = g_game.base + 0x77C3;
        for (int i = 0; i < 5; i++) {
            DWORD oldProtect;
            VirtualProtectEx(g_game.hProcess, (LPVOID)(code + i), 5, PAGE_EXECUTE_READWRITE, &oldProtect);
            WriteMemory(g_game.hProcess, code + i, bytes2[i]);
            VirtualProtectEx(g_game.hProcess, (LPVOID)(code + i), 5, oldProtect, &oldProtect);
        }
        };
    s1.OnTick = []() {
        int value = 999999;
        WriteMemory(g_game.hProcess, g_game.base + 0x6B04E0, value);
        };
    s1.OnUnCheck = []() {
        int value = 0;
        WriteMemory(g_game.hProcess, g_game.base + 0x6B04E0, value);
        char bytes2[5] = { 0x66, 0x83, 0x48, 0x02, 0x01 };
        DWORD code = g_game.base + 0x77C3;
        for (int i = 0; i < 5; i++) {
            DWORD oldProtect;
            VirtualProtectEx(g_game.hProcess, (LPVOID)(code + i), 5, PAGE_EXECUTE_READWRITE, &oldProtect);
            WriteMemory(g_game.hProcess, code + i, bytes2[i]);
            VirtualProtectEx(g_game.hProcess, (LPVOID)(code + i), 5, oldProtect, &oldProtect);
        }
        };
    g_selections.push_back(s1);

    Selection s2;
    s2.hotkey_vk = VK_F2;
    s2.description = L"（尽量）禁止反击弹";
    s2.is_checkbox = true;
    s2.OnCheck = []() {};
    s2.OnTick = []() {
        int value = 0;
        WriteMemory(g_game.hProcess, g_game.base + 0x18F1B4, value);
        };
    s2.OnUnCheck = []() {};
    g_selections.push_back(s2);

    Selection s3;
    s3.hotkey_vk = VK_F3;
    s3.description = L"无限残机";
    s3.is_checkbox = true;
    s3.OnCheck = []() {};
    s3.OnTick = []() {
        int value = 7;
        WriteMemory(g_game.hProcess, g_game.base + 0x6B0448, value);
        };
    s3.OnUnCheck = []() {};
    g_selections.push_back(s3);

    Selection s4;
    s4.hotkey_vk = VK_F4;
    s4.description = L"锁圣歌血";
    s4.is_checkbox = true;
    s4.OnCheck = []() {};
    s4.OnTick = []() {
        int value = 1800;
        WriteMemory(g_game.hProcess, g_game.base + 0x18F124, value);
        };
    s4.OnUnCheck = []() {};
    g_selections.push_back(s4);

    Selection s5;
    s5.hotkey_vk = VK_F5;
    s5.description = L"进入圣歌";
    s5.is_checkbox = false;
    s5.OnCheck = []() {
        int value = 1800;
        WriteMemory(g_game.hProcess, g_game.base + 0x18F124, value);
        };
    s5.OnTick = []() {};
    s5.OnUnCheck = []() {};
    g_selections.push_back(s5);
}

std::wstring BuildStatusLine()
{
    if (!g_game.attached)
        return L"未获取到游戏，请进入游戏并进入Normal模式";

    std::wostringstream oss;
    oss << L"已获取到游戏窗口 pid =" << g_game.pid;
    return oss.str();
}

void RenderContent(HDC hdc)
{
    SelectObject(hdc, g_font);
    SetBkMode(hdc, TRANSPARENT);

    int x = 10;
    int y = 10;
    int lineHeight = 24;

    std::wstring status = BuildStatusLine();
    TextOutW(hdc, x, y, status.c_str(), (int)status.size());
    y += lineHeight;

    for (const auto& sel : g_selections)
    {
        std::wstring line;
        if (sel.is_checkbox)
        {
            line = std::wstring(sel.checked ? L"[√]" : L"[ ]")
                + L"(" + VkToString(sel.hotkey_vk) + L")"
                + sel.description;
        }
        else
        {
            line = L"(" + VkToString(sel.hotkey_vk) + L")" + sel.description;
        }

        TextOutW(hdc, x, y, line.c_str(), (int)line.size());
        y += lineHeight;
    }
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_mainWnd = hwnd;
        g_font = CreateFontW(
            20, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"黑体"
        );

        BuildSelections();
        AttachGame();

        SetTimer(hwnd, 1, 50, nullptr); // 热键轮询/重连
        SetTimer(hwnd, 2, 5, nullptr);  // OnTick
        return 0;
    }
    case WM_TIMER:
    {
        if (wParam == 1)
        {
            if (!g_game.attached) {
                AttachGame();
            }
            PollHotkeys();
            InvalidateRect(hwnd, nullptr, TRUE);
        }
        else if (wParam == 2)
        {
            TickSelections();
        }
        return 0;
    }
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RenderContent(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
    {
        if (g_font) DeleteObject(g_font);
        if (g_game.hProcess) CloseHandle(g_game.hProcess);
        PostQuitMessage(0);
        return 0;
    }
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TrainerWindowClass";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);

    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        L"TrainerWindowClass",
        L"圣歌练习器（Normal游戏/P1）",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        100, 100, 480, 220,
        nullptr, nullptr, hInstance, nullptr
    );

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}