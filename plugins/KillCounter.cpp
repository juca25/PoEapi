/*
* KillCounter.cpp, 8/31/2020 3:07 PM
*/

using Monsters = std::unordered_set<int>;

struct AreaStat : public AhkObj {

    int index;
    wstring name;
    int level;
    int player_level;
    std::time_t timestamp;
    unsigned int latest_exp;
    int gained_exp = 0;
    unsigned int latest_time;
    unsigned int used_time = 0;
    int kills[4] = {0, 0, 0, 0};
    Monsters total;
    Monsters killed;
};

class KillCounter : public PoEPlugin {
public:

    std::unordered_map<int, AreaStat*> latest_areas;
    AreaStat* current_area = nullptr;
    Monsters nearby_monsters;
    int nearby_radius = 50;
    int num_of_monsters, num_of_minions;
    int total_monsters, kills;
    int maximum_area_count = 99;
    bool event_enabled;

    KillCounter() : PoEPlugin(L"KillCounter", "0.11"), current_area(nullptr) {
        add_property(L"radius", &nearby_radius, AhkInt);
        add_property(L"monsters", &num_of_monsters, AhkInt);
        add_property(L"minions", &num_of_minions, AhkInt);
        add_property(L"total", &total_monsters, AhkInt);
        add_property(L"kills", &kills, AhkInt);
        add_property(L"eventEnabled", &event_enabled, AhkBool);

        add_method(L"getStat", this, (MethodType)&KillCounter::get_stat, AhkObject);
        add_method(L"getStats", this, (MethodType)&KillCounter::get_stats, AhkObject);

        num_of_monsters = num_of_minions = 0;
        total_monsters = kills = 0;
    }

    AhkObjRef* get_stat() {
        wchar_t buffer[64];

        if (!current_area)
            return nullptr;

        std::wcsftime(buffer, 64, L"%m/%d/%y %H:%M:%S", std::localtime(&current_area->timestamp));
        current_area->__set(L"areaName", current_area->name.c_str(), AhkWString,
                            L"areaLevel", current_area->level, AhkInt,
                            L"playerLevel", current_area->player_level, AhkInt,
                            L"timestamp", buffer, AhkWString,
                            L"gainedExp", current_area->gained_exp + player->get_exp() - current_area->latest_exp, AhkInt,
                            L"usedTime", (current_area->used_time + GetTickCount() - current_area->latest_time) / 1000, AhkInt,
                            L"totalMonsters", current_area->total.size(), AhkInt,
                            L"totalKills", current_area->killed.size(), AhkInt,
                            L"normalKills", current_area->kills[0], AhkInt,
                            L"magicKills", current_area->kills[1], AhkInt,
                            L"rareKills", current_area->kills[2], AhkInt,
                            L"uniqueKills", current_area->kills[3], AhkInt,
                            nullptr);

        return *current_area;
    }

    AhkObjRef* get_stats() {
        AhkObj stats;
        wchar_t buffer[64];

        for (auto& i : latest_areas) {
            AreaStat* stat = i.second;
            wstring index = std::to_wstring(stat->index + 1);
            stats.__set(index.c_str(), (AhkObjRef*)*i.second, AhkObject, nullptr);

            std::wcsftime(buffer, 64, L"%m/%d/%y %H:%M:%S", std::localtime(&stat->timestamp));
            stat->__set(L"areaName", stat->name.c_str(), AhkWString,
                        L"areaLevel", stat->level, AhkInt,
                        L"playerLevel", stat->player_level, AhkInt,
                        L"timestamp", buffer, AhkWString,
                        L"gainedExp", stat->gained_exp, AhkInt,
                        L"usedTime", stat->used_time / 1000, AhkInt,
                        L"totalMonsters", stat->total.size(), AhkInt,
                        L"totalKills", stat->killed.size(), AhkInt,
                        L"normalKills", stat->kills[0], AhkInt,
                        L"magicKills", stat->kills[1], AhkInt,
                        L"rareKills", stat->kills[2], AhkInt,
                        L"uniqueKills", stat->kills[3], AhkInt,
                        nullptr);

            if (current_area == stat) {
                stat->__set(L"gainedExp", stat->gained_exp + player->get_exp() - stat->latest_exp, AhkInt,
                            L"usedTime", (stat->used_time + GetTickCount() - stat->latest_time) / 1000, AhkInt,
                            nullptr);
            }
        }
        __set(L"stats", (AhkObjRef*)stats, AhkObject, nullptr);

        return stats;
    }

    void on_area_changed(AreaTemplate* world_area, int hash_code, LocalPlayer* player) {
        nearby_monsters.clear();
        if (current_area) {
            current_area->used_time += GetTickCount() - current_area->latest_time;
            current_area->gained_exp += player->get_exp() - current_area->latest_exp;
        }

        if (world_area->is_town() || world_area->is_hideout()) {
            current_area = nullptr;
            PostThreadMessage(thread_id, WM_KILL_COUNTER, 0, 0);
            return;
        }

        if (latest_areas.find(hash_code) == latest_areas.end()) {
            // Remove the oldest area record.
            if (latest_areas.size() == maximum_area_count)
                latest_areas.erase(maximum_area_count - 1);

            current_area = new AreaStat();
            current_area->name = world_area->name();
            current_area->level = world_area->level();
            current_area->player_level = player->level();
            current_area->timestamp = std::time(nullptr);
            current_area->latest_exp = player->get_exp();
            current_area->latest_time = GetTickCount();

            for (auto& i : latest_areas) {
                if (i.second->index >= 0)
                    i.second->index += 1;
            }
            latest_areas[hash_code] = current_area;
        } else {
            current_area = latest_areas[hash_code];
            for (auto& i : latest_areas) {
                if (i.second->index < current_area->index)
                    i.second->index += 1;
            }

            current_area->latest_exp = player->get_exp();
            current_area->latest_time = GetTickCount();
        }
        current_area->index = 0;

        total_monsters = kills = 0;
        PostThreadMessage(thread_id, WM_KILL_COUNTER, kills, total_monsters);
    }

    void on_entity_changed(EntityList& entities, EntityList& removed, EntityList& added) {
        int n_monsters = 0, charges = 0, n_minions = 0;
        if (!current_area)
            return;

        if (entities.size() == added.size())
            nearby_monsters.clear();

        for (auto& i : removed) {
            shared_ptr<Entity>& entity = i.second;
            if (entity->is_monster) {
                if (nearby_monsters.count(i.first) && !current_area->killed.count(i.first)) {
                    current_area->killed.insert(i.first);
                    current_area->kills[entity->rarity]++;
                }
            }
        }

        nearby_monsters.clear();
        for (auto& i : entities) {
            Entity* entity = i.second.get();
            if (entity->is_monster) {
                if (!entity->is_neutral) {
#if 0
                    if (entity->has_component("DiesAfterTime"))
                        continue;
#endif
                    int life = entity->life();
                    if (life == 1)  // Some special monsters only have 1 life.
                        continue;

                    if (life == 0) {
                        if (current_area->total.count(i.first) && !current_area->killed.count(i.first)) {
                            current_area->killed.insert(i.first);
                            current_area->kills[entity->rarity]++;
                        }
                        continue;
                    }

                    if (entity->path.find(L"AfflictionVolatile") != wstring::npos)
                        continue;

                    if (current_area->total.count(i.first) == 0) {
                        if (event_enabled && entity->rarity > 1) {     // rare and unique monsters
                            Vector3 pos = entity->pos;
                            poe->in_game_state->transform(pos);
                            PostThreadMessage(thread_id, WM_NEW_MONSTER, (WPARAM)entity->name().c_str(),
                                              (LPARAM)entity->id);
                        }
                        current_area->total.insert(i.first);
                    }
                }

                int dist = player->dist(*entity);
                if (!entity->is_neutral && dist < 2 * nearby_radius)
                    nearby_monsters.insert(i.first);

                if (dist < nearby_radius) {
                    if (entity->is_neutral) {
                        n_minions++;
                        continue;
                    }

                    n_monsters++;
                    if (entity->rarity > 0)
                        charges += 1 + (1 << (entity->rarity - 1)) * 2.5;
                    else
                        charges += 1;
                }
            }
        }

        if (num_of_minions != n_minions) {
            num_of_minions = n_minions;
            PostThreadMessage(thread_id, WM_MINION_CHANGED, num_of_minions, 0);
        }

        if (num_of_monsters != n_monsters) {
            num_of_monsters = n_monsters;
            PostThreadMessage(thread_id, WM_MONSTER_CHANGED, num_of_monsters, charges);
        }

        if (kills != current_area->killed.size() || total_monsters != current_area->total.size()) {
            total_monsters = current_area->total.size();
            kills = current_area->killed.size();
            __int64 gained_exp = current_area->gained_exp + player->get_exp() - current_area->latest_exp;
            gained_exp = (gained_exp << 32) >> 32;
            
            __int64 wparam = (total_monsters << 16) | kills;
            __int64 lparam = (gained_exp << 16) | current_area->player_level;
            PostThreadMessage(thread_id, WM_KILL_COUNTER, wparam, lparam);
        }
    }
};
