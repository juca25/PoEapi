/*
*  PoE.cpp, 8/4/2020 9:11 PM
*/

#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <iostream>
#include <map>
#include <memory>
#include <queue>

using namespace std;

typedef unsigned __int64 addrtype;

#include "ahkpp"
#include "Hud.cpp"
#include "PoEMemory.cpp"
#include "RemoteMemoryObject.cpp"

std::map<string, int> poe_offsets {
    {"active_game_states", 0x20},
    {"all_game_states",    0x48},
};

class PoE : public RemoteMemoryObject, public AhkObj, public Hud {
protected:

    int get_process_by_name(const char* name) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        PROCESSENTRY32 process = {};
        process.dwSize = sizeof(process);
        DWORD pid = 0;

        // walkthrough all processes.
        if (Process32First(snapshot, &process)) {
            do {
                if (string(process.szExeFile) == string(name)) {
                    pid = process.th32ProcessID;
                    break;
                }
            } while (Process32Next(snapshot, &process));
        }
        CloseHandle(snapshot);

        return pid;
    }

    std::map<addrtype, int>& get_all_game_states() {
        addrtype base_address, state_ptr;
        int size = 12;

        all_game_states.clear();
        base_address = address + (*offsets)["all_game_states"];
        for (int id = 0; id < size; ++id) {
            addrtype state_ptr = PoEMemory::read<addrtype>(base_address);
            base_address += 0x10;
            all_game_states[state_ptr] = id;
        }

        return all_game_states;
    }

    HWND get_hwnd() {
        DWORD pid;

        while (1) {
            HWND hwnd = FindWindow("POEWindowClass", "Path of Exile");
            if (hwnd == nullptr)
                break;

            GetWindowThreadProcessId(hwnd, &pid);
            if (pid == process_id)
                return hwnd;
        }

        return 0;
    }

    int parse_pattern(const char* pattern_str, byte* pattern, byte* mask) {
        int len = 0;

        while (*pattern_str) {
            if (*pattern_str == ' ') {
                pattern_str++;
                continue;
            }

            if (*pattern_str == 'x' || *pattern_str == '?') {
                pattern_str += 2;
                mask[len++] = '?';
                continue;
            }

            int c, p = 0;
            for (int i = 0; i < 2; i++) {
                c = pattern_str[i];
                if (c >= '0' && c <= '9')
                    p = (p << 4) | (c - 0x30);
                else if (c >= 'a' && c <= 'f')
                    p = (p << 4) | (c - 0x57);
                else if (c >= 'A' && c <= 'F')
                    p = (p << 4) | (c - 0x37);
            }
            pattern[len] = p;
            mask[len++] = 'x';
            pattern_str += 2;
        }

        return len;
    }

    int compare(const byte* pattern, const byte* data, const byte* mask, int len) {
        int matched = true;

        for (int i = 0; i < len; i++) {
            if (mask[i] == 'x' && pattern[i] != data[i]) {
                matched = false;
                break;
            }
        }

        return matched;
    }

    addrtype find_bytes(const byte* data, const byte* mask, int len) {
        addrtype end_address = base_address + size_of_image;
        byte* buffer = new byte[size_of_image];

        if (PoEMemory::read(base_address, buffer, size_of_image)) {
            for (int i = 0; i < size_of_image - len; i++) {
                if (compare(data, &buffer[i], mask, len)) {
                    delete[] buffer;
                    return base_address + i;
                }
            }
        }
        delete[] buffer;

        return 0;
    }

    addrtype find_string(const string str) {
        byte mask[str.length()];
        for (int i = 0; i < str.length(); ++i)
            mask[i] = 'x';

        return find_bytes((const byte*)str.c_str(), mask, str.length());
    }

    addrtype find_pattern(const char* pattern_str) {
        byte pattern[256], mask[256];
        int len = parse_pattern(pattern_str, pattern, mask);
        return find_bytes(pattern, mask, len);
    }

public:

    const char* executable_names[4] = {
        "PathOfExile.exe",
        "PathOfExile_x64.exe",
        "PathOfExileSteam.exe",
        "PathOfExile_KG.exe"
    };

    addrtype base_address;
    int size_of_image;
    int process_id;
    HWND window;
    char version_string[128];

    std::map<addrtype, int> all_game_states;
    shared_ptr<GameState> active_game_state;
    shared_ptr<InGameState> in_game_state;
    InGameUI* in_game_ui;
    InGameData* in_game_data;
    ServerData* server_data;
    LocalPlayer *local_player;
    bool in_game_flag = false;
    volatile bool is_loading = false;
    bool is_ready = false;

    PoE() : RemoteMemoryObject(0, &poe_offsets) {
    }

    void check_game_state() {
        std::vector<addrtype> state_ptrs;
        
        if (!process_handle && !open_target_process())
            return;

        state_ptrs = read_array<addrtype>("active_game_states", 0x0, 0x10);
        if (state_ptrs.empty()) {
            in_game_flag = false;
            open_target_process();
            return;
        }

        if (all_game_states[state_ptrs[0]] == 0x4) {    // InGameState
            if (!in_game_state || in_game_state->address != state_ptrs[0])
                in_game_state.reset(new InGameState(0x4, state_ptrs[0]));
            is_loading = state_ptrs.size() > 1;
            in_game_flag = true;
        } else {
            is_loading = false;
            in_game_flag = false;
        }
    }

    const char* get_version() {
        if (addrtype addr = find_string("release tags")) {
            PoEMemory::read<char>(addr, version_string, 128);
        }

        return version_string;
    }

    void reset() {
        check_game_state();
        if (in_game_flag) {
            in_game_ui = in_game_state->in_game_ui();
            in_game_data = in_game_state->in_game_data();
            server_data = in_game_state->server_data();
            if (!in_game_ui || !in_game_data || !server_data)
                return;

            local_player = in_game_data->local_player();
            if (!local_player)
                return;

            is_ready = true;
        }
    }

    bool open_target_process() {
        for (auto i : executable_names) {
            if (process_id = get_process_by_name(i))
                break;
        }

        window = (HWND)0;
        process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, false, process_id);
        if (process_handle) {
            HMODULE module;
            DWORD size;

            window = get_hwnd();
            if (!window) {
                process_handle = (HANDLE)0;
                return false;
            }

            if (EnumProcessModules(process_handle, &module, sizeof(module), &size)) {
                MODULEINFO module_info;
                GetModuleInformation(process_handle, module, &module_info, sizeof(MODULEINFO));
                base_address = (addrtype)module_info.lpBaseOfDll;
                size_of_image = module_info.SizeOfImage;
            }

            if (addrtype addr = find_pattern("48 8b f1 33 ed 48 39 2d")) {
                address = PoEMemory::read<addrtype>(addr + PoEMemory::read<int>(addr + 8) + 12);
                get_all_game_states();
                bind(window);
                return true;
            }
        }

        return false;
    }

    void list_game_states() {
        if (!all_game_states.empty()) {
            wprintf(L"%llx: All game states\n", read<addrtype>("all_game_states"));
            for (auto&  i : all_game_states)
                wprintf(L"    %llx => %2d\n", i.first, i.second);
        }
    }

    Point get_pos(Entity* entity) {
        if (in_game_state && entity) {
            Render* render = entity->get_component<Render>();
            if (render) {
                Vector3 vec = render->position();
                in_game_state->transform(vec);

                return {(int)vec.x, (int)vec.y};
            }
        }

        return {0, 0};
    }
    
    void mouse_click(Point pos) {
        ClientToScreen(window, (LPPOINT)&pos);
        SetCursorPos (pos.x, pos.y);
        Sleep(10);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
    }

    void mouse_move(Point pos) {
        ClientToScreen(window, (LPPOINT)&pos);
        SetCursorPos (pos.x, pos.y);
        Sleep(30);
    }

    void logout() {
        PMIB_TCPTABLE_OWNER_PID  tcp_table;
        DWORD size = 0;

        GetExtendedTcpTable(tcp_table, &size, 0, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        tcp_table = (PMIB_TCPTABLE_OWNER_PID)malloc(size);
        GetExtendedTcpTable(tcp_table, &size, 0, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
        if (tcp_table->dwNumEntries > 0) {
            for (int i = 0; i < tcp_table->dwNumEntries; ++i) {
                if (tcp_table->table[i].dwOwningPid == process_id) {
                    tcp_table->table[i].dwState = MIB_TCP_STATE_DELETE_TCB;
                    SetTcpEntry((PMIB_TCPROW)&tcp_table->table[i]);
                }
            }
        }
        free(tcp_table);
    }
};
