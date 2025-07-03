/*
* HeistBlueprint.cpp, 11/9/2020 7:49 PM
*/

static std::map<string, int> heist_job_offsets {
    {"id",         0x0},
    {"skill_name", 0x8},
};

class HeistJob : public PoEObject {
public:

    wstring skill_name;
    int level;

    HeistJob(addrtype address) : PoEObject(address, &heist_job_offsets) {
        wchar_t buffer[32];
        skill_name = PoEMemory::read<wchar_t>(read<addrtype>("skill_name"), buffer, 32);
    }

    void __new() {
        PoEObject::__new();
        __set(L"skillName", skill_name.c_str(), AhkWString,
              L"level", level, AhkInt,
              nullptr);
    }
};

static std::map<string, int> reward_room_offsets {
    {"reward", 0x0},
    {"art",    0x8},
    {"type",  0x10},
    {"name",  0x34}
};

class RewardRoom : public PoEObject {
public:

    wstring type, name, reward;

    RewardRoom(addrtype address) : PoEObject(address, &reward_room_offsets) {
        wchar_t buffer[32];
        name = PoEMemory::read<wchar_t>(read<addrtype>("name"), buffer, 32);
        reward = PoEMemory::read<wchar_t>(read<addrtype>("reward"), buffer, 32);
    }

    void __new() {
        PoEObject::__new();
        __set(L"name", name.c_str(), AhkWString,
              L"reward", reward.c_str(), AhkWString,
              nullptr);
    }
};

static std::map<string, int> wing_offsets {
    {"jobs",          0x00},
    {"rewared_rooms", 0x20},
};

class Wing : public PoEObject {
private:

    AhkObjRef* __get_jobs() {
        AhkObj jobs;
        for (auto& i : get_jobs())
            jobs.__set(L"", (AhkObjRef*)*i, AhkObject, nullptr);
        __set(L"jobs", (AhkObjRef*)jobs, AhkObject, nullptr);
        return jobs;
    }

    AhkObjRef* __get_reward_rooms() {
        AhkObj rooms;
        for (auto& i : get_reward_rooms())
            rooms.__set(L"", (AhkObjRef*)*i, AhkObject, nullptr);
        __set(L"rooms", (AhkObjRef*)rooms, AhkObject, nullptr);
        return rooms;
    }

public:

    std::vector<shared_ptr<HeistJob>> jobs;
    std::vector<shared_ptr<RewardRoom>> reward_rooms;

    Wing(addrtype address) : PoEObject(address, &wing_offsets)
    {
        add_method(L"getJobs", this, (MethodType)&Wing::__get_jobs, AhkObject);
        add_method(L"getRooms", this, (MethodType)&Wing::__get_reward_rooms, AhkObject);
    }

    void __new() {
        PoEObject::__new();
        __get_jobs();
        __get_reward_rooms();
    }

    std::vector<shared_ptr<HeistJob>>& get_jobs() {
        if (jobs.empty()) {
            for (auto addr : read_array<addrtype>("jobs", 0x18)) {
                HeistJob* job = new HeistJob(PoEMemory::read<addrtype>(addr));
                job->level = PoEMemory::read<byte>(addr + 0x10);
                jobs.push_back(shared_ptr<HeistJob>(job));
            }
        }
        return jobs;
    }

    std::vector<shared_ptr<RewardRoom>>& get_reward_rooms() {
        if (reward_rooms.empty()) {
            for (auto addr : read_array<addrtype>("rewared_rooms", 0x0, 0x18))
                reward_rooms.push_back(shared_ptr<RewardRoom>(new RewardRoom(addr)));
        }
        return reward_rooms;
    }
};

/* HeistBlueprint component offsets */

static std::map<string, int> heist_blueprint_component_offsets {
    {"area_level", 0x1c},
    {"wings",      0x20},
};

class HeistBlueprint : public Component {
private:

    AhkObjRef* __get_wings() {
        AhkObj temp_wings;
        for (auto& i : get_wings())
            temp_wings.__set(L"", (AhkObjRef*)*i, AhkObject, nullptr);
        __set(L"wings", (AhkObjRef*)temp_wings, AhkObject, nullptr);
        return temp_wings;
    }

public:

    std::vector<shared_ptr<Wing>> wings;

    HeistBlueprint(addrtype address)
        : Component(address, "HeistBlueprint", &heist_blueprint_component_offsets)
    {
        add_method(L"getWings", this, (MethodType)&HeistBlueprint::__get_wings, AhkObject);
    }

    std::vector<shared_ptr<Wing>>& get_wings() {
        if (wings.empty()) {
            for (auto addr : read_array<addrtype>("wings", 0x50))
                wings.push_back(shared_ptr<Wing>(new Wing(addr)));
        }
        return wings;
    }
};
