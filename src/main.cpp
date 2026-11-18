// NorseSound - swap the game's sounds and choose its music by rule (UE4SS v4.0.0 C++ mod for NORSE: Oath of Blood),
// 2026-11-18. Norse uses Unreal's own audio (MetaSounds, sound waves): AudioEvent assets (SoundToPlay), SoundSets (named
// sound lists), sound notifies on animations, unit effects and mission activities hold references to sounds, and a
// MusicSubsystem plays the music. Rules swap one sound for another wherever those hold it - nothing in the game's files
// changes - and play, stop or change music when a NorseEvents moment happens. A sound played on a hit or a death is
// NorseImpact's job (its Sound = line). Modelled on Sound Record Distributor (Skyrim). Survey:
// RESEARCH-more-frameworks.md (#15).
//
// sounds\*.ini (NorseManifest order), one [section] per rule; its name is only a label:
//   [Heavier hits]
//   Swap = /Game/Norse/Audio/.../Old.Old -> /Game/MyPack/Audio/New.New     ; repeatable
//   Load = /Game/...                                                        ; an asset to load first (optional)
//
//   [Fight music]
//   On = FightStarted        ; FightStarted, FightEnded, MapLoaded, UnitDied, UnitDamaged, Payload:<name>
//   When = Map(Borre)        ; NorseConditions (optional), about the event's unit
//   Music = /Game/Norse/Audio/Music/Battle/S_Music_Battle_Even.S_Music_Battle_Even
//   Volume = 1
//   Stop = true              ; stop the music instead
//   State = 2                ; MusicSubsystem::SetState (the arrangement's state)
//   Stinger = 0              ; MusicSubsystem::PlayStinger variant
//   Chance = 100
// Console: snd.reload, snd.apply (swaps again), snd.music (what is playing), snd.test <rule>.
#include "NorseReflect.hpp"
#include "NorseManifest.hpp"
#include "NorseEvents.h"

#include <map>
#include <random>

namespace fs = std::filesystem;
using namespace nrn;
using namespace nrn::ue;
using namespace norse;

namespace
{
    fs::path g_mod;

    fs::path ModDir()
    {
        HMODULE self = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModDir), &self);
        wchar_t buf[MAX_PATH * 2]{};
        GetModuleFileNameW(self, buf, static_cast<DWORD>(std::size(buf)));
        return fs::path(buf).parent_path().parent_path();
    }

    std::wstring PathOfObj(UObject* o)
    {
        std::wstring full = o->GetFullName(nullptr);
        return full.substr(full.find(L' ') + 1);
    }

    struct Swap
    {
        std::wstring from, to;
        std::string where;
    };
    struct Rule
    {
        std::string name, on, payload, where;
        std::wstring when, music;
        int cond = 0;
        bool condTried = false, stop = false;
        int state = -1, stinger = -1, chance = 100;
        float volume = 1;
    };
    std::vector<Swap> g_swaps;
    std::vector<std::wstring> g_loads;
    std::vector<Rule> g_rules;

    // Where Norse (and the engine) keep references to sounds
    const wchar_t* kSoundHolders[] = {L"AudioEvent", L"SoundSet", L"AnimNotify_PlaySound", L"AnimNotify_PlaySound_Dynamic",
                                      L"UnitEffect_SpawnSound", L"CombatEventAction_Sound", L"Activity_PlaySound", L"Activity_PlaySoundSet",
                                      L"Activity_MusicPlay", L"VillageAction_PlaySound", L"AmbientLoopableSound"};

    using NcCompileFn = int (*)(const char*, char*, int);
    using NcEvalFn = int (*)(int, void*, void*);
    NcCompileFn g_ncCompile = nullptr;
    NcEvalFn g_ncEval = nullptr;

    template <typename F>
    F ModExport(const wchar_t* mod, const char* name)
    {
        HMODULE m = GetModuleHandleW((g_mod.parent_path() / mod / L"dlls" / L"main.dll").c_str());
        return m ? reinterpret_cast<F>(GetProcAddress(m, name)) : nullptr;
    }

    void CompileConditions()
    {
        if (!g_ncCompile) g_ncCompile = ModExport<NcCompileFn>(L"NorseConditions", "NC_Compile");
        if (!g_ncEval) g_ncEval = ModExport<NcEvalFn>(L"NorseConditions", "NC_Eval");
        for (Rule& r : g_rules)
        {
            if (r.when.empty() || r.cond || r.condTried) continue;
            if (!g_ncCompile)
            {
                NRN_WARN("[%s] When = %s, but NorseConditions is not loaded - the rule runs without it", r.name.c_str(), Narrow(r.when).c_str());
                continue;
            }
            char err[256]{};
            r.condTried = true;
            r.cond = g_ncCompile(Narrow(r.when).c_str(), err, sizeof(err));
            if (!r.cond) NRN_ERROR("[%s] When does not compile: %s - the rule is off", r.name.c_str(), err);
        }
    }

    void LoadRules()
    {
        g_swaps.clear();
        g_loads.clear();
        g_rules.clear();
        fs::path dir = g_mod / "sounds";
        std::vector<fs::path> files;
        std::error_code ec;
        if (fs::is_directory(dir, ec))
            for (const auto& f : fs::directory_iterator(dir, ec))
                if (f.is_regular_file() && Lower(f.path().extension().wstring()) == L".ini") files.push_back(f.path());
        nm::Manifest man;
        man.Resolve(g_mod.parent_path().parent_path());
        files = man.Arrange(files, [](const std::string& s) { NRN_INFO("load order: %s", s.c_str()); });
        for (const auto& f : files)
        {
            Rule* cur = nullptr;
            int n = 0;
            for (const auto& raw : ReadLines(f))
            {
                ++n;
                std::wstring l = Trim(StripComment(raw));
                if (l.empty()) continue;
                std::string where = Narrow(f.filename().wstring()) + ":" + std::to_string(n);
                if (l.front() == L'[' && l.back() == L']')
                {
                    g_rules.push_back({});
                    cur = &g_rules.back();
                    cur->name = Narrow(Trim(l.substr(1, l.size() - 2)));
                    cur->where = where;
                    continue;
                }
                size_t eq = l.find(L'=');
                if (!cur || eq == std::wstring::npos)
                {
                    NRN_WARN("%s: not a [rule] or key = value line - skipped", where.c_str());
                    continue;
                }
                std::wstring k = Lower(Trim(l.substr(0, eq))), v = Trim(l.substr(eq + 1));
                if (k == L"swap")
                {
                    size_t arrow = v.find(L"->");
                    if (arrow == std::wstring::npos) NRN_WARN("%s: Swap needs 'old -> new' - skipped", where.c_str());
                    else g_swaps.push_back({Trim(v.substr(0, arrow)), Trim(v.substr(arrow + 2)), where});
                }
                else if (k == L"load") g_loads.push_back(v);
                else if (k == L"on")
                {
                    std::string on = Narrow(v);
                    if (on.rfind("Payload:", 0) == 0)
                    {
                        cur->payload = on.substr(8);
                        on = "Payload";
                    }
                    cur->on = on;
                }
                else if (k == L"when") cur->when = v;
                else if (k == L"music") cur->music = v;
                else if (k == L"volume") cur->volume = static_cast<float>(_wtof(v.c_str()));
                else if (k == L"stop") cur->stop = _wcsicmp(v.c_str(), L"true") == 0 || v == L"1";
                else if (k == L"state") cur->state = _wtoi(v.c_str());
                else if (k == L"stinger") cur->stinger = _wtoi(v.c_str());
                else if (k == L"chance") cur->chance = std::clamp(_wtoi(v.c_str()), 0, 100);
                else NRN_WARN("%s: unknown key '%s' (Swap, Load, On, When, Music, Volume, Stop, State, Stinger, Chance)", where.c_str(), Narrow(k).c_str());
            }
        }
        g_rules.erase(std::remove_if(g_rules.begin(), g_rules.end(), [](const Rule& r) {
                          bool act = !r.music.empty() || r.stop || r.state >= 0 || r.stinger >= 0;
                          if (act && r.on.empty()) NRN_WARN("[%s] (%s) has music but no On = line - left out", r.name.c_str(), r.where.c_str());
                          return !act || r.on.empty(); // a swap-only section keeps its swaps in g_swaps
                      }),
                      g_rules.end());
        CompileConditions();
        NRN_INFO("sound rules: %zu swap(s), %zu music rule(s) from %zu file(s)", g_swaps.size(), g_rules.size(), files.size());
    }

    // ------------------------------------------------------------------ swaps
    std::wstring ReplaceAllI(const std::wstring& s, const std::wstring& from, const std::wstring& to, int& count)
    {
        std::wstring ls = Lower(s), lf = Lower(from), out;
        size_t pos = 0;
        for (size_t hit; (hit = ls.find(lf, pos)) != std::wstring::npos; pos = hit + from.size())
        {
            out += s.substr(pos, hit - pos) + to;
            ++count;
        }
        return out + s.substr(pos);
    }

    void ApplySwaps(const char* why)
    {
        if (g_swaps.empty()) return;
        for (const auto& l : g_loads)
            if (!LoadByPath(l.c_str())) NRN_WARN("Load %s: not found", Narrow(l).c_str());
        std::vector<bool> found(g_swaps.size());
        for (size_t i = 0; i < g_swaps.size(); ++i)
        {
            found[i] = LoadKept(g_swaps[i].to.c_str()) != nullptr;
            if (!found[i]) NRN_WARN("Swap %s (%s): the new sound %s was not found - swap skipped", Narrow(ShortName(g_swaps[i].from)).c_str(), g_swaps[i].where.c_str(),
                                    Narrow(g_swaps[i].to).c_str());
        }
        int objects = 0, fields = 0, refs = 0;
        for (const wchar_t* holder : kSoundHolders)
        {
            std::vector<UObject*> objs;
            try
            {
                RC::Unreal::UObjectGlobals::FindAllOf(holder, objs);
            }
            catch (...)
            {
            }
            for (UObject* o : objs)
            {
                ++objects;
                auto* cls = reinterpret_cast<UStruct*>(Reflect::Get().ClassOf(o));
                ForEachProp(cls, [&](FProperty* p) {
                    void* addr = reinterpret_cast<uint8_t*>(o) + p->GetOffset_Internal();
                    std::wstring text = Export(p, addr, o);
                    if (text == L"<unreadable>" || text.size() < 8) return;
                    int hits = 0;
                    std::wstring now = text;
                    for (size_t i = 0; i < g_swaps.size(); ++i)
                        if (found[i]) now = ReplaceAllI(now, g_swaps[i].from, g_swaps[i].to, hits);
                    if (!hits) return;
                    bool faulted = false;
                    if (!SehImport(p, now.c_str(), addr, o, &faulted) || faulted)
                    {
                        NRN_WARN("%s.%s: Unreal did not accept the swapped value", Narrow(ShortName(PathOfObj(o))).c_str(), Narrow(p->GetName()).c_str());
                        return;
                    }
                    ++fields;
                    refs += hits;
                    NRN_INFO("swapped %d reference(s) in %s.%s", hits, Narrow(ShortName(PathOfObj(o))).c_str(), Narrow(p->GetName()).c_str());
                });
            }
        }
        NRN_INFO("swaps (%s): %d reference(s) in %d field(s), %d sound holder(s) looked at", why, refs, fields, objects);
    }

    // ------------------------------------------------------------------ music
    UObject* Music()
    {
        std::vector<UObject*> all;
        try
        {
            RC::Unreal::UObjectGlobals::FindAllOf(L"MusicSubsystem", all);
        }
        catch (...)
        {
        }
        for (UObject* o : all)
            if (ShortName(PathOfObj(o)).rfind(L"Default__", 0) != 0) return o;
        return nullptr;
    }

    std::string NowPlaying()
    {
        UObject* m = Music();
        if (!m) return "(no MusicSubsystem)";
        Buf b;
        if (!Reflect::Get().Call(m, L"GetMusicPlaying", b.b, 8, sizeof(b.b))) return "(GetMusicPlaying failed)";
        UObject* s = nullptr;
        std::memcpy(&s, b.b, 8);
        return s ? Narrow(ShortName(PathOfObj(s))) : "(nothing)";
    }

    void Play(const Rule& r, const char* why)
    {
        UObject* m = Music();
        if (!m)
        {
            NRN_WARN("[%s] %s: no MusicSubsystem", r.name.c_str(), why);
            return;
        }
        std::string done;
        if (r.stop)
        {
            Buf b;
            done += Reflect::Get().Call(m, L"StopMusic", b.b, 0, sizeof(b.b)) ? " stop" : " stop FAILED";
        }
        if (!r.music.empty())
        {
            UObject* s = LoadKept(r.music.c_str());
            if (!s) done += " music NOT FOUND";
            else
            {
                // PlayMusic(Sound @0, Volume @8, Pitch @12) -> AudioComponent @16
                Buf b;
                std::memcpy(b.b, &s, 8);
                float vol = r.volume, pitch = 1;
                std::memcpy(b.b + 8, &vol, 4);
                std::memcpy(b.b + 12, &pitch, 4);
                done += Reflect::Get().Call(m, L"PlayMusic", b.b, 24, sizeof(b.b)) ? " music " + Narrow(ShortName(r.music)) : " music FAILED";
            }
        }
        if (r.state >= 0)
        {
            Buf b;
            int32_t st = r.state;
            std::memcpy(b.b, &st, 4);
            done += Reflect::Get().Call(m, L"SetState", b.b, 4, sizeof(b.b)) ? " state " + std::to_string(st) : " state FAILED";
        }
        if (r.stinger >= 0)
        {
            Buf b; // PlayStinger(SyncMethod @0 (0 = the first), Variant @4)
            int32_t v = r.stinger;
            std::memcpy(b.b + 4, &v, 4);
            done += Reflect::Get().Call(m, L"PlayStinger", b.b, 8, sizeof(b.b)) ? " stinger " + std::to_string(v) : " stinger FAILED";
        }
        NRN_INFO("[%s] on %s:%s - now playing %s", r.name.c_str(), why, done.c_str(), NowPlaying().c_str());
    }

    void Fire(const char* on, const char* text, void* subject, void* other)
    {
        static std::mt19937 rng{std::random_device{}()};
        for (const Rule& r : g_rules)
        {
            if (r.on != on) continue;
            if (r.on == "Payload" && (!text || r.payload != text)) continue;
            if (!r.when.empty())
            {
                if (!r.cond || !g_ncEval || !g_ncEval(r.cond, subject, other))
                {
                    NRN_INFO("[%s] on %s: When '%s' is false%s", r.name.c_str(), on, Narrow(r.when).c_str(), r.cond ? "" : " (no compiled condition)");
                    continue;
                }
            }
            if (r.chance < 100 && static_cast<int>(rng() % 100) >= r.chance) continue;
            try
            {
                Play(r, on);
            }
            catch (...)
            {
                NRN_ERROR("[%s] threw", r.name.c_str());
            }
        }
    }

    void OnEvent(const NE_Event* e, void*)
    {
        if (!e || !e->name) return;
        Fire(e->name, NE_Text(e), e->subject, e->other);
    }

    bool g_subscribed = false;
    void Subscribe()
    {
        if (g_subscribed) return;
        if (const ne::ApiTable* api = ne::Api())
        {
            api->Subscribe("*", &OnEvent, nullptr);
            g_subscribed = true;
            NRN_INFO("listening to NorseEvents");
        }
    }

    // MapLoaded fires 3 s (real time) after the map's game state starts, once the game's own music has started. Real
    // time, not frames: a game window that is not in front runs at a few frames a second (120 frames took 28 s).
    ULONGLONG g_mapLoadedAt = 0;

    bool RunCommand(const std::wstring& c)
    {
        std::wstring cmd = Trim(c), arg;
        if (size_t sp = cmd.find(L' '); sp != std::wstring::npos)
        {
            arg = Trim(cmd.substr(sp + 1));
            cmd = cmd.substr(0, sp);
        }
        cmd = Lower(cmd);
        NRN_INFO("command: %s %s", Narrow(cmd).c_str(), Narrow(arg).c_str());
        if (cmd == L"snd.reload")
        {
            LoadRules();
            ApplySwaps("snd.reload");
        }
        else if (cmd == L"snd.apply") ApplySwaps("snd.apply");
        else if (cmd == L"snd.music") NRN_INFO("snd.music: now playing %s", NowPlaying().c_str());
        else if (cmd == L"snd.test" && !arg.empty())
        {
            bool any = false;
            for (const Rule& r : g_rules)
                if (_stricmp(r.name.c_str(), Narrow(arg).c_str()) == 0)
                {
                    Play(r, "snd.test");
                    any = true;
                }
            if (!any) NRN_WARN("snd.test: no music rule named '%s'", Narrow(arg).c_str());
        }
        else NRN_WARN("unknown command - snd.reload | snd.apply | snd.music | snd.test <rule>");
        return true;
    }
} // namespace

class NorseSound : public RC::CppUserModBase
{
  public:
    NorseSound()
    {
        ModName = L"NorseSound";
        ModVersion = L"" NSD_VERSION;
        ModDescription = L"Swap the game's sounds and choose its music by rule.";
        ModAuthors = L"ApocryphaRealm";
        for (auto& s : g_nopVtbl) s = reinterpret_cast<void*>(&NopFn);
        g_errOut.vtbl = g_nopVtbl;
        g_mod = ModDir();
        std::wstring dir;
        for (const auto& raw : ReadLines(g_mod / "NorseSound.ini"))
        {
            std::wstring l = Trim(raw);
            if (l.rfind(L"sLogDir=", 0) == 0) dir = Trim(l.substr(8));
        }
        Log::Get().Open((dir.empty() ? g_mod : fs::path(dir)) / "NorseSound.log", 2);
        Log::Get().Write(Level::Error, "NorseSound v%s", NSD_VERSION);
        LoadRules();
    }

    auto on_unreal_init() -> void override
    {
        Subscribe();
        try
        {
            RC::Unreal::Hook::RegisterInitGameStatePostCallback([](RC::Unreal::AGameModeBase*) {
                Subscribe();
                CompileConditions(); // NorseConditions may have loaded after this mod
                try
                {
                    ApplySwaps("map load");
                }
                catch (...)
                {
                    NRN_ERROR("map-load swap pass threw");
                }
                g_mapLoadedAt = GetTickCount64() + 3000;
            });
            RC::Unreal::Hook::RegisterEngineTickPreCallback([](RC::Unreal::UEngine*, float) {
                if (g_mapLoadedAt && GetTickCount64() >= g_mapLoadedAt)
                {
                    g_mapLoadedAt = 0;
                    NRN_INFO("MapLoaded");
                    Fire("MapLoaded", nullptr, nullptr, nullptr);
                }
            });
            RC::Unreal::Hook::RegisterProcessConsoleExecCallback(
                    [](UObject*, const wchar_t* cmd, RC::Unreal::FOutputDevice&, UObject*) -> bool {
                        if (!cmd || _wcsnicmp(cmd, L"snd.", 4) != 0) return false;
                        static ULONGLONG last = 0;
                        static std::wstring lastCmd;
                        ULONGLONG now = GetTickCount64();
                        if (lastCmd == cmd && now - last < 250) return true;
                        last = now;
                        lastCmd = cmd;
                        return RunCommand(cmd);
                    });
        }
        catch (const std::exception& e)
        {
            NRN_ERROR("callback registration failed: %s", e.what());
        }
    }
};

extern "C" __declspec(dllexport) RC::CppUserModBase* start_mod()
{
    return new NorseSound();
}

extern "C" __declspec(dllexport) void uninstall_mod(RC::CppUserModBase* mod)
{
    delete mod;
}
