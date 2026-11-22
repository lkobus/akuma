#define WIN32_LEAN_AND_MEAN
#define CINTERFACE
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <wchar.h>
#include "hud_skin.h"
#include "quest_starts.h"
#ifdef AKUMA_HUD_TRACE
#include <stdio.h>
#endif

enum { GAME_WIDTH = 640, HEIGHT = 480, WIDE_WIDTH = 854, CAMPAIGN_SCENE = 13 };
#define GAME_WNDPROC ((WNDPROC)(uintptr_t)0x421660)
#define FRAME_COUNT (*(volatile const DWORD *)(uintptr_t)0x4D3000)
#define CAMERA_COLUMNS (*(volatile int *)(uintptr_t)0x4D3004)
#define CAMERA_ROWS (*(volatile int *)(uintptr_t)0x4D3008)
#define CAMERA_REVEAL_X (*(int *)(uintptr_t)0x4D300C)
#define CAMERA_REVEAL_Y (*(int *)(uintptr_t)0x4D3010)
#define GAME_SCENE (*(volatile const DWORD *)(uintptr_t)0x499A6C)
#define GAME_VIDEO ((GameVideo *)(uintptr_t)0x49B9E0)
#define GAME_MAP (*(const unsigned char * const volatile *)(uintptr_t)0x4B9EAC)
#define GAME_VIEW (*(unsigned char * const volatile *)(uintptr_t)0x49B8F8)
// Opening the chat sets 0x49B8E0 (paused) and 0x49B8DC (chat open) together, but
// the world keeps running. Only a pause without the chat is a real pause menu.
#define GAME_CHAT_OPEN (*(volatile const DWORD *)(uintptr_t)0x49B8DC)
#define GAME_PAUSED ((*(volatile const DWORD *)(uintptr_t)0x49B8E0) && !GAME_CHAT_OPEN)
#define CAMPAIGN_TICKS (*(const DWORD *)(uintptr_t)0x49B8E8)
#define NET_MULTI (*(const volatile DWORD *)(uintptr_t)0x49B8CC)
#define NET_HOST (*(const volatile DWORD *)(uintptr_t)0x4998DC)
#define NET_LOCAL_ID (*(const volatile DWORD *)(uintptr_t)0x4998D8)
static void net_log_party(void);
static void install_dialog_hook(void);
static void install_crash_log(void);
static void note_estimated_hit(const void *actor);
static BOOL net_log_enabled(void);
static void net_log_line(char direction, DWORD from, DWORD to, DWORD flags, const unsigned char *data, DWORD size);
#define WORLD_CLIP_LEFT (*(LONG *)(uintptr_t)0x48F470)
#define WORLD_CLIP_TOP (*(LONG *)(uintptr_t)0x48F474)
#define WORLD_CLIP_RIGHT (*(LONG *)(uintptr_t)0x48F478)
#define WORLD_CLIP_BOTTOM (*(LONG *)(uintptr_t)0x48F47C)
#define WORLD_PIXELS (*(void **)(uintptr_t)0x49B908)
#define GAME_MOUSE (*(DWORD *)(uintptr_t)0x49B8F4)
#define GAME_TOOLBAR (*(void **)(uintptr_t)0x49B900)
#define GAME_DIALOGUE (*(const unsigned char * const *)(uintptr_t)0x49B904)
#define NEUTRAL_MOUSE MAKELPARAM(320, 223)

typedef struct {
    int reserved;
    HWND window;
    IDirectDraw2 *draw;
    IDirectDrawSurface3 *primary;
    IDirectDrawSurface3 *back;
    unsigned char fields[0x28];
    int flip;
} GameVideo;

C_ASSERT(FIELD_OFFSET(GameVideo, flip) == 0x3C);

typedef struct {
    short source_x, source_y, width, height;
    short x, y;
} Panel;

typedef struct {
    int x, y, width, height;
} MapRect;

typedef struct {
    MapRect source, target;
} MapView;

static const Panel scene = {0, 0, 480, 446, 0, 0};
static const Panel classic = {0, 0, GAME_WIDTH, HEIGHT, (WIDE_WIDTH - GAME_WIDTH)/2, 0};
static const Panel hud[] = {
    { 486, 6, 138, 24,   700, 18 },
    { 499, 205, 113, 81, 714, 109 },
    { 499, 290, 113, 81, 714, 213 },
    { 499, 374, 113, 81, 714, 317 },
    {  97, 445, 36, 35, 670, 79  },
    { 161, 445, 36, 35, 670, 154 },
    { 225, 445, 36, 35, 670, 229 },
    { 289, 445, 36, 35, 670, 304 },
    { 353, 445, 36, 35, 670, 379 },
};
enum {
    NAV_START = 4, NATIVE_BELT_Y = 446, BELT_Y = 464, OVERLAY_BOTTOM = 448, MENU_BOTTOM = 464,
    SIDE_X = 688, SCENE_WIDTH = 480, MAP_GRIP_HEIGHT = 14,
    TILE = 32, WORLD_TOP = 16, WORLD_RIGHT = 464, NATIVE_WIDE_RIGHT = 624,
    NATIVE_WIDE_COLUMNS = 19, NATIVE_ROWS = 14, VIEW_COLUMNS = 21,
    WIDE_RIGHT = WORLD_TOP + VIEW_COLUMNS * TILE, VIEW_HEIGHT = BELT_Y - WORLD_TOP,
    MAX_COLUMNS = 38, MAX_ROWS = 26, FIXED_HEADER_BOTTOM = 64,
    QUICK_FIRST_X = 83, QUICK_SLOT_WIDTH = 38, QUICK_SLOTS = 10, QUICK_BOTTOM = 54,
};
static const Panel campaign_menu = {WORLD_TOP, WORLD_TOP, WORLD_RIGHT - WORLD_TOP, MENU_BOTTOM - WORLD_TOP,
    WORLD_TOP + (SIDE_X - WORLD_RIGHT) / 2, WORLD_TOP};
static const Panel pause_menu = {0, 0, GAME_WIDTH, HEIGHT, (SIDE_X - GAME_WIDTH) / 2, 0};
enum { WORLD_MAP_WIDTH = 800, WORLD_MAP_HEIGHT = 600 };
static const MapRect world_map_panel = {(SIDE_X - 600) / 2, (HEIGHT - 450) / 2, 600, 450};
#define COUNT(array) (sizeof(array)/sizeof((array)[0]))
typedef struct { int columns, height; } CameraView;
static const CameraView camera_views[] = {
    {VIEW_COLUMNS, VIEW_HEIGHT}, {27, 576}, {30, 640}, {34, 725}, {MAX_COLUMNS, 811},
};
static unsigned zoom_level;
static BOOL smooth_camera, smooth_animating, smooth_drawing;
static int frame_cam_x, frame_cam_y;
static BOOL recenter_zoom, wasd_camera, zoom_down, exclusive_party, pixel_zoom;
enum { SELECT_ALL = 3, FOCUS_FIRST = 4, LOOT_KEY = 7, BINDING_COUNT = 8 };
static unsigned bindings[BINDING_COUNT] = {VK_F5, VK_F6, VK_F7, VK_F4,
    VK_F5 | 512, VK_F6 | 512, VK_F7 | 512, VK_MENU};
static unsigned binding_down, party_selection, party_present;

// Akuma's own keyboard actions, in the order of its key tables (0x421d8c / 0x421e30): menu, the ten
// quick items, character, items, quests, map, weapons tab, items tab and Space. The launcher
// passes the key chosen for each one in AKUMA_NATIVE_KEYS; we translate it back to the native key
// before the game sees the message and block a native key whose action was moved away.
enum { NATIVE_ACTIONS = 18 };
static const unsigned char native_default[NATIVE_ACTIONS] = {VK_ESCAPE, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
    'C', 'I', 'M', 'W', 'A', 'S', VK_SPACE};
static unsigned char native_user[NATIVE_ACTIONS];
static unsigned char native_translate[256], native_blocked[256];
static int hint_action = -1;
// Counters for the network log: how often the engine draws world tiles / presents a frame.
static volatile unsigned long tile_calls, present_calls;

static void configure_native_keys(void)
{
    char configured[160] = "";
    for (unsigned i = 0; i < NATIVE_ACTIONS; ++i) native_user[i] = native_default[i];
    DWORD length = GetEnvironmentVariableA("AKUMA_NATIVE_KEYS", configured, sizeof(configured));
    if (length && length < sizeof(configured)) {
        const char *start = configured;
        for (unsigned i = 0; i < NATIVE_ACTIONS; ++i) {
            char *end;
            unsigned long value = strtoul(start, &end, 10);
            if (end == start || (*end != ',' && *end != '\0')) break;
            native_user[i] = value > 255 ? 0 : (unsigned char)value;
            if (*end == '\0') break;
            start = end + 1;
        }
    }
    memset(native_translate, 0, sizeof(native_translate));
    memset(native_blocked, 0, sizeof(native_blocked));
    for (unsigned i = 0; i < NATIVE_ACTIONS; ++i) {
        if (native_user[i] == native_default[i]) continue;
        native_blocked[native_default[i]] = 1;
        if (native_user[i]) native_translate[native_user[i]] = native_default[i];
    }
}

// Name of a key for tooltips ("Q", "Space", "F3"…).
static int native_key_name(unsigned vk, WCHAR *out, int capacity)
{
    if ((vk >= '0' && vk <= '9') || (vk >= 'A' && vk <= 'Z')) { out[0] = (WCHAR)vk; out[1] = 0; return 1; }
    if (vk == VK_ESCAPE) return lstrlenW(lstrcpynW(out, L"Esc", capacity));
    if (vk == VK_SPACE) return lstrlenW(lstrcpynW(out, L"Space", capacity));
    if (vk >= VK_F1 && vk <= VK_F12) return wsprintfW(out, L"F%u", vk - VK_F1 + 1);
    return GetKeyNameTextW((LONG)(MapVirtualKeyW(vk, 0) << 16), out, capacity);
}
static const unsigned char *party_map;
static int party_map_id;
static unsigned item_code_page = 1252;
enum { PORTUGUESE, ENGLISH, KOREAN, CHINESE };
static unsigned hud_language;
static const unsigned char *previous_world_map;
static const unsigned char *dialogue_camera_map;
static int dialogue_camera_x, dialogue_camera_y;
static BOOL dialogue_camera_saved;
static CameraView frame_view, displayed_view;
static MapRect map_frame = {291, 24, 155, 163};
static MapView presented_map;
static BOOL map_detail, map_right_down, map_dragging, map_hidden, map_moved;
static BOOL map_minimize_down;
static int map_press_x, map_press_y, map_start_x, map_start_y;
static int pointer_x = -1, pointer_y = -1;

static BOOL wide_mode, show_hud = TRUE, presented_hud, tracking_mouse;
static BOOL wide_frame, presented_wide_world;
static BOOL overflow_ready, drawing_overflow, world_render_failed;
static int presented_world_right = SCENE_WIDTH;
static unsigned wide_drag;
static uint16_t terrain[MAX_ROWS * TILE][MAX_COLUMNS * TILE];
static uint16_t native_panel[14 * TILE][NATIVE_WIDE_RIGHT - WORLD_RIGHT];
static uint16_t header_world[FIXED_HEADER_BOTTOM - WORLD_TOP][WORLD_RIGHT - WORLD_TOP];
static uint16_t header_canvas[FIXED_HEADER_BOTTOM][WIDE_RIGHT];
static uint16_t header_background[FIXED_HEADER_BOTTOM][WIDE_RIGHT];
static BOOL header_staged, header_ready;
static uint16_t quest_world[VIEW_HEIGHT][WORLD_RIGHT - WORLD_TOP];
static uint16_t quest_canvas[HEIGHT][WIDE_RIGHT];
static Panel quest_panel;
static BOOL quest_ready, quest_empty, presented_quest, dialogue_ready, presented_dialogue;
static Panel dialogue_panel;
static BOOL presented_menu, sidebar_ready, open_journal_pending, sidebar_click;
static BOOL world_map_ready, pause_ready, presented_world_map, presented_pause;
static BOOL drawing_full_world_map;
static POINT world_map_part;
static uint16_t world_map_pixels[WORLD_MAP_HEIGHT][WORLD_MAP_WIDTH];
static uint16_t sidebar_content[HEIGHT][GAME_WIDTH - SCENE_WIDTH];
static uint16_t sidebar_border[BELT_Y][16];
static BOOL sidebar_border_ready;
static uint16_t world_snapshot[BELT_Y][SIDE_X];
static int quest_marker_y;
enum { NATIVE_MAP_X = 488, NATIVE_MAP_Y = 35, NATIVE_MAP_SIZE = 128 };
static uint32_t map_markers[NATIVE_MAP_SIZE][NATIVE_MAP_SIZE];
enum { SPRITE_SIZE = 32, ORB_COUNT = 5, SPRITE_TRANSPARENT = 0xFFFF };
typedef uint16_t HudSprite[SPRITE_SIZE][SPRITE_SIZE];
static uint16_t sprite_canvas[HEIGHT][GAME_WIDTH];
static HudSprite orb_sprites[ORB_COUNT], cursor_sprite;
static BOOL orb_ready[ORB_COUNT], cursor_ready;
static RECT selection;
static BOOL selection_visible;
static uint16_t selection_color;
static IDirectDrawSurface3 *output_surface;
static IDirectDrawSurface3 *composition_surface;
static IDirectDrawSurface3 *overflow_surface;
static unsigned drag_buttons;
static LPARAM last_mouse = NEUTRAL_MOUSE;
static DWORD last_frame, last_update;
static DWORD last_ui_state;
static DWORD last_scene;
static DWORD scene_since;
static int has_frame;

typedef struct { const unsigned char *item; unsigned count; } LootTile;
static LootTile loot_tiles[128][128];
static const unsigned char *loot_map;
typedef struct { RECT rect; POINT tile; } LootLabel;
static LootLabel loot_labels[96];
static unsigned loot_label_count;
static BOOL loot_click;
typedef struct {
    char text[160];
    int x, y, count;
    COLORREF color;
    HFONT font;
    BOOL piece;
} NativeHint;
static NativeHint world_hint[32], notice_hint;
static unsigned world_hint_count;
static BOOL quick_item_hint;
static int npc_name_index;
static POINT npc_anchor;

static unsigned held_modifiers(unsigned key)
{
    unsigned mods = 0;
    if (key != VK_SHIFT && (GetKeyState(VK_SHIFT) & 0x8000)) mods |= 256;
    if (key != VK_CONTROL && (GetKeyState(VK_CONTROL) & 0x8000)) mods |= 512;
    if (key != VK_MENU && (GetKeyState(VK_MENU) & 0x8000)) mods |= 1024;
    return mods;
}

static BOOL loot_held(void)
{
    unsigned key = bindings[LOOT_KEY] & 255;
    return key && GetForegroundWindow() == GAME_VIDEO->window
        && (GetAsyncKeyState(key) & 0x8000) && (key | held_modifiers(key)) == bindings[LOOT_KEY];
}

typedef struct {
    const unsigned char *map;
    int x, y, amount;
    DWORD start;
    unsigned kind;
} DamageNumber;
static DamageNumber damage_numbers[32];
static unsigned next_damage;

static int clamp(int value, int min, int max)
{
    if (value < min) return min;
    return value > max ? max : value;
}

static BOOL in_rect(const MapRect *rect, int x, int y)
{
    return x >= rect->x && x < rect->x + rect->width
        && y >= rect->y && y < rect->y + rect->height;
}

static BOOL dialogue_active(void)
{
    const unsigned char *manager = GAME_DIALOGUE;
    if (!manager || *(const int *)(manager + 0x28) == -1) return FALSE;
    const uintptr_t *dialogue = *(const uintptr_t * const *)(manager + 0x2C);
    return dialogue && *dialogue == 0x48A988;
}

static BOOL world_draw_active(void)
{
    const unsigned char *view = GAME_VIEW;
    return view && view[0x8C] == 1 && (!GAME_PAUSED || *(const DWORD *)(view + 0xE4) || dialogue_active());
}

static BOOL world_view_active(void)
{
    return world_draw_active() && !GAME_PAUSED && !*(const DWORD *)(GAME_VIEW + 0xE4);
}

// English strings the original engine draws itself (gameplay notices) or that live in its
// multiplayer dialog resources. They are not in lang.gos, so they are translated here.
// Columns: Portuguese, Korean, Traditional Chinese. English keeps the original text.
typedef struct { const char *en; const WCHAR *text[3]; } Phrase;

static const Phrase game_messages[] = {
    {"No place to take this item", {L"Sem lugar para pegar este item", L"이 아이템을 가질 곳이 없습니다", L"沒有地方可以拿取此道具"}},
    {"Overflow the limit of weight to take", {L"Peso máximo excedido", L"소지 가능 무게를 초과했습니다", L"超過可負重量上限"}},
    {"Not in the same region", {L"Não está na mesma região", L"같은 지역에 있지 않습니다", L"不在同一個區域"}},
    {"The Teleport Gate of the other side is not operated",
        {L"O Portal de Teleporte do outro lado não está ativado", L"반대편 이동 관문이 작동하지 않습니다", L"另一端的傳送門尚未啟動"}},
    {"No room to take this item", {L"Sem espaço para pegar este item", L"이 아이템을 넣을 공간이 없습니다", L"沒有空間放置此道具"}},
    {"No Weapons", {L"Sem armas", L"무기 없음", L"沒有武器"}},
};

static const Phrase dialog_texts[] = {
    {"SELECT CHARACTER", {L"ESCOLHER PERSONAGEM", L"주인공 고르기", L"選擇角色"}},
    {"Enter character's name", {L"Digite o nome do personagem", L"주인공 이름을 정하세요", L"請輸入角色名稱"}},
    {"OK", {L"OK", L"확인", L"確定"}},
    {"CANCEL", {L"Cancelar", L"취소", L"取消"}},
    {"Cancel", {L"Cancelar", L"취소", L"取消"}},
    {"MULTIPLAY SETTING", {L"CONFIGURAÇÃO MULTIJOGADOR", L"멀티 플레이 설정", L"多人遊戲設定"}},
    {"Choose connection type", {L"Escolha o tipo de conexão", L"연결할 방법을 선택하세요", L"請選擇連線方式"}},
    {"Host IP Address", {L"Endereço IP do host", L"IP 주소", L"主機 IP 位址"}},
    {"Select Modem", {L"Selecionar modem", L"모뎀 선택", L"選擇數據機"}},
    {"Telephone number to call", {L"Número de telefone para ligar", L"상대 전화 번호", L"要撥打的電話號碼"}},
    {"Fail to initialize...", {L"Falha ao inicializar...", L"초기화 실패...", L"初始化失敗..."}},
    {"Check your settings", {L"Verifique suas configurações", L"설정 사항을 확인해 보세요", L"請檢查您的設定"}},
    {"Fail to initialize...\nCheck your settings", {L"Falha ao inicializar...\nVerifique suas configurações", L"초기화 실패...\n설정 사항을 확인해 보세요", L"初始化失敗...\n請檢查您的設定"}},
    {"Fail to initialize...\r\nCheck your settings", {L"Falha ao inicializar...\r\nVerifique suas configurações", L"초기화 실패...\r\n설정 사항을 확인해 보세요", L"初始化失敗...\r\n請檢查您的設定"}},
    {"Input New Host Name", {L"Nome da nova sala", L"만들 방의 이름", L"新主機名稱"}},
    {"Max. Number of Players", {L"Máximo de jogadores", L"참여 가능한 최대 사람수", L"最大玩家人數"}},
    {"World", {L"Mundo", L"세계", L"地上世界"}},
    {"Underworld", {L"Submundo", L"지하세계", L"地下世界"}},
    {"- Above Level 20", {L"- Nível 20 ou mais", L"- 레벨 20이상", L"- 等級 20 以上"}},
    {"Select Host", {L"Entrar", L"들어 가기", L"加入主機"}},
    {"Find Host", {L"Procurar salas", L"방 보기", L"搜尋主機"}},
    {"Hosting", {L"Criar sala", L"방 만들기", L"建立主機"}},
};

// Returns the translation of an exact English string (ignoring trailing blanks), or NULL.
static const WCHAR *phrase_for(const Phrase *table, unsigned count, const char *text, int length)
{
    if (hud_language == ENGLISH || !text) return NULL;
    while (length > 0 && (text[length - 1] == ' ' || text[length - 1] == '\r' || text[length - 1] == '\n')) --length;
    for (unsigned i = 0; i < count; ++i)
        if ((int)strlen(table[i].en) == length && !memcmp(table[i].en, text, (size_t)length))
            return table[i].text[hud_language == PORTUGUESE ? 0 : hud_language == KOREAN ? 1 : 2];
    return NULL;
}

static void capture_hint(NativeHint *hint, HDC dc, int x, int y, const char *text, int count)
{
    hint->count = min(count, (int)sizeof(hint->text) - 1);
    memcpy(hint->text, text, (size_t)hint->count);
    hint->text[hint->count] = '\0';
    hint->x = x;
    hint->y = y;
    hint->color = GetTextColor(dc);
    hint->font = (HFONT)GetCurrentObject(dc, OBJ_FONT);
    hint->piece = FALSE;
}

__declspec(dllexport) BOOL WINAPI HudHoverText(HDC dc, int x, int y, const char *text, int count)
{
    uintptr_t caller = (uintptr_t)__builtin_return_address(0);
    if (caller == 0x46AC32 && presented_hud && GAME_SCENE == CAMPAIGN_SCENE) return TRUE;
    if (wide_mode && show_hud && GAME_SCENE == CAMPAIGN_SCENE && world_view_active()) {
        if (caller == 0x469626) {
            if (text && count > 0) capture_hint(&notice_hint, dc, x, y, text, count);
            return TRUE;
        }
        BOOL quick_text = quick_item_hint
            && ((caller > 0x4689D1 && caller < 0x4693BC) || caller == 0x4696FD);
        BOOL attribute_text = caller == 0x4696FD && world_hint_count;
        if (quick_text || attribute_text || (caller >= 0x467738 && caller < 0x468907)) {
            if (text && count > 0 && world_hint_count < COUNT(world_hint)) {
                if (caller == 0x468861 && !quick_item_hint) {
                    const unsigned char *actor = *(const unsigned char * const *)(GAME_VIEW + 0xD0);
                    if (actor && *(const uintptr_t *)actor == 0x48A5FC) {
                        npc_name_index = (int)world_hint_count;
                        npc_anchor.x = *(const int *)(actor + 0x0C);
                        npc_anchor.y = *(const int *)(actor + 0x10);
                    }
                }
                capture_hint(&world_hint[world_hint_count], dc, x, y, text, count);
                world_hint[world_hint_count++].piece = attribute_text;
            }
            return TRUE;
        }
    }
    const WCHAR *translated = phrase_for(game_messages, COUNT(game_messages), text, count);
    if (translated) return TextOutW(dc, x, y, translated, lstrlenW(translated));
    return TextOutA(dc, x, y, text, count);
}

static BOOL party_member(const unsigned char *actor)
{
    return actor && *(const int *)(actor + 0x750) && *(const int *)(actor + 0x40);
}

static unsigned party_bit(const unsigned char * const *actors, unsigned index)
{
    const unsigned char *actor = actors[index];
    if (!party_member(actor)) return 0;
    if (NET_MULTI) {
        // Each player controls their own hero and the game draws the local ones in
        // list order, so a lone visitor hero of any kind sits in the first portrait.
        unsigned rank = 0;
        for (unsigned i = 0; i < index; ++i) rank += party_member(actors[i]);
        return 1u << (rank < 2 ? rank : 2);
    }
    int kind = *(const int *)(actor + 0x34);
    return kind == 1 ? 1 : kind == 2 ? 2 : 4;
}

static unsigned sync_party(const unsigned char *map, unsigned restore)
{
    unsigned present = 0, selected = 0;
    unsigned char * const *actors = (unsigned char * const *)(map + 0x14AAC);
    for (unsigned i = 0; i < 20; ++i) {
        unsigned char *actor = actors[i];
        unsigned bit = party_bit((const unsigned char * const *)actors, i);
        if (!bit) continue;
        int *flag = (int *)(actor + 0x2AC);
        present |= bit;
        if (restore & bit) *flag = (party_selection & bit) != 0;
        if (*flag) selected |= bit;
    }
    party_selection = (party_selection & ~present) | selected;
    return present;
}

static void update_party_map(const unsigned char *map)
{
    int map_id = *(const int *)(map + 0x34D38);
    unsigned restore = 0;
    if (party_map) restore = map != party_map || map_id != party_map_id ? 7 : 7 & ~party_present;
    else party_selection = 0;
    party_present = sync_party(map, restore);
    party_map = map;
    party_map_id = map_id;
}

static BOOL explored(const unsigned char *map, int x, int y)
{
    if (x < 0 || y < 0 || x >= map[0x10] * TILE || y >= map[0x11] * TILE) return FALSE;
    return map[0x14D38 + (y / 16) * 256 + x / 16] < 0x78;
}

static void add_damage(const unsigned char *actor, unsigned map_offset, int before, int after, unsigned kind)
{
    if (GAME_SCENE != CAMPAIGN_SCENE || !wide_mode || before <= 0 || after >= before) return;
    const unsigned char *map = *(const unsigned char * const *)(actor + map_offset);
    if (!map || map != GAME_MAP) return;
    int x = *(const int *)(actor + 0x0C), y = *(const int *)(actor + 0x10);
    if (!explored(map, x, y)) return;
    damage_numbers[next_damage] = (DamageNumber){map, x, y, before - max(0, after), GetTickCount(), kind};
    next_damage = (next_damage + 1) % COUNT(damage_numbers);
}

__declspec(dllexport) int WINAPI HudDamageParty(const unsigned char *actor,
    int direction, int amount, int flags, int level, int kind)
{
    typedef int (__thiscall *DamageParty)(const unsigned char *, int, int, int, int, int);
    int before = *(const int *)(actor + 0x810);
    int result = ((DamageParty)(uintptr_t)0x42B390)(actor, direction, amount, flags, level, kind);
    if (*(const int *)(actor + 0x750))
        add_damage(actor, 0x790, before, *(const int *)(actor + 0x810), (kind & 255) == 3 ? 2 : 1);
    return result;
}

__declspec(dllexport) int WINAPI HudDamageMob(const unsigned char *actor, int direction, int amount, int kind)
{
    typedef int (__thiscall *DamageMob)(const unsigned char *, int, int, int);
    int before = *(const int *)(actor + 0x300);
    int result = ((DamageMob)(uintptr_t)0x43E620)(actor, direction, amount, kind);
    // A visitor only sends the hit to the host (message 0x45 with the damage in
    // its fifth byte) and leaves the mob's life alone, so show the number it sent.
    if (NET_MULTI && !NET_HOST) {
        if (amount > 0) {
            note_estimated_hit(actor);
            add_damage(actor, 0x2DC, before, before - amount, (kind & 255) != 0 ? 2 : 0);
        }
        return result;
    }
    add_damage(actor, 0x2DC, before, *(const int *)(actor + 0x300), (kind & 255) != 0 ? 2 : 0);
    return result;
}

static BOOL use_lateral_hud(void)
{
    return wide_mode && show_hud && presented_hud && GAME_SCENE == CAMPAIGN_SCENE
        && world_draw_active() && (world_view_active() || quest_ready || dialogue_active());
}

static BOOL camera_keys_active(void)
{
    return wasd_camera && GAME_SCENE == CAMPAIGN_SCENE && world_view_active()
        && !presented_pause
        && GetForegroundWindow() == GAME_VIDEO->window
        && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000);
}

__declspec(dllexport) SHORT WINAPI HudCameraKey(int key)
{
    typedef SHORT (WINAPI *ReadKey)(int);
    ReadKey read_key = *(ReadKey const *)(uintptr_t)0x4891C0;
    SHORT result = read_key(key);
    static const int movement[] = {'A', 'W', 'D', 'S'};
    if (key >= VK_LEFT && key <= VK_DOWN && camera_keys_active())
        result |= read_key(movement[key - VK_LEFT]) & 0x8000;
    return result;
}

typedef void (__cdecl *NativeSprite)(int, int, const void *, void *, int, int, unsigned);
#define DRAW_SPRITE ((NativeSprite)(uintptr_t)0x41A980)

static BOOL capture_sprite(HudSprite pixels, const void *sprite, int width, int height, unsigned flags)
{
    if (!sprite || width != SPRITE_SIZE || height != SPRITE_SIZE) return FALSE;
    memset(sprite_canvas, 0xFF, sizeof(sprite_canvas[0]) * SPRITE_SIZE);
    DRAW_SPRITE(0, 0, sprite, sprite_canvas, width, height, flags);
    for (int y = 0; y < SPRITE_SIZE; ++y)
        memcpy(pixels[y], sprite_canvas[y], sizeof(pixels[y]));
    return TRUE;
}

__declspec(dllexport) void __cdecl HudOrbSprite(int x, int y, const void *sprite, void *surface,
    int width, int height, unsigned flags)
{
    int index = (x - 102) / 64;
    if (wide_mode && show_hud && GAME_SCENE == CAMPAIGN_SCENE && y == 446
        && index >= 0 && index < ORB_COUNT && x == 102 + 64 * index) {
        orb_ready[index] = capture_sprite(orb_sprites[index], sprite, width, height, flags);
        if ((use_lateral_hud() || sidebar_ready) && orb_ready[index]) return;
    }
    DRAW_SPRITE(x, y, sprite, surface, width, height, flags);
}

__declspec(dllexport) void __cdecl HudCursorSprite(int x, int y, const void *sprite, void *surface,
    int width, int height, unsigned flags)
{
    if (use_lateral_hud() || (presented_menu && GAME_SCENE == CAMPAIGN_SCENE))
        cursor_ready = capture_sprite(cursor_sprite, sprite, width, height, flags);
    else DRAW_SPRITE(x, y, sprite, surface, width, height, flags);
}

__declspec(dllexport) void __cdecl HudCursorBlend(int x, int y, const void *sprite, void *surface,
    int width, int height, unsigned flags)
{
    if (use_lateral_hud() || (presented_menu && GAME_SCENE == CAMPAIGN_SCENE))
        cursor_ready = capture_sprite(cursor_sprite, sprite, width, height, flags);
    else ((NativeSprite)(uintptr_t)0x41AA60)(x, y, sprite, surface, width, height, flags);
}

__declspec(dllexport) BOOL WINAPI HudWorldClip(const RECT *clip)
{
    // This call is the campaign's drag rectangle, expressed in the old
    // 640x480 layout. cnc-ddraw already confines the physical pointer.
    if (use_lateral_hud() && clip) return TRUE;
    typedef BOOL (WINAPI *NativeClip)(const RECT *);
    return (*(NativeClip const *)(uintptr_t)0x4891C4)(clip);
}

__declspec(dllexport) void __cdecl HudSelection(void *pixels, int x0, int y0,
    int x1, int y1, unsigned color)
{
    if (use_lateral_hud()) {
        selection = (RECT){min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)};
        selection_color = (uint16_t)color;
        selection_visible = TRUE;
        return;
    }
    typedef void (__cdecl *NativeSelection)(void *, int, int, int, int, unsigned);
    ((NativeSelection)(uintptr_t)0x41B1B0)(pixels, x0, y0, x1, y1, color);
}

static MapView current_map_view(void)
{
    MapView result = {0};
    const unsigned char *map = GAME_MAP;
    if (!map || !world_draw_active()) return result;

    // The native minimap uses one pixel per 32px tile in a 128px slot.
    int width = map[0x10];
    int height = map[0x11];
    if (width < 14 || width > 128 || height < 14 || height > 128) return result;
    int x = NATIVE_MAP_X + (NATIVE_MAP_SIZE - width) / 2;
    int y = NATIVE_MAP_Y + (NATIVE_MAP_SIZE - height) / 2;
    if (map_detail) {
        int camera_x = *(const int *)(map + 0x14);
        int camera_y = *(const int *)(map + 0x18);
        int crop_width = width / 2, crop_height = height / 2;
        x += clamp(camera_x + CAMERA_COLUMNS / 2 - crop_width / 2, 0, width - crop_width);
        y += clamp(camera_y + CAMERA_ROWS / 2 - crop_height / 2, 0, height - crop_height);
        width = crop_width;
        height = crop_height;
    }
    result.source = (MapRect){x, y, width, height};
    int inner = map_frame.width - 6;
    int longest = width > height ? width : height;
    result.target.width = width * inner / longest;
    result.target.height = height * inner / longest;
    result.target.x = map_frame.x + (map_frame.width - result.target.width) / 2;
    result.target.y = map_frame.y + MAP_GRIP_HEIGHT
        + (map_frame.height - MAP_GRIP_HEIGHT - result.target.height) / 2;
    return result;
}

static unsigned char *row(void *pixels, LONG pitch, int y)
{
    return (unsigned char *)pixels + pitch * y;
}

__declspec(dllexport) HRESULT WINAPI DrawTileOverlay(IDirectDrawSurface3 *target, DWORD x,
    DWORD y, IDirectDrawSurface3 *tiles, RECT *tile_rect, DWORD flags)
{
    ++tile_calls;
    DDSURFACEDESC src = {0}, dst = {0};
    src.dwSize = sizeof(src);
    dst.dwSize = sizeof(dst);
    HRESULT result = IDirectDrawSurface3_Lock(tiles, NULL, &src, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (FAILED(result)) return IDirectDrawSurface3_BltFast(target, x, y, tiles, tile_rect, flags);
    result = IDirectDrawSurface3_Lock(target, NULL, &dst, DDLOCK_WAIT, NULL);
    if (SUCCEEDED(result)) {
        LONG width = tile_rect->right - tile_rect->left;
        LONG height = tile_rect->bottom - tile_rect->top;
        if (src.lpSurface && dst.lpSurface && src.ddpfPixelFormat.dwRGBBitCount == 16
            && dst.ddpfPixelFormat.dwRGBBitCount == 16 && width == 32 && height == 32
            && tile_rect->left >= 0 && tile_rect->top >= 0
            && (DWORD)tile_rect->right <= src.dwWidth && (DWORD)tile_rect->bottom <= src.dwHeight
            && x < dst.dwWidth && y < dst.dwHeight
            && dst.dwWidth - x >= 32 && dst.dwHeight - y >= 32
            && src.lPitch >= (LONG)src.dwWidth * 2 && dst.lPitch >= (LONG)dst.dwWidth * 2) {
            for (int line = 0; line < height; ++line) {
                const uint16_t *from = (const uint16_t *)row(src.lpSurface, src.lPitch, tile_rect->top + line)
                    + tile_rect->left;
                uint16_t *to = (uint16_t *)row(dst.lpSurface, dst.lPitch, y + line) + x;
                // Zero in a .Til overlay leaves the already drawn ground visible.
                for (int col = 0; col < width; ++col)
                    if (from[col]) to[col] = from[col];
            }
        } else {
            result = DDERR_INVALIDPARAMS;
        }
        IDirectDrawSurface3_Unlock(target, NULL);
    }
    IDirectDrawSurface3_Unlock(tiles, NULL);
    if (FAILED(result)) return IDirectDrawSurface3_BltFast(target, x, y, tiles, tile_rect, flags);
    return result;
}

typedef void (__thiscall *RebuildTerrain)(unsigned char *);

__declspec(dllexport) void WINAPI HudBeginWorld(unsigned char *map)
{
    header_staged = header_ready = FALSE;
    world_hint_count = 0;
    quick_item_hint = FALSE;
    notice_hint.count = 0;
    npc_name_index = -1;
    if (map && GAME_SCENE == CAMPAIGN_SCENE) update_party_map(map);
    wide_frame = FALSE;
    overflow_ready = FALSE;
    world_render_failed = FALSE;
    quest_ready = dialogue_ready = FALSE;
    selection_visible = FALSE;
    CAMERA_COLUMNS = 14;
    CAMERA_ROWS = NATIVE_ROWS;
    CAMERA_REVEAL_X = CAMERA_REVEAL_Y = 31;
    if (map) {
        int *camera_x = (int *)(map + 0x14);
        int *camera_y = (int *)(map + 0x18);
        if (dialogue_active()) {
            if (dialogue_camera_saved && dialogue_camera_map == map
                && (*camera_x != dialogue_camera_x || *camera_y != dialogue_camera_y)) {
                *camera_x = dialogue_camera_x;
                *camera_y = dialogue_camera_y;
                ((RebuildTerrain)(uintptr_t)0x457680)(map);
            }
        } else {
            dialogue_camera_map = map;
            dialogue_camera_x = *camera_x;
            dialogue_camera_y = *camera_y;
            dialogue_camera_saved = TRUE;
        }
    }
    if (!wide_mode || !show_hud || GAME_SCENE != CAMPAIGN_SCENE
        || !world_draw_active() || !map || map[0x10] < NATIVE_WIDE_COLUMNS) return;

    CameraView previous_view = frame_view;
    frame_view = (CameraView){NATIVE_WIDE_COLUMNS, VIEW_HEIGHT};
    if (overflow_surface) {
        for (int level = (int)zoom_level; level >= 0; --level) {
            CameraView choice = camera_views[level];
            if (map[0x10] >= choice.columns && map[0x11] * TILE >= choice.height) {
                frame_view = choice;
                break;
            }
        }
    }
    int columns = frame_view.columns;
    int rows = (frame_view.height + TILE - 1) / TILE;
    int *camera_x = (int *)(map + 0x14);
    int *camera_y = (int *)(map + 0x18);
    int x = *camera_x;
    int y = *camera_y;
    if (recenter_zoom && previous_world_map == map) {
        x += (previous_view.columns - columns) / 2;
        int previous_rows = (previous_view.height + TILE - 1) / TILE;
        y += (previous_rows - rows) / 2;
    }
    recenter_zoom = FALSE;
    previous_world_map = map;
    x = clamp(x, 0, map[0x10] - columns);
    y = clamp(y, 0, max(0, map[0x11] - rows));
    if (*camera_x != x || *camera_y != y) {
        *camera_x = x;
        *camera_y = y;
        ((RebuildTerrain)(uintptr_t)0x457680)(map);
    }
    CAMERA_COLUMNS = columns;
    CAMERA_ROWS = rows;
    // Keep the engine's three half-tile margin around the expanded view.
    CAMERA_REVEAL_X = min(columns * 2 + 3, (map[0x10] - x) * 2);
    CAMERA_REVEAL_Y = min(rows * 2 + 3, (map[0x11] - y) * 2);
    WORLD_CLIP_RIGHT = NATIVE_WIDE_RIGHT;
    memset(map_markers, 0, sizeof(map_markers));
    frame_cam_x = x;
    frame_cam_y = y;
    wide_frame = TRUE;
}

typedef void (__cdecl *NativeMapDot)(void *, int, int, DWORD);

__declspec(dllexport) void __cdecl HudMapDot(void *pixels, int x, int y, DWORD colors)
{
    int column = x - NATIVE_MAP_X;
    int line = y - NATIVE_MAP_Y;
    if (drawing_overflow) return;
    if (wide_frame && WORLD_CLIP_RIGHT == NATIVE_WIDE_RIGHT && line >= 0 && line < NATIVE_MAP_SIZE
        && column >= 0 && column < NATIVE_MAP_SIZE - 1) {
        // Actor drawing calls this before the native minimap. Keep the dots
        // for the floating map and leave the shared terrain strip untouched.
        map_markers[line][column] = 0x10000 | (colors & 0xFFFF);
        map_markers[line][column + 1] = 0x10000 | (colors >> 16);
        return;
    }
    ((NativeMapDot)(uintptr_t)0x41B110)(pixels, x, y, colors);
}

static void draw_tiles(IDirectDrawSurface3 *back, int shift_x, int shift_y,
    int first, int end, int rows, BOOL foreground)
{
    const unsigned char *map = GAME_MAP;
    IDirectDrawSurface3 * const *tiles = (IDirectDrawSurface3 * const *)(uintptr_t)0x4B9B90;
    const uint16_t *ids = (const uint16_t *)(map + 0x1C);
    int camera_x = *(const int *)(map + 0x14) + shift_x;
    int camera_y = *(const int *)(map + 0x18) + shift_y;
    if (!foreground) {
        RECT rect = {WORLD_TOP + first * TILE, WORLD_TOP, WORLD_TOP + end * TILE, WORLD_TOP + rows * TILE};
        DDBLTFX fx = {0};
        fx.dwSize = sizeof(fx);
        IDirectDrawSurface3_Blt(back, &rect, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &fx);
    }
    for (int y = 0; y < rows; ++y) {
        if (camera_y + y < 0 || camera_y + y >= map[0x11]) continue;
        for (int x = first; x < end; ++x) {
            if (camera_x + x < 0 || camera_x + x >= map[0x10]) continue;
            const uint16_t *cell = ids + 2 * ((camera_y + y) * 128 + camera_x + x);
            int base = cell[0], upper = cell[1];
            int layers[2] = {foreground ? -1 : base, -1};
            if (upper != 0xFFFF && (upper < 10000) == foreground)
                layers[1] = foreground ? upper : upper - 10000;
            for (int layer = 0; layer < 2; ++layer) {
                int id = layers[layer];
                if (id < 0 || id >= 2700 || !tiles[id / 100]) continue;
                LONG sx = (id % 20) * TILE, sy = (id % 100 / 20) * TILE;
                RECT rect = {sx, sy, sx + TILE, sy + TILE};
                DWORD left = WORLD_TOP + x * TILE, top = WORLD_TOP + y * TILE;
                if (layer) DrawTileOverlay(back, left, top, tiles[id / 100], &rect, DDBLTFAST_SRCCOLORKEY);
                else IDirectDrawSurface3_BltFast(back, left, top, tiles[id / 100], &rect, 0);
            }
        }
    }
}

static BOOL lock_world_surface(IDirectDrawSurface3 *surface, DDSURFACEDESC *pixels, DWORD flags)
{
    pixels->dwSize = sizeof(*pixels);
    if (FAILED(IDirectDrawSurface3_Lock(surface, NULL, pixels, DDLOCK_WAIT | flags, NULL))) return FALSE;
    if (pixels->lpSurface && pixels->dwWidth == GAME_WIDTH
        && pixels->dwHeight == HEIGHT && pixels->lPitch == GAME_WIDTH * 2
        && pixels->ddpfPixelFormat.dwRGBBitCount == 16) return TRUE;
    IDirectDrawSurface3_Unlock(surface, NULL);
    return FALSE;
}

__declspec(dllexport) void WINAPI HudBeginCampaignPaint(void)
{
    world_map_ready = pause_ready = FALSE;
}

static void keep_world_map_label_visible(int *x, int *y, int width, int height)
{
    if (!drawing_full_world_map) return;
    int left = world_map_part.x - WORLD_TOP, top = world_map_part.y - WORLD_TOP;
    *x = clamp(*x + left, 4, WORLD_MAP_WIDTH - width - 4) - left;
    *y = clamp(*y + top, 4, WORLD_MAP_HEIGHT - height - 4) - top;
}

__declspec(dllexport) void __cdecl HudWorldMapLabel(int x, int y, const void *sprite,
    void *surface, int width, int height, unsigned flags)
{
    keep_world_map_label_visible(&x, &y, width, height);
    ((NativeSprite)(uintptr_t)0x419F10)(x, y, sprite, surface, width, height, flags);
}

__declspec(dllexport) void __cdecl HudWorldMapShadeLabel(int x, int y, const void *sprite,
    void *surface, int width, int height, unsigned flags, DWORD shade)
{
    typedef void (__cdecl *ShadeSprite)(int, int, const void *, void *, int, int, unsigned, DWORD);
    keep_world_map_label_visible(&x, &y, width, height);
    ((ShadeSprite)(uintptr_t)0x41A560)(x, y, sprite, surface, width, height, flags, shade);
}

__declspec(dllexport) BOOL WINAPI HudWorldMap(void)
{
    if (!wide_mode || !show_hud || !sidebar_ready || GAME_SCENE != CAMPAIGN_SCENE) return FALSE;
    IDirectDrawSurface3 *surface = *(IDirectDrawSurface3 **)(uintptr_t)0x49B264;
    DDSURFACEDESC pixels = {0};
    pixels.dwSize = sizeof(pixels);
    if (!surface || FAILED(IDirectDrawSurface3_Lock(surface, NULL, &pixels,
        DDLOCK_WAIT | DDLOCK_READONLY, NULL))) return FALSE;
    BOOL supported = pixels.lpSurface && pixels.dwWidth == WORLD_MAP_WIDTH
        && pixels.dwHeight == WORLD_MAP_HEIGHT && pixels.lPitch >= sizeof(world_map_pixels[0])
        && pixels.ddpfPixelFormat.dwRGBBitCount == 16;
    if (supported) {
        for (int y = 0; y < WORLD_MAP_HEIGHT; ++y)
            memcpy(world_map_pixels[y], row(pixels.lpSurface, pixels.lPitch, y), sizeof(world_map_pixels[y]));
    }
    IDirectDrawSurface3_Unlock(surface, NULL);
    if (!supported) return FALSE;

    // The original label rasterizer has a 640-pixel pitch. Draw the same
    // markers in four clipped passes and assemble them at their map coordinates.
    typedef void (__thiscall *MapLabel)(const unsigned char *, int, int, int, int, int);
    const MapLabel draw_label = (MapLabel)(uintptr_t)0x45D450;
    const unsigned char * const *maps = (const unsigned char * const *)(uintptr_t)0x4B9CFC;
    BOOL surface_world = *(const DWORD *)(uintptr_t)0x49B8E4 != 0;
    unsigned first = surface_world ? 0 : 22, end = surface_world ? 22 : 42;
    int current = GAME_MAP ? *(const int *)(GAME_MAP + 0x34D38) : -1;
    const MapRect *panel = &world_map_panel;
    int mouse_x = -1, mouse_y = -1;
    if (in_rect(panel, pointer_x, pointer_y)) {
        mouse_x = (pointer_x - panel->x) * WORLD_MAP_WIDTH / panel->width;
        mouse_y = (pointer_y - panel->y) * WORLD_MAP_HEIGHT / panel->height;
    }
    enum { PART_WIDTH = WORLD_MAP_WIDTH / 2, PART_HEIGHT = WORLD_MAP_HEIGHT / 2 };
    RECT clip = {WORLD_CLIP_LEFT, WORLD_CLIP_TOP, WORLD_CLIP_RIGHT, WORLD_CLIP_BOTTOM};
    void *original_pixels = WORLD_PIXELS;
    WORLD_CLIP_LEFT = WORLD_CLIP_TOP = WORLD_TOP;
    WORLD_CLIP_RIGHT = WORLD_TOP + PART_WIDTH;
    WORLD_CLIP_BOTTOM = WORLD_TOP + PART_HEIGHT;
    WORLD_PIXELS = sprite_canvas;
    drawing_full_world_map = TRUE;
    for (int top = 0; top < WORLD_MAP_HEIGHT; top += PART_HEIGHT) {
        for (int left = 0; left < WORLD_MAP_WIDTH; left += PART_WIDTH) {
            world_map_part = (POINT){left, top};
            for (int y = 0; y < PART_HEIGHT; ++y)
                memcpy(sprite_canvas[WORLD_TOP + y] + WORLD_TOP, world_map_pixels[top + y] + left,
                    PART_WIDTH * sizeof(uint16_t));
            BOOL hovering = mouse_x >= left && mouse_x < left + PART_WIDTH
                && mouse_y >= top && mouse_y < top + PART_HEIGHT;
            for (unsigned i = first; i < end; ++i) {
                if (maps[i] && !*(const int *)(uintptr_t)(0x497ACC + i * 0x5C))
                    draw_label(maps[i], left, top, hovering ? mouse_x - left : 0,
                        hovering ? mouse_y - top : 0, i == current);
            }
            for (int y = 0; y < PART_HEIGHT; ++y)
                memcpy(world_map_pixels[top + y] + left, sprite_canvas[WORLD_TOP + y] + WORLD_TOP,
                    PART_WIDTH * sizeof(uint16_t));
        }
    }
    drawing_full_world_map = FALSE;
    WORLD_CLIP_LEFT = clip.left;
    WORLD_CLIP_TOP = clip.top;
    WORLD_CLIP_RIGHT = clip.right;
    WORLD_CLIP_BOTTOM = clip.bottom;
    WORLD_PIXELS = original_pixels;
    world_map_ready = TRUE;
    return TRUE;
}

static uint16_t pause_shade(uint16_t pixel, unsigned mask)
{
    return (pixel & mask) >> 1;
}

__declspec(dllexport) void WINAPI HudPauseBackdrop(void *menu)
{
    if (!wide_mode || !show_hud || !sidebar_ready || GAME_SCENE != CAMPAIGN_SCENE) return;
    DDSURFACEDESC pixels = {0};
    if (!lock_world_surface(GAME_VIDEO->back, &pixels, 0)) return;
    unsigned mask = pixels.ddpfPixelFormat.dwRBitMask == 0x7C00 ? 0x7BDE : 0xF7DE;
    for (int y = 0; y < HEIGHT; ++y) {
        const uint16_t *from = y < BELT_Y ? world_snapshot[y] : hud_belt[y - BELT_Y];
        uint16_t *to = (uint16_t *)row(pixels.lpSurface, pixels.lPitch, y);
        for (int x = 0; x < GAME_WIDTH; ++x)
            to[x] = pause_shade(from[pause_menu.x + x], mask);
    }
    IDirectDrawSurface3_Unlock(GAME_VIDEO->back, NULL);
    if (!presented_pause) {
        typedef int (__thiscall *PauseHover)(void *, int, int);
        int x = pointer_x - pause_menu.x, y = pointer_y;
        if (x < 0 || x >= GAME_WIDTH || y < 0 || y >= HEIGHT) x = y = 0;
        ((PauseHover)(uintptr_t)0x40B4E0)(menu, x, y);
    }
    pause_ready = TRUE;
}

__declspec(dllexport) void WINAPI HudExtendGround(void)
{
    if (!wide_frame) return;
    DDSURFACEDESC pixels = {0};
    if (!lock_world_surface(GAME_VIDEO->back, &pixels, DDLOCK_READONLY)) {
        wide_frame = FALSE;
        world_render_failed = TRUE;
        WORLD_CLIP_RIGHT = WORLD_RIGHT;
        return;
    }
    // The native HUD still uses these pixels later in the same backbuffer.
    for (int y = 0; y < 14 * TILE; ++y)
        memcpy(native_panel[y], row(pixels.lpSurface, pixels.lPitch, y + WORLD_TOP) + WORLD_RIGHT * 2,
            sizeof(native_panel[0]));
    IDirectDrawSurface3_Unlock(GAME_VIDEO->back, NULL);
    draw_tiles(GAME_VIDEO->back, 0, 0, 14, NATIVE_WIDE_COLUMNS, NATIVE_ROWS, FALSE);
}

__declspec(dllexport) void WINAPI HudExtendForeground(void)
{
    if (wide_frame) draw_tiles(GAME_VIDEO->back, 0, 0, 14, NATIVE_WIDE_COLUMNS, NATIVE_ROWS, TRUE);
}

typedef void (__thiscall *WorldDraw)(const unsigned char *map);

typedef unsigned char (__thiscall *FogTransition)(const unsigned char *map, int index);
typedef void (__cdecl *FogBlock)(void *pixels);
typedef void (__cdecl *FogEdge)(void *pixels, const void *mask);

static void shade_world_section(const DDSURFACEDESC *pixels, int source_x,
    int shift_x, int shift_y, int columns, int rows)
{
    const unsigned char *map = GAME_MAP;
    if (!*(const DWORD *)(map + 0x14D00)) return;

    // The native fog pass at 0x45A9AF shades only the original 28x28
    // half-tiles. Reuse its transition masks and RGB15/RGB16 blenders.
    static const uintptr_t blenders[0x79][3] = {
        [3]    = {0x41B370, 0x41B5D0, 0x41B5D0},
        [5]    = {0x41B4A0, 0x41B770, 0x41B6A0},
        [8]    = {0x41B530, 0x41B950, 0x41B870},
        [0x28] = {0x41B270, 0x41BB10, 0x41BA20},
        [0x38] = {0x41B300, 0x41BCA0, 0x41BBE0},
        [0x48] = {0x41B410, 0x41BE60, 0x41BD70},
        [0x78] = {0x41B5A0, 0x41BFE0, 0x41BF30},
    };
    enum { FOG_TILE = TILE / 2, FOG_STRIDE = 256 };
    int x0 = (*(const int *)(map + 0x14) + shift_x) * 2;
    int y0 = (*(const int *)(map + 0x18) + shift_y) * 2;
    int width = min(columns * 2, min(map[0x10] * 2, FOG_STRIDE) - x0);
    int height = min(rows * 2, min(map[0x11] * 2, FOG_STRIDE) - y0);
    const unsigned char *grid = map + 0x14D38;
    const void *mask = map + 0x14D18;
    FogTransition transition = (FogTransition)(uintptr_t)0x45A2A0;
    for (int y = 0; y < height; ++y) {
        unsigned char *line = row(pixels->lpSurface, pixels->lPitch,
            WORLD_TOP + y * FOG_TILE) + (WORLD_TOP + source_x * TILE) * 2;
        for (int x = 0; x < width; ++x) {
            int index = (y0 + y) * FOG_STRIDE + x0 + x;
            unsigned darkness = grid[index];
            if (darkness >= COUNT(blenders) || !blenders[darkness][0]) continue;
            unsigned edge = transition(map, index);
            edge = darkness == 3 ? (edge != 0) : (edge <= 2 ? edge : 0);
            void *block = line + x * FOG_TILE * 2;
            if (edge) ((FogEdge)blenders[darkness][edge])(block, mask);
            else ((FogBlock)blenders[darkness][0])(block);
        }
    }
}


typedef void (__thiscall *ObjectDraw)(const unsigned char *);

static uint16_t item_scratch[HEIGHT][GAME_WIDTH];
static uint16_t item_backup[HEIGHT][GAME_WIDTH];
static BOOL loot_rgb555;

static BOOL life_bar_pixel(uint16_t v)
{
    int r, g, b;
    if (loot_rgb555) { r = v >> 10 & 31; g = (v >> 5 & 31) * 2; b = v & 31; }
    else { r = v >> 11; g = v >> 5 & 63; b = v & 31; }
    return (g >= 48 && r <= 8 && b <= 8) || (r >= 26 && g <= 14 && b <= 8);
}
enum { ITEM_KEY = 0xF81F };

static BOOL loot_under_pointer(int x, int y, POINT *tile);

static BOOL hovered_loot_tile(int x, int y)
{
    POINT tile;
    return loot_under_pointer(pointer_x, pointer_y, &tile) && tile.x == x && tile.y == y;
}

// Draws the item sprite again after the actor pass, so it stays above corpses,
// and outlines the real sprite while the loot key is held or the item is hovered.
static void redraw_loot_items(void *pixels, int x0, int y0, int columns, int rows)
{
    const unsigned char *map = GAME_MAP;
    if (!map) return;
    typedef int (__thiscall *ItemVisible)(const unsigned char *);
    ItemVisible visible = (ItemVisible)(uintptr_t)0x447E00;
    const unsigned char * const *lists = (const unsigned char * const *)(map + 0x34D54);
    BOOL all = loot_held();
    void *real_pixels = WORLD_PIXELS;
    for (int y = max(0, y0); y < min(map[0x11], min(128, y0 + rows)); ++y) {
        for (const unsigned char *item = lists[y]; item; item = *(const unsigned char * const *)(item + 0x2C)) {
            uintptr_t type = *(const uintptr_t *)item;
            if ((type != 0x48A430 && type != 0x48A45C) || !visible(item)) continue;
            int x = *(const int *)(item + 0x14);
            if (x < x0 || x >= x0 + columns || x >= 128) continue;
            ObjectDraw draw = (ObjectDraw)(*(const uintptr_t * const *)item)[2];
            int top = clamp(WORLD_TOP + (y - y0) * TILE - 40, 0, HEIGHT - 1);
            int bottom = clamp(WORLD_TOP + (y - y0) * TILE + 72, 0, HEIGHT);
            int left = clamp(WORLD_TOP + (x - x0) * TILE - 24, 0, GAME_WIDTH - 1);
            int right = clamp(WORLD_TOP + (x - x0) * TILE + 56, 0, GAME_WIDTH);
            for (int j = top; j < bottom; ++j)
                memcpy(item_backup[j] + left, (uint16_t *)row(pixels, GAME_WIDTH * 2, j) + left,
                    (size_t)(right - left) * 2);
            draw(item);
            // Health bars stay on top of the item sprite.
            for (int j = top; j < bottom; ++j) {
                uint16_t *to = (uint16_t *)row(pixels, GAME_WIDTH * 2, j);
                for (int i = left; i < right; ++i)
                    if (life_bar_pixel(item_backup[j][i])) to[i] = item_backup[j][i];
            }
            if (!all && !hovered_loot_tile(x, y)) continue;
            for (int j = top; j < bottom; ++j)
                for (int i = left; i < right; ++i) item_scratch[j][i] = ITEM_KEY;
            WORLD_PIXELS = item_scratch;
            draw(item);
            WORLD_PIXELS = real_pixels;
            int min_x = WORLD_TOP, max_x = WORLD_TOP + columns * TILE;
            int min_y = WORLD_TOP, max_y = WORLD_TOP + rows * TILE;
            // The sentinel pixels that border the sprite form the outline.
            for (int j = max(top + 1, min_y); j < min(bottom - 1, max_y); ++j) {
                uint16_t *to = (uint16_t *)row(pixels, GAME_WIDTH * 2, j);
                for (int i = max(left + 1, min_x); i < min(right - 1, max_x); ++i) {
                    if (item_scratch[j][i] != ITEM_KEY) continue;
                    if (item_scratch[j][i - 1] != ITEM_KEY || item_scratch[j][i + 1] != ITEM_KEY
                        || item_scratch[j - 1][i] != ITEM_KEY || item_scratch[j + 1][i] != ITEM_KEY)
                        to[i] = 0xFFFF;
                }
            }
        }
    }
}

static void draw_overflow_actors(void *pixels, int shift_x, int shift_y,
    int columns, int rows, BOOL top_layer)
{
    const unsigned char *map = GAME_MAP;
    GameVideo *video = GAME_VIDEO;
    int *camera_x = (int *)(map + 0x14);
    int *camera_y = (int *)(map + 0x18);
    int previous_x = *camera_x;
    int previous_y = *camera_y;
    LONG previous_left = WORLD_CLIP_LEFT, previous_right = WORLD_CLIP_RIGHT;
    LONG previous_bottom = WORLD_CLIP_BOTTOM;
    void *previous_pixels = WORLD_PIXELS;
    IDirectDrawSurface3 *previous_back = video->back;
    DWORD video_lock_info[2];
    memcpy(video_lock_info, (unsigned char *)video + 0x18, sizeof(video_lock_info));

    // Draw the rightmost two columns into a pitch-compatible surface. Rebase
    // the drawing coordinates for this pass and restore the live camera before
    // input, exploration and the native minimap consume it.
    *camera_x += shift_x;
    *camera_y += shift_y;
    WORLD_CLIP_LEFT = WORLD_TOP;
    WORLD_CLIP_RIGHT = WORLD_TOP + columns * TILE;
    WORLD_CLIP_BOTTOM = WORLD_TOP + rows * TILE;
    video->back = overflow_surface;
    drawing_overflow = TRUE;
    WORLD_PIXELS = pixels;
    if (top_layer) {
        ((WorldDraw)(uintptr_t)0x45D380)(map);
        ((WorldDraw)(uintptr_t)0x45D390)(map);
    } else {
        ((WorldDraw)(uintptr_t)0x45C310)(map);
        ((WorldDraw)(uintptr_t)0x45D340)(map);
        ((WorldDraw)(uintptr_t)0x45D370)(map);
        (*(WorldDraw const * const *)map)[0x2C / 4](map);
        redraw_loot_items(pixels, previous_x + shift_x, previous_y + shift_y, columns, rows);
    }
    drawing_overflow = FALSE;
    video->back = previous_back;
    memcpy((unsigned char *)video + 0x18, video_lock_info, sizeof(video_lock_info));
    WORLD_PIXELS = previous_pixels;
    WORLD_CLIP_LEFT = previous_left;
    WORLD_CLIP_RIGHT = previous_right;
    WORLD_CLIP_BOTTOM = previous_bottom;
    *camera_x = previous_x;
    *camera_y = previous_y;
}

static BOOL draw_world_section(int shift_x, int shift_y, int columns, int rows)
{
    DDSURFACEDESC pixels = {0};
    draw_tiles(overflow_surface, shift_x, shift_y, 0, columns, rows, FALSE);
    if (!lock_world_surface(overflow_surface, &pixels, 0)) return FALSE;
    draw_overflow_actors(pixels.lpSurface, shift_x, shift_y, columns, rows, FALSE);
    IDirectDrawSurface3_Unlock(overflow_surface, NULL);

    draw_tiles(overflow_surface, shift_x, shift_y, 0, columns, rows, TRUE);
    if (!lock_world_surface(overflow_surface, &pixels, 0)) return FALSE;
    draw_overflow_actors(pixels.lpSurface, shift_x, shift_y, columns, rows, TRUE);
    shade_world_section(&pixels, 0, shift_x, shift_y, columns, rows);
    for (int y = 0; y < rows * TILE; ++y)
        memcpy(terrain[shift_y * TILE + y] + shift_x * TILE,
            row(pixels.lpSurface, pixels.lPitch, WORLD_TOP + y) + WORLD_TOP * 2, columns * TILE * 2);
    IDirectDrawSurface3_Unlock(overflow_surface, NULL);
    return TRUE;
}

__declspec(dllexport) void WINAPI HudEndWorld(void)
{
    if (!wide_frame) return;
    WORLD_CLIP_RIGHT = WORLD_RIGHT;
    DDSURFACEDESC pixels = {0};
    if (!lock_world_surface(GAME_VIDEO->back, &pixels, 0)) {
        wide_frame = FALSE;
        world_render_failed = TRUE;
        return;
    }
    loot_rgb555 = pixels.ddpfPixelFormat.dwRBitMask == 0x7C00;
    if (GAME_MAP) {
        void *previous_pixels = WORLD_PIXELS;
        LONG previous_right = WORLD_CLIP_RIGHT;
        const unsigned char *map = GAME_MAP;
        WORLD_PIXELS = pixels.lpSurface;
        WORLD_CLIP_RIGHT = NATIVE_WIDE_RIGHT;
        redraw_loot_items(pixels.lpSurface, *(const int *)(map + 0x14), *(const int *)(map + 0x18), NATIVE_WIDE_COLUMNS, NATIVE_ROWS);
        WORLD_CLIP_RIGHT = previous_right;
        WORLD_PIXELS = previous_pixels;
    }
    shade_world_section(&pixels, 14, 14, 0, NATIVE_WIDE_COLUMNS - 14, NATIVE_ROWS);
    for (int y = 0; y < 14 * TILE; ++y) {
        unsigned char *native = row(pixels.lpSurface, pixels.lPitch, y + WORLD_TOP);
        memcpy(terrain[y], native + WORLD_TOP * 2, NATIVE_WIDE_COLUMNS * TILE * 2);
        memcpy(native + WORLD_RIGHT * 2, native_panel[y], sizeof(native_panel[0]));
    }
    {
        // The last tile row is shown from this copy (the belt covers the live
        // frame there) but the engine fogs the live frame only. Fog the copy.
        static uint16_t fog_scratch[HEIGHT][GAME_WIDTH];
        enum { LAST_ROW = (NATIVE_ROWS - 1) * TILE };
        for (int y = 0; y < TILE; ++y)
            memcpy(fog_scratch[WORLD_TOP + y] + WORLD_TOP, terrain[LAST_ROW + y], 14 * TILE * 2);
        DDSURFACEDESC scratch = pixels;
        scratch.lpSurface = fog_scratch;
        scratch.lPitch = GAME_WIDTH * 2;
        shade_world_section(&scratch, 0, 0, NATIVE_ROWS - 1, 14, 1);
        for (int y = 0; y < TILE; ++y)
            memcpy(terrain[LAST_ROW + y], fog_scratch[WORLD_TOP + y] + WORLD_TOP, 14 * TILE * 2);
    }
    IDirectDrawSurface3_Unlock(GAME_VIDEO->back, NULL);
    int added_columns = frame_view.columns - NATIVE_WIDE_COLUMNS;
    if (!added_columns) return;
    overflow_ready = draw_world_section(NATIVE_WIDE_COLUMNS, 0, added_columns, NATIVE_ROWS);
    int added_rows = CAMERA_ROWS - NATIVE_ROWS;
    if (overflow_ready && added_rows) {
        // The bottom HUD covers part of the last native tile row.
        int first_row = VIEW_HEIGHT / TILE;
        overflow_ready = draw_world_section(0, first_row, NATIVE_WIDE_COLUMNS, CAMERA_ROWS - first_row)
            && draw_world_section(NATIVE_WIDE_COLUMNS, NATIVE_ROWS, added_columns, added_rows);
    }
    world_render_failed = !overflow_ready;
}

static void paint_world(const DDSURFACEDESC *src, const DDSURFACEDESC *dst,
    int right, CameraView view, BOOL journal_background, int rows);

__declspec(dllexport) void WINAPI HudBeginHeader(void)
{
    if (!wide_frame || !overflow_ready || world_render_failed
        || (!smooth_camera && frame_view.columns == VIEW_COLUMNS && frame_view.height == VIEW_HEIGHT)) return;
    DDSURFACEDESC pixels = {0};
    if (!lock_world_surface(GAME_VIDEO->back, &pixels, 0)) return;

    // The fog pass is complete. Put the projected ground under the original
    // caption and quick slots so their transparency blends at the final scale.
    DDSURFACEDESC canvas = {0};
    canvas.lpSurface = header_canvas;
    canvas.lPitch = sizeof(header_canvas[0]);
    paint_world(&pixels, &canvas, WIDE_RIGHT, frame_view, FALSE, COUNT(header_world));
    for (int y = WORLD_TOP; y < FIXED_HEADER_BOTTOM; ++y)
        memcpy(header_background[y], header_canvas[y], sizeof(header_background[y]));
    for (int y = WORLD_TOP; y < FIXED_HEADER_BOTTOM; ++y) {
        unsigned char *native = row(pixels.lpSurface, pixels.lPitch, y) + WORLD_TOP * 2;
        memcpy(header_world[y - WORLD_TOP], native, sizeof(header_world[0]));
        memcpy(native, header_canvas[y] + WORLD_TOP, sizeof(header_world[0]));
    }
    IDirectDrawSurface3_Unlock(GAME_VIDEO->back, NULL);
    header_staged = TRUE;
}

__declspec(dllexport) void WINAPI HudEndHeader(void)
{
    if (!header_staged) return;
    header_staged = FALSE;
    DDSURFACEDESC pixels = {0};
    if (!lock_world_surface(GAME_VIDEO->back, &pixels, 0)) {
        world_render_failed = TRUE;
        return;
    }
    for (int y = WORLD_TOP; y < FIXED_HEADER_BOTTOM; ++y) {
        unsigned char *native = row(pixels.lpSurface, pixels.lPitch, y) + WORLD_TOP * 2;
        memcpy(header_canvas[y] + WORLD_TOP, native, sizeof(header_world[0]));
        memcpy(native, header_world[y - WORLD_TOP], sizeof(header_world[0]));
    }
    IDirectDrawSurface3_Unlock(GAME_VIDEO->back, NULL);
    header_ready = TRUE;
}

static BOOL quest_in_log(unsigned order)
{
    for (int slot = 0; slot < 10; ++slot) {
        unsigned entry = *(const unsigned *)(uintptr_t)(0x4BC368 + slot * 0x238);
        if (entry && (!order || entry == order)) return TRUE;
    }
    return FALSE;
}

typedef struct {
    unsigned order, finished;
    const WCHAR *npc[4], *step[4];
} QuestLead;

// These dialogue branches can introduce a quest without a previous quest
// item. The progress flag is set by the same script when the offer is taken.
static const QuestLead quest_leads[] = {
    {101, 0, {L"Chefe da vila", L"Village Chief", L"마을 촌장", L"村長"},
        {L"Escolha “Está bem…”, depois “Dizes mais” e aceite buscar o presente na Vila Mantis.",
         L"Choose “OK!”, then “Tell me more”, and agree to fetch the gift in Mantis Village.",
         L"“좋아”, “더 말해줘”를 선택하고 만티스 마을에서 선물을 가져오겠다고 하세요.",
         L"選擇「好!」,再選「多告訴我一些」,並答應去螳螂村取回禮物。"}},
    {103, 0, {L"Lojista", L"Shopkeeper", L"상점 주인", L"店主"},
        {L"Pergunte sobre os demônios perto da vila.",
         L"Ask about the demons around the village.",
         L"마을 주변의 악마에 대해 물어보세요.",
         L"詢問村子附近惡魔的事。"}},
    {107, 44, {L"Mercador de itens", L"Item Merchant", L"아이템 상인", L"道具商人"},
        {L"Escolha conversar e pergunte sobre o pedido.",
         L"Choose Chat and ask about the request.",
         L"대화를 선택하고 부탁에 대해 물어보세요.",
         L"選擇交談,並詢問委託的事。"}},
    {104, 0, {L"Ferreiro", L"Blacksmith", L"대장장이", L"鐵匠"},
        {L"Converse sobre a espada e pergunte pelos materiais.",
         L"Talk about a sword and ask about the materials.",
         L"검에 대해 이야기하고 재료를 물어보세요.",
         L"談談劍的事,並詢問材料。"}},
    {109, 0, {L"Joalheiro", L"Jeweler", L"보석상", L"珠寶商"},
        {L"Pergunte como conseguir uma boa joia.",
         L"Ask how to get a good jewel.",
         L"좋은 보석을 얻는 방법을 물어보세요.",
         L"詢問如何取得好寶石。"}},
    {117, 0, {L"Joalheiro", L"Jeweler", L"보석상", L"珠寶商"},
        {L"Converse sobre a vila e ofereça ajuda.",
         L"Ask about the village and offer to help.",
         L"마을에 대해 묻고 도와주겠다고 하세요.",
         L"詢問村子的事,並表示願意幫忙。"}},
};

static BOOL quest_flag_on_party(const unsigned char *map, unsigned flag)
{
    typedef int (__thiscall *HasFlag)(const void *, unsigned);
    const unsigned char * const *actors = (const unsigned char * const *)(map + 0x14AAC);
    for (unsigned i = 0; i < 20; ++i) {
        if (party_bit(actors, i)
            && ((HasFlag)(uintptr_t)0x44A730)(actors[i] + 0x59C, flag)) return TRUE;
    }
    return FALSE;
}

static const QuestLead *quest_lead_for_order(unsigned order)
{
    for (unsigned i = 0; i < COUNT(quest_leads); ++i)
        if (quest_leads[i].order == order) return &quest_leads[i];
    return NULL;
}

typedef struct { const QuestLead *lead; const unsigned char *npc; unsigned year; } QuestTarget;

static QuestTarget next_quest_lead(void)
{
    const unsigned char *map = GAME_MAP;
    QuestTarget best = {0};
    if (!map || quest_in_log(0) || !*(const DWORD *)(uintptr_t)0x49B8E4) return best;
    unsigned year = 101 + CAMPAIGN_TICKS / 25600;
    const unsigned char * const *maps = (const unsigned char * const *)(uintptr_t)0x4B9CFC;
    typedef const unsigned char *(__thiscall *FindNpc)(const void *, unsigned);
    // The script loader and the native year tick use this same event table.
    for (unsigned i = 0; i < COUNT(quest_starts); ++i) {
        if (quest_starts[i].surface != 1) continue;
        unsigned event = quest_starts[i].event;
        const unsigned char *record = (const unsigned char *)(uintptr_t)(0x4B9ED0 + event * 44);
        if (*(const int *)record == -1 || record[4] >= 42 || maps[record[4]] != map) continue;
        const QuestLead *lead = quest_lead_for_order(quest_starts[i].order);
        if (!lead || quest_flag_on_party(map, lead->order)
            || (lead->finished && quest_flag_on_party(map, lead->finished))) continue;
        const unsigned char *npc = ((FindNpc)(uintptr_t)0x45C320)(map, record[5]);
        if (!npc || *(const uintptr_t *)npc != 0x48A5FC) continue;
        int basic = *(const int *)(npc + 0x44);
        int flow = *(const int *)(npc + 0x50) ? 0 : *(const int *)(npc + 0x48);
        unsigned unlock = *(const unsigned short *)(record + 38);
        BOOL available = event == (unsigned)(flow ? flow : basic) && unlock <= year;
        if (!available && unlock <= year) continue;
        unsigned future = available ? 0 : unlock;
        if (!best.lead || future < best.year || (future == best.year && lead < best.lead))
            best = (QuestTarget){lead, npc, future};
    }
    return best;
}

static BOOL stage_world_panel(Panel *panel, int y)
{
    if (!wide_mode || !show_hud || GAME_SCENE != CAMPAIGN_SCENE || !world_draw_active()
        || world_render_failed) return FALSE;
    int right = !wide_frame ? WORLD_RIGHT : overflow_ready ? WIDE_RIGHT : NATIVE_WIDE_RIGHT;
    int x = WORLD_TOP + (right - WORLD_RIGHT) / 2;
    y = clamp(y, WORLD_TOP, BELT_Y);
    *panel = (Panel){WORLD_TOP, y, WORLD_RIGHT - WORLD_TOP, OVERLAY_BOTTOM - y, x, y};
    if (!wide_frame) return TRUE;

    DDSURFACEDESC pixels = {0};
    if (!lock_world_surface(GAME_VIDEO->back, &pixels, 0)) return FALSE;
    for (int line = 0; line < VIEW_HEIGHT; ++line)
        memcpy(quest_world[line], row(pixels.lpSurface, pixels.lPitch, WORLD_TOP + line)
            + WORLD_TOP * 2, sizeof(quest_world[0]));
    DDSURFACEDESC canvas = {0};
    canvas.lpSurface = quest_canvas;
    canvas.lPitch = sizeof(quest_canvas[0]);
    CameraView view = overflow_ready ? frame_view : (CameraView){NATIVE_WIDE_COLUMNS, VIEW_HEIGHT};
    paint_world(&pixels, &canvas, right, view, TRUE, VIEW_HEIGHT);
    for (int line = y; line < BELT_Y; ++line)
        memcpy(row(pixels.lpSurface, pixels.lPitch, line) + WORLD_TOP * 2,
            quest_canvas[line] + x, sizeof(quest_world[0]));
    IDirectDrawSurface3_Unlock(GAME_VIDEO->back, NULL);
    return TRUE;
}

__declspec(dllexport) void WINAPI HudQuest(const unsigned char *journal)
{
    quest_ready = stage_world_panel(&quest_panel, (12 - *(const int *)(journal + 0x10)) * TILE);
    if (quest_ready) {
        quest_marker_y = 0;
        quest_empty = !journal[0x0C] && !quest_in_log(0);
        if (!journal[0x0C]) {
            for (int slot = 0; slot < 10; ++slot) {
                if (*(const int *)(uintptr_t)(0x4BC368 + slot * 0x238)) {
                    quest_marker_y = quest_panel.y + 77 + slot * TILE;
                    break;
                }
            }
        }
    }
    ((WorldDraw)(uintptr_t)0x47D470)(journal);
}

__declspec(dllexport) void WINAPI HudDialogue(const unsigned char *dialogue)
{
    int top = (12 - *(const int *)(dialogue + 0x30)) * TILE;
    if (*(const int *)(dialogue + 0x34)) top -= 78;
    dialogue_ready = stage_world_panel(&dialogue_panel, top);
    ((WorldDraw)(uintptr_t)0x47B410)(dialogue);
}

static void copy_panel(const DDSURFACEDESC *src, const DDSURFACEDESC *dst, const Panel *p)
{
    for (int i = 0; i < p->height; ++i) {
        const unsigned char *from = row(src->lpSurface, src->lPitch, p->source_y + i) + 2 * p->source_x;
        unsigned char *to = row(dst->lpSurface, dst->lPitch, p->y + i) + 2 * p->x;
        memcpy(to, from, (size_t)p->width * 2);
    }
}

static BOOL on_orb(int x, int y)
{
    int dx = x - 17;
    int dy = y - 17;
    return dx * dx + dy * dy <= 18 * 18;
}

static void copy_orb(const DDSURFACEDESC *dst, const Panel *p, const HudSprite pixels)
{
    for (int y = 0; y < SPRITE_SIZE; ++y) {
        uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, p->y + 1 + y) + p->x + 2;
        for (int x = 0; x < SPRITE_SIZE; ++x) {
            int dx = x - 16, dy = y - 16;
            BOOL bezel = dx * dx + dy * dy > 13 * 13;
            int sx = bezel ? y : x;
            int sy = bezel ? SPRITE_SIZE - 1 - x : y;
            uint16_t color = pixels[sy][sx];
            if (color != SPRITE_TRANSPARENT) to[x] = color;
        }
    }
}

static uint16_t dim_pixel(uint16_t value)
{
    return (value >> 1) & 0x7BEF;
}

static void hud_pixel(const DDSURFACEDESC *dst, int x, int y, uint16_t color)
{
    ((uint16_t *)row(dst->lpSurface, dst->lPitch, y))[x] = color;
}

static BOOL click_marker_enabled;
static struct { const unsigned char *map; int x, y; DWORD start; } click_marker;

static void paint_click_marker(const DDSURFACEDESC *dst)
{
    const unsigned char *map = GAME_MAP;
    DWORD age = GetTickCount() - click_marker.start;
    if (!click_marker_enabled || !map || map != click_marker.map || age >= 520) return;
    BOOL rgb555 = dst->ddpfPixelFormat.dwRBitMask == 0x7C00;
    int width = presented_world_right - WORLD_TOP;
    int cx = WORLD_TOP + (click_marker.x - *(const int *)(map + 0x14) * TILE) * width
        / (displayed_view.columns * TILE);
    int cy = WORLD_TOP + (click_marker.y - *(const int *)(map + 0x18) * TILE) * VIEW_HEIGHT
        / displayed_view.height;
    int radius = 20 - (int)age * 14 / 520;
    uint16_t bright = rgb555 ? 0x7FFF : 0xFFFF, gold = rgb555 ? 0x7F40 : 0xFEA0;
    for (int step = 0; step < 72; ++step) {
        double angle = step * 3.14159265 / 36;
        for (int ring = 0; ring < 2; ++ring) {
            int r = radius - ring;
            int x = cx + (int)(cos(angle) * r), y = cy + (int)(sin(angle) * r * 0.55);
            if (x >= WORLD_TOP && x < presented_world_right - 7 && y >= WORLD_TOP && y < BELT_Y)
                hud_pixel(dst, x, y, ring ? gold : bright);
        }
    }
}

static void paint_damage(const DDSURFACEDESC *dst)
{
    static const unsigned char digits[10][5] = {
        {7, 5, 5, 5, 7}, {2, 6, 2, 2, 7}, {7, 1, 7, 4, 7}, {7, 1, 7, 1, 7},
        {5, 5, 7, 1, 1}, {7, 4, 7, 1, 7}, {7, 4, 7, 5, 7}, {7, 1, 2, 2, 2},
        {7, 5, 7, 5, 7}, {7, 5, 7, 1, 7},
    };
    BOOL rgb555 = dst->ddpfPixelFormat.dwRBitMask == 0x7C00;
    const uint16_t colors[] = {rgb555 ? 0x7FFF : 0xFFFF, rgb555 ? 0x7CC6 : 0xF986, rgb555 ? 0x263F : 0x443F};
    const unsigned char *map = GAME_MAP;
    if (!map) return;
    int camera_x = *(const int *)(map + 0x14) * TILE;
    int camera_y = *(const int *)(map + 0x18) * TILE;
    DWORD now = GetTickCount();
    for (unsigned i = 0; i < COUNT(damage_numbers); ++i) {
        const DamageNumber *hit = &damage_numbers[i];
        DWORD age = now - hit->start;
        if (hit->map != map || age >= 750) continue;
        int x = WORLD_TOP + (hit->x - camera_x) * (presented_world_right - WORLD_TOP) / (displayed_view.columns * TILE);
        int y = WORLD_TOP + (hit->y - camera_y) * VIEW_HEIGHT / displayed_view.height - 24 - (int)age / 26;
        unsigned amount = (unsigned)hit->amount;
        unsigned char text[10];
        int count = 0;
        do { text[count++] = amount % 10; } while ((amount /= 10) != 0);
        x -= count * 4;
        for (int outline = 1; outline >= 0; --outline) {
            for (int digit = 0; digit < count; ++digit) {
                for (int j = 0; j < 5; ++j) {
                    for (int k = 0; k < 3; ++k) {
                        if (!(digits[text[count - 1 - digit]][j] & (4 >> k))) continue;
                        int px = x + digit * 8 + k * 2, py = y + j * 2;
                        for (int dy = -outline; dy < 2 + outline; ++dy) {
                            for (int dx = -outline; dx < 2 + outline; ++dx) {
                                if (px + dx >= WORLD_TOP && px + dx < presented_world_right - 7
                                    && py + dy >= WORLD_TOP && py + dy < BELT_Y)
                                    hud_pixel(dst, px + dx, py + dy, outline ? 0 : colors[hit->kind]);
                            }
                        }
                    }
                }
            }
        }
    }
}

static void paint_npc_markers(const DDSURFACEDESC *dst, const MapView *view)
{
    const unsigned char *map = GAME_MAP;
    if (!map) return;
    int left = NATIVE_MAP_X + (NATIVE_MAP_SIZE - map[0x10]) / 2;
    int top = NATIVE_MAP_Y + (NATIVE_MAP_SIZE - map[0x11]) / 2;
    const MapRect *from = &view->source, *to = &view->target;
    // Map objects of type 4 use this list. Only entries with an event can
    // be interacted with, while the remaining entries provide scenery.
    const unsigned char *npc = *(const unsigned char * const *)(map + 0x34F60);
    QuestTarget target = next_quest_lead();
    for (; npc; npc = *(const unsigned char * const *)(npc + 0x2C)) {
        if (*(const uintptr_t *)npc != 0x48A5FC) continue;
        int event = *(const int *)(npc + 0x44), alternate = *(const int *)(npc + 0x48);
        if (!event && !alternate) continue;
        int x = *(const int *)(npc + 0x0C), y = *(const int *)(npc + 0x10);
        int mx = left + x / TILE, my = top + y / TILE;
        if (!in_rect(from, mx, my)) continue;
        int dx = to->x + (2 * (mx - from->x) + 1) * to->width / (2 * from->width);
        int dy = to->y + (2 * (my - from->y) + 1) * to->height / (2 * from->height);
        BOOL quest = npc == target.npc && !target.year;
        if (!explored(map, x, y) && (!quest || *(const DWORD *)(map + 0x14D00))) continue;
        int radius = quest ? 4 : 3;
        for (int j = -radius; j <= radius; ++j) {
            for (int i = -radius; i <= radius; ++i) {
                int dist = abs(i) + abs(j);
                if (dist <= radius && in_rect(to, dx + i, dy + j)) {
                    uint16_t color = dist == radius ? 0x08C9 : quest ? 0xFEA1 : 0x47FF;
                    if (quest && i == 0 && j >= -2 && j <= 2 && j != 1) color = 0x18A1;
                    hud_pixel(dst, dx + i, dy + j, color);
                }
            }
        }
    }
}

static void collect_loot(void)
{
    memset(loot_tiles, 0, sizeof(loot_tiles));
    const unsigned char *map = GAME_MAP;
    if (loot_map != map) loot_label_count = 0;
    loot_map = map;
    if (!map) return;
    const unsigned char * const *rows = (const unsigned char * const *)(map + 0x34D54);
    typedef int (__thiscall *ItemVisible)(const unsigned char *);
    ItemVisible visible = (ItemVisible)(uintptr_t)0x447E00;
    for (int y = 0; y < min(map[0x11], 128); ++y) {
        for (const unsigned char *item = rows[y]; item; item = *(const unsigned char * const *)(item + 0x2C)) {
            uintptr_t type = *(const uintptr_t *)item;
            if ((type != 0x48A430 && type != 0x48A45C) || !visible(item)) continue;
            int x = *(const int *)(item + 0x14);
            if (x < 0 || x >= min(map[0x10], 128)) continue;
            LootTile *tile = &loot_tiles[y][x];
            if (!tile->item) tile->item = item;
            ++tile->count;
        }
    }
}

static void paint_loot_world_markers(const DDSURFACEDESC *dst)
{
    const unsigned char *map = GAME_MAP;
    if (!map || map != loot_map) return;
    int x0 = *(const int *)(map + 0x14), y0 = *(const int *)(map + 0x18);
    int width = presented_world_right - WORLD_TOP;
    int rows = (displayed_view.height + TILE - 1) / TILE;
    BOOL outline = loot_held();
    for (int y = y0; y < min(map[0x11], min(128, y0 + rows)); ++y) {
        for (int x = x0; x < min(map[0x10], min(128, x0 + displayed_view.columns)); ++x) {
            if (!loot_tiles[y][x].item) continue;
            int sx = WORLD_TOP + ((x - x0) * TILE + TILE / 2) * width
                / (displayed_view.columns * TILE);
            int sy = WORLD_TOP + ((y - y0) * TILE + TILE / 2) * VIEW_HEIGHT
                / displayed_view.height - 13;
            if (sx < WORLD_TOP + 5 || sx >= presented_world_right - 12
                || sy < WORLD_TOP + 5 || sy >= BELT_Y - 7) continue;
            if (outline) {
                for (int j = -5; j <= 5; ++j) {
                    for (int i = -5; i <= 5; ++i) {
                        int distance = abs(i) + abs(j);
                        if (distance == 5 || distance == 6)
                            hud_pixel(dst, sx + i, sy + j, 0xFFFF);
                    }
                }
            }
            // A small item-shaped glint is composited after actors/corpses, so a
            // newly dropped item is immediately visible instead of being buried.
            for (int j = -3; j <= 3; ++j) {
                int radius = 3 - abs(j);
                for (int i = -radius; i <= radius; ++i)
                    hud_pixel(dst, sx + i, sy + j, i == 0 ? 0xFFFF : 0xF59F);
            }
            hud_pixel(dst, sx - 1, sy + 4, 0xA2B7);
            hud_pixel(dst, sx, sy + 4, 0xF59F);
            hud_pixel(dst, sx + 1, sy + 4, 0xA2B7);
        }
    }
}

static void paint_loot_markers(const DDSURFACEDESC *dst, const MapView *view)
{
    const unsigned char *map = GAME_MAP;
    if (!map) return;
    const MapRect *from = &view->source, *to = &view->target;
    int left = NATIVE_MAP_X + (NATIVE_MAP_SIZE - map[0x10]) / 2;
    int top = NATIVE_MAP_Y + (NATIVE_MAP_SIZE - map[0x11]) / 2;
    for (int my = 0; my < from->height; ++my) {
        int y = from->y + my - top;
        for (int mx = 0; mx < from->width; ++mx) {
            int x = from->x + mx - left;
            if (x < 0 || x >= 128 || y < 0 || y >= 128 || !loot_tiles[y][x].item) continue;
            int dx = to->x + (2 * mx + 1) * to->width / (2 * from->width);
            int dy = to->y + (2 * my + 1) * to->height / (2 * from->height);
            for (int j = -1; j <= 1; ++j)
                for (int i = -1; i <= 1; ++i)
                    if (in_rect(to, dx + i, dy + j)) hud_pixel(dst, dx + i, dy + j,
                        i == 0 && j == 0 ? 0xF59F : 0xA2B7);
        }
    }
}

static Panel placed_panel(unsigned index);

static int hovered_orb(void)
{
    for (unsigned i = NAV_START; i < COUNT(hud); ++i) {
        Panel p = placed_panel(i);
        int x = pointer_x - p.x, y = pointer_y - p.y;
        if (x >= 0 && x < p.width && y >= 0 && y < p.height && on_orb(x, y))
            return (int)(i - NAV_START);
    }
    return -1;
}

static BOOL over_world(int x, int y)
{
    return presented_wide_world && !presented_quest && x >= WORLD_TOP
        && x < presented_world_right && y >= WORLD_TOP && y < BELT_Y
        && (y >= 54 || x >= WORLD_RIGHT)
        && !(presented_map.target.width && in_rect(&map_frame, x, y));
}

__declspec(dllexport) const unsigned char *WINAPI HudQuickItem(void)
{
    int x = pointer_x, y = pointer_y;
    const unsigned char *view = GAME_VIEW;
    if (!wide_frame || !presented_hud || presented_quest || !world_view_active()
        || !*(const int *)(view + 0x90) || map_dragging || wide_drag || drag_buttons
        || x < QUICK_FIRST_X || x >= QUICK_FIRST_X + QUICK_SLOTS * QUICK_SLOT_WIDTH
        || y < 0 || y >= QUICK_BOTTOM
        || (presented_map.target.width && in_rect(&map_frame, x, y))) return NULL;
    const unsigned char *actor = *(const unsigned char * const *)(view + 0x70);
    if (!actor) return NULL;
    unsigned slot = (x - QUICK_FIRST_X) / QUICK_SLOT_WIDTH;
    const unsigned char *item = *(const unsigned char * const *)(actor + 0x2DC + slot * 4);
    quick_item_hint = item != NULL;
    return item;
}

static BOOL loot_under_pointer(int x, int y, POINT *tile)
{
    const unsigned char *map = GAME_MAP;
    if (!map || map != loot_map || !world_view_active() || !over_world(x, y) || hovered_orb() >= 0) return FALSE;
    int column = *(const int *)(map + 0x14) + (x - WORLD_TOP) * displayed_view.columns
        / (presented_world_right - WORLD_TOP);
    int line = *(const int *)(map + 0x18) + (y - WORLD_TOP) * displayed_view.height / (VIEW_HEIGHT * TILE);
    if (column < 0 || column >= 128 || line < 0 || line >= 128 || !loot_tiles[line][column].item) return FALSE;
    *tile = (POINT){column, line};
    return TRUE;
}

static BOOL loot_at_label(int x, int y, POINT *tile)
{
    POINT point = {x, y};
    for (unsigned i = 0; i < loot_label_count; ++i) {
        const LootLabel *label = &loot_labels[i];
        if (!PtInRect(&label->rect, point)) continue;
        *tile = label->tile;
        return TRUE;
    }
    return FALSE;
}

static void paint_loot_names(HDC dc, const RECT *hint_box)
{
    POINT target = {-1, -1};
    BOOL all = loot_held();
    BOOL found = loot_at_label(pointer_x, pointer_y, &target)
        || loot_under_pointer(pointer_x, pointer_y, &target);
    loot_label_count = 0;
    if (!presented_wide_world || presented_quest || !world_view_active() || (!found && !all)) return;
    const unsigned char *map = GAME_MAP;
    if (!map || map != loot_map) return;
    static HFONT font;
    if (!font) font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = font ? SelectObject(dc, font) : NULL;
    int old_mode = SetBkMode(dc, OPAQUE);
    COLORREF old_color = GetTextColor(dc);
    COLORREF old_background = GetBkColor(dc);
    int x0 = *(const int *)(map + 0x14), y0 = *(const int *)(map + 0x18);
    int width = presented_world_right - WORLD_TOP;
    RECT map_box = {map_frame.x, map_frame.y, map_frame.x + map_frame.width, map_frame.y + map_frame.height};
    for (int y = y0; y < min(map[0x11], min(128, y0 + (displayed_view.height + TILE - 1) / TILE)); ++y) {
        for (int x = x0; x < min(map[0x10], min(128, x0 + displayed_view.columns)); ++x) {
            const LootTile *tile = &loot_tiles[y][x];
            BOOL hovered_tile = found && target.x == x && target.y == y;
            if ((!all && !hovered_tile) || !tile->item || loot_label_count >= COUNT(loot_labels)) continue;
            int id = *(const int *)(tile->item + 0x44);
            if (id < 0 || id >= 256) continue;
            BOOL weapon = *(const uintptr_t *)tile->item == 0x48A430;
            const char *name = (const char *)(uintptr_t)(weapon ? 0x494100 + id * 124 : 0x49136A + id * 112);
            int bytes = (int)strnlen(name, weapon ? 20 : 30);
            WCHAR text[80];
            int count = bytes ? MultiByteToWideChar(item_code_page, 0, name, bytes, text, 56) : 0;
            if (!count) continue;
            if (tile->count > 1) count += wsprintfW(text + count, L" (+%u)", tile->count - 1);
            SIZE extent;
            if (!GetTextExtentPoint32W(dc, text, count, &extent) || extent.cx + 8 >= width) continue;
            int sx = WORLD_TOP + ((x - x0) * TILE + TILE / 2) * width / (displayed_view.columns * TILE);
            int sy = WORLD_TOP + ((y - y0) * TILE + TILE / 2) * VIEW_HEIGHT / displayed_view.height;
            if (sy >= BELT_Y) continue;
            int left = clamp(sx - extent.cx / 2, WORLD_TOP, presented_world_right - extent.cx - 12);
            int bottom = max(sy - 5, WORLD_TOP + extent.cy + 4);
            RECT box = { left, bottom - extent.cy - 4, left + extent.cx + 8, bottom };
            for (int attempt = 0; attempt < 5; ++attempt) {
                RECT joint;
                BOOL overlap = IntersectRect(&joint, &box, hint_box)
                    || (presented_map.target.width && IntersectRect(&joint, &box, &map_box));
                for (unsigned i = 0; !overlap && i < loot_label_count; ++i)
                    overlap = IntersectRect(&joint, &box, &loot_labels[i].rect);
                if (overlap) {
                    OffsetRect(&box, 0, box.top - extent.cy - 6 >= WORLD_TOP ? -extent.cy - 6 : extent.cy + 6);
                    continue;
                }
                if (box.top < WORLD_TOP || box.bottom >= BELT_Y) break;
                POINT point = {pointer_x, pointer_y};
                BOOL highlighted = PtInRect(&box, point) || hovered_tile;
                SetTextColor(dc, highlighted ? RGB(237, 249, 255) : RGB(255, 232, 171));
                SetBkColor(dc, highlighted ? RGB(23, 76, 154) : RGB(28, 20, 30));
                if (ExtTextOutW(dc, box.left + 4, box.top + 2, ETO_OPAQUE, &box, text, count, NULL))
                    loot_labels[loot_label_count++] = (LootLabel){box, {x, y}};
                break;
            }
        }
    }
    SetBkColor(dc, old_background);
    SetTextColor(dc, old_color);
    SetBkMode(dc, old_mode);
    if (old_font) SelectObject(dc, old_font);
}

// Native text is ANSI bytes in the game's language (cp949/cp950 for Korean/Chinese). Convert it to
// UTF-16 so GDI draws it correctly whatever the system code page is.
static int hint_to_wide(const NativeHint *hint, WCHAR *out, int capacity)
{
    const WCHAR *translated = phrase_for(game_messages, COUNT(game_messages), hint->text, hint->count);
    if (translated) {
        int length = lstrlenW(translated);
        if (length >= capacity) length = capacity - 1;
        memcpy(out, translated, (size_t)length * sizeof(WCHAR));
        return length;
    }
    return hint->count > 0 ? MultiByteToWideChar(item_code_page, 0, hint->text, hint->count, out, capacity) : 0;
}

static void paint_native_hint(HDC dc, const NativeHint *hint, RECT *occupied)
{
    WCHAR wide[192];
    int chars = hint_to_wide(hint, wide, COUNT(wide));
    HGDIOBJ previous = hint->font ? SelectObject(dc, hint->font) : NULL;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, hint->color);
    SIZE size;
    if (chars && GetTextExtentPoint32W(dc, wide, chars, &size)
        && TextOutW(dc, hint->x, hint->y, wide, chars)) {
        RECT box = {hint->x, hint->y, hint->x + size.cx, hint->y + size.cy};
        UnionRect(occupied, occupied, &box);
    }
    if (previous) SelectObject(dc, previous);
}

static void paint_actor_hints(HDC dc, RECT *occupied)
{
    static HFONT font;
    if (!font) font = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = font ? SelectObject(dc, font) : NULL;
    int old_mode = SetBkMode(dc, OPAQUE);
    COLORREF old_background = SetBkColor(dc, RGB(24, 20, 18));
    COLORREF old_color = GetTextColor(dc);
    int x = WORLD_TOP + 6, y = BELT_Y - 25;
    RECT combined = {0};
    int piece_index = 0;
    for (unsigned i = 0; i < world_hint_count; ++i) {
        const NativeHint *hint = &world_hint[i];
        SIZE size;
        WCHAR wide[192];
        int chars = hint_to_wide(hint, wide, COUNT(wide));
        if (!chars || !GetTextExtentPoint32W(dc, wide, chars, &size)) continue;
        int pad = hint->piece ? 0 : 5;
        if (hint->piece) {
            ++piece_index;
            if (piece_index == 1) x += 6;
        } else piece_index = 0;
        int width = min(size.cx + 2 * pad + (hint->piece ? 6 : 0), presented_world_right - x - 4);
        if (width <= 6) break;
        RECT box = {x, y, x + width, min(BELT_Y - 3, y + size.cy + 5)};
        SetTextColor(dc, hint->color);
        if (ExtTextOutW(dc, x + pad, y + 2, ETO_OPAQUE | ETO_CLIPPED,
                &box, wide, chars, NULL)) {
            UnionRect(&combined, &combined, &box);
            x = box.right + (hint->piece ? 0 : 4);
        }
    }
    if (!IsRectEmpty(&combined)) UnionRect(occupied, occupied, &combined);
    SetTextColor(dc, old_color);
    SetBkColor(dc, old_background);
    SetBkMode(dc, old_mode);
    if (old_font) SelectObject(dc, old_font);
}

static void paint_name_hint(HDC dc, const char *label, int x, int y, BOOL align_right, RECT *occupied)
{
    static HFONT font;
    if (!font) font = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = font ? SelectObject(dc, font) : NULL;
    SetTextColor(dc, RGB(249, 237, 214));
    SetBkColor(dc, RGB(29, 24, 20));
    SetBkMode(dc, OPAQUE);
    WCHAR text[160];
    int bytes = (int)strnlen(label, 159);
    int chars = bytes ? MultiByteToWideChar(item_code_page, 0, label, bytes, text, COUNT(text)) : 0;
    if (chars && hint_action >= 0 && native_user[hint_action] != native_default[hint_action]) {
        // The tooltip ends with the key in parentheses, "CHARACTER (C)": show the key now in use.
        int open = chars - 1;
        while (open > 0 && text[open] != L'(' && text[open] != 0xFF08) --open;
        WCHAR name[24] = L"";
        int length = native_user[hint_action] ? native_key_name(native_user[hint_action], name, COUNT(name)) : 0;
        if (open > 0 && length > 0 && open + 1 + length + 1 < (int)COUNT(text)) {
            memcpy(text + open + 1, name, (size_t)length * sizeof(WCHAR));
            text[open + 1 + length] = L')';
            chars = open + 2 + length;
        }
    }
    if (chars && hud_language == CHINESE) {
        // Some names reach the HUD in English; show the Chinese name when we have one.
        static const WCHAR *const names[][2] = {
            {L"Blacksmith", L"鐵匠"}, {L"Shopkeeper", L"店主"}, {L"Item Merchant", L"道具商人"},
            {L"Village Chief", L"村長"}, {L"Witch", L"女巫"}, {L"Jeweler", L"珠寶商"}, {L"Trainer", L"教官"},
            {L"Retired General", L"退役將軍"}, {L"White Lady", L"白衣夫人"},
            {L"Eastern Demon", L"東方惡魔"}, {L"Western Demon", L"西方惡魔"},
        };
        for (unsigned n = 0; n < COUNT(names); ++n)
            if (chars == lstrlenW(names[n][0]) && !wcsncmp(text, names[n][0], (size_t)chars)) {
                chars = lstrlenW(names[n][1]);
                wcsncpy(text, names[n][1], COUNT(text) - 1);
                break;
            }
    }
    SIZE size;
    if (chars && GetTextExtentPoint32W(dc, text, chars, &size)) {
        int width = min(size.cx + 12, presented_world_right - WORLD_TOP - 8);
        int left = clamp(x - (align_right ? width : width / 2), WORLD_TOP + 4,
            presented_world_right - width - 4);
        int top = clamp(y - (size.cy + 6) / 2, WORLD_TOP, BELT_Y - size.cy - 6);
        RECT box = {left, top, left + width, top + size.cy + 6};
        if (ExtTextOutW(dc, left + 6, top + 3, ETO_OPAQUE | ETO_CLIPPED, &box, text, chars, NULL))
            UnionRect(occupied, occupied, &box);
    }
    if (old_font) SelectObject(dc, old_font);
}

static void paint_hover_hint(HDC dc, RECT *occupied)
{
    static const unsigned menu_ids[] = {12, 13, 14, 15, 16};
    typedef const char *(__cdecl *GameString)(unsigned);
    int orb = hovered_orb();
    if (orb >= 0) {
        Panel button = placed_panel(NAV_START + orb);
        const char *label = ((GameString)(uintptr_t)0x47E180)(menu_ids[orb]);
        static const int orb_actions[] = {11, 12, 13, 14, 0};   // character, items, quests, map, menu
        hint_action = orb < (int)COUNT(orb_actions) ? orb_actions[orb] : -1;
        paint_name_hint(dc, label, button.x - 7, button.y + button.height / 2, TRUE, occupied);
        hint_action = -1;
    } else if (world_hint_count && (quick_item_hint || over_world(pointer_x, pointer_y))) {
        const unsigned char *map = GAME_MAP;
        if (npc_name_index >= 0 && map) {
            int x = npc_anchor.x - *(const int *)(map + 0x14) * TILE;
            int y = npc_anchor.y - *(const int *)(map + 0x18) * TILE - 58;
            x = WORLD_TOP + x * (presented_world_right - WORLD_TOP) / (displayed_view.columns * TILE);
            y = WORLD_TOP + y * VIEW_HEIGHT / displayed_view.height;
            paint_name_hint(dc, world_hint[npc_name_index].text, x, y, FALSE, occupied);
        } else if (!quick_item_hint && GAME_VIEW
            && *(const unsigned char * const *)(GAME_VIEW + 0xD0)) {
            paint_actor_hints(dc, occupied);
        } else {
            for (unsigned i = 0; i < world_hint_count; ++i) paint_native_hint(dc, &world_hint[i], occupied);
        }
    } else if (notice_hint.count) {
        paint_native_hint(dc, &notice_hint, occupied);
    }
}

static void paint_quest_lead(HDC dc)
{
    static const WCHAR *const next[] = {L"Como começar uma missão", L"Start a quest", L"퀘스트 시작하기", L"如何開始任務"};
    static const WCHAR *const target[] = {L"Fale com", L"Talk to", L"대화 상대", L"對話對象"};
    static const WCHAR *const empty[] = {L"Sem missões ativas", L"No active quests", L"진행 중인 퀘스트 없음", L"沒有進行中的任務"};
    static const WCHAR *const upcoming[] = {
        L"Próxima oferta conhecida", L"Next known quest offer", L"다음 퀘스트 제안", L"下一個已知任務",
    };
    static const WCHAR *const explore[] = {
        L"Nenhuma oferta conhecida neste mapa. Procure uma missão nos diálogos das vilas.",
        L"No known offer on this map. Look for quests in village conversations.",
        L"이 지도에는 알려진 의뢰가 없습니다. 마을에서 대화해 보세요.",
        L"此地圖沒有已知的委託。請在村莊的對話中尋找任務。",
    };
    static HFONT title_font, body_font;
    if (!title_font) title_font = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, 0, L"Segoe UI");
    if (!body_font) body_font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, 0, L"Segoe UI");

    QuestTarget suggestion = next_quest_lead();
    const QuestLead *lead = suggestion.lead;
    unsigned lang = hud_language;
    int left = quest_panel.x + 30, right = quest_panel.x + quest_panel.width - 30;
    int top = quest_panel.y + 64, height = suggestion.year ? 158 : lead ? 122 : 78;
    if (top + height > BELT_Y - 10) top = max(WORLD_TOP + 6, quest_panel.y - height - 8);
    RECT card = {left, top, right, top + height};
    HBRUSH brush = (HBRUSH)GetStockObject(DC_BRUSH);
    SetDCBrushColor(dc, RGB(36, 28, 24));
    FillRect(dc, &card, brush);
    SetDCBrushColor(dc, RGB(147, 104, 60));
    FrameRect(dc, &card, brush);

    const WCHAR *heading = !lead ? empty[lang] : suggestion.year ? upcoming[lang] : next[lang];
    SetBkMode(dc, TRANSPARENT);
    HGDIOBJ old_font = title_font ? SelectObject(dc, title_font) : NULL;
    SetTextColor(dc, RGB(251, 210, 135));
    TextOutW(dc, left + 13, top + 8, heading, lstrlenW(heading));

    if (body_font) SelectObject(dc, body_font);
    SetTextColor(dc, RGB(246, 234, 216));
    if (lead) {
        WCHAR name[96];
        if (suggestion.year) {
            static const WCHAR *const year_format[] = {
                L"Disponível a partir do ano %u · %ls",
                L"Available from year %u · %ls",
                L"%u년부터 가능 · %ls",
                L"%u 年起可接 · %ls",
            };
            swprintf(name, COUNT(name), year_format[lang], suggestion.year, lead->npc[lang]);
        } else {
            swprintf(name, COUNT(name), L"%ls %ls", target[lang], lead->npc[lang]);
        }
        TextOutW(dc, left + 13, top + 34, name, lstrlenW(name));
    }
    const WCHAR *detail = lead ? lead->step[lang] : explore[lang];
    RECT text = {left + 13, top + (lead ? 54 : 34), right - 13,
        card.bottom - (suggestion.year ? 67 : lead ? 28 : 6)};
    DrawTextW(dc, detail, -1, &text, DT_NOPREFIX | DT_WORDBREAK);
    if (lead) {
        static const WCHAR *const marker[] = {
            L"Procure o ! amarelo. Aceite para registrar a missão.",
            L"Find the yellow !. Accept to add the quest.",
            L"노란색 ! 표시를 찾으세요. 수락하면 일지에 추가됩니다.",
            L"尋找黃色的 !,接受後會加入任務日誌。",
        };
        static const WCHAR *const waiting[] = {
            L"Jogue com a partida despausada. A barra ao lado do ano mostra o progresso.",
            L"Keep playing with the game unpaused. The bar beside the year shows your progress.",
            L"일시 정지를 풀고 플레이하세요. 연도 옆 막대가 진행 상황을 보여 줍니다.",
            L"請取消暫停繼續遊玩。年份旁的進度條會顯示進度。",
        };
        SetTextColor(dc, RGB(222, 175, 113));
        if (suggestion.year) {
            RECT footer = {left + 13, card.bottom - 62, right - 13, card.bottom - 8};
            DrawTextW(dc, waiting[lang], -1, &footer, DT_NOPREFIX | DT_WORDBREAK);
        } else {
            TextOutW(dc, left + 13, card.bottom - 24, marker[lang], lstrlenW(marker[lang]));
        }
    }
    if (old_font) SelectObject(dc, old_font);
}

static void paint_drop_hint(HDC dc)
{
    static const WCHAR *const text[] = {L"Soltar item aqui", L"Drop item here", L"여기에 아이템 버리기", L"在此丟棄物品"};
    static HFONT font;
    if (!font) font = CreateFontW(-15, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ old_font = font ? SelectObject(dc, font) : NULL;
    const WCHAR *label = text[hud_language];
    SIZE size;
    if (GetTextExtentPoint32W(dc, label, lstrlenW(label), &size)) {
        int left = clamp(pointer_x + 22, 4, WIDE_WIDTH - size.cx - 12);
        int top = clamp(pointer_y + 26, 4, HEIGHT - size.cy - 10);
        RECT box = {left, top, left + size.cx + 8, top + size.cy + 4};
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(23, 76, 154));
        SetTextColor(dc, RGB(237, 249, 255));
        ExtTextOutW(dc, left + 4, top + 2, ETO_OPAQUE, &box, label, lstrlenW(label), NULL);
    }
    if (old_font) SelectObject(dc, old_font);
}

static void fill_rect(HDC dc, int left, int top, int right, int bottom, COLORREF color)
{
    RECT box = {left, top, right, bottom};
    SetDCBrushColor(dc, color);
    FillRect(dc, &box, (HBRUSH)GetStockObject(DC_BRUSH));
}

static void outlined_text(HDC dc, int x, int y, const WCHAR *text, int count, COLORREF color)
{
    SetTextColor(dc, RGB(0, 0, 0));
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
            if (dx || dy) ExtTextOutW(dc, x + dx, y + dy, 0, NULL, text, count, NULL);
    SetTextColor(dc, color);
    ExtTextOutW(dc, x, y, 0, NULL, text, count, NULL);
}

// Other players' heroes (a0 in the party list). Their maximum life is not
// replicated, so the highest value seen for each player stands in for it.
static int ally_peak(const unsigned char *actor)
{
    static struct { DWORD owner; int peak; } peaks[24];
    int life = *(const int *)(actor + 0x810), top = *(const int *)(actor + 0x7E0);
    DWORD owner = *(const DWORD *)(actor + 0x754);
    unsigned slot = 0;
    while (slot < COUNT(peaks) - 1 && peaks[slot].owner && peaks[slot].owner != owner) ++slot;
    peaks[slot].owner = owner;
    if (top > peaks[slot].peak) peaks[slot].peak = top;
    if (life > peaks[slot].peak) peaks[slot].peak = life;
    return peaks[slot].peak > 0 ? peaks[slot].peak : 1;
}

static COLORREF life_color(int life, int peak)
{
    int percent = life * 100 / peak;
    return percent > 50 ? RGB(64, 200, 64) : percent > 25 ? RGB(230, 190, 40) : RGB(220, 50, 40);
}

static void paint_life_bar(HDC dc, int left, int top, int width, int height, int life, int peak)
{
    int fill = (int)((long long)width * (life > 0 ? life : 0) / peak);
    if (fill > width) fill = width;
    fill_rect(dc, left - 1, top - 1, left + width + 1, top + height + 1, RGB(0, 0, 0));
    fill_rect(dc, left, top, left + width, top + height, RGB(70, 24, 24));
    if (life > 0) fill_rect(dc, left, top, left + (fill > 0 ? fill : 1), top + height, life_color(life, peak));
}

static HFONT ui_font(int height, int weight)
{
    static HFONT fonts[64];
    static int sizes[64];
    for (unsigned i = 0; i < COUNT(fonts); ++i) {
        if (fonts[i] && sizes[i] == height * 1000 + weight) return fonts[i];
        if (!fonts[i]) {
            fonts[i] = CreateFontW(-height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            sizes[i] = height * 1000 + weight;
            return fonts[i];
        }
    }
    return NULL;
}

static int ansi_to_wide(const char *text, int bytes, WCHAR *out, int capacity)
{
    return bytes > 0 ? MultiByteToWideChar(item_code_page, 0, text, bytes, out, capacity) : 0;
}

static void paint_ally_marks(HDC dc)
{
    const unsigned char *map = GAME_MAP;
    if (!map) return;
    int camera_x = *(const int *)(map + 0x14) * TILE, camera_y = *(const int *)(map + 0x18) * TILE;
    int width = presented_world_right - WORLD_TOP, span = displayed_view.columns * TILE;
    // Everything follows the zoom, down to a small readable minimum.
    int name_size = width * 12 / span < 10 ? 10 : 12;
    HGDIOBJ old_font = SelectObject(dc, ui_font(name_size, FW_SEMIBOLD));
    int old_mode = SetBkMode(dc, TRANSPARENT);
    const unsigned char * const *actors = (const unsigned char * const *)(map + 0x14AAC);
    for (unsigned i = 0; i < 20; ++i) {
        const unsigned char *actor = actors[i];
        if (!actor || party_member(actor) || !*(const int *)(actor + 0x40)) continue;
        int life = *(const int *)(actor + 0x810), peak = ally_peak(actor);
        if (life <= 0) continue;
        int x = WORLD_TOP + (*(const int *)(actor + 0x0C) - camera_x) * width / span;
        int y = WORLD_TOP + (*(const int *)(actor + 0x10) - camera_y) * VIEW_HEIGHT / displayed_view.height;
        int bar_width = max(14, 34 * width / span), bar_top = y - 60 * width / span;
        int left = x - bar_width / 2;
        if (left < WORLD_TOP + 2 || left + bar_width > presented_world_right - 9
            || bar_top < WORLD_TOP + 14 || bar_top > BELT_Y - 8) continue;
        paint_life_bar(dc, left, bar_top, bar_width, 4, life, peak);
        WCHAR name[32];
        int chars = ansi_to_wide((const char *)(actor + 0x740), (int)strnlen((const char *)(actor + 0x740), 15), name, 31);
        SIZE size;
        if (chars && GetTextExtentPoint32W(dc, name, chars, &size)) {
            int text_left = clamp(x - size.cx / 2, WORLD_TOP + 2, presented_world_right - size.cx - 10);
            outlined_text(dc, text_left, bar_top - size.cy - 1, name, chars, RGB(255, 244, 214));
        }
    }
    SetBkMode(dc, old_mode);
    if (old_font) SelectObject(dc, old_font);
}

// Chat lines the game keeps in the chat object: five pointers at +0x34 with their
// remaining display time at +0x48. The newest is last. Drawn bottom-up with a size
// that does not depend on the zoom.
static void paint_chat_lines(HDC dc)
{
    const unsigned char *chat = *(const unsigned char * const *)(uintptr_t)0x4998E4;
    if (!chat) return;
    HGDIOBJ old_font = SelectObject(dc, ui_font(16, FW_SEMIBOLD));
    int old_mode = SetBkMode(dc, TRANSPARENT);
    int bottom = GAME_CHAT_OPEN ? BELT_Y - 52 : BELT_Y - 12;
    for (int k = 4; k >= 0; --k) {
        const char *line = *(const char * const *)(chat + 0x34 + 4 * k);
        if (!line || !*(const int *)(chat + 0x48 + 4 * k) || IsBadReadPtr(line, 2)) continue;
        WCHAR text[80];
        int chars = ansi_to_wide(line, (int)strnlen(line, 49), text, 79);
        SIZE size;
        if (!chars || !GetTextExtentPoint32W(dc, text, chars, &size)) continue;
        bottom -= size.cy + 2;
        if (bottom < WORLD_TOP + 24) break;
        outlined_text(dc, WORLD_TOP + 12, bottom, text, chars, RGB(236, 236, 236));
    }
    SetBkMode(dc, old_mode);
    if (old_font) SelectObject(dc, old_font);
}

// The native chat input is parked out of sight, so show what is being typed.
static void paint_chat_input(HDC dc)
{
    const unsigned char *chat = *(const unsigned char * const *)(uintptr_t)0x4998E4;
    HWND edit = chat ? *(HWND const *)(chat + 0x28) : NULL;
    if (!edit || !IsWindow(edit)) return;
    char typed[160] = "";
    GetWindowTextA(edit, typed, sizeof(typed));
    WCHAR text[200];
    int chars = ansi_to_wide(typed, (int)strlen(typed), text, 170);
    if (((GetTickCount() / 500) & 1) && chars < 170) text[chars++] = L'_';
    HGDIOBJ old_font = SelectObject(dc, ui_font(17, FW_NORMAL));
    int world = presented_world_right - WORLD_TOP;
    int box_width = min(560, world - 40);
    RECT box = {WORLD_TOP + (world - box_width) / 2, BELT_Y - 44, 0, BELT_Y - 14};
    box.right = box.left + box_width;
    fill_rect(dc, box.left - 2, box.top - 2, box.right + 2, box.bottom + 2, RGB(200, 170, 90));
    fill_rect(dc, box.left, box.top, box.right, box.bottom, RGB(24, 20, 18));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 244, 214));
    SIZE size;
    int first = 0;
    while (first < chars && GetTextExtentPoint32W(dc, text + first, chars - first, &size)
        && size.cx > box_width - 16) ++first;
    ExtTextOutW(dc, box.left + 8, box.top + 4, ETO_CLIPPED, &box, text + first, chars - first, NULL);
    if (old_font) SelectObject(dc, old_font);
}

// Same look as the dialogue box: a thin steel frame around a darkened, see-through
// background (the darkening is done on the pixels in paint_sidebar).
static RECT party_frame(void)
{
    RECT frame = {hud[2].x - 3, hud[2].y - 4, hud[3].x + hud[3].width + 3, hud[3].y + hud[3].height + 3};
    return frame;
}

// Seven layers, outside in, sampled from the NPC dialogue box: outer steel line, a
// blue-grey step, a dark navy band, a step back up and the inner steel line.
static void paint_steel_frame(HDC dc, const RECT *r)
{
    static const COLORREF layers[7] = {
        RGB(58, 62, 74), RGB(150, 156, 168), RGB(78, 94, 122), RGB(26, 40, 66),
        RGB(14, 20, 34), RGB(56, 74, 112), RGB(100, 112, 146),
    };
    for (int i = 0; i < 7; ++i) {
        // One pixel wide ring per layer, so the darkened interior stays visible.
        fill_rect(dc, r->left + i, r->top + i, r->right - i, r->top + i + 1, layers[i]);
        fill_rect(dc, r->left + i, r->bottom - i - 1, r->right - i, r->bottom - i, layers[i]);
        fill_rect(dc, r->left + i, r->top + i, r->left + i + 1, r->bottom - i, layers[i]);
        fill_rect(dc, r->right - i - 1, r->top + i, r->right - i, r->bottom - i, layers[i]);
    }
}

// Replaces the two native name bars under the portrait: the other players, with
// their life, inside a frame that matches the wooden sidebar. Up to seven rows.
static void paint_party_panel(HDC dc)
{
    static const WCHAR *const title[] = {L"Jogadores", L"Players", L"플레이어", L"玩家"};
    const unsigned char *map = GAME_MAP;
    if (!map) return;
    RECT frame = party_frame();
    paint_steel_frame(dc, &frame);
    HGDIOBJ old_font = SelectObject(dc, ui_font(12, FW_SEMIBOLD));
    int old_mode = SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(226, 226, 226));
    const WCHAR *label = title[hud_language < 4 ? hud_language : 1];
    ExtTextOutW(dc, frame.left + 12, frame.top + 9, 0, NULL, label, lstrlenW(label), NULL);
    fill_rect(dc, frame.left + 10, frame.top + 25, frame.right - 10, frame.top + 26, RGB(78, 94, 122));
    SelectObject(dc, ui_font(11, FW_SEMIBOLD));
    int row = 0, y = frame.top + 31;
    const unsigned char * const *actors = (const unsigned char * const *)(map + 0x14AAC);
    for (unsigned i = 0; i < 20 && row < 7; ++i) {
        const unsigned char *actor = actors[i];
        if (!actor || party_member(actor) || !*(const int *)(actor + 0x40)) continue;
        int life = *(const int *)(actor + 0x810), peak = ally_peak(actor);
        WCHAR name[32];
        int chars = ansi_to_wide((const char *)(actor + 0x740), (int)strnlen((const char *)(actor + 0x740), 15), name, 31);
        SetTextColor(dc, life > 0 ? RGB(236, 236, 236) : RGB(140, 140, 146));
        RECT clip = {frame.left + 12, y, frame.right - 10, y + 14};
        if (chars) ExtTextOutW(dc, clip.left, y, ETO_CLIPPED, &clip, name, chars, NULL);
        paint_life_bar(dc, frame.left + 12, y + 15, frame.right - frame.left - 26, 3, life, peak);
        y += 21;
        ++row;
    }
    SetBkMode(dc, old_mode);
    if (old_font) SelectObject(dc, old_font);
}

static void paint_netplay_overlays(IDirectDrawSurface3 *target)
{
    if (!NET_MULTI || GAME_SCENE != CAMPAIGN_SCENE || !presented_hud) return;
    BOOL world = presented_wide_world && !presented_quest && !presented_dialogue && !presented_menu
        && (world_view_active() || GAME_CHAT_OPEN);
    BOOL side = sidebar_ready;
    if (!world && !side) return;
    HDC dc;
    if (FAILED(IDirectDrawSurface3_GetDC(target, &dc))) return;
    if (side) paint_party_panel(dc);
    if (world) {
        paint_ally_marks(dc);
        paint_chat_lines(dc);
        if (GAME_CHAT_OPEN) paint_chat_input(dc);
    }
    IDirectDrawSurface3_ReleaseDC(target, dc);
}

static void paint_text_overlays(IDirectDrawSurface3 *target)
{
    if (presented_pause) return;
    paint_netplay_overlays(target);
    if (presented_menu && !presented_world_map && pointer_x >= 0 && pointer_x < SIDE_X
        && pointer_y >= 0 && pointer_y < BELT_Y && hovered_orb() < 0) {
        MapRect sheet = {campaign_menu.x, campaign_menu.y, campaign_menu.width, campaign_menu.height};
        HDC dc;
        if (!in_rect(&sheet, pointer_x, pointer_y) && SUCCEEDED(IDirectDrawSurface3_GetDC(target, &dc))) {
            paint_drop_hint(dc);
            IDirectDrawSurface3_ReleaseDC(target, dc);
        }
    }
    if (presented_quest || presented_menu) {
        HDC dc;
        int orb = hovered_orb();
        if (((presented_quest && quest_empty) || orb >= 0)
            && SUCCEEDED(IDirectDrawSurface3_GetDC(target, &dc))) {
            RECT occupied = {0};
            if (presented_quest && quest_empty) paint_quest_lead(dc);
            if (orb >= 0) paint_hover_hint(dc, &occupied);
            IDirectDrawSurface3_ReleaseDC(target, dc);
        }
    }
    if (!presented_hud || presented_quest || !world_view_active()) {
        loot_label_count = 0;
        return;
    }
    POINT tile;
    if (hovered_orb() < 0 && !world_hint_count && !notice_hint.count && !loot_held()
        && !loot_at_label(pointer_x, pointer_y, &tile) && !loot_under_pointer(pointer_x, pointer_y, &tile)) {
        loot_label_count = 0;
        return;
    }
    HDC dc;
    if (FAILED(IDirectDrawSurface3_GetDC(target, &dc))) {
        loot_label_count = 0;
        return;
    }
    RECT occupied = {0};
    paint_hover_hint(dc, &occupied);
    paint_loot_names(dc, &occupied);
    IDirectDrawSurface3_ReleaseDC(target, dc);
}

static void paint_map(const DDSURFACEDESC *src, const DDSURFACEDESC *dst, const MapView *map)
{
    const MapRect *from = &map->source;
    const MapRect *to = &map->target;
    for (int y = 0; y < map_frame.height; ++y) {
        uint16_t *line = (uint16_t *)row(dst->lpSurface, dst->lPitch, map_frame.y + y) + map_frame.x;
        for (int x = 0; x < map_frame.width; ++x) {
            BOOL edge = x < 2 || y < 2 || x >= map_frame.width - 2 || y >= map_frame.height - 2;
            if (edge) line[x] = 0x8A88;
            else if (y < MAP_GRIP_HEIGHT) line[x] = hud_header[y][x % COUNT(hud_header[0])];
            else line[x] = dim_pixel(line[x]);
        }
    }
    for (int y = 0; y < to->height; ++y) {
        int sy = from->y + y * from->height / to->height;
        const uint16_t *source = (const uint16_t *)row(src->lpSurface, src->lPitch, sy) + from->x;
        uint16_t *target = (uint16_t *)row(dst->lpSurface, dst->lPitch, to->y + y) + to->x;
        for (int x = 0; x < to->width; ++x) {
            int column = x * from->width / to->width;
            uint32_t marker = presented_wide_world
                ? map_markers[sy - NATIVE_MAP_Y][from->x + column - NATIVE_MAP_X] : 0;
            if (marker) target[x] = (uint16_t)marker;
            else if (source[column]) target[x] = source[column];
        }
    }
    if (presented_wide_world) {
        paint_loot_markers(dst, map);
        paint_npc_markers(dst, map);
    }
    int button_x = map_frame.x + map_frame.width - (int)COUNT(hud_minimize[0]) - 3;
    int button_y = map_frame.y;
    for (unsigned y = 0; y < COUNT(hud_minimize); ++y) {
        uint16_t *target = (uint16_t *)row(dst->lpSurface, dst->lPitch, button_y + y) + button_x;
        memcpy(target, hud_minimize[y], sizeof(hud_minimize[y]));
    }
}

static BOOL over_map_minimize(int x, int y)
{
    int left = map_frame.x + map_frame.width - 15;
    return presented_map.target.width && x >= left && x < map_frame.x + map_frame.width
        && y >= map_frame.y && y < map_frame.y + MAP_GRIP_HEIGHT;
}

static void paint_round_button(const DDSURFACEDESC *dst, const Panel *panel)
{
    // The Slayers clasp hugs the orb's right rim; its hooks wrap the top and bottom.
    int top = panel->y + 17 - (int)COUNT(hud_round_button) / 2;
    int left = panel->x + 19;
    for (unsigned y = 0; y < COUNT(hud_round_button); ++y) {
        if (top + (int)y < 0 || top + (int)y >= BELT_Y) continue;
        uint16_t *target = (uint16_t *)row(dst->lpSurface, dst->lPitch, top + y) + left;
        for (unsigned x = 0; x < COUNT(hud_round_button[0]); ++x)
            if (hud_round_button[y][x] != SPRITE_TRANSPARENT) target[x] = hud_round_button[y][x];
    }
}

static Panel placed_panel(unsigned index)
{
    Panel p = hud[index];
    if (presented_wide_world && index >= NAV_START) p.x = presented_world_right - 18;
    return p;
}

enum { DROP_X = 542, DROP_Y = 110, GRAB_W = 160, GRAB_H = 256, GRAB_BACK_X = 64, GRAB_BACK_Y = 96 };
static uint16_t grab_background[GRAB_H][GRAB_W];
static BOOL grab_background_ready, menu_outside;

// While the pointer is off the sheet the engine still draws the carried item
// near its native cursor. Lift the pixels that differ from the empty pit.
static void paint_grabbed_item(const DDSURFACEDESC *src, const DDSURFACEDESC *dst)
{
    if (!presented_menu || presented_pause || presented_world_map) return;
    if (!menu_outside) {
        for (int y = 0; y < GRAB_H; ++y)
            memcpy(grab_background[y], (const uint16_t *)row(src->lpSurface, src->lPitch,
                DROP_Y - GRAB_BACK_Y + y) + DROP_X - GRAB_BACK_X, sizeof(grab_background[y]));
        grab_background_ready = TRUE;
        return;
    }
    if (!grab_background_ready || pointer_x < 0 || pointer_y < 0) return;
    for (int y = 0; y < GRAB_H; ++y) {
        const uint16_t *from = (const uint16_t *)row(src->lpSurface, src->lPitch,
            DROP_Y - GRAB_BACK_Y + y) + DROP_X - GRAB_BACK_X;
        int ty = pointer_y + y - GRAB_BACK_Y;
        if (ty < 0 || ty >= BELT_Y) continue;
        uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, ty);
        for (int x = 0; x < GRAB_W; ++x) {
            int tx = pointer_x + x - GRAB_BACK_X;
            if (tx < WORLD_TOP || tx >= SIDE_X || from[x] == grab_background[y][x]) continue;
            to[tx] = from[x];
        }
    }
}

static void drop_held_item(HWND window)
{
    if (!presented_menu || presented_pause || presented_world_map || GAME_SCENE != CAMPAIGN_SCENE) return;
    LPARAM position = MAKELPARAM(DROP_X, DROP_Y);
    GAME_WNDPROC(window, WM_LBUTTONDOWN, MK_LBUTTON, position);
    GAME_WNDPROC(window, WM_LBUTTONUP, 0, position);
}

static void draw_pointer(const DDSURFACEDESC *dst)
{
    if (!cursor_ready || pointer_x < 0 || pointer_x >= WIDE_WIDTH || pointer_y < 0 || pointer_y >= HEIGHT) return;
    for (int y = 0; y < SPRITE_SIZE && pointer_y + y < HEIGHT; ++y) {
        uint16_t *line = (uint16_t *)row(dst->lpSurface, dst->lPitch, pointer_y + y) + pointer_x;
        for (int x = 0; x < SPRITE_SIZE && pointer_x + x < WIDE_WIDTH; ++x)
            if (cursor_sprite[y][x] != SPRITE_TRANSPARENT) line[x] = cursor_sprite[y][x];
    }
}

static BOOL supported(const DDSURFACEDESC *pixels, DWORD width);

// The text overlays are drawn through GDI after the pixel pass, so the pointer
// is drawn once more over them and always stays on top.
static void redraw_pointer(IDirectDrawSurface3 *target)
{
    if (!presented_hud || !cursor_ready) return;
    DDSURFACEDESC dst = {0};
    dst.dwSize = sizeof(dst);
    if (FAILED(IDirectDrawSurface3_Lock(target, NULL, &dst, DDLOCK_WAIT, NULL))) return;
    if (supported(&dst, WIDE_WIDTH)) draw_pointer(&dst);
    IDirectDrawSurface3_Unlock(target, NULL);
}

static void draw_selection(const DDSURFACEDESC *dst)
{
    if (!selection_visible) return;
    int left = selection.left, right = selection.right, top = selection.top, bottom = selection.bottom;
    if (presented_wide_world) {
        int width = displayed_view.columns * TILE;
        left = WORLD_TOP + (left - WORLD_TOP) * (presented_world_right - WORLD_TOP) / width;
        right = WORLD_TOP + (right - WORLD_TOP) * (presented_world_right - WORLD_TOP) / width;
        top = WORLD_TOP + (top - WORLD_TOP) * VIEW_HEIGHT / displayed_view.height;
        bottom = WORLD_TOP + (bottom - WORLD_TOP) * VIEW_HEIGHT / displayed_view.height;
    }
    left = clamp(left, WORLD_TOP, presented_world_right - 1);
    right = clamp(right, left, presented_world_right - 1);
    top = clamp(top, WORLD_TOP, BELT_Y - 1);
    bottom = clamp(bottom, top, BELT_Y - 1);
    uint16_t *first = (uint16_t *)row(dst->lpSurface, dst->lPitch, top);
    uint16_t *last = (uint16_t *)row(dst->lpSurface, dst->lPitch, bottom);
    for (int x = left; x <= right; ++x) first[x] = last[x] = selection_color;
    for (int y = top + 1; y < bottom; ++y) {
        uint16_t *line = (uint16_t *)row(dst->lpSurface, dst->lPitch, y);
        line[left] = line[right] = selection_color;
    }
}

static const uint16_t *world_native_line(const DDSURFACEDESC *src, int y, BOOL journal_background)
{
    if (y >= VIEW_HEIGHT) return NULL;
    if (journal_background) return quest_world[y];
    // The native belt starts at y=446 of the live frame (studs and all); those
    // rows come from the terrain copy taken before the belt was drawn.
    if (y >= NATIVE_BELT_Y - WORLD_TOP) return NULL;
    return (const uint16_t *)row(src->lpSurface, src->lPitch, WORLD_TOP + y) + WORLD_TOP;
}

static uint16_t world_pixel(const uint16_t *native, int x, int y)
{
    return native && x < WORLD_RIGHT - WORLD_TOP ? native[x] : terrain[y][x];
}

static uint16_t blend_world(uint16_t left, uint16_t right, unsigned fraction,
    unsigned red_shift, unsigned green_mask)
{
    unsigned a = (((left >> red_shift) & 31) << 16) | (left & 31);
    unsigned b = (((right >> red_shift) & 31) << 16) | (right & 31);
    unsigned rb = (a * (256 - fraction) + b * fraction + 0x800080) >> 8;
    unsigned g = ((left & green_mask) * (256 - fraction) + (right & green_mask) * fraction + 4096) >> 8;
    return ((rb >> 16) << red_shift) | (g & green_mask) | (rb & 31);
}

// The engine fills health bars with three exact colors (green, amber, red).
// After the bilinear zoom they smear and change width from frame to frame, so
// each bar run is painted again as a crisp rectangle at the final scale.
static uint16_t smooth_pixel(int x, int y);

static void paint_crisp_bars(const DDSURFACEDESC *src, const DDSURFACEDESC *dst,
    int right, CameraView view, BOOL journal_background, int rows)
{
    static const uint16_t bar_colors[] = {0x07E0, 0xD6A0, 0xD000};
    int width = right - WORLD_TOP, source_width = view.columns * TILE;
    for (int y = 0; y < view.height; ++y) {
        int dy0 = y * VIEW_HEIGHT / view.height;
        int dy1 = max(dy0 + 1, (y + 1) * VIEW_HEIGHT / view.height);
        if (dy0 >= rows) break;
        const uint16_t *native = world_native_line(src, y, journal_background);
        for (int x = 0; x < source_width;) {
            uint16_t color = smooth_drawing ? smooth_pixel(x, y) : world_pixel(native, x, y);
            BOOL bar = FALSE;
            for (unsigned i = 0; i < COUNT(bar_colors); ++i) bar |= color == bar_colors[i];
            if (!bar) { ++x; continue; }
            int end = x + 1;
            while (end < source_width && (smooth_drawing ? smooth_pixel(end, y) : world_pixel(native, end, y)) == color) ++end;
            if (end - x >= 2) {
                int dx0 = x * width / source_width, dx1 = max(dx0 + 1, end * width / source_width);
                for (int dy = dy0; dy < dy1 && dy < rows; ++dy) {
                    uint16_t *target = (uint16_t *)row(dst->lpSurface, dst->lPitch, WORLD_TOP + dy) + WORLD_TOP;
                    for (int dx = dx0; dx < dx1 && dx < width; ++dx) target[dx] = color;
                }
            }
            x = end;
        }
    }
}

static void paint_world(const DDSURFACEDESC *src, const DDSURFACEDESC *dst,
    int right, CameraView view, BOOL journal_background, int rows)
{
    int width = right - WORLD_TOP;
    int source_width = view.columns * TILE;
    if (source_width == width && view.height == VIEW_HEIGHT) {
        for (int y = 0; y < rows; ++y) {
            unsigned char *target = row(dst->lpSurface, dst->lPitch, WORLD_TOP + y);
            if (journal_background) memcpy(target + WORLD_TOP * 2, quest_world[y], sizeof(quest_world[0]));
            if (!journal_background && y >= NATIVE_BELT_Y - WORLD_TOP)
                memcpy(target + WORLD_TOP * 2, terrain[y], (right - WORLD_TOP) * 2);
            else memcpy(target + WORLD_RIGHT * 2,
                    terrain[y] + 14 * TILE, (right - WORLD_RIGHT) * 2);
        }
        return;
    }

    int source_columns[VIEW_COLUMNS * TILE];
    BOOL rgb555 = src->ddpfPixelFormat.dwRBitMask == 0x7C00;
    unsigned red_shift = rgb555 ? 10 : 11;
    unsigned green_mask = rgb555 ? 0x03E0 : 0x07E0;
    for (int x = 0; x < width; ++x) {
        source_columns[x] = pixel_zoom ? (x * source_width / width) * 256
            : ((2 * x + 1) * source_width * 128 / width) - 128;
    }
    for (int y = 0; y < rows; ++y) {
        int source_y = pixel_zoom ? (y * view.height / VIEW_HEIGHT) * 256
            : ((2 * y + 1) * view.height * 128 / VIEW_HEIGHT) - 128;
        int sy = source_y >> 8, next_y = min(sy + 1, view.height - 1);
        const uint16_t *original = world_native_line(src, sy, journal_background);
        const uint16_t *next = world_native_line(src, next_y, journal_background);
        uint16_t *target = (uint16_t *)row(dst->lpSurface, dst->lPitch, WORLD_TOP + y) + WORLD_TOP;
        for (int x = 0; x < width; ++x) {
            int sx = source_columns[x] >> 8;
            unsigned fraction = source_columns[x] & 255;
            uint16_t top = world_pixel(original, sx, sy);
            if (pixel_zoom) {
                target[x] = top;
                continue;
            }
            int next_x = min(sx + 1, source_width - 1);
            uint16_t bottom = world_pixel(next, sx, next_y);
            top = blend_world(top, world_pixel(original, next_x, sy), fraction, red_shift, green_mask);
            bottom = blend_world(bottom, world_pixel(next, next_x, next_y), fraction, red_shift, green_mask);
            target[x] = blend_world(top, bottom, source_y & 255, red_shift, green_mask);
        }
    }
    if (!pixel_zoom && !rgb555) paint_crisp_bars(src, dst, right, view, journal_background, rows);
}

// Smooth camera: the engine scrolls in whole 32px tiles. When the camera steps,
// the previous frame is kept as an anchor and the view slides between the two
// images over a short time, repainted faster than the game updates.
enum { SMOOTH_MS = 110, SMOOTH_STRIDE = MAX_COLUMNS * TILE, SMOOTH_ROWS = MAX_ROWS * TILE };
static uint16_t smooth_images[3][SMOOTH_ROWS][SMOOTH_STRIDE];    // current, last, anchor
static const unsigned char *smooth_map;
static int smooth_cam_x, smooth_cam_y, smooth_anchor_dx, smooth_anchor_dy;
static int smooth_view_columns, smooth_view_height, smooth_start_x, smooth_start_y;
static int smooth_shift_x, smooth_shift_y;
static DWORD smooth_frame, smooth_start;
static BOOL smooth_have;

static void smooth_offset(int *x, int *y)
{
    DWORD age = GetTickCount() - smooth_start;
    if (age >= SMOOTH_MS) { *x = *y = 0; return; }
    *x = smooth_start_x * (int)(SMOOTH_MS - age) / SMOOTH_MS;
    *y = smooth_start_y * (int)(SMOOTH_MS - age) / SMOOTH_MS;
}

static uint16_t smooth_pixel(int x, int y)
{
    int width = displayed_view.columns * TILE, height = displayed_view.height;
    int cx = x + smooth_shift_x, cy = y + smooth_shift_y;
    if (cx >= 0 && cx < width && cy >= 0 && cy < height) return smooth_images[0][cy][cx];
    int ax = cx + smooth_anchor_dx, ay = cy + smooth_anchor_dy;
    if (ax >= 0 && ax < width && ay >= 0 && ay < height) return smooth_images[2][ay][ax];
    return smooth_images[0][clamp(cy, 0, height - 1)][clamp(cx, 0, width - 1)];
}

static void smooth_update(const DDSURFACEDESC *src, const unsigned char *map)
{
    int width = displayed_view.columns * TILE, height = displayed_view.height;
    DWORD frame = FRAME_COUNT;
    if (width > SMOOTH_STRIDE || height > SMOOTH_ROWS) { smooth_animating = FALSE; return; }
    if (!smooth_have || frame != smooth_frame) {
        BOOL same = smooth_have && smooth_map == map && smooth_view_columns == displayed_view.columns
            && smooth_view_height == displayed_view.height;
        int dx = frame_cam_x - smooth_cam_x, dy = frame_cam_y - smooth_cam_y;
        if (same && (dx || dy) && abs(dx) <= 2 && abs(dy) <= 2) {
            int now_x, now_y;
            smooth_offset(&now_x, &now_y);      // the shift being shown right now
            smooth_start_x = clamp(now_x - dx * TILE, -TILE, TILE);
            smooth_start_y = clamp(now_y - dy * TILE, -TILE, TILE);
            smooth_start = GetTickCount();
            smooth_anchor_dx = dx * TILE;       // old frame column = new column + this
            smooth_anchor_dy = dy * TILE;
            for (int y = 0; y < height; ++y)
                memcpy(smooth_images[2][y], smooth_images[1][y], (size_t)width * 2);
        } else if (!same || abs(dx) > 2 || abs(dy) > 2) {
            smooth_start_x = smooth_start_y = 0;
        }
        for (int y = 0; y < height; ++y) {
            const uint16_t *native = world_native_line(src, y, FALSE);
            for (int x = 0; x < width; ++x) smooth_images[0][y][x] = world_pixel(native, x, y);
        }
        for (int y = 0; y < height; ++y)
            memcpy(smooth_images[1][y], smooth_images[0][y], (size_t)width * 2);
        smooth_map = map;
        smooth_cam_x = frame_cam_x;
        smooth_cam_y = frame_cam_y;
        smooth_view_columns = displayed_view.columns;
        smooth_view_height = displayed_view.height;
        smooth_frame = frame;
        smooth_have = TRUE;
    }
    smooth_offset(&smooth_shift_x, &smooth_shift_y);
    smooth_animating = smooth_shift_x || smooth_shift_y;
}

static void paint_world_smooth(const DDSURFACEDESC *src, const DDSURFACEDESC *dst, int right,
    CameraView view, int rows)
{
    int width = right - WORLD_TOP, source_width = view.columns * TILE;
    BOOL rgb555 = src->ddpfPixelFormat.dwRBitMask == 0x7C00;
    unsigned red_shift = rgb555 ? 10 : 11, green_mask = rgb555 ? 0x03E0 : 0x07E0;
    int source_columns[MAX_COLUMNS * TILE];
    for (int x = 0; x < width; ++x)
        source_columns[x] = pixel_zoom ? (x * source_width / width) * 256
            : ((2 * x + 1) * source_width * 128 / width) - 128;
    for (int y = 0; y < rows; ++y) {
        int source_y = pixel_zoom ? (y * view.height / VIEW_HEIGHT) * 256
            : ((2 * y + 1) * view.height * 128 / VIEW_HEIGHT) - 128;
        int sy = max(0, source_y >> 8), next_y = min(sy + 1, view.height - 1);
        uint16_t *target = (uint16_t *)row(dst->lpSurface, dst->lPitch, WORLD_TOP + y) + WORLD_TOP;
        for (int x = 0; x < width; ++x) {
            int sx = max(0, source_columns[x] >> 8);
            unsigned fraction = source_columns[x] & 255;
            uint16_t top = smooth_pixel(sx, sy);
            if (pixel_zoom) { target[x] = top; continue; }
            int next_x = min(sx + 1, source_width - 1);
            uint16_t bottom = smooth_pixel(sx, next_y);
            top = blend_world(top, smooth_pixel(next_x, sy), fraction, red_shift, green_mask);
            bottom = blend_world(bottom, smooth_pixel(next_x, next_y), fraction, red_shift, green_mask);
            target[x] = blend_world(top, bottom, source_y & 255, red_shift, green_mask);
        }
    }
    if (!pixel_zoom && !rgb555) {
        smooth_drawing = TRUE;
        paint_crisp_bars(src, dst, right, view, FALSE, rows);
        smooth_drawing = FALSE;
    }
}

static void paint_sidebar(const DDSURFACEDESC *dst)
{
    if (NET_MULTI) {
        RECT area = party_frame();
        for (int y = area.top + 3; y < area.bottom - 3; ++y) {
            uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, y);
            for (int x = area.left + 3; x < area.right - 3; ++x) to[x] = dim_pixel(dim_pixel(to[x]));
        }
    }
    for (unsigned i = 0; i < NAV_START; ++i) {
        const Panel *p = &hud[i];
        for (int y = 0; y < p->height; ++y) {
            const uint16_t *from = sidebar_content[p->source_y + y] + p->source_x - SCENE_WIDTH;
            uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, p->y + y) + p->x;
            if (i == 0) {
                memcpy(to, from, p->width * sizeof(*to));
                continue;
            }
            if (NET_MULTI && i >= 2) continue;
            const uint16_t *background = portrait_background[p->source_y + y - hud[1].source_y];
            for (int x = 0; x < p->width; ++x)
                if (from[x] != background[x]) to[x] = from[x];
        }
        if (i && (party_selection & party_present & (1u << (i - 1)))) {
            RECT frame = {p->x - 3, p->y - 3, p->x + p->width + 2, p->y + p->height + 2};
            for (int y = frame.top; y <= frame.bottom; ++y)
                for (int x = frame.left; x <= frame.right; ++x)
                    if (x - frame.left < 2 || frame.right - x < 2
                        || y - frame.top < 2 || frame.bottom - y < 2)
                        hud_pixel(dst, x, y, 0xFEA0);
        }
    }
}

static void paint_full_world_map(const DDSURFACEDESC *dst)
{
    const MapRect *panel = &world_map_panel;
    BOOL rgb555 = dst->ddpfPixelFormat.dwRBitMask == 0x7C00;
    unsigned red_shift = rgb555 ? 10 : 11, green_mask = rgb555 ? 0x03E0 : 0x07E0;
    for (int y = 0; y < panel->height; ++y) {
        int sy = ((2 * y + 1) * WORLD_MAP_HEIGHT * 128 / panel->height) - 128;
        const uint16_t *top = world_map_pixels[sy >> 8], *bottom = world_map_pixels[(sy >> 8) + 1];
        uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, panel->y + y) + panel->x;
        for (int x = 0; x < panel->width; ++x) {
            int sx = ((2 * x + 1) * WORLD_MAP_WIDTH * 128 / panel->width) - 128;
            int column = sx >> 8;
            uint16_t a = blend_world(top[column], top[column + 1], sx & 255, red_shift, green_mask);
            uint16_t b = blend_world(bottom[column], bottom[column + 1], sx & 255, red_shift, green_mask);
            to[x] = blend_world(a, b, sy & 255, red_shift, green_mask);
        }
    }
}

static void paint(const DDSURFACEDESC *src, const DDSURFACEDESC *dst, BOOL lateral, BOOL menu)
{
    presented_map = (MapView){0};
    presented_menu = menu;
    presented_pause = menu && pause_ready;
    presented_world_map = menu && !presented_pause && world_map_ready;
    presented_quest = lateral && quest_ready && !world_view_active();
    presented_dialogue = lateral && dialogue_ready && dialogue_active();
    presented_wide_world = lateral && wide_frame;
    int right = presented_wide_world ? (overflow_ready ? WIDE_RIGHT : NATIVE_WIDE_RIGHT) : SCENE_WIDTH;
    presented_world_right = menu ? SIDE_X : right;
    displayed_view = overflow_ready ? frame_view : (CameraView){NATIVE_WIDE_COLUMNS, VIEW_HEIGHT};
    if (lateral || menu) {
        for (int y = 0; y < BELT_Y; ++y) {
            uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, y);
            memcpy(to + SCENE_WIDTH, hud_wood[y], (WIDE_WIDTH - SCENE_WIDTH) * sizeof(*to));
            if (menu) memcpy(to, world_snapshot[y], sizeof(world_snapshot[y]));
        }
        if (!menu) {
            map_frame.x = clamp(map_frame.x, 16, presented_world_right - 16 - map_frame.width);
            copy_panel(src, dst, &scene);
            for (int y = NATIVE_BELT_Y; y < BELT_Y; ++y)
                memcpy(row(dst->lpSurface, dst->lPitch, y),
                    row(src->lpSurface, src->lPitch, y), WORLD_TOP * 2);
            for (int y = 0; y < HEIGHT; ++y)
                memcpy(sidebar_content[y], row(src->lpSurface, src->lPitch, y) + SCENE_WIDTH * 2,
                    sizeof(sidebar_content[y]));
            if (world_view_active()) {
                for (int y = 0; y < BELT_Y; ++y)
                    memcpy(sidebar_border[y], row(src->lpSurface, src->lPitch, y) + 624 * 2,
                        sizeof(sidebar_border[y]));
                sidebar_border_ready = TRUE;
            }
        }
        for (unsigned y = 0; y < COUNT(hud_header); ++y) {
            uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, y);
            for (int x = SCENE_WIDTH; x < WIDE_WIDTH - 16; ++x)
                to[x] = hud_header[y][(x - SCENE_WIDTH) % COUNT(hud_header[0])];
        }
        if (sidebar_border_ready) {
            for (int y = 0; y < BELT_Y; ++y)
                memcpy(row(dst->lpSurface, dst->lPitch, y) + (WIDE_WIDTH - 16) * 2,
                    sidebar_border[y], sizeof(sidebar_border[y]));
        }
        for (int y = BELT_Y; y < HEIGHT; ++y)
            memcpy(row(dst->lpSurface, dst->lPitch, y), hud_belt[y - BELT_Y], sizeof(hud_belt[0]));
        if (presented_world_map) {
            paint_full_world_map(dst);
        } else if (menu && !presented_pause) {
            copy_panel(src, dst, &campaign_menu);
        }
        if (lateral || sidebar_ready) paint_sidebar(dst);
        int divider = SIDE_X;
        if (presented_wide_world) {
            BOOL smooth = smooth_camera && !menu && !presented_quest && !presented_dialogue
                && overflow_ready && GAME_MAP && world_view_active();
            smooth_animating = FALSE;
            if (smooth) smooth_update(src, GAME_MAP);
            if (smooth && smooth_animating)
                paint_world_smooth(src, dst, presented_world_right, displayed_view, VIEW_HEIGHT);
            else paint_world(src, dst, presented_world_right, displayed_view,
                presented_quest || presented_dialogue, VIEW_HEIGHT);
            if (header_ready) {
                for (int y = WORLD_TOP; y < FIXED_HEADER_BOTTOM; ++y) {
                    uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, y);
                    if (!(smooth && smooth_animating)) {
                        memcpy(to + WORLD_TOP, header_canvas[y] + WORLD_TOP, sizeof(header_world[0]));
                        continue;
                    }
                    // While the camera slides, keep only the interface pixels of
                    // the strip fixed and let the world underneath move with it.
                    for (int x = WORLD_TOP; x < WORLD_TOP + (int)COUNT(header_world[0]); ++x)
                        if (header_canvas[y][x] != header_background[y][x]) to[x] = header_canvas[y][x];
                }
            }
            if (!presented_quest && !presented_dialogue) {
                paint_damage(dst);
                paint_click_marker(dst);
                collect_loot();
            }
            divider = presented_world_right;
        }
        if (!menu && !presented_quest && !presented_dialogue) draw_selection(dst);
        for (int y = WORLD_TOP; y < BELT_Y; ++y)
            memcpy(row(dst->lpSurface, dst->lPitch, y) + (divider - 7) * 2,
                hud_divider[(y - WORLD_TOP) % COUNT(hud_divider)], sizeof(hud_divider[0]));
        for (unsigned i = NAV_START; i < COUNT(hud); ++i) {
            Panel p = placed_panel(i);
            if (orb_ready[i - NAV_START]) copy_orb(dst, &p, orb_sprites[i - NAV_START]);
            paint_round_button(dst, &p);
        }
        if (!map_hidden && !menu) presented_map = current_map_view();
        if (presented_map.target.width) paint_map(src, dst, &presented_map);
        if (presented_dialogue) copy_panel(src, dst, &dialogue_panel);
        if (presented_quest) {
            copy_panel(src, dst, &quest_panel);
            if (quest_marker_y >= quest_panel.y + 4 && quest_marker_y < BELT_Y - 4) {
                for (int y = -4; y <= 4; ++y) {
                    for (int x = 0; x <= 4 - abs(y); ++x)
                        hud_pixel(dst, quest_panel.x + 26 + x, quest_marker_y + y, 0xFE47);
                }
            }
        }
        if (lateral && world_view_active()) {
            for (int y = 0; y < BELT_Y; ++y)
                memcpy(world_snapshot[y], row(dst->lpSurface, dst->lPitch, y), sizeof(world_snapshot[y]));
            sidebar_ready = TRUE;
        }
        if (presented_pause) {
            unsigned mask = dst->ddpfPixelFormat.dwRBitMask == 0x7C00 ? 0x7BDE : 0xF7DE;
            for (int y = 0; y < HEIGHT; ++y) {
                uint16_t *to = (uint16_t *)row(dst->lpSurface, dst->lPitch, y);
                for (int x = 0; x < WIDE_WIDTH; ++x) to[x] = pause_shade(to[x], mask);
            }
            copy_panel(src, dst, &pause_menu);
        }
        paint_grabbed_item(src, dst);
        draw_pointer(dst);
    } else {
        for (int y = 0; y < HEIGHT; ++y) {
            unsigned char *to = row(dst->lpSurface, dst->lPitch, y);
            memset(to, 0, classic.x * 2);
            memset(to + (classic.x + GAME_WIDTH) * 2, 0, (WIDE_WIDTH - classic.x - GAME_WIDTH) * 2);
        }
        copy_panel(src, dst, &classic);
    }
}

__declspec(dllexport) HRESULT WINAPI HudMode(GameVideo *video, DWORD width, DWORD height, DWORD bits)
{
    install_crash_log();
    install_dialog_hook();
    char enabled[2] = "";
    char camera_keys[2] = "";
    char configured[96] = "", exclusive[2] = "", pixels[2] = "", language[3] = "", marker[2] = "";
    wasd_camera = GetEnvironmentVariableA("AKUMA_WASD_CAMERA", camera_keys, sizeof(camera_keys)) == 1
        && camera_keys[0] == '1';
    configure_native_keys();
    DWORD length = GetEnvironmentVariableA("AKUMA_BINDINGS", configured, sizeof(configured));
    if (length && length < sizeof(configured)) {
        char *start = configured;
        for (unsigned i = 0; i < COUNT(bindings); ++i) {
            char *end;
            unsigned long value = strtoul(start, &end, 10);
            if (end == start || value > 2047 || (*end != ',' && *end != '\0')) break;
            bindings[i] = (unsigned)value;
            if (*end == '\0') break;
            start = end + 1;
        }
    }
    int chinese = 0;
    if (GetEnvironmentVariableA("AKUMA_LANGUAGE", language, sizeof(language)) == 2) {
        chinese = strcmp(language, "zh") == 0;
                hud_language = strcmp(language, "ko") == 0 ? KOREAN : chinese ? CHINESE
            : strcmp(language, "en") == 0 ? ENGLISH : PORTUGUESE;
    }
    item_code_page = hud_language == KOREAN ? 949 : chinese ? 950 : 1252;
    {
        char smooth[2] = "";
        smooth_camera = GetEnvironmentVariableA("AKUMA_SMOOTH_CAMERA", smooth, sizeof(smooth)) == 1 && smooth[0] == '1';
    }
    click_marker_enabled = GetEnvironmentVariableA("AKUMA_CLICK_MARKER", marker, sizeof(marker)) == 1
        && marker[0] == '1';
    exclusive_party = GetEnvironmentVariableA("AKUMA_EXCLUSIVE_PARTY", exclusive, sizeof(exclusive)) == 1
        && exclusive[0] == '1';
    pixel_zoom = GetEnvironmentVariableA("AKUMA_PIXEL_ZOOM", pixels, sizeof(pixels)) == 1 && pixels[0] == '1';
    if (output_surface) {
        IDirectDrawSurface3_Release(output_surface);
        output_surface = NULL;
    }
    if (overflow_surface) {
        IDirectDrawSurface3_Release(overflow_surface);
        overflow_surface = NULL;
    }
    if (composition_surface) {
        IDirectDrawSurface3_Release(composition_surface);
        composition_surface = NULL;
    }
    wide_mode = FALSE;
    CAMERA_COLUMNS = 14;
    CAMERA_ROWS = NATIVE_ROWS;
    CAMERA_REVEAL_X = CAMERA_REVEAL_Y = 31;
    presented_hud = FALSE;
    presented_menu = sidebar_ready = open_journal_pending = sidebar_click = FALSE;
    sidebar_border_ready = FALSE;
    world_map_ready = pause_ready = presented_world_map = presented_pause = FALSE;
    presented_wide_world = FALSE;
    wide_frame = FALSE;
    world_render_failed = quest_ready = presented_quest = dialogue_ready = presented_dialogue = FALSE;
    cursor_ready = FALSE;
    memset(orb_ready, 0, sizeof(orb_ready));
    presented_map = (MapView){0};
    has_frame = 0;
    if (width == GAME_WIDTH && height == HEIGHT && bits == 16
        && GetEnvironmentVariableA("AKUMA_WIDE_HUD", enabled, sizeof(enabled)) == 1
        && enabled[0] == '1') {
        HRESULT result = IDirectDraw2_SetDisplayMode(video->draw, WIDE_WIDTH, height, bits, 0, 0);
        if (SUCCEEDED(result)) {
            // The separate offscreen surface keeps the engine's 640-pixel pitch.
            video->flip = 0;
            wide_mode = TRUE;
            return result;
        }
    }
    return IDirectDraw2_SetDisplayMode(video->draw, width, height, bits, 0, 0);
}

static IDirectDrawSurface3 *create_staging(IDirectDraw2 *draw, int width)
{
    DDSURFACEDESC description = {0};
    description.dwSize = sizeof(description);
    description.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    description.dwWidth = width;
    description.dwHeight = HEIGHT;
    description.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    IDirectDrawSurface *surface = NULL;
    HRESULT result = IDirectDraw2_CreateSurface(draw, &description, &surface, NULL);
    if (FAILED(result)) return NULL;

    const IID *iid = (const IID *)(uintptr_t)0x4892B0;
    IDirectDrawSurface3 *staging = NULL;
    result = IDirectDrawSurface_QueryInterface(surface, iid, (void **)&staging);
    IDirectDrawSurface_Release(surface);
    if (FAILED(result)) return NULL;
    return staging;
}

__declspec(dllexport) BOOL WINAPI HudAttach(GameVideo *video)
{
    if (!wide_mode) return TRUE;
    IDirectDrawSurface3 *staging = create_staging(video->draw, GAME_WIDTH);
    if (!staging) return FALSE;
    overflow_surface = create_staging(video->draw, GAME_WIDTH);
    composition_surface = create_staging(video->draw, WIDE_WIDTH);

    // The engine's full and partial blits now target its own 640x480 primary.
    // The real primary is only updated by the complete widescreen compositor.
    output_surface = video->primary;
    video->primary = staging;
    return TRUE;
}

#ifdef AKUMA_HUD_TRACE
static void trace_scene(DWORD active)
{
    static DWORD previous = (DWORD)-1;
    if (previous == active) return;
    previous = active;
    FILE *log = fopen("hud-trace.txt", "a");
    if (log) { fprintf(log, "scene %lu\n", active); fclose(log); }
}
#endif

static BOOL supported(const DDSURFACEDESC *pixels, DWORD width)
{
    return pixels->lpSurface && pixels->dwWidth == width && pixels->dwHeight == HEIGHT
        && pixels->lPitch >= (LONG)width * 2 && pixels->ddpfPixelFormat.dwRGBBitCount == 16;
}

static BOOL click_toolbar(unsigned index)
{
    if (!GAME_TOOLBAR) return FALSE;
    typedef int (__thiscall *ToolbarClick)(void *, int, int);
    const Panel *p = &hud[NAV_START + index];
    return ((ToolbarClick)(uintptr_t)0x46A300)(GAME_TOOLBAR,
        p->source_x + p->width / 2, p->source_y + p->height / 2) != 0;
}

// The chat input is a native EDIT child that keeps keyboard focus. It is drawn at
// fixed pixels over the scaled output and blinks in the wrong place, so park it
// out of sight; paint_chat_input shows the typed text instead.
static void place_chat_edit(void)
{
    static HWND parked;
    if (!GAME_CHAT_OPEN) {
        parked = NULL;
        return;
    }
    const unsigned char *chat = *(const unsigned char * const *)(uintptr_t)0x4998E4;
    HWND edit = chat ? *(HWND const *)(chat + 0x28) : NULL;
    if (!edit || edit == parked || !IsWindow(edit)) return;
    SetWindowPos(edit, NULL, -4000, -4000, 520, 24, SWP_NOACTIVATE | SWP_NOZORDER);
    parked = edit;
}

// Native chat lines are drawn in a corner with a small font and shrink away when the
// camera zooms out. In multiplayer the HUD draws them (paint_chat_lines) instead.
__declspec(dllexport) void WINAPI HudChatLines(void *chat)
{
    typedef void (__thiscall *NativeChat)(void *);
    if (NET_MULTI && wide_mode && show_hud) return;
    ((NativeChat)(uintptr_t)0x423940)(chat);
}

// Life changes that the game does not report through the damage hooks: another
// player's hero, and (on a visitor) mobs hurt by the host.
static struct { const void *actor; DWORD tick; } estimated_hits[16];
static unsigned next_estimated_hit;

static void note_estimated_hit(const void *actor)
{
    estimated_hits[next_estimated_hit].actor = actor;
    estimated_hits[next_estimated_hit].tick = GetTickCount();
    next_estimated_hit = (next_estimated_hit + 1) % COUNT(estimated_hits);
}

static BOOL recent_estimated_hit(const void *actor)
{
    DWORD now = GetTickCount();
    for (unsigned i = 0; i < COUNT(estimated_hits); ++i)
        if (estimated_hits[i].actor == actor && now - estimated_hits[i].tick < 1200) return TRUE;
    return FALSE;
}

static void track_netplay_damage(void)
{
    static const unsigned char *tracked_map;
    static int mob_life[128];
    static struct { DWORD owner; int life; } hero_life[24];
    const unsigned char *map = GAME_MAP;
    if (!NET_MULTI || !map || GAME_SCENE != CAMPAIGN_SCENE) {
        tracked_map = NULL;
        return;
    }
    if (map != tracked_map) {
        tracked_map = map;
        memset(mob_life, 0, sizeof(mob_life));
        memset(hero_life, 0, sizeof(hero_life));
    }
    const unsigned char * const *actors = (const unsigned char * const *)(map + 0x14AAC);
    for (unsigned i = 0; i < 20; ++i) {
        const unsigned char *actor = actors[i];
        if (!actor || party_member(actor) || !*(const int *)(actor + 0x40)) continue;
        int life = *(const int *)(actor + 0x810);
        DWORD owner = *(const DWORD *)(actor + 0x754);
        unsigned slot = 0;
        while (slot < COUNT(hero_life) - 1 && hero_life[slot].owner && hero_life[slot].owner != owner) ++slot;
        hero_life[slot].owner = owner;
        if (hero_life[slot].life > 0 && life < hero_life[slot].life)
            add_damage(actor, 0x790, hero_life[slot].life, life, 1);
        hero_life[slot].life = life;
    }
    if (NET_HOST) return;
    const unsigned char * const *mobs = (const unsigned char * const *)(map + 0x14AFC + 4);
    for (unsigned id = 0; id < COUNT(mob_life); ++id) {
        const unsigned char *mob = mobs[id];
        if (!mob || IsBadReadPtr(mob, 0x304)) {
            mob_life[id] = 0;
            continue;
        }
        int life = *(const int *)(mob + 0x300);
        if (life < 0 || life > 200000) continue;
        if (mob_life[id] > 0 && life < mob_life[id] && !recent_estimated_hit(mob))
            add_damage(mob, 0x2DC, mob_life[id], life, 0);
        mob_life[id] = life;
    }
}

__declspec(dllexport) void WINAPI HudFlush(void)
{
    net_log_party();
    if (!wide_mode || !output_surface || !GAME_VIDEO->primary) return;
    place_chat_edit();
    track_netplay_damage();
    DWORD now = GetTickCount();
    DWORD frame = FRAME_COUNT;
    DWORD active = GAME_SCENE;
    const unsigned char *view = GAME_VIEW;
    DWORD ui_state = active | (GAME_PAUSED ? 1u << 8 : 0) | (show_hud ? 1u << 9 : 0)
        | (pause_ready ? 1u << 10 : 0) | (world_map_ready ? 1u << 11 : 0)
        | (quest_ready ? 1u << 12 : 0) | (dialogue_ready ? 1u << 13 : 0);
    if (view)
        ui_state ^= (DWORD)view[0x8C] << 16 | (DWORD)view[0x8D] << 20
            | (*(const DWORD *)(view + 0xE4) ? 1u << 28 : 0);
    static BOOL last_loot_held;
    BOOL loot_now = loot_held();
    if (loot_now != last_loot_held) {
        last_loot_held = loot_now;
        has_frame = 0;
    }
    if (ui_state != last_ui_state) {
        last_ui_state = ui_state;
        has_frame = 0;
    }
    if (active != CAMPAIGN_SCENE) {
        party_map = NULL;
        sidebar_ready = open_journal_pending = FALSE;
        world_map_ready = pause_ready = FALSE;
        CAMERA_COLUMNS = 14;
        CAMERA_ROWS = NATIVE_ROWS;
        CAMERA_REVEAL_X = CAMERA_REVEAL_Y = 31;
    }
    if (open_journal_pending && !pause_ready && world_view_active() && !GAME_VIEW[0x8D]) {
        open_journal_pending = FALSE;
        click_toolbar(2);
    }
#ifdef AKUMA_HUD_TRACE
    trace_scene(active);
#endif
    if (active != last_scene) {
        last_scene = active;
        scene_since = now;
        has_frame = 0;
    }
    if (has_frame && frame == last_frame && now - last_update < (smooth_animating ? 8u : 25u)) return;

    DDSURFACEDESC src = {0}, dst = {0};
    src.dwSize = sizeof(src);
    dst.dwSize = sizeof(dst);
    IDirectDrawSurface3 *input = GAME_VIDEO->primary;
    if (FAILED(IDirectDrawSurface3_Lock(input, NULL, &src, DDLOCK_WAIT | DDLOCK_READONLY, NULL))) return;

    IDirectDrawSurface3 *target = composition_surface ? composition_surface : output_surface;
    HRESULT result = IDirectDrawSurface3_Lock(target, NULL, &dst, DDLOCK_WAIT, NULL);
    BOOL painted = FALSE;
    if (SUCCEEDED(result)) {
        BOOL campaign = show_hud && active == CAMPAIGN_SCENE && GAME_VIEW && now - scene_since >= 100;
        BOOL waiting_overlay = campaign && !sidebar_ready && !pause_ready && GAME_VIEW[0x8C] == 1 && !quest_ready && !dialogue_ready
            && (GAME_VIEW[0x8D] == 4 || (world_draw_active() && !world_view_active()));
        BOOL lateral = campaign && !pause_ready && world_draw_active()
            && (world_view_active() || quest_ready || dialogue_ready);
        BOOL menu = campaign && sidebar_ready && !lateral;
        if (!(lateral && world_render_failed) && !waiting_overlay
            && supported(&src, GAME_WIDTH) && supported(&dst, WIDE_WIDTH)) {
            paint(&src, &dst, lateral, menu);
            presented_hud = lateral || menu;
            painted = TRUE;
        }
        IDirectDrawSurface3_Unlock(target, NULL);
    }
    IDirectDrawSurface3_Unlock(input, NULL);
    if (painted) {
        paint_text_overlays(target);
        redraw_pointer(target);
        RECT full = {0, 0, WIDE_WIDTH, HEIGHT};
        if (target != output_surface && FAILED(IDirectDrawSurface3_Blt(output_surface, &full,
            target, &full, DDBLT_WAIT, NULL))) return;
        last_frame = frame;
        last_update = now;
        has_frame = TRUE;
    }
}

static BOOL map_panel(const Panel *p, int *x, int *y)
{
    if (*x < p->x || *x >= p->x + p->width || *y < p->y || *y >= p->y + p->height)
        return FALSE;
    *x += p->source_x - p->x;
    *y += p->source_y - p->y;
    return TRUE;
}

static BOOL source_point(int *x, int *y)
{
    if (!presented_hud) return map_panel(&classic, x, y);
    if (presented_pause) return map_panel(&pause_menu, x, y);
    if (presented_world_map) return FALSE;
    if (presented_menu) return map_panel(&campaign_menu, x, y);
    if (presented_dialogue) {
        if (map_panel(&dialogue_panel, x, y)) return TRUE;
        *x = *y = 0;
        return TRUE;
    }
    if (presented_quest) {
        Panel journal_button = placed_panel(NAV_START + 2);
        if (map_panel(&quest_panel, x, y) || map_panel(&journal_button, x, y)) return TRUE;
        *x = 0;
        *y = 0;
        return TRUE;
    }
    const MapRect *map = &presented_map.target;
    if (map->width && in_rect(&map_frame, *x, *y)) {
        if (!in_rect(map, *x, *y)) return FALSE;
        *x = presented_map.source.x + (*x - map->x) * presented_map.source.width / map->width;
        *y = presented_map.source.y + (*y - map->y) * presented_map.source.height / map->height;
        return TRUE;
    }
    for (unsigned i = 0; i < COUNT(hud); ++i) {
        Panel p = placed_panel(i);
        if (i >= NAV_START && !on_orb(*x - p.x, *y - p.y)) continue;
        if (map_panel(&p, x, y)) return TRUE;
    }
    return map_panel(&scene, x, y);
}

static BOOL edge_point(int *x, int *y)
{
    if (*x < 0 || *x >= WIDE_WIDTH || *y < 0 || *y >= HEIGHT) return FALSE;
    BOOL edge = FALSE;
    // The original camera checks the global mouse at the limits of 640x480.
    if (*x < 6) { *x = 0; edge = TRUE; }
    else if (*x >= WIDE_WIDTH - 6) { *x = GAME_WIDTH - 1; edge = TRUE; }
    if (*y < 6) { *y = 0; edge = TRUE; }
    else if (*y >= HEIGHT - 6) { *y = HEIGHT - 1; edge = TRUE; }
    return edge;
}

typedef int (__thiscall *WorldMouse)(unsigned char *, int, int);

static BOOL request_pickup(POINT tile)
{
    const unsigned char *map = GAME_MAP;
    if (!map || loot_map != map || !world_view_active()) return FALSE;
    typedef const unsigned char *(__thiscall *FindGroundItem)(const unsigned char *, int, int);
    typedef int (__thiscall *HasSelection)(const void *);
    typedef void (__thiscall *PartyOrder)(const void *, int, int, int, unsigned, int, int);
    const unsigned char *item = ((FindGroundItem)(uintptr_t)0x45C1D0)(map, tile.x, tile.y);
    if (!item || (item[0x38] != 0 && item[0x38] != 1)) return FALSE;
    const void *party = map + 0x14AA8;
    // Match the campaign's right-click pickup order at 0x4670B2. The
    // engine chooses a selected character and handles movement and capacity.
    if (((HasSelection)(uintptr_t)0x4346D0)(party)) {
        PartyOrder command = (*(PartyOrder const * const *)party)[2];
        command(party, 6, tile.x, tile.y, item[0x38], *(const int *)(item + 0x44), 0);
    }
    return TRUE;
}

static BOOL world_mouse(UINT message, int x, int y, BOOL extra)
{
    unsigned char *view = GAME_VIEW;
    if (!view || !world_view_active()) {
        wide_drag = 0;
        return FALSE;
    }
    uintptr_t handler;
    switch (message) {
    case WM_MOUSEMOVE: handler = 0x4662B0; break;
    case WM_LBUTTONDOWN: handler = 0x465F00; wide_drag |= 1; break;
    case WM_LBUTTONUP: handler = 0x466130; wide_drag &= ~1u; break;
    case WM_RBUTTONDOWN: handler = 0x4661E0; wide_drag |= 2; break;
    case WM_RBUTTONUP: wide_drag &= ~2u; return TRUE;
    default: return extra;
    }
    x = clamp(x, WORLD_TOP, presented_world_right - 1);
    y = clamp(y, WORLD_TOP, BELT_Y - 1);
    int source_width = displayed_view.columns * TILE;
    x = WORLD_TOP + (x - WORLD_TOP) * source_width / (presented_world_right - WORLD_TOP);
    y = WORLD_TOP + (y - WORLD_TOP) * displayed_view.height / VIEW_HEIGHT;
    GAME_MOUSE = MAKELPARAM(min(x, GAME_WIDTH - 3), min(y, HEIGHT - 3));
    int *right = (int *)(view + 0x14);
    int *bottom = (int *)(view + 0x18);
    int original_right = *right;
    int original_bottom = *bottom;
    *right = WORLD_TOP + source_width;
    *bottom = WORLD_TOP + displayed_view.height;
    ((WorldMouse)handler)(view, x, y);
    *right = original_right;
    *bottom = original_bottom;
    return TRUE;
}

static void move_minimap(int x, int y)
{
    int dx = x - map_press_x, dy = y - map_press_y;
    if (dx <= -4 || dx >= 4 || dy <= -4 || dy >= 4) map_moved = TRUE;
    if (!map_moved) return;
    map_frame.x = clamp(map_start_x + dx, 16, presented_world_right - 16 - map_frame.width);
    map_frame.y = clamp(map_start_y + dy, 16, BELT_Y - map_frame.height);
    has_frame = 0;
}

static void select_party(unsigned slot)
{
    const unsigned char *map = GAME_MAP;
    if (!map) return;
    unsigned present = sync_party(map, FALSE);
    if (slot == SELECT_ALL) {
        party_selection = present && (party_selection & present) == present ? 0 : 7;
    } else if (exclusive_party) {
        party_selection = 1u << slot;
    } else {
        party_selection ^= (1u << slot) & present;
    }
    sync_party(map, 7);
}

static void focus_party(HWND window, unsigned slot)
{
    const Panel *portrait = &hud[slot + 1];
    LPARAM position = MAKELPARAM(portrait->source_x + portrait->width / 2,
        portrait->source_y + portrait->height / 2);
    const unsigned char *map = GAME_MAP;
    if (map) sync_party(map, FALSE);
    GAME_WNDPROC(window, WM_RBUTTONDOWN, MK_RBUTTON, position);
    GAME_WNDPROC(window, WM_RBUTTONUP, 0, position);
}

// Closing the window ends the process without the DirectPlay cleanup the game does
// when it leaves the session through its menu, so the other players keep a ghost
// of this one until the connection times out. Run the same steps first (all three
// are safe to repeat: they test the interface pointer).
static void leave_net_session(void)
{
    typedef void (__thiscall *Destroy)(void *, DWORD);
    typedef void (__thiscall *Close)(void *);
    static BOOL left;
    if (left || !NET_MULTI) return;
    left = TRUE;
    if (net_log_enabled()) net_log_line('L', 0, 0, 0, NULL, 0);
    void *session = (void *)(uintptr_t)0x49B9A8;
    ((Destroy)(uintptr_t)0x401FC0)(session, NET_LOCAL_ID);
    ((Close)(uintptr_t)0x401B10)(session);
    ((Close)(uintptr_t)0x401B20)(session);
}

__declspec(dllexport) LRESULT CALLBACK HudWindow(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_CLOSE || message == WM_DESTROY) leave_net_session();
    if ((message == WM_KEYDOWN || message == WM_KEYUP || message == WM_SYSKEYDOWN || message == WM_SYSKEYUP)
        && !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000)) {
        unsigned key = (unsigned)wparam & 255;
        if (native_translate[key]) wparam = native_translate[key];
        else if (native_blocked[key]) return 0;
    }
    BOOL key_down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
    BOOL key_up = message == WM_KEYUP || message == WM_SYSKEYUP;
    if (key_down || key_up) {
        if (key_down && wparam == VK_ESCAPE) drop_held_item(window);
        unsigned mods = held_modifiers((unsigned)wparam);
        for (unsigned slot = 0; slot < COUNT(bindings); ++slot) {
            unsigned bit = 1u << slot;
            if (!bindings[slot] || (bindings[slot] & 255) != wparam) continue;
            if (slot == LOOT_KEY) { has_frame = 0; continue; }
            if (key_up && (binding_down & bit)) {
                binding_down &= ~bit;
                return 0;
            }
            if (key_down && bindings[slot] == ((unsigned)wparam | mods)
                && GAME_SCENE == CAMPAIGN_SCENE && world_view_active()) {
                if (!(binding_down & bit) && !(lparam & (1L << 30))) {
                    if (slot < FOCUS_FIRST) select_party(slot);
                    else focus_party(window, slot - FOCUS_FIRST);
                }
                binding_down |= bit;
                return 0;
            }
        }
    }
    if ((message == WM_KEYDOWN || message == WM_KEYUP) && camera_keys_active()
        && (wparam == 'W' || wparam == 'A' || wparam == 'S' || wparam == 'D')) return 0;
    if (wide_mode && message == WM_CAPTURECHANGED && (HWND)lparam != window) map_dragging = FALSE;
    if (wide_mode && (message == WM_MOUSELEAVE || message == WM_KILLFOCUS)) {
        tracking_mouse = FALSE;
        pointer_x = pointer_y = -1;
        map_minimize_down = FALSE;
        if (message == WM_KILLFOCUS) {
            drag_buttons = 0;
            wide_drag = 0;
            map_dragging = FALSE;
            map_right_down = FALSE;
            loot_click = FALSE;
            sidebar_click = FALSE;
            zoom_down = FALSE;
            binding_down = 0;
        }
        GAME_WNDPROC(window, WM_MOUSEMOVE, 0, NEUTRAL_MOUSE);
    }
    if (wide_mode && wparam == VK_F8 && (message == WM_KEYUP || message == WM_SYSKEYUP) && zoom_down) {
        zoom_down = FALSE;
        return 0;
    }
    if (wide_mode && (wparam == VK_F10
        || (show_hud && GAME_SCENE == CAMPAIGN_SCENE && world_view_active() && wparam == VK_F8)
        || (presented_hud && wparam == VK_F9))) {
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
            if (!(lparam & (1L << 30)) && !(wparam == VK_F8 && zoom_down)) {
                if (wparam == VK_F10) show_hud = !show_hud;
                else if (wparam == VK_F9) {
                    map_hidden = !map_hidden;
                    map_minimize_down = FALSE;
                }
                else {
                    zoom_down = TRUE;
                    zoom_level = (zoom_level + 1) % COUNT(camera_views);
                    recenter_zoom = TRUE;
                }
                if (!show_hud) {
                    CAMERA_COLUMNS = 14;
                    CAMERA_ROWS = NATIVE_ROWS;
                    CAMERA_REVEAL_X = CAMERA_REVEAL_Y = 31;
                }
                if (map_dragging && GetCapture() == window) ReleaseCapture();
                map_dragging = FALSE;
                has_frame = 0;
            }
            return 0;
        }
        if (message == WM_KEYUP || message == WM_SYSKEYUP) return 0;
    }
    if (wide_mode && message >= WM_MOUSEMOVE && message <= WM_MBUTTONDBLCLK) {
        int x = (short)LOWORD(lparam);
        int y = (short)HIWORD(lparam);
        pointer_x = x;
        pointer_y = y;
        if (presented_menu && !presented_pause && GAME_SCENE == CAMPAIGN_SCENE && !drag_buttons) {
            int orb = hovered_orb();
            if (message == WM_LBUTTONDOWN && orb >= 0) {
                sidebar_click = TRUE;
                return 0;
            }
            if (message == WM_LBUTTONUP && sidebar_click) {
                sidebar_click = FALSE;
                drop_held_item(window);
                unsigned char mode = GAME_VIEW ? GAME_VIEW[0x8C] : 0;
                if (orb == 2 && (mode == 2 || mode == 3))
                    open_journal_pending = click_toolbar(mode == 2 ? 0 : 3);
                else if (orb >= 0) click_toolbar((unsigned)orb);
                has_frame = 0;
                return 0;
            }
        }
        if (message == WM_LBUTTONUP && loot_click) {
            loot_click = FALSE;
            return 0;
        }
        BOOL map_minimize = over_map_minimize(x, y);
        if ((message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) && map_minimize
            && !drag_buttons && !wide_drag) {
            map_minimize_down = TRUE;
            return 0;
        }
        if (message == WM_LBUTTONUP && map_minimize_down) {
            map_minimize_down = FALSE;
            if (map_minimize) {
                map_hidden = TRUE;
                has_frame = 0;
            }
            return 0;
        }
        if (message == WM_LBUTTONDOWN && presented_wide_world && !map_dragging && !drag_buttons && !wide_drag) {
            POINT tile;
            if ((loot_at_label(x, y, &tile) || loot_under_pointer(x, y, &tile)) && request_pickup(tile)) {
                loot_click = TRUE;
                has_frame = 0;
                return 0;
            }
        }
        if (map_dragging && message == WM_LBUTTONUP) {
            move_minimap(x, y);
            map_dragging = FALSE;
            if (GetCapture() == window) ReleaseCapture();
            int mx = map_press_x, my = map_press_y;
            if (!map_moved && presented_hud && presented_map.target.width
                && in_rect(&presented_map.target, mx, my) && source_point(&mx, &my)) {
                LPARAM position = MAKELPARAM(mx, my);
                GAME_WNDPROC(window, WM_LBUTTONDOWN, MK_LBUTTON, position);
                GAME_WNDPROC(window, WM_LBUTTONUP, wparam, position);
            }
            return 0;
        }
        if (map_dragging && message == WM_MOUSEMOVE) {
            move_minimap(x, y);
            return 0;
        }
        if (message == WM_RBUTTONUP && map_right_down) {
            map_right_down = FALSE;
            return 0;
        }
        BOOL over_map = presented_hud && !presented_quest && !presented_dialogue
            && presented_map.target.width && in_rect(&map_frame, x, y);
        if (over_map && !drag_buttons && !wide_drag) {
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONDBLCLK) {
                map_press_x = x;
                map_press_y = y;
                map_start_x = map_frame.x;
                map_start_y = map_frame.y;
                map_moved = FALSE;
                map_dragging = TRUE;
                SetCapture(window);
                GAME_WNDPROC(window, WM_MOUSEMOVE, 0, NEUTRAL_MOUSE);
                return 0;
            }
            if (message == WM_RBUTTONDOWN) {
                map_right_down = TRUE;
                map_detail = !map_detail;
                has_frame = 0;
                return 0;
            }
            if (message == WM_RBUTTONDBLCLK) return 0;
        }
        if (message == WM_MOUSEMOVE && !tracking_mouse) {
            TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE, window, 0};
            tracking_mouse = TrackMouseEvent(&track);
        }
        BOOL over_button = FALSE;
        if (presented_wide_world) {
            for (unsigned i = NAV_START; i < COUNT(hud); ++i) {
                Panel p = placed_panel(i);
                if (x >= p.x && x < p.x + p.width && y >= p.y && y < p.y + p.height
                    && on_orb(x - p.x, y - p.y)) over_button = TRUE;
            }
        }
        BOOL edge = presented_hud && !presented_menu && !presented_dialogue && !presented_quest && !over_map && !over_button
            && message == WM_MOUSEMOVE
            && !drag_buttons && !wide_drag && edge_point(&x, &y);
        if (edge) return GAME_WNDPROC(window, message, wparam, MAKELPARAM(x, y));
        BOOL terrain = presented_wide_world && !presented_quest && !presented_dialogue && !over_map && !over_button && x >= WORLD_TOP
            && x < presented_world_right && y >= WORLD_TOP && y < BELT_Y && (y >= 54 || x >= WORLD_RIGHT);
        if (click_marker_enabled && terrain && message == WM_RBUTTONDOWN && !drag_buttons && !wide_drag
            && world_view_active() && GAME_MAP && GAME_VIEW && !*(const unsigned char * const *)(GAME_VIEW + 0xD0)
            && !loot_under_pointer(x, y, &(POINT){0})) {
            const unsigned char *map = GAME_MAP;
            click_marker.map = map;
            click_marker.x = *(const int *)(map + 0x14) * TILE
                + (x - WORLD_TOP) * displayed_view.columns * TILE / (presented_world_right - WORLD_TOP);
            click_marker.y = *(const int *)(map + 0x18) * TILE
                + (y - WORLD_TOP) * displayed_view.height / VIEW_HEIGHT;
            click_marker.start = GetTickCount();
        }
        if ((terrain || wide_drag) && !drag_buttons && world_mouse(message, x, y, terrain)) return 0;
        BOOL mapped = source_point(&x, &y);
        if (presented_menu) menu_outside = FALSE;
        if (!mapped && presented_menu && !presented_pause && !presented_world_map
            && GAME_SCENE == CAMPAIGN_SCENE && x >= 0 && x < WIDE_WIDTH && y >= 0 && y < HEIGHT) {
            // The sheet hides the native right column, whose dark pit is
            // where a held item is dropped. Portraits keep their own spots; any
            // other point outside the sheet lands in the pit at the top (the old minimap).
            for (unsigned i = 1; i < NAV_START && !mapped; ++i) {
                Panel portrait = hud[i];
                mapped = map_panel(&portrait, &x, &y);
            }
            if (!mapped) {
                x = DROP_X;
                y = DROP_Y;
                mapped = TRUE;
                menu_outside = TRUE;
            }
        }
        unsigned button = message == WM_MOUSEMOVE ? 0 : 1u << ((message - WM_LBUTTONDOWN) / 3);
        BOOL up = message == WM_LBUTTONUP || message == WM_RBUTTONUP || message == WM_MBUTTONUP;
        if (mapped) last_mouse = MAKELPARAM(x, y);
        if (button) {
            if (up) {
                if (!(drag_buttons & button)) return 0;
                drag_buttons &= ~button;
            } else {
                if (!mapped) return 0;
                drag_buttons |= button;
            }
        }
        lparam = mapped || drag_buttons || up ? last_mouse : presented_pause ? 0 : NEUTRAL_MOUSE;
    }
    return GAME_WNDPROC(window, message, wparam, lparam);
}




// Opt-in network trace. Create a file named netlog.enable beside the game (or
// set AKUMA_NETLOG=1) and every DirectPlay Send/Receive is appended to
// netlog.txt as: ms dir host me from to flags size hex. Nothing is written
// otherwise, so it costs one flag test per message.
enum { NET_LOG_BYTES = 4096 };
static HANDLE net_log = INVALID_HANDLE_VALUE;
static int net_log_state = -1;
static DWORD net_log_start;

static BOOL net_log_enabled(void)
{
    if (net_log_state < 0) {
        char flag[2] = "";
        BOOL on = GetEnvironmentVariableA("AKUMA_NETLOG", flag, sizeof(flag)) == 1 && flag[0] == '1';
        if (!on) on = GetFileAttributesA("netlog.enable") != INVALID_FILE_ATTRIBUTES;
        if (on) {
            net_log = CreateFileA("netlog.txt", FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, NULL);
            if (net_log == INVALID_HANDLE_VALUE) {
                // Another copy of the game already owns netlog.txt.
                char other[40];
                wsprintfA(other, "netlog-%lu.txt", GetCurrentProcessId());
                net_log = CreateFileA(other, FILE_APPEND_DATA, FILE_SHARE_READ, NULL, OPEN_ALWAYS,
                    FILE_ATTRIBUTE_NORMAL, NULL);
            }
            on = net_log != INVALID_HANDLE_VALUE;
            net_log_start = GetTickCount();
        }
        net_log_state = on;
    }
    return net_log_state == 1;
}

static void net_log_line(char direction, DWORD from, DWORD to, DWORD flags, const unsigned char *data, DWORD size)
{
    static const char digits[] = "0123456789abcdef";
    char line[96 + NET_LOG_BYTES * 2];
    int length = wsprintfA(line, "%lu %c %lu %lu %lu %lu %lu %lu ", GetTickCount() - net_log_start, direction,
        NET_HOST, NET_LOCAL_ID, from, to, flags, size);
    DWORD shown = data ? (size < NET_LOG_BYTES ? size : NET_LOG_BYTES) : 0;
    for (DWORD i = 0; i < shown; ++i) {
        line[length++] = digits[data[i] >> 4];
        line[length++] = digits[data[i] & 15];
    }
    line[length++] = '\n';
    DWORD written;
    WriteFile(net_log, line, (DWORD)length, &written, NULL);
}

__declspec(dllexport) void WINAPI HudNetSend(DWORD from, DWORD to, DWORD flags, const void *data, DWORD size)
{
    if (net_log_enabled()) net_log_line('S', from, to, flags, data, size);
}

__declspec(dllexport) void WINAPI HudNetRecv(DWORD from, DWORD to, const void *data, DWORD size)
{
    if (net_log_enabled()) net_log_line('R', from, to, 0, data, size);
}

// Every two seconds, with the same flag, dump what the HUD sees of the party
// and of the name labels, so multiplayer HUD problems can be read from the log:
//   <ms> P scene multi host me map wide hud
//        actors  [slot k<hero kind> a<+0x750> l<+0x40> o<owner id> hp/max s<selected> @x,y]
//        hints   "text"@x,y
static void net_log_party(void)
{
    if (!net_log_enabled()) return;
    DWORD now = GetTickCount();
    static DWORD last;
    if (last && now - last < 2000) return;
    last = now;
    const unsigned char *map = GAME_MAP;
    char line[3072];
    static unsigned long last_tiles;
    unsigned long tiles_now = tile_calls;
    // tiles = world tile draws since the previous line; clock = the engine's session clock (0x49B8E8);
    // view = the view's menu flag (+0xE4); dlg = pause owner flag (0x49B8DC). If "tiles" stays above
    // zero while pause=1 the engine keeps drawing the world behind a menu.
    int length = wsprintfA(line, "%lu P scene=%lu multi=%lu host=%lu me=%lu map=%lx wide=%d hud=%d zoom=%u chat=%lu pause=%lu held=%d menu=%d tiles=%lu clock=%lu view=%lu dlg=%lu",
        now - net_log_start, GAME_SCENE, NET_MULTI, NET_HOST, NET_LOCAL_ID, (unsigned long)(uintptr_t)map,
        wide_mode, show_hud, zoom_level, GAME_CHAT_OPEN, *(const DWORD *)(uintptr_t)0x49B8E0,
        loot_held(), presented_menu, tiles_now - last_tiles, *(const DWORD *)(uintptr_t)0x49B8E8,
        GAME_VIEW ? *(const DWORD *)(GAME_VIEW + 0xE4) : 0, *(const DWORD *)(uintptr_t)0x49B8DC);
    last_tiles = tiles_now;
    if (map) {
        // map+0x14AAC holds up to 20 heroes: the local ones (a1) and, in multiplayer, the
        // other players' (a0). The HUD party code only accepts a1.
        const unsigned char * const *actors = (const unsigned char * const *)(map + 0x14AAC);
        for (unsigned i = 0; i < 20 && length < 2000; ++i) {
            const unsigned char *actor = actors[i];
            if (!actor) continue;
            char name[17] = "";
            for (int c = 0; c < 16; ++c) {
                unsigned char ch = actor[0x740 + c];
                if (!ch) break;
                name[c] = ch < 32 || ch == '"' ? '?' : (char)ch;
            }
            length += wsprintfA(line + length, " [%u \"%s\" k%d a%d l%d o%lu hp%d/%d s%d @%d,%d v%lx]", i, name,
                *(const int *)(actor + 0x34), *(const int *)(actor + 0x750), *(const int *)(actor + 0x40),
                *(const unsigned long *)(actor + 0x754), *(const int *)(actor + 0x810),
                *(const int *)(actor + 0x7E0), *(const int *)(actor + 0x2AC),
                *(const int *)(actor + 0x0C) >> 5, *(const int *)(actor + 0x10) >> 5,
                *(const unsigned long *)actor);
        }
    }
    for (unsigned i = 0; i < world_hint_count && length < 2900; ++i) {
        const NativeHint *hint = &world_hint[i];
        length += wsprintfA(line + length, " \"");
        for (int c = 0; c < hint->count && c < 40 && length < 2950; ++c) {
            unsigned char ch = (unsigned char)hint->text[c];
            line[length++] = ch < 32 || ch == '"' ? '?' : (char)ch;
        }
        length += wsprintfA(line + length, "\"@%d,%d", hint->x, hint->y);
    }
    if (notice_hint.count)
        length += wsprintfA(line + length, " notice=\"%.40s\"@%d,%d", notice_hint.text, notice_hint.x, notice_hint.y);
    if (GAME_CHAT_OPEN) {
        const unsigned char *chat = *(const unsigned char * const *)(uintptr_t)0x4998E4;
        HWND edit = chat ? *(HWND const *)(chat + 0x28) : NULL;
        HWND parent = *(HWND const *)(uintptr_t)0x49B8A4;
        RECT box = {0}, client = {0};
        if (edit && IsWindow(edit)) GetWindowRect(edit, &box);
        if (parent) GetClientRect(parent, &client);
        length += wsprintfA(line + length, " edit=%lx vis=%d rect=%ld,%ld,%ld,%ld client=%ldx%ld",
            (unsigned long)(uintptr_t)edit, edit ? IsWindowVisible(edit) : 0, box.left, box.top, box.right,
            box.bottom, client.right, client.bottom);
    }
    line[length++] = '\n';
    DWORD written;
    WriteFile(net_log, line, (DWORD)length, &written, NULL);
}

// The multiplayer screens (character, host and connection) are native dialogs laid
// out for 640x480 that the game places at its original window rectangle. Centre
// them in the game window and, with dialogscale.enable (or AKUMA_DIALOG_SCALE=1),
// scale the dialog, its controls and their fonts to the window. With the netlog
// flag on, each dialog and its controls are recorded ("D" lines).
static BOOL dialog_scaling(void)
{
    static int state = -1;
    if (state < 0) {
        char flag[2] = "";
        state = (GetEnvironmentVariableA("AKUMA_DIALOG_SCALE", flag, sizeof(flag)) == 1 && flag[0] == '1')
            || GetFileAttributesA("dialogscale.enable") != INVALID_FILE_ATTRIBUTES;
    }
    return state == 1;
}

static void net_log_text(const char *text)
{
    if (!net_log_enabled()) return;
    char line[1024];
    int length = wsprintfA(line, "%lu D %.900s\n", GetTickCount() - net_log_start, text);
    DWORD written;
    WriteFile(net_log, line, (DWORD)length, &written, NULL);
}

static struct { HWND dialog; double scale; int offset_x, offset_y; } dialog_layout;

typedef struct { HWND dialog; double scale; int offset_x, offset_y, count; char text[700]; } DialogJob;

static BOOL CALLBACK scale_dialog_child(HWND child, LPARAM param)
{
    DialogJob *job = (DialogJob *)param;
    if (GetParent(child) != job->dialog) return TRUE;
    RECT box;
    char name[24] = "";
    GetWindowRect(child, &box);
    MapWindowPoints(NULL, job->dialog, (POINT *)&box, 2);
    GetClassNameA(child, name, sizeof(name));
    int used = lstrlenA(job->text);
    if (used < (int)sizeof(job->text) - 80)
        wsprintfA(job->text + used, " [%d %s %d,%d,%d,%d]", GetDlgCtrlID(child), name,
            box.left, box.top, box.right - box.left, box.bottom - box.top);
    ++job->count;
    if (job->scale <= 1.0) return TRUE;
    HFONT old = (HFONT)SendMessageA(child, WM_GETFONT, 0, 0);
    LOGFONTA face;
    if (old && GetObjectA(old, sizeof(face), &face)) {
        face.lfHeight = (LONG)(face.lfHeight * job->scale);
        face.lfWidth = 0;
        if (hud_language == CHINESE || hud_language == KOREAN) {
            // The dialog font is the bitmap "System" font, which has no CJK glyphs (they show as ?).
            lstrcpyA(face.lfFaceName, hud_language == CHINESE ? "Microsoft JhengHei" : "Malgun Gothic");
            face.lfCharSet = DEFAULT_CHARSET;
        }
        HFONT bigger = CreateFontIndirectA(&face);
        if (bigger) SendMessageA(child, WM_SETFONT, (WPARAM)bigger, TRUE);
    }
    int new_height = (int)((box.bottom - box.top) * job->scale);
    if (!lstrcmpA(name, "ComboBox")) {
        // For a combo box the window height is the drop-down height too. The closed height we read is
        // only the selection field, so keep room for the list or it collapses and only the first
        // entry (for instance "2 players") can be picked.
        LRESULT rows = SendMessageA(child, CB_GETCOUNT, 0, 0), item = SendMessageA(child, CB_GETITEMHEIGHT, 0, 0);
        if (rows < 1 || rows == CB_ERR) rows = 6;
        if (rows > 10) rows = 10;
        if (item <= 0 || item == CB_ERR) item = new_height;
        new_height += (int)(rows * item) + 6;
    }
    MoveWindow(child, job->offset_x + (int)(box.left * job->scale), job->offset_y + (int)(box.top * job->scale),
        (int)((box.right - box.left) * job->scale), new_height, TRUE);
    return TRUE;
}

typedef struct { DWORD pid; HWND found; long area; } WindowSearch;

static BOOL CALLBACK find_game_window(HWND window, LPARAM param)
{
    WindowSearch *search = (WindowSearch *)param;
    DWORD pid = 0;
    char name[32] = "";
    RECT client;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid || !IsWindowVisible(window)) return TRUE;
    GetClassNameA(window, name, sizeof(name));
    if (!lstrcmpA(name, "#32770") || !GetClientRect(window, &client)) return TRUE;
    long area = client.right * client.bottom;
    if (area > search->area) {
        search->area = area;
        search->found = window;
    }
    return TRUE;
}

// cnc-ddraw makes the game believe its window is the logical size (854x480): the
// usual GetClientRect/GetWindowRect answer for that window with it. Ask for the real
// client area another way: GetWindowInfo first, then the true frame from the window
// manager together with the width chosen in the launcher (ddraw.ini).
typedef HRESULT (WINAPI *FrameBounds)(HWND, DWORD, void *, DWORD);

static BOOL game_client_rect(RECT *out, char *how, size_t how_size)
{
    WindowSearch search = {GetCurrentProcessId(), NULL, 0};
    EnumWindows(find_game_window, (LPARAM)&search);
    if (!search.found) return FALSE;
    RECT told = {0};
    GetClientRect(search.found, &told);
    WINDOWINFO info = {sizeof(info)};
    if (GetWindowInfo(search.found, &info)) {
        int width = info.rcClient.right - info.rcClient.left, height = info.rcClient.bottom - info.rcClient.top;
        if (width > 0 && height > 0 && (width != told.right || height != told.bottom)) {
            *out = info.rcClient;
            wsprintfA(how, "info");
            return TRUE;
        }
    }
    RECT frame = {0};
    HMODULE dwm = LoadLibraryA("dwmapi.dll");
    FrameBounds bounds = dwm ? (FrameBounds)(void *)GetProcAddress(dwm, "DwmGetWindowAttribute") : NULL;
    if (!bounds || FAILED(bounds(search.found, 9, &frame, sizeof(frame)))) return FALSE;
    char ini[MAX_PATH] = "";
    if (!GetEnvironmentVariableA("CNC_DDRAW_CONFIG_FILE", ini, sizeof(ini))) lstrcpyA(ini, ".\\ddraw.ini");
    int configured_width = (int)GetPrivateProfileIntA("ddraw", "width", 0, ini);
    int configured_height = (int)GetPrivateProfileIntA("ddraw", "height", 0, ini);
    int frame_width = frame.right - frame.left, frame_height = frame.bottom - frame.top;
    int width = configured_width > 0 && configured_width <= frame_width ? configured_width : frame_width;
    int height = configured_height > 0 && configured_height <= frame_height ? configured_height : frame_height;
    int side = (frame_width - width) / 2;
    out->left = frame.left + side;
    out->right = out->left + width;
    out->bottom = frame.bottom - side;
    out->top = out->bottom - height;
    wsprintfA(how, "dwm ini=%dx%d frame=%ld,%ld,%ld,%ld", configured_width, configured_height,
        frame.left, frame.top, frame.right, frame.bottom);
    return TRUE;
}

static void adapt_dialog(HWND dialog)
{
    char name[32] = "";
    GetClassNameA(dialog, name, sizeof(name));
    RECT box;
    if (lstrcmpA(name, "#32770") || !GetWindowRect(dialog, &box)) return;
    // The game sizes these dialogs to a stored rectangle (the whole screen when it is
    // maximised), which also covers the title bar. Fit the dialog to the game window's
    // client area, and lay its controls out in a 640x480 box inside it.
    RECT view = box;
    char how[160] = "none";
    game_client_rect(&view, how, sizeof(how));
    int width = view.right - view.left, height = view.bottom - view.top;
    if (width <= 0 || height <= 0) return;
    SetWindowPos(dialog, NULL, view.left, view.top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    double scale = dialog_scaling() ? min(width / 640.0, height / 480.0) : 1.0;
    if (scale < 1.05) scale = 1.0;
    int offset_x = (int)((width - 640 * scale) / 2), offset_y = (int)((height - 480 * scale) / 2);
    DialogJob job = {dialog, scale, offset_x, offset_y, 0, ""};
    EnumChildWindows(dialog, scale_dialog_child, (LPARAM)&job);
    dialog_layout.dialog = dialog;
    dialog_layout.scale = scale;
    dialog_layout.offset_x = offset_x;
    dialog_layout.offset_y = offset_y;
    if (net_log_enabled()) {
        char text[1000];
        wsprintfA(text, "dialog=%lx was=%ld,%ld,%ld,%ld view=%ld,%ld,%ld,%ld via=%s scale=%d%% children=%d%s",
            (unsigned long)(uintptr_t)dialog, box.left, box.top, box.right, box.bottom,
            view.left, view.top, view.right, view.bottom, how, (int)(scale * 100), job.count, job.text);
        net_log_text(text);
    }
}

static void translate_control(HWND window)
{
    WCHAR wide[160];
    char text[160];
    if (!GetWindowTextW(window, wide, COUNT(wide))) return;
    int length = WideCharToMultiByte(1252, 0, wide, -1, text, sizeof(text), NULL, NULL);
    if (length <= 1) return;
    const WCHAR *translated = phrase_for(dialog_texts, COUNT(dialog_texts), text, length - 1);
    if (translated) SetWindowTextW(window, translated);
}

static BOOL CALLBACK translate_child(HWND window, LPARAM unused)
{
    (void)unused;
    translate_control(window);
    return TRUE;
}

// With a relay configured in the launcher, the TCP/IP provider only makes sense as "relay": every
// socket of the game is routed to it. Rename the entry and tell players to leave the IP empty.
static BOOL relay_mode(void)
{
    char relay[8] = "", off[2] = "";
    if (GetEnvironmentVariableA("AKUMA_RELAY_OFF", off, sizeof(off)) == 1 && off[0] == '1') return FALSE;
    return GetEnvironmentVariableA("AKUMA_RELAY", relay, sizeof(relay)) > 0;
}

static void relay_dialog(HWND dialog)
{
    static const WCHAR *const entry[] = {L"Servidor Relay (Internet)", L"Relay Server (Internet)",
        L"릴레이 서버 (인터넷)", L"中繼伺服器 (網際網路)"};
    static const WCHAR *const hint[] = {L"Deixe vazio para procurar salas no relay", L"Leave empty to find rooms on the relay",
        L"비워 두면 릴레이의 방을 찾습니다", L"留空以搜尋中繼伺服器上的房間"};
    HWND list = GetDlgItem(dialog, 1011), label = GetDlgItem(dialog, 1016);
    if (!list || !label || !relay_mode()) return;
    LRESULT count = SendMessageW(list, LB_GETCOUNT, 0, 0);
    for (LRESULT i = 0; i < count; ++i) {
        WCHAR name[128] = L"";
        if (SendMessageW(list, LB_GETTEXTLEN, (WPARAM)i, 0) >= (LRESULT)COUNT(name)) continue;
        SendMessageW(list, LB_GETTEXT, (WPARAM)i, (LPARAM)name);
        if (!wcsstr(name, L"TCP")) continue;
        LRESULT selected = SendMessageW(list, LB_GETCURSEL, 0, 0);
        LRESULT data = SendMessageW(list, LB_GETITEMDATA, (WPARAM)i, 0);
        SendMessageW(list, LB_DELETESTRING, (WPARAM)i, 0);
        SendMessageW(list, LB_INSERTSTRING, (WPARAM)i, (LPARAM)entry[hud_language]);
        SendMessageW(list, LB_SETITEMDATA, (WPARAM)i, (LPARAM)data);
        if (selected != LB_ERR) SendMessageW(list, LB_SETCURSEL, (WPARAM)selected, 0);
        break;
    }
    SetWindowTextW(label, hint[hud_language]);
}

static void translate_dialog(HWND dialog)
{
    translate_control(dialog);
    EnumChildWindows(dialog, translate_child, 0);
    relay_dialog(dialog);
}

static LRESULT CALLBACK dialog_hook(int code, WPARAM wparam, LPARAM lparam)
{
    if (code == HC_ACTION && ((const CWPRETSTRUCT *)lparam)->message == WM_INITDIALOG) {
        translate_dialog(((const CWPRETSTRUCT *)lparam)->hwnd);
        adapt_dialog(((const CWPRETSTRUCT *)lparam)->hwnd);
    }
    return CallNextHookEx(NULL, code, wparam, lparam);
}

// Crash log: writes crash.txt (code, address, module+offset, registers, stack) when the game
// dies from an unhandled exception, so a failure can be diagnosed from a file.
static LONG WINAPI crash_filter(EXCEPTION_POINTERS *info)
{
    FILE *file = fopen("crash.txt", "a");
    if (file) {
        const CONTEXT *c = info->ContextRecord;
        HMODULE module = NULL;
        char name[MAX_PATH] = "?";
        GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            (LPCSTR)info->ExceptionRecord->ExceptionAddress, &module);
        if (module) GetModuleFileNameA(module, name, sizeof(name));
        const char *base = strrchr(name, '\\');
        char lang[8] = "";
        GetEnvironmentVariableA("AKUMA_LANGUAGE", lang, sizeof(lang));
        fprintf(file, "exception %08lX at %p (%s+%lX) lang=%s", info->ExceptionRecord->ExceptionCode,
            info->ExceptionRecord->ExceptionAddress, base ? base + 1 : name,
            (unsigned long)((uintptr_t)info->ExceptionRecord->ExceptionAddress - (uintptr_t)module), lang);
        if (info->ExceptionRecord->NumberParameters >= 2)
            fprintf(file, " access=%lu addr=%08lX", (unsigned long)info->ExceptionRecord->ExceptionInformation[0],
                (unsigned long)info->ExceptionRecord->ExceptionInformation[1]);
        fprintf(file, "\neax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX\nstack:",
            c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
        const DWORD *stack = (const DWORD *)c->Esp;
        for (int i = 0; i < 24 && !IsBadReadPtr(stack + i, 4); ++i) fprintf(file, " %08lX", stack[i]);
        fprintf(file, "\n\n");
        fclose(file);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void install_crash_log(void)
{
    SetUnhandledExceptionFilter(crash_filter);
}

static void install_dialog_hook(void)
{
    static HHOOK hook;
    if (!hook) hook = SetWindowsHookExA(WH_CALLWNDPROCRET, dialog_hook, NULL, GetCurrentThreadId());
}

// Optional ready-made backgrounds: dialogbg\<width>x<height>.bmp, one per window size
// (for instance 1600x900.bmp). The game's own 640x480 art is written once to
// dialogbg\original-640x480.bmp as a starting point for better versions.
static HBITMAP dialog_art(int width, int height)
{
    static HBITMAP cached;
    static int cached_width, cached_height;
    if (cached_width != width || cached_height != height) {
        char path[64];
        if (cached) DeleteObject(cached);
        wsprintfA(path, "dialogbg\\%dx%d.bmp", width, height);
        cached = (HBITMAP)LoadImageA(NULL, path, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);
        cached_width = width;
        cached_height = height;
    }
    return cached;
}

static void dump_dialog_art(const void *bits, const BITMAPINFO *info)
{
    static BOOL done;
    if (done || !bits || !info || info->bmiHeader.biWidth != 640 || labs(info->bmiHeader.biHeight) != 480) return;
    done = TRUE;
    CreateDirectoryA("dialogbg", NULL);
    HANDLE file = CreateFileA("dialogbg\\original-640x480.bmp", GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    const BITMAPINFOHEADER *header = &info->bmiHeader;
    DWORD palette = header->biCompression == BI_BITFIELDS ? 12
        : header->biClrUsed ? header->biClrUsed * 4 : header->biBitCount <= 8 ? (1u << header->biBitCount) * 4 : 0;
    DWORD stride = ((DWORD)header->biWidth * header->biBitCount + 31) / 32 * 4;
    DWORD pixels = stride * (DWORD)labs(header->biHeight);
    BITMAPFILEHEADER head = {0x4D42, 14 + header->biSize + palette + pixels, 0, 0, 14 + header->biSize + palette};
    DWORD written;
    WriteFile(file, &head, sizeof(head), &written, NULL);
    WriteFile(file, info, header->biSize + palette, &written, NULL);
    WriteFile(file, bits, pixels, &written, NULL);
    CloseHandle(file);
}

// The dialogs paint their 640x480 background art with one StretchDIBits call
// (0x46B3EB). In a scaled dialog, use a ready-made image for this window size when
// there is one, otherwise stretch the art to the same box as the controls and blank
// the rest, so the art, the portraits and the controls stay aligned.
__declspec(dllexport) int WINAPI HudDialogBlt(HDC dc, int x, int y, int width, int height, int source_x,
    int source_y, int source_width, int source_height, const void *bits, const BITMAPINFO *info,
    UINT usage, DWORD operation)
{
    dump_dialog_art(bits, info);
    HWND window = WindowFromDC(dc);
    if (window && window == dialog_layout.dialog && dialog_layout.scale > 1.0) {
        RECT client;
        if (GetClientRect(window, &client)) {
            FillRect(dc, &client, (HBRUSH)GetStockObject(BLACK_BRUSH));
            HBITMAP art = dialog_art(client.right, client.bottom);
            if (art) {
                HDC memory = CreateCompatibleDC(dc);
                HGDIOBJ previous = SelectObject(memory, art);
                BitBlt(dc, 0, 0, client.right, client.bottom, memory, 0, 0, SRCCOPY);
                SelectObject(memory, previous);
                DeleteDC(memory);
                return source_height;
            }
        }
        SetStretchBltMode(dc, HALFTONE);
        SetBrushOrgEx(dc, 0, 0, NULL);
        return StretchDIBits(dc, dialog_layout.offset_x + (int)(x * dialog_layout.scale),
            dialog_layout.offset_y + (int)(y * dialog_layout.scale), (int)(width * dialog_layout.scale),
            (int)(height * dialog_layout.scale), source_x, source_y, source_width, source_height, bits, info,
            usage, operation);
    }
    return StretchDIBits(dc, x, y, width, height, source_x, source_y, source_width, source_height, bits, info,
        usage, operation);
}
