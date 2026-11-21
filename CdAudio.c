/*
 * CD audio emulation for the portable Akuma build.
 *
 * The game plays its soundtrack through the MCI "cdaudio" device
 * (mciSendCommandA). This module replaces that import with a player that reads
 * Music\trackNN.ogg next to the executable, so the game does not need the disc.
 * If no Music folder exists, every call is forwarded to the real WINMM.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define STB_VORBIS_NO_PUSHDATA_API
#include "third_party/stb_vorbis.c"

enum { FAKE_DEVICE = 0x7A11, MAX_TRACK = 99 };

typedef MCIERROR (WINAPI *MciSendCommand)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
typedef MMRESULT (WINAPI *AuxVolumeSet)(UINT, DWORD);
typedef MMRESULT (WINAPI *AuxVolumeGet)(UINT, LPDWORD);

static MciSendCommand real_mci;
static AuxVolumeSet real_aux_set;
static AuxVolumeGet real_aux_get;

static char music_dir[MAX_PATH];
static int track_count;               /* highest track number found, CD style */
static CRITICAL_SECTION lock;
static HANDLE worker;
static HANDLE stop_event;
static HWAVEOUT current_out;
static volatile LONG generation;
static HWND notify_window;
static BOOL notify_pending;
static BOOL paused;
static DWORD volume = 0xFFFFFFFFu;

typedef struct {
    int first, last;                  /* inclusive track range */
    HWND window;
    BOOL notify;
    LONG generation;
} PlayJob;

static void send_notify(HWND window, WPARAM status)
{
    if (window) PostMessageA(window, MM_MCINOTIFY, status, FAKE_DEVICE);
}

static BOOL play_track(int number, LONG gen)
{
    char path[MAX_PATH];
    short *pcm = NULL;
    int channels = 0, rate = 0;
    snprintf(path, sizeof path, "%s\\track%02d.ogg", music_dir, number);
    int frames = stb_vorbis_decode_filename(path, &channels, &rate, &pcm);
    if (frames <= 0 || !pcm) return TRUE;               /* missing track: skip */
    if (gen != generation) { free(pcm); return FALSE; }

    HANDLE done = CreateEventA(NULL, FALSE, FALSE, NULL);
    WAVEFORMATEX format = {WAVE_FORMAT_PCM, (WORD)channels, (DWORD)rate,
                           (DWORD)(rate * channels * 2), (WORD)(channels * 2), 16, 0};
    HWAVEOUT out = NULL;
    BOOL finished = FALSE;
    if (waveOutOpen(&out, WAVE_MAPPER, &format, (DWORD_PTR)done, 0, CALLBACK_EVENT) == MMSYSERR_NOERROR) {
        WAVEHDR header = {0};
        header.lpData = (LPSTR)pcm;
        header.dwBufferLength = (DWORD)frames * channels * 2;
        waveOutSetVolume(out, volume);
        waveOutPrepareHeader(out, &header, sizeof header);
        EnterCriticalSection(&lock);
        current_out = out;
        paused = FALSE;
        LeaveCriticalSection(&lock);
        waveOutWrite(out, &header, sizeof header);
        HANDLE events[2] = {done, stop_event};
        for (;;) {
            DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);
            if (wait == WAIT_OBJECT_0 && (header.dwFlags & WHDR_DONE)) { finished = TRUE; break; }
            if (wait != WAIT_OBJECT_0) break;
        }
        EnterCriticalSection(&lock);
        current_out = NULL;
        LeaveCriticalSection(&lock);
        waveOutReset(out);
        waveOutUnprepareHeader(out, &header, sizeof header);
        waveOutClose(out);
    }
    CloseHandle(done);
    free(pcm);
    return finished;
}

static DWORD WINAPI play_thread(LPVOID argument)
{
    PlayJob job = *(PlayJob *)argument;
    free(argument);
    BOOL completed = TRUE;
    for (int track = job.first; track <= job.last && completed; track++)
        completed = play_track(track, job.generation);
    EnterCriticalSection(&lock);
    BOOL current = job.generation == generation;
    if (current) notify_pending = FALSE;
    LeaveCriticalSection(&lock);
    if (completed && current && job.notify) send_notify(job.window, MCI_NOTIFY_SUCCESSFUL);
    return 0;
}

/* Stops playback; the pending notification gets the given MCI status. */
static void stop_playback(WPARAM status)
{
    EnterCriticalSection(&lock);
    HANDLE old = worker;
    worker = NULL;
    BOOL notify = notify_pending;
    HWND window = notify_window;
    notify_pending = FALSE;
    paused = FALSE;
    InterlockedIncrement(&generation);
    LeaveCriticalSection(&lock);
    if (old) {
        SetEvent(stop_event);
        WaitForSingleObject(old, 3000);
        CloseHandle(old);
        ResetEvent(stop_event);
    }
    if (notify) send_notify(window, status);
}

static void start_playback(int first, int last, HWND window, BOOL notify)
{
    stop_playback(MCI_NOTIFY_SUPERSEDED);
    PlayJob *job = malloc(sizeof *job);
    if (!job) return;
    EnterCriticalSection(&lock);
    job->first = first;
    job->last = last;
    job->window = window;
    job->notify = notify;
    job->generation = generation;
    notify_pending = notify;
    notify_window = window;
    worker = CreateThread(NULL, 0, play_thread, job, 0, NULL);
    if (!worker) { notify_pending = FALSE; free(job); }
    LeaveCriticalSection(&lock);
}

static MCIERROR WINAPI emulated_mci(MCIDEVICEID device, UINT message, DWORD_PTR flags, DWORD_PTR parameters)
{
    if (message == MCI_OPEN && device == 0 && (flags & MCI_OPEN_TYPE_ID) && parameters) {
        MCI_OPEN_PARMSA *open = (MCI_OPEN_PARMSA *)parameters;
        if (((DWORD_PTR)open->lpstrDeviceType & 0xFFFF) == MCI_DEVTYPE_CD_AUDIO && track_count) {
            open->wDeviceID = FAKE_DEVICE;
            return 0;
        }
    }
    if (device != FAKE_DEVICE) return real_mci(device, message, flags, parameters);

    switch (message) {
    case MCI_STATUS: {
        MCI_STATUS_PARMS *status = (MCI_STATUS_PARMS *)parameters;
        if (status && (flags & MCI_STATUS_ITEM)) {
            status->dwReturn = status->dwItem == MCI_STATUS_NUMBER_OF_TRACKS ? (DWORD_PTR)track_count : 0;
            if (status->dwItem == MCI_STATUS_READY || status->dwItem == MCI_STATUS_MEDIA_PRESENT) status->dwReturn = 1;
        }
        return 0;
    }
    case MCI_PLAY: {
        MCI_PLAY_PARMS *play = (MCI_PLAY_PARMS *)parameters;
        if (!(flags & MCI_FROM)) {                       /* resume after pause */
            EnterCriticalSection(&lock);
            if (current_out && paused) { waveOutRestart(current_out); paused = FALSE; }
            LeaveCriticalSection(&lock);
            return 0;
        }
        int first = (int)(play->dwFrom & 0xFF);
        int last = (flags & MCI_TO) ? (int)(play->dwTo & 0xFF) - 1 : track_count;
        if (first < 1) first = 1;
        if (last > track_count) last = track_count;
        if (last < first) last = first;
        start_playback(first, last, (HWND)play->dwCallback, (flags & MCI_NOTIFY) != 0);
        return 0;
    }
    case MCI_PAUSE:
        EnterCriticalSection(&lock);
        if (current_out && !paused) { waveOutPause(current_out); paused = TRUE; }
        LeaveCriticalSection(&lock);
        return 0;
    case MCI_STOP:
    case MCI_CLOSE:
        stop_playback(MCI_NOTIFY_ABORTED);
        return 0;
    default:
        return 0;                                        /* SET, SEEK, ... */
    }
}

static MMRESULT WINAPI emulated_aux_set(UINT device, DWORD value)
{
    if (!track_count) return real_aux_set(device, value);
    volume = value;
    EnterCriticalSection(&lock);
    if (current_out) waveOutSetVolume(current_out, value);
    LeaveCriticalSection(&lock);
    return MMSYSERR_NOERROR;
}

static MMRESULT WINAPI emulated_aux_get(UINT device, LPDWORD value)
{
    if (!track_count) return real_aux_get(device, value);
    if (value) *value = volume;
    return MMSYSERR_NOERROR;
}

static void hook_imports(HMODULE module)
{
    unsigned char *base = (unsigned char *)module;
    IMAGE_NT_HEADERS *nt = (IMAGE_NT_HEADERS *)(base + ((IMAGE_DOS_HEADER *)base)->e_lfanew);
    IMAGE_DATA_DIRECTORY dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (IMAGE_IMPORT_DESCRIPTOR *entry = (IMAGE_IMPORT_DESCRIPTOR *)(base + dir.VirtualAddress);
         entry->Name; entry++) {
        if (_stricmp((const char *)(base + entry->Name), "winmm.dll")) continue;
        IMAGE_THUNK_DATA *names = (IMAGE_THUNK_DATA *)(base + (entry->OriginalFirstThunk ? entry->OriginalFirstThunk : entry->FirstThunk));
        IMAGE_THUNK_DATA *slots = (IMAGE_THUNK_DATA *)(base + entry->FirstThunk);
        for (; names->u1.AddressOfData; names++, slots++) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char *name = (const char *)((IMAGE_IMPORT_BY_NAME *)(base + names->u1.AddressOfData))->Name;
            void *replacement = NULL, **original = NULL;
            if (!strcmp(name, "mciSendCommandA")) { replacement = emulated_mci; original = (void **)&real_mci; }
            else if (!strcmp(name, "auxSetVolume")) { replacement = emulated_aux_set; original = (void **)&real_aux_set; }
            else if (!strcmp(name, "auxGetVolume")) { replacement = emulated_aux_get; original = (void **)&real_aux_get; }
            if (!replacement) continue;
            DWORD old;
            if (!VirtualProtect(&slots->u1.Function, sizeof(void *), PAGE_READWRITE, &old)) continue;
            *original = (void *)slots->u1.Function;
            slots->u1.Function = (ULONG_PTR)replacement;
            VirtualProtect(&slots->u1.Function, sizeof(void *), old, &old);
        }
    }
}

static void find_music(void)
{
    char exe[MAX_PATH];
    DWORD length = GetModuleFileNameA(NULL, exe, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    char *slash = strrchr(exe, '\\');
    if (!slash) return;
    *slash = 0;
    snprintf(music_dir, sizeof music_dir, "%s\\Music", exe);
    for (int number = 2; number <= MAX_TRACK; number++) {
        char path[MAX_PATH];
        snprintf(path, sizeof path, "%s\\track%02d.ogg", music_dir, number);
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) track_count = number;
    }
}

void NetShimAttach(void);

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance; (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        InitializeCriticalSection(&lock);
        stop_event = CreateEventA(NULL, TRUE, FALSE, NULL);
        find_music();
        HMODULE game = GetModuleHandleA(NULL);
        if (game) hook_imports(game);
        NetShimAttach();
    }
    return TRUE;
}
