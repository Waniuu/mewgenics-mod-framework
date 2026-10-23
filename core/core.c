// MewCore - native mod loader for Mewgenics 1.1.21239 (replacement for the 1.0-only MewgenicsCore).
// Loaded as a version.dll proxy. Loads Mods\*.dll (NativeAOT mods built with MewgenicsModSdk),
// passes them the MewgenicsApi table and raises the SDK game events from inline hooks (MinHook).
// Optional dev console: the in-game agent (TCP 127.0.0.1:47800) when Mods\config\core.cfg has agent=1.
#define AGENT_LIB
#include "../agent/agent.c"
#include "minhook/include/MinHook.h"

#define CORE_VERSION "1.2.0"
#define CORE_VERSION_W L"1.2.0"
#define GAME_PE_TIMESTAMP 0x6A9C51E5u

// ---- RVAs (keep in sync with MewgenicsModSdk.Game/GameAddressMap.cs) ----
enum {
    RVA_HOUSE_UPDATE = 0x1E5110, RVA_PASS_DAY = 0x1EF4D0, RVA_TURN_START = 0x8E3C60, RVA_TURN_CONTROL_UPDATE = 0x8E2A30,
    RVA_APPLY_DAMAGE = 0x110120, RVA_STAT_RECOMPUTE = 0x102520, RVA_BREAK_EQUIPMENT = 0x10A4C0, RVA_LOAD_LOCKED = 0x22B970,
    RVA_ATTACH_BRAIN = 0x3D5880, RVA_SAVESCUM = 0x8E1610, RVA_TRY_CLEAN_QUIT = 0x2972A0, RVA_REMOVE_CAT = 0x1FF2E0,
    RVA_PURCHASE_ITEM = 0x79A600, RVA_ITEM_TOOLTIP = 0x7B5480, RVA_CAN_ADVANCE = 0x8F2070, RVA_LEVEL_START = 0x35BB80,
    RVA_LEVEL_UPDATE = 0x3611E0, RVA_TRANSITION = 0x3C0210, RVA_START_CHAPTER = 0x3B0D20, RVA_LEVELUP_REROLL = 0x383FE0, RVA_GET_ANIM_CTRL = 0x122D70, RVA_CATDB_FIND = 0xD7220,
    RVA_ATTACH_PASSIVE = 0x494080, RVA_CHARACTER_DIE = 0x115070, RVA_WARNING_BOX = 0x9522C0, RVA_APPLY_STATUS = 0x483120, RVA_STRING_ASSIGN = 0x520D0, RVA_STRING_FREE = 0x52780,
    RVA_SDL_GL_SWAPWINDOW = 0xBA5550,
    RVA_WORLD_PTR = 0x13DAC30, RVA_INVENTORY_GLOBAL = 0x13C7560,
};
enum { DMG_ATTACKER = 0x98, OFF_CATDB = 1432, CHAR_HP = 1200, CHAR_MAX_HP = 1212, CHAR_RECOMPUTE_DEPTH = 3764, TC_SPEED = 376, ANIM_SPEED = 80, INV_COINS = 328 };

// ---- event ids (MewgenicsModSdk.Api.GameEventId) ----
enum { EV_ITEM_PURCHASED = 1, EV_TOOLTIP, EV_DAY_ADVANCED, EV_DIALOG_CAN_ADVANCE, EV_SHOP_REROLLED, EV_HOUSE_UPDATE, EV_TURN_START,
       EV_COMBAT_FRAME, EV_CAT_DAMAGED, EV_STAT_RECOMPUTE, EV_ITEM_BREAK, EV_CLASS_LOCKED, EV_BRAIN_FACTORY, EV_STEVEN_SPAWN,
       EV_STEVEN_QUIT, EV_CAT_REMOVED, EV_FIGHT_START, EV_FIGHT_END, EV_ADVENTURE_START, EV_ADVENTURE_RETURN, EV_HOUSE_BOSS_INTRO,
       EV_RUN_COIN_CHANGED, EV_ATTACH_PASSIVE, EV_CHARACTER_DIED, EV_STATUS_APPLY, EV_COUNT };

// ---- event args (layouts must match MewgenicsModSdk.Api.*EventArgs) ----
typedef struct { intptr_t A1, A2, A3, A4, Result; } ItemPurchasedArgs;
typedef struct { intptr_t A1, WstringPtr, EquipmentPtr; } TooltipArgs;
typedef struct { intptr_t HousePtr; } HousePtrArgs;
typedef struct { intptr_t Controller, SequencePiece; uint8_t Result, Handled; } DialogArgs;
typedef struct { intptr_t TurnControlPtr; } TurnStartArgs;
typedef struct { intptr_t TurnControlPtr, A2, A3; double SpeedMultiplier; uint8_t SpeedDirty; } CombatFrameArgs;
typedef struct { intptr_t A1, A2; uint8_t Cancel; } DamageArgs;
typedef struct { intptr_t CharPtr; int32_t PrevCurrentHp, PrevMaxHp; uint8_t IsTopLevel; } StatArgs;
typedef struct { intptr_t A1; uint8_t Cancel; } ItemBreakArgs;
typedef struct { intptr_t LockedContentPtr, Result; } ClassLockedArgs;
typedef struct { intptr_t BrainNamePtr, A2, A3; uint8_t UseAI; } BrainArgs;
typedef struct { intptr_t A1; int32_t A2; uint8_t Cancel; } StevenSpawnArgs;
typedef struct { intptr_t A1, A2, A3, GameBase; } StevenQuitArgs;
typedef struct { int64_t CatId; intptr_t CatPtr, EntityPtr; } CatRemovedArgs;
typedef struct { intptr_t CombatScenePtr; } FightStartArgs;
typedef struct { int32_t Result; } FightEndArgs;
typedef struct { uint8_t _unused; } EmptyArgs;
typedef struct { intptr_t ShopPtr; } ShopRerolledArgs;
typedef struct { uint8_t IsEarly; } AdventureReturnArgs;
typedef struct { int32_t OldCoins, NewCoins, Delta; } RunCoinArgs;
typedef struct { intptr_t NamePtr, CharPtr, GonPtr; uint64_t Flag; uint8_t Handled; } AttachPassiveArgs;
typedef struct { intptr_t CharPtr; } CharacterDiedArgs;
typedef struct { intptr_t NamePtr, TargetPtr, SourcePtr; int32_t Amount; uint8_t Cancel; } StatusApplyArgs;

// ---- API table (layout must match MewgenicsModSdk.Api.MewgenicsApi) ----
typedef struct ModDescriptor { const char *Id, *Name, *Description, *Category; void (*OnEnable)(void); void (*OnDisable)(void); uint32_t Version, _pad; void (*OnConfigReload)(void); } ModDescriptor;
typedef struct MewgenicsApi {
    uint32_t ApiVersion; intptr_t GameBase;
    void (*Log)(const char *);
    void *(*HookRva)(uint64_t, void *);
    void (*Unhook)(void *);
    void (*Subscribe)(int, void *);
    void (*Unsubscribe)(int, void *);
    void (*RegisterConsoleCommand)(const char *, void (*)(char **, int));
    void (*SendTcpEvent)(const char *);
    void (*PatchBytes)(uint64_t, const uint8_t *, int);
    void (*NopBytes)(uint64_t, int);
    void (*RegisterMod)(ModDescriptor *);
    void (*RegisterFrameDrain)(void (*)(void));
    intptr_t *CurrentTurnControlPtr;
    void (*SuppressWarning)(const char *);   // exact text of a game warning box to swallow (e.g. "NO STATUS NAMED MyStatus")
} MewgenicsApi;

// ---- state ----
#define MAX_SUBS 64
#define MAX_MODS 64
static CRITICAL_SECTION g_cs;
static void *g_subs[EV_COUNT][MAX_SUBS];
static void (*g_drains[MAX_MODS])(void); static int g_ndrains;
static struct { char name[64]; void (*fn)(char **, int); } g_cmds[128]; static int g_ncmds;
typedef struct { HMODULE h; char file[MAX_PATH]; const char *id, *name, *version, *description; ModDescriptor *desc; void (*enable)(void); void (*disable)(void); void (*reload)(void); void (*action)(const char *key); int enabled; } Mod;
static Mod g_mods[MAX_MODS]; static int g_nmods;
static MewgenicsApi g_api;
static intptr_t g_turnControl;
static FILE *g_corelog;
static char g_dir[MAX_PATH];
static struct { void *rvaTarget; void *tramp; } g_hooks[256]; static int g_nhooks;

static void clog(const char *fmt, ...) {
    char buf[2048]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    if (g_corelog) { SYSTEMTIME t; GetLocalTime(&t); fprintf(g_corelog, "[%02d:%02d:%02d] %s\n", t.wHour, t.wMinute, t.wSecond, buf); fflush(g_corelog); }
    OutputDebugStringA(buf); OutputDebugStringA("\n");
}

// ---- API implementation ----
static void api_log(const char *s) { clog("%s", s ? s : ""); }
static void *api_hook(uint64_t rva, void *fn) {
    void *target = (void *)(g_base + rva), *tramp = NULL;
    MH_STATUS st = MH_CreateHook(target, fn, &tramp);
    if (st == MH_OK) st = MH_EnableHook(target);
    if (st != MH_OK) { clog("[Hook] RVA 0x%llX failed: %s", (unsigned long long)rva, MH_StatusToString(st)); return NULL; }
    EnterCriticalSection(&g_cs); if (g_nhooks < 256) { g_hooks[g_nhooks].rvaTarget = target; g_hooks[g_nhooks].tramp = tramp; g_nhooks++; } LeaveCriticalSection(&g_cs);
    return tramp;
}
static void api_unhook(void *tramp) {
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_nhooks; i++) if (g_hooks[i].tramp == tramp) { MH_DisableHook(g_hooks[i].rvaTarget); MH_RemoveHook(g_hooks[i].rvaTarget); g_hooks[i] = g_hooks[--g_nhooks]; break; }
    LeaveCriticalSection(&g_cs);
}
static void api_subscribe(int ev, void *cb) {
    if (ev <= 0 || ev >= EV_COUNT || !cb) return;
    EnterCriticalSection(&g_cs);
    int dup = 0; for (int i = 0; i < MAX_SUBS; i++) if (g_subs[ev][i] == cb) dup = 1;
    if (!dup) for (int i = 0; i < MAX_SUBS; i++) if (!g_subs[ev][i]) { g_subs[ev][i] = cb; break; }
    LeaveCriticalSection(&g_cs);
}
static void api_unsubscribe(int ev, void *cb) {
    if (ev <= 0 || ev >= EV_COUNT) return;
    EnterCriticalSection(&g_cs); for (int i = 0; i < MAX_SUBS; i++) if (g_subs[ev][i] == cb) g_subs[ev][i] = NULL; LeaveCriticalSection(&g_cs);
}
static void api_register_cmd(const char *name, void (*fn)(char **, int)) {
    EnterCriticalSection(&g_cs); if (g_ncmds < 128 && name) { strncpy(g_cmds[g_ncmds].name, name, 63); g_cmds[g_ncmds].fn = fn; g_ncmds++; } LeaveCriticalSection(&g_cs);
}
static void api_send_event(const char *s) { clog("[event] %s", s ? s : ""); }
static void api_patch(uint64_t rva, const uint8_t *b, int n) { if (b && n > 0) safe_write(g_base + rva, b, (size_t)n); }
static void api_nop(uint64_t rva, int n) { uint8_t buf[256]; while (n > 0) { int k = n > 256 ? 256 : n; memset(buf, 0x90, k); safe_write(g_base + rva, buf, (size_t)k); rva += k; n -= k; } }
static void api_register_mod(ModDescriptor *d) {
    if (!d) return;
    EnterCriticalSection(&g_cs);
    for (int i = 0; i < g_nmods; i++) if (g_mods[i].id && d->Id && !strcmp(g_mods[i].id, d->Id)) { g_mods[i].desc = d; break; }
    LeaveCriticalSection(&g_cs);
}
static char g_silence[64][160]; static int g_nsilence;
static void api_suppress_warning(const char *text) {
    if (!text || strlen(text) >= 160) return;
    EnterCriticalSection(&g_cs);
    int dup = 0; for (int i = 0; i < g_nsilence; i++) if (!strcmp(g_silence[i], text)) dup = 1;
    if (!dup && g_nsilence < 64) strcpy(g_silence[g_nsilence++], text);
    LeaveCriticalSection(&g_cs);
}
static void api_register_drain(void (*fn)(void)) { EnterCriticalSection(&g_cs); if (g_ndrains < MAX_MODS && fn) g_drains[g_ndrains++] = fn; LeaveCriticalSection(&g_cs); }

// ---- event dispatch (callbacks run on the game thread inside the hook) ----
static void fire(int ev, void *args) {
    void *list[MAX_SUBS]; EnterCriticalSection(&g_cs); memcpy(list, g_subs[ev], sizeof list); LeaveCriticalSection(&g_cs);
    for (int i = 0; i < MAX_SUBS; i++) if (list[i]) { __try { ((void (*)(void *))list[i])(args); } __except (EXCEPTION_EXECUTE_HANDLER) { clog("[event %d] subscriber %p threw 0x%lX", ev, list[i], GetExceptionCode()); } }
}
static int has_subs(int ev) { for (int i = 0; i < MAX_SUBS; i++) if (g_subs[ev][i]) return 1; return 0; }

// ---- hooks ----
typedef uint64_t (*f1)(uint64_t);
typedef uint64_t (*f2)(uint64_t, uint64_t);
typedef uint64_t (*f3)(uint64_t, uint64_t, uint64_t);
typedef uint64_t (*f4)(uint64_t, uint64_t, uint64_t, uint64_t);
typedef uint64_t (*f3d)(uint64_t, uint64_t, uint64_t, double);
static f1 o_house_update, o_pass_day, o_turn_start, o_tc_update, o_remove_cat, o_load_locked, o_level_start, o_reroll;
static f2 o_damage, o_savescum, o_purchase, o_can_advance;
static f3 o_stat, o_break, o_brain, o_quit, o_tooltip, o_level_update;
static f2 o_start_chapter;
static volatile LONG g_advFired;   // one OnAdventureStart per departure (reset by the house update)
static uint64_t g_endLevel; static uint8_t g_endWon, g_endLost;
static f3d o_transition;
static void *(*o_swap)(void *);
static int g_lastCoins = -1;

static void menu_frame(void);
static void *h_swap(void *win) {
    // Every frame on the game thread: SDK GameThread queues, then dev-console jobs.
    void (*d[MAX_MODS])(void); int n; EnterCriticalSection(&g_cs); n = g_ndrains; memcpy(d, g_drains, sizeof d); LeaveCriticalSection(&g_cs);
    for (int i = 0; i < n; i++) { __try { d[i](); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
    menu_frame();
    agent_frame_tick();
    return o_swap(win);
}
static uint64_t h_house_update(uint64_t house) { g_advFired = 0; HousePtrArgs a = { (intptr_t)house }; fire(EV_HOUSE_UPDATE, &a); return o_house_update(house); }
static void fire_adventure_start(void) { if (InterlockedExchange(&g_advFired, 1) == 0) { EmptyArgs a = {0}; fire(EV_ADVENTURE_START, &a); } }
static uint64_t h_start_chapter(uint64_t md, uint64_t map) { fire_adventure_start(); return o_start_chapter(md, map); }
static uint64_t h_pass_day(uint64_t cap) {
    uint64_t r = o_pass_day(cap); HousePtrArgs a = { 0 }; safe_read(cap, &a.HousePtr, 8); fire(EV_DAY_ADVANCED, &a); return r;
}
static uint64_t h_turn_start(uint64_t tc) { g_turnControl = (intptr_t)tc; TurnStartArgs a = { (intptr_t)tc }; fire(EV_TURN_START, &a); return o_turn_start(tc); }
static uint64_t h_tc_update(uint64_t tc) {
    g_turnControl = (intptr_t)tc;
    CombatFrameArgs a = { (intptr_t)tc, 0, 0, 1.0, 0 }; fire(EV_COMBAT_FRAME, &a);
    uint64_t r = o_tc_update(tc);
    if (a.SpeedDirty && a.SpeedMultiplier > 0) {
        __try { *(double *)(tc + TC_SPEED) = a.SpeedMultiplier; uint64_t ac = ((f1)(g_base + RVA_GET_ANIM_CTRL))(tc); if (ac) *(double *)(ac + ANIM_SPEED) = a.SpeedMultiplier; } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    uint64_t inv = 0; int coins;
    if (safe_read(g_base + RVA_INVENTORY_GLOBAL, &inv, 8) && inv && safe_read(inv + INV_COINS, &coins, 4)) {
        if (g_lastCoins >= 0 && coins != g_lastCoins) { RunCoinArgs c = { g_lastCoins, coins, coins - g_lastCoins }; fire(EV_RUN_COIN_CHANGED, &c); }
        g_lastCoins = coins;
    }
    return r;
}
static uint64_t h_damage(uint64_t ch, uint64_t dmg) {
    DamageArgs a = { (intptr_t)ch, 0, 0 }; { intptr_t src = 0; if (dmg && safe_read(dmg, &src, 8) && src) safe_read(src + DMG_ATTACKER, &a.A2, 8); }   // DamageInstance+0 = source object, source+0x98 = attacking Character (1.1.21239); 0 for sourceless damage
    fire(EV_CAT_DAMAGED, &a); if (a.Cancel) return 0; return o_damage(ch, dmg);
}
static uint64_t h_stat(uint64_t ch, uint64_t a2, uint64_t a3) {
    if (!has_subs(EV_STAT_RECOMPUTE)) return o_stat(ch, a2, a3);
    StatArgs a = { (intptr_t)ch, 0, 0, 0 }; int depth = 1;
    safe_read(ch + CHAR_HP, &a.PrevCurrentHp, 4); safe_read(ch + CHAR_MAX_HP, &a.PrevMaxHp, 4); safe_read(ch + CHAR_RECOMPUTE_DEPTH, &depth, 4);
    a.IsTopLevel = depth == 0;
    uint64_t r = o_stat(ch, a2, a3); if (a.IsTopLevel) fire(EV_STAT_RECOMPUTE, &a); return r;
}
// attach_passive(name, character, config, flag): the game's factory of passives by name. A subscriber that sets Handled owns the passive (the game gets no object).
static f4 o_attach_passive;
static uint64_t h_attach_passive(uint64_t name, uint64_t ch, uint64_t gon, uint64_t flag) {
    if (!has_subs(EV_ATTACH_PASSIVE)) return o_attach_passive(name, ch, gon, flag);
    AttachPassiveArgs a = { (intptr_t)name, (intptr_t)ch, (intptr_t)gon, flag, 0 }; fire(EV_ATTACH_PASSIVE, &a);
    if (a.Handled) return 0; return o_attach_passive(name, ch, gon, flag);
}
// The game's modal "WARNING" box (log + SDL message box, it freezes the game thread). Texts registered with SuppressWarning are swallowed.
static f4 o_warn;
static uint64_t h_warn(uint64_t a1, uint64_t msg, uint64_t a3, uint64_t a4) {
    if (g_nsilence && msg) {
        uint64_t size = 0, cap = 0, p = msg; safe_read(msg + 16, &size, 8); safe_read(msg + 24, &cap, 8);
        if (size && size < 159) {
            if (cap > 15) safe_read(msg, &p, 8);
            char buf[160];
            if (safe_read(p, buf, (size_t)size)) {
                buf[size] = 0;
                for (int i = 0; i < g_nsilence; i++) {
                    if (!strcmp(g_silence[i], "*")) { clog("[Warning] %s", buf); return 0; }   // "*" = swallow every warning box (logged instead)
                    if (!strcmp(buf, g_silence[i])) return 0;
                }
            }
        }
    }
    return o_warn(a1, msg, a3, a4);
}
// Character::Die(character, ...): the event fires once, right after the game finished killing it.
static f4 o_die;
static uint64_t h_die(uint64_t ch, uint64_t a2, uint64_t a3, uint64_t a4) {
    uint8_t was = 1; safe_read(ch + 1218, &was, 1);
    uint64_t r = o_die(ch, a2, a3, a4);
    if (!was && has_subs(EV_CHARACTER_DIED)) { CharacterDiedArgs a = { (intptr_t)ch }; fire(EV_CHARACTER_DIED, &a); }
    return r;
}
// apply_status(name, target, amount, config, source, damageInstance, flag, flag): a subscriber can cancel it (the game does nothing, not even complain about unknown names).
typedef uint64_t (*f8)(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
static f8 o_apply_status;
static uint64_t h_apply_status(uint64_t name, uint64_t target, uint64_t amount, uint64_t cfg, uint64_t src, uint64_t di, uint64_t f1, uint64_t f2) {
    if (!has_subs(EV_STATUS_APPLY)) return o_apply_status(name, target, amount, cfg, src, di, f1, f2);
    StatusApplyArgs a = { (intptr_t)name, (intptr_t)target, (intptr_t)src, (int32_t)amount, 0 }; fire(EV_STATUS_APPLY, &a);
    if (a.Cancel) return 0; return o_apply_status(name, target, amount, cfg, src, di, f1, f2);
}
static uint64_t h_break(uint64_t ch, uint64_t eq, uint64_t force) { ItemBreakArgs a = { (intptr_t)eq, 0 }; fire(EV_ITEM_BREAK, &a); if (a.Cancel) return 0; return o_break(ch, eq, force); }
static uint64_t h_load_locked(uint64_t lc) { uint64_t r = o_load_locked(lc); ClassLockedArgs a = { (intptr_t)lc, (intptr_t)r }; fire(EV_CLASS_LOCKED, &a); return r; }
static uint64_t h_brain(uint64_t name, uint64_t ent, uint64_t gon) {
    BrainArgs a = { (intptr_t)name, (intptr_t)ent, (intptr_t)gon, 0 }; fire(EV_BRAIN_FACTORY, &a);
    if (!a.UseAI) return o_brain(name, ent, gon);
    // attach_brain takes the std::string by value and destroys it: hand it an SSO copy of "GenericBrain"
    // and release the caller's original ourselves.
    uint8_t s[32] = {0}; memcpy(s, "GenericBrain", 12); *(uint64_t *)(s + 16) = 12; *(uint64_t *)(s + 24) = 15;
    uint64_t r = o_brain((uint64_t)s, ent, gon);
    ((void (*)(uint64_t))(g_base + RVA_STRING_FREE))(name);
    return r;
}
static uint64_t h_savescum(uint64_t a1, uint64_t a2) { StevenSpawnArgs a = { (intptr_t)a1, (int32_t)a2, 0 }; fire(EV_STEVEN_SPAWN, &a); if (a.Cancel) return 0; return o_savescum(a1, a2); }
static uint64_t h_quit(uint64_t a1, uint64_t a2, uint64_t a3) { StevenQuitArgs a = { (intptr_t)a1, (intptr_t)a2, (intptr_t)a3, (intptr_t)g_base }; fire(EV_STEVEN_QUIT, &a); return o_quit(a1, a2, a3); }
static uint64_t h_remove_cat(uint64_t ent) {
    CatRemovedArgs a = { 0, 0, (intptr_t)ent }; uint64_t md = 0, db = 0;
    safe_read(ent + 128, &a.CatId, 8);
    if (safe_read(g_base + RVA_WORLD_PTR, &md, 8) && md && safe_read(md + OFF_CATDB, &db, 8) && db) __try { a.CatPtr = (intptr_t)((f2)(g_base + RVA_CATDB_FIND))(db, (uint64_t)a.CatId); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    fire(EV_CAT_REMOVED, &a); return o_remove_cat(ent);
}
static uint64_t h_purchase(uint64_t a1, uint64_t a2) { uint64_t r = o_purchase(a1, a2); ItemPurchasedArgs a = { (intptr_t)a1, (intptr_t)a2, 0, 0, (intptr_t)r }; fire(EV_ITEM_PURCHASED, &a); return r; }
static uint64_t h_tooltip(uint64_t a1, uint64_t ws, uint64_t eq) { uint64_t r = o_tooltip(a1, ws, eq); TooltipArgs a = { (intptr_t)a1, (intptr_t)ws, (intptr_t)eq }; fire(EV_TOOLTIP, &a); return r; }
static uint64_t h_can_advance(uint64_t c, uint64_t p) { DialogArgs a = { (intptr_t)c, (intptr_t)p, 0, 0 }; fire(EV_DIALOG_CAN_ADVANCE, &a); if (a.Handled) return a.Result; return o_can_advance(c, p); }
static uint64_t h_level_start(uint64_t lvl) { uint64_t r = o_level_start(lvl); g_lastCoins = -1; g_endLevel = lvl; g_endWon = g_endLost = 0; FightStartArgs a = { (intptr_t)lvl }; fire(EV_FIGHT_START, &a); return r; }
// Level::update runs every frame: report the rising edge of the victory (+424) / defeat (+426) flags once per level.
static uint64_t h_level_update(uint64_t lvl, uint64_t a2, uint64_t a3) {
    uint64_t r = o_level_update(lvl, a2, a3);
    uint8_t f[3] = {0}; safe_read(lvl + 424, f, 3);
    if (lvl != g_endLevel) { g_endLevel = lvl; g_endWon = f[0]; g_endLost = f[2]; }
    if (f[0] && !g_endWon) { g_endWon = 1; FightEndArgs a = { 0 }; fire(EV_FIGHT_END, &a); }
    if (f[2] && !g_endLost && !g_endWon) { g_endLost = 1; FightEndArgs a = { 1 }; fire(EV_FIGHT_END, &a); }
    return r;
}
static int gstr_eq(uint64_t s, const char *lit) {
    uint64_t size = 0, cap = 0, p = s; char buf[32] = {0}; size_t n = strlen(lit);
    if (!safe_read(s + 16, &size, 8) || !safe_read(s + 24, &cap, 8) || size != n || n >= sizeof buf) return 0;
    if (cap > 15 && !safe_read(s, &p, 8)) return 0;
    return safe_read(p, buf, n) && !memcmp(buf, lit, n);
}
static uint64_t h_transition(uint64_t md, uint64_t name, uint64_t cont, double t) {
    if (gstr_eq(name, "embark")) fire_adventure_start();
    else if (gstr_eq(name, "return")) { AdventureReturnArgs a = { 0 }; fire(EV_ADVENTURE_RETURN, &a); }
    else if (gstr_eq(name, "return_early")) { AdventureReturnArgs a = { 1 }; fire(EV_ADVENTURE_RETURN, &a); }
    else if (gstr_eq(name, "defeat")) { if (!g_endLost && !g_endWon) { g_endLost = 1; FightEndArgs a = { 1 }; fire(EV_FIGHT_END, &a); } }
    else if (gstr_eq(name, "house_boss_intro")) { EmptyArgs a = {0}; fire(EV_HOUSE_BOSS_INTRO, &a); }
    return o_transition(md, name, cont, t);
}

// LevelUpScreen::Reroll (the reroll button of the post-fight level-up choices). Raised only when a reroll was spent.
static uint64_t h_reroll(uint64_t screen) {
    int before = 0; safe_read(screen + 228, &before, 4);
    uint64_t r = o_reroll(screen);
    if (before > 0) { ShopRerolledArgs a = { (intptr_t)screen }; fire(EV_SHOP_REROLLED, &a); }
    return r;
}

#define HOOK(rva, h, o) do { o = api_hook(rva, (void *)h); clog("[Hook] %-22s 0x%06X %s", #h, rva, o ? "ok" : "FAILED"); } while (0)
static void install_core_hooks(void) {
    HOOK(RVA_HOUSE_UPDATE, h_house_update, o_house_update);
    HOOK(RVA_PASS_DAY, h_pass_day, o_pass_day);
    HOOK(RVA_TURN_START, h_turn_start, o_turn_start);
    HOOK(RVA_TURN_CONTROL_UPDATE, h_tc_update, o_tc_update);
    HOOK(RVA_APPLY_DAMAGE, h_damage, o_damage);
    HOOK(RVA_ATTACH_PASSIVE, h_attach_passive, o_attach_passive);
    HOOK(RVA_CHARACTER_DIE, h_die, o_die);
    HOOK(RVA_WARNING_BOX, h_warn, o_warn);
    HOOK(RVA_APPLY_STATUS, h_apply_status, o_apply_status);
    HOOK(RVA_STAT_RECOMPUTE, h_stat, o_stat);
    HOOK(RVA_BREAK_EQUIPMENT, h_break, o_break);
    HOOK(RVA_LOAD_LOCKED, h_load_locked, o_load_locked);
    HOOK(RVA_ATTACH_BRAIN, h_brain, o_brain);
    HOOK(RVA_SAVESCUM, h_savescum, o_savescum);
    HOOK(RVA_TRY_CLEAN_QUIT, h_quit, o_quit);
    HOOK(RVA_REMOVE_CAT, h_remove_cat, o_remove_cat);
    HOOK(RVA_PURCHASE_ITEM, h_purchase, o_purchase);
    HOOK(RVA_ITEM_TOOLTIP, h_tooltip, o_tooltip);
    HOOK(RVA_CAN_ADVANCE, h_can_advance, o_can_advance);
    HOOK(RVA_LEVEL_START, h_level_start, o_level_start);
    HOOK(RVA_LEVEL_UPDATE, h_level_update, o_level_update);
    HOOK(RVA_START_CHAPTER, h_start_chapter, o_start_chapter);
    HOOK(RVA_LEVELUP_REROLL, h_reroll, o_reroll);
    HOOK(RVA_TRANSITION, h_transition, o_transition);
    // per-frame tick: patch the SDL dynapi jump slot (no inline hook needed)
    uint64_t *slot = slot_of(g_base + RVA_SDL_GL_SWAPWINDOW);
    if (slot) { DWORD old; VirtualProtect(slot, 8, PAGE_READWRITE, &old); o_swap = (void *(*)(void *))*slot; *slot = (uint64_t)h_swap; VirtualProtect(slot, 8, old, &old); g_frame_driven = 1; }
    clog("[Hook] frame tick (SDL_GL_SwapWindow slot) %s", slot ? "ok" : "FAILED");
}

// ---- config (Mods\config\core.cfg: agent=1, disabled=id1,id2) ----
static char g_cfg_disabled[2048]; static int g_cfg_agent; static char g_cfg_menu_key[32] = "F10";
static void load_cfg(void) {
    char p[MAX_PATH]; snprintf(p, sizeof p, "%s\\Mods\\config\\core.cfg", g_dir);
    FILE *f = fopen(p, "r"); if (!f) return;
    char line[2048];
    while (fgets(line, sizeof line, f)) {
        char *e = strchr(line, '='); if (!e) continue; *e++ = 0; e[strcspn(e, "\r\n")] = 0;
        if (!_stricmp(line, "agent")) g_cfg_agent = atoi(e) || !_stricmp(e, "true");
        else if (!_stricmp(line, "menu_key")) strncpy(g_cfg_menu_key, e, sizeof g_cfg_menu_key - 1);
        else if (!_stricmp(line, "disabled")) strncpy(g_cfg_disabled, e, sizeof g_cfg_disabled - 1);
    }
    fclose(f);
}
static int is_disabled(const char *id) {
    if (!id) return 0; size_t n = strlen(id); const char *p = g_cfg_disabled;
    while ((p = strstr(p, id)) != NULL) { if ((p == g_cfg_disabled || p[-1] == ',' || p[-1] == ' ') && (p[n] == 0 || p[n] == ',' || p[n] == ' ')) return 1; p += n; }
    return 0;
}

// ---- mod loading ----
typedef struct { const char *Id, *Name, *Version, *Description; } ModInfo;
static void set_enabled(Mod *m, int on) {
    if (m->enabled == on) return;
    void (*fn)(void) = on ? (m->desc && m->desc->OnEnable ? m->desc->OnEnable : m->enable) : (m->desc && m->desc->OnDisable ? m->desc->OnDisable : m->disable);
    if (fn) __try { fn(); } __except (EXCEPTION_EXECUTE_HANDLER) { clog("[ModLoader] %s %s threw", m->id, on ? "enable" : "disable"); }
    m->enabled = on; clog("[ModLoader] %s %s", m->id ? m->id : m->file, on ? "enabled" : "disabled");
}
static void load_mods(void) {
    char pat[MAX_PATH], dir[MAX_PATH]; snprintf(dir, sizeof dir, "%s\\Mods", g_dir); CreateDirectoryA(dir, NULL);
    snprintf(pat, sizeof pat, "%s\\config", dir); CreateDirectoryA(pat, NULL);
    snprintf(pat, sizeof pat, "%s\\*.dll", dir);
    WIN32_FIND_DATAA fd; HANDLE fh = FindFirstFileA(pat, &fd); if (fh == INVALID_HANDLE_VALUE) { clog("[ModLoader] no mods in %s", dir); return; }
    do {
        if (g_nmods >= MAX_MODS) break;
        char path[MAX_PATH]; snprintf(path, sizeof path, "%s\\%s", dir, fd.cFileName);
        HMODULE h = LoadLibraryA(path); if (!h) { clog("[ModLoader] %s: LoadLibrary failed (%lu)", fd.cFileName, GetLastError()); continue; }
        void (*init)(MewgenicsApi *) = (void (*)(MewgenicsApi *))GetProcAddress(h, "MewMod_Init");
        if (!init) { clog("[ModLoader] %s: missing MewMod_Init export, skipped", fd.cFileName); FreeLibrary(h); continue; }
        Mod *m = &g_mods[g_nmods]; memset(m, 0, sizeof *m); m->h = h; strncpy(m->file, fd.cFileName, MAX_PATH - 1);
        ModInfo *(*getinfo)(void) = (ModInfo *(*)(void))GetProcAddress(h, "MewMod_GetInfo");
        ModInfo *info = NULL; if (getinfo) __try { info = getinfo(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        m->id = info && info->Id ? info->Id : m->file;
        m->name = info && info->Name && *info->Name ? info->Name : m->id; m->version = info && info->Version ? info->Version : ""; m->description = info && info->Description ? info->Description : "";
        m->enable = (void (*)(void))GetProcAddress(h, "MewMod_Enable"); m->disable = (void (*)(void))GetProcAddress(h, "MewMod_Disable"); m->reload = (void (*)(void))GetProcAddress(h, "MewMod_ConfigReload"); m->action = (void (*)(const char *))GetProcAddress(h, "MewMod_Action");
        g_nmods++;
        clog("[ModLoader] %s: %s %s", fd.cFileName, m->id, info && info->Version ? info->Version : "");
        __try { init(&g_api); } __except (EXCEPTION_EXECUTE_HANDLER) { clog("[ModLoader] %s: MewMod_Init threw 0x%lX", m->id, GetExceptionCode()); continue; }
        // Mods with AutoEnable enable themselves inside Init; InternalEnable/InternalDisable are idempotent.
        if (is_disabled(m->id)) { m->enabled = 1; set_enabled(m, 0); }
        else { m->enabled = 0; set_enabled(m, 1); }
    } while (FindNextFileA(fh, &fd));
    FindClose(fh);
}

#include "menu.c"

// ---- dev console extensions: mods / enable / disable / cmd / menu ----
static int core_console(SOCKET s, char *line) {
    char out[4096];
    if (menu_console(s, line)) return 1;
    if (!strcmp(line, "mods")) { size_t n = (size_t)snprintf(out, sizeof out, "ok"); for (int i = 0; i < g_nmods; i++) n += (size_t)snprintf(out + n, sizeof out - n, " %s=%d", g_mods[i].id, g_mods[i].enabled); sendstr(s, out); return 1; }
    if (!strncmp(line, "enable ", 7) || !strncmp(line, "disable ", 8)) {
        int on = line[0] == 'e'; const char *id = line + (on ? 7 : 8);
        for (int i = 0; i < g_nmods; i++) if (!strcmp(g_mods[i].id, id)) { menu_queue(i, on); sendstr(s, "ok"); return 1; }   // applied next frame and saved to core.cfg
        sendstr(s, "err no such mod"); return 1;
    }
    if (!strncmp(line, "cmd ", 4)) {
        char *tok[16]; int nt = 0; for (char *t = strtok(line + 4, " "); t && nt < 16; t = strtok(NULL, " ")) tok[nt++] = t;
        if (!nt) { sendstr(s, "err usage: cmd <name> [args]"); return 1; }
        for (int i = 0; i < g_ncmds; i++) if (!strcmp(g_cmds[i].name, tok[0])) { __try { g_cmds[i].fn(tok + 1, nt - 1); } __except (EXCEPTION_EXECUTE_HANDLER) {} sendstr(s, "ok"); return 1; }
        sendstr(s, "err no such command"); return 1;
    }
    if (!strcmp(line, "core")) { snprintf(out, sizeof out, "ok MewCore %s hooks=%d mods=%d tc=%llx", CORE_VERSION, g_nhooks, g_nmods, (unsigned long long)g_turnControl); sendstr(s, out); return 1; }
    return 0;
}

// Crash reporter: logs faults raised inside the game code (address, registers, game return addresses on the stack)
// so a crash can be mapped to a hook or a game function. Limited to a few reports per run.
static volatile LONG g_crashReports;
static LONG CALLBACK crash_veh(EXCEPTION_POINTERS *ep) {
    DWORD code = ep->ExceptionRecord->ExceptionCode;
    if (code != EXCEPTION_ACCESS_VIOLATION && code != EXCEPTION_INT_DIVIDE_BY_ZERO && code != EXCEPTION_ILLEGAL_INSTRUCTION) return EXCEPTION_CONTINUE_SEARCH;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + dos->e_lfanew);
    uint64_t size = nt->OptionalHeader.SizeOfImage, rip = (uint64_t)ep->ExceptionRecord->ExceptionAddress;
    if (rip < g_base || rip >= g_base + size) return EXCEPTION_CONTINUE_SEARCH;   // managed null checks etc. are not ours to report
    if (InterlockedIncrement(&g_crashReports) > 6) return EXCEPTION_CONTINUE_SEARCH;
    CONTEXT *cx = ep->ContextRecord; char bt[512]; size_t n = 0; bt[0] = 0;
    for (int i = 0, k = 0; i < 512 && k < 16; i++) { uint64_t v = 0; if (!safe_read(cx->Rsp + i * 8, &v, 8)) break; if (v >= g_base && v < g_base + size) { n += (size_t)snprintf(bt + n, sizeof bt - n, " %llX", (unsigned long long)(v - g_base)); k++; } }
    clog("[Crash] code %08lX at r:%llX fault=%llX rcx=%llX rdx=%llX r8=%llX thread=%lu stack:%s", code, (unsigned long long)(rip - g_base),
         (unsigned long long)(ep->ExceptionRecord->NumberParameters > 1 ? ep->ExceptionRecord->ExceptionInformation[1] : 0),
         (unsigned long long)cx->Rcx, (unsigned long long)cx->Rdx, (unsigned long long)cx->R8, GetCurrentThreadId(), bt);
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI bootstrap(LPVOID p) {
    (void)p;
    IMAGE_DOS_HEADER *dos = (IMAGE_DOS_HEADER *)g_base; IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(g_base + dos->e_lfanew);
    uint32_t ts = nt->FileHeader.TimeDateStamp;
    clog("[Bootstrap] MewCore %s, game base %llx, PE timestamp %08X%s", CORE_VERSION, (unsigned long long)g_base, ts, ts == GAME_PE_TIMESTAMP ? " (1.1.21239)" : " (UNSUPPORTED BUILD: hooks disabled)");
    load_cfg();
    if (g_cfg_agent || getenv("MEW_AGENT")) { g_ext_handler = core_console; agent_init(g_self, "agent.log"); }
    if (ts != GAME_PE_TIMESTAMP) return 0;
    AddVectoredExceptionHandler(1, crash_veh);
    MH_STATUS st = MH_Initialize(); if (st != MH_OK && st != MH_ERROR_ALREADY_INITIALIZED) { clog("[Bootstrap] MinHook init failed: %s", MH_StatusToString(st)); return 0; }
    install_core_hooks();
    g_api.ApiVersion = 2; g_api.GameBase = (intptr_t)g_base; g_api.Log = api_log; g_api.HookRva = api_hook; g_api.Unhook = api_unhook;
    g_api.Subscribe = api_subscribe; g_api.Unsubscribe = api_unsubscribe; g_api.RegisterConsoleCommand = api_register_cmd; g_api.SendTcpEvent = api_send_event;
    g_api.PatchBytes = api_patch; g_api.NopBytes = api_nop; g_api.RegisterMod = api_register_mod; g_api.RegisterFrameDrain = api_register_drain;
    g_api.CurrentTurnControlPtr = &g_turnControl; g_api.SuppressWarning = api_suppress_warning;
    load_mods();
    clog("[Bootstrap] ready: %d hooks, %d mods", g_nhooks, g_nmods);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r) {
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        char exe[MAX_PATH]; GetModuleFileNameA(NULL, exe, MAX_PATH);
        if (!strstr(exe, "Mewgenics")) return TRUE;
        g_self = h; g_base = (uint64_t)GetModuleHandleA(NULL);
        GetModuleFileNameA(h, g_dir, MAX_PATH); char *sl = strrchr(g_dir, '\\'); if (sl) *sl = 0;
        char lp[MAX_PATH]; snprintf(lp, sizeof lp, "%s\\mods_debug.log", g_dir); g_corelog = fopen(lp, "w");
        InitializeCriticalSection(&g_cs);
        CreateThread(NULL, 0, bootstrap, NULL, 0, NULL);
    }
    return TRUE;
}
