// Applet manager (appletOE), apm, and lm.
#include <stdlib.h>
#include <string.h>

#include "../hle.h"
#include "../ipc.h"
#include "../settings.h"

#define AM_NO_MESSAGE MAKE_RESULT(128, 3)
#define AM_NO_DATA MAKE_RESULT(128, 2)

static uint32_t msg_event_r, msg_event_w;
static KEvent *msg_event;
static uint32_t msg_queue[16];
static int msg_count;

static bool preselected_popped;
RT_ON_RESET(am_reset) {  // (reboot: the message queue starts over with FocusStateChanged)
    msg_event = NULL;
    msg_event_r = msg_event_w = 0;
    msg_count = 0;
    preselected_popped = false;
}

static void am_init_once(void) {
    if (msg_event) return;
    msg_event = kernel_create_event_pair(&msg_event_w, &msg_event_r);
    msg_queue[msg_count++] = 15;  // FocusStateChanged
    pthread_mutex_lock(&g_klock);
    kernel_signal(msg_event);
    pthread_mutex_unlock(&g_klock);
}

static uint32_t new_event(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// An event that is already signaled: the game waits on it as a gate (ISelfController::GetLibraryAppletLaunchableEvent,
// which the system signals once the application can launch library applets). The plain new_event() never fires, so the
// game's main thread hung for good when Plus at PICROSS S's main menu waited on it (backtrace: PICRECOMP_PROF_SVC=1).
static uint32_t new_signaled_event(void) {
    uint32_t w, r;
    KEvent *ev = kernel_create_event_pair(&w, &r);
    pthread_mutex_lock(&g_klock);
    kernel_signal(ev);
    pthread_mutex_unlock(&g_klock);
    return r;
}

#define UNHANDLED(iface) LOG("[am] %s cmd %u (unhandled, ok)\n", iface, req->cmd)

static void common_state_getter(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: resp->copy_handles[resp->ncopy++] = msg_event_r; break;  // GetEventHandle
    case 1:  // ReceiveMessage
        pthread_mutex_lock(&g_klock);
        if (msg_count) {
            OUT(uint32_t, 0) = msg_queue[0];
            memmove(msg_queue, msg_queue + 1, --msg_count * sizeof(uint32_t));
            if (!msg_count) msg_event->signaled = false;
            resp_size(resp, 4);
        } else {
            resp->result = AM_NO_MESSAGE;
        }
        pthread_mutex_unlock(&g_klock);
        break;
    case 5: OUT(uint8_t, 0) = 1; resp_size(resp, 1); break;   // GetOperationMode: docked
    case 6: OUT(uint32_t, 0) = 1; resp_size(resp, 4); break;  // GetPerformanceMode: boost
    case 8: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;   // GetBootMode
    case 9: OUT(uint8_t, 0) = 1; resp_size(resp, 1); break;   // GetCurrentFocusState: in focus
    case 13: case 61: resp->copy_handles[resp->ncopy++] = new_event(); break;
    case 60: {  // GetDefaultDisplayResolution
        int w = getenv("PICRECOMP_DISPLAY_W") ? atoi(getenv("PICRECOMP_DISPLAY_W")) : 1920;
        LOG("[am] GetDefaultDisplayResolution -> %dx1080\n", w);
        OUT(int32_t, 0) = w; OUT(int32_t, 4) = 1080; resp_size(resp, 8); break;
    }
    default: UNHANDLED("ICommonStateGetter");
    }
}

static void self_controller(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: LOG("[am] application requested exit\n"); exit(0);
    case 9: resp->copy_handles[resp->ncopy++] = new_signaled_event(); break;  // GetLibraryAppletLaunchableEvent
    case 40: OUT(uint64_t, 0) = 1; resp_size(resp, 8); break;  // CreateManagedDisplayLayer
    case 69: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;
    default: UNHANDLED("ISelfController");
    }
}

static void window_controller(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1: OUT(uint64_t, 0) = 0x1234; resp_size(resp, 8); break;  // GetAppletResourceUserId
    default: UNHANDLED("IWindowController");
    }
}

static void generic_ok(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[am] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
}

// ------------------------------------------------------------------ IStorage / IStorageAccessor
typedef struct { uint8_t *data; uint64_t size; } AmStorage;

static void storage_accessor(Service *s, IpcReq *req, IpcResp *resp) {
    AmStorage *st = s->state;
    switch (req->cmd) {
    case 0: OUT(int64_t, 0) = (int64_t)st->size; resp_size(resp, 8); break;  // GetSize
    case 10: {  // Write(offset, A)
        int64_t off = IN(int64_t, 0);
        IpcBuf *a = in_buf(req, 0);
        uint64_t n = a->size;
        if ((uint64_t)off + n > st->size) n = st->size - (uint64_t)off;
        memcpy(st->data + off, (void *)(uintptr_t)a->addr, n);
        break;
    }
    case 11: {  // Read(offset, B)
        int64_t off = IN(int64_t, 0);
        IpcBuf *b = out_buf(req, 0);
        uint64_t n = b->size;
        if ((uint64_t)off + n > st->size) n = st->size - (uint64_t)off;
        memcpy((void *)(uintptr_t)b->addr, st->data + off, n);
        break;
    }
    default: LOG("[am] IStorageAccessor cmd %u (ok)\n", req->cmd);
    }
}

static void am_storage(Service *s, IpcReq *req, IpcResp *resp) {
    if (req->cmd == 0) ipc_return_object(resp, service_new("IStorageAccessor", storage_accessor, s->state));
    else LOG("[am] IStorage cmd %u (ok)\n", req->cmd);
}

static Service *new_am_storage(const void *data, uint64_t size) {
    AmStorage *st = calloc(1, sizeof *st);
    st->data = calloc(1, size);
    st->size = size;
    if (data) memcpy(st->data, data, size);
    return service_new("IStorage", am_storage, st);
}

// ------------------------------------------------------------------ ILibraryAppletCreator / ILibraryAppletAccessor
// The game launches a system applet (PICROSS S: Plus at the main menu). None is emulated: the applet is created, and
// finishes as soon as it is started, with no output. The log names the applet id (nn::applet::AppletId) and every
// call, so which applet it is and what the game does with it can be read from there.
typedef struct { uint32_t id, mode; KEvent *ev; uint32_t ev_r; } LibApplet;

static void applet_finish(LibApplet *a) {
    pthread_mutex_lock(&g_klock);
    kernel_signal(a->ev);  // GetAppletStateChangedEvent
    pthread_mutex_unlock(&g_klock);
}

static void library_applet_accessor(Service *s, IpcReq *req, IpcResp *resp) {
    LibApplet *a = s->state;
    LOG("[am] ILibraryAppletAccessor(applet 0x%x) cmd %u\n", a->id, req->cmd);
    switch (req->cmd) {
    case 0: resp->copy_handles[resp->ncopy++] = a->ev_r; break;  // GetAppletStateChangedEvent
    case 1: OUT(uint8_t, 0) = 1; resp_size(resp, 1); break;      // IsCompleted
    case 10: case 20: case 25: applet_finish(a); break;          // Start / RequestExit / Terminate
    case 30: break;                                              // GetResult: success
    case 101: case 104: ipc_return_object(resp, new_am_storage(NULL, 0x1000)); break;  // PopOutData / PopInteractiveOutData
    case 105: case 106: resp->copy_handles[resp->ncopy++] = a->ev_r; break;  // GetPopOutDataEvent / ...InteractiveInDataEvent
    case 110: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;    // NeedsToExitProcess
    default: break;  // (PushInData and the rest: accepted, ignored)
    }
}

static void library_applet_creator(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: {  // CreateLibraryApplet(AppletId, LibraryAppletMode)
        LibApplet *a = calloc(1, sizeof *a);
        uint32_t w;
        a->id = IN(uint32_t, 0);
        a->mode = IN(uint32_t, 4);
        a->ev = kernel_create_event_pair(&w, &a->ev_r);
        LOG("[am] CreateLibraryApplet id 0x%x mode %u\n", a->id, a->mode);
        ipc_return_object(resp, service_new("ILibraryAppletAccessor", library_applet_accessor, a));
        break;
    }
    case 10: ipc_return_object(resp, new_am_storage(NULL, (uint64_t)IN(int64_t, 0))); break;  // CreateStorage(size)
    default: UNHANDLED("ILibraryAppletCreator");
    }
}

extern const uint8_t g_user_id[16];

static void application_functions(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1:  // PopLaunchParameter(kind)
        if (IN(uint32_t, 0) == 2 && !preselected_popped) {  // PreselectedUser
            uint8_t p[0x88] = {0};
            *(uint32_t *)p = 0xC79497CA;
            *(uint32_t *)(p + 4) = 1;
            memcpy(p + 8, g_user_id, 16);
            preselected_popped = true;
            ipc_return_object(resp, new_am_storage(p, sizeof p));
        } else {
            resp->result = AM_NO_DATA;
        }
        break;
    case 20: OUT(uint64_t, 0) = 0; resp_size(resp, 8); break;  // EnsureSaveData
    case 21: {  // GetDesiredLanguage: the game's text language (settings: runtime/settings.c, "language")
        char lang[8] = {0};
        snprintf(lang, sizeof lang, "%s", settings_language());
        memcpy(resp->data, lang, 8);
        resp_size(resp, 8);
        break;
    }
    case 23: memcpy(resp->data, "1.0.0", 6); resp_size(resp, 0x10); break;
    case 40: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // NotifyRunning
    case 130: case 140: resp->copy_handles[resp->ncopy++] = new_event(); break;
    default: UNHANDLED("IApplicationFunctions");
    }
}

static void application_proxy(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    static const struct { int cmd; const char *name; ServiceHandler h; } subs[] = {
        {0, "ICommonStateGetter", common_state_getter},
        {1, "ISelfController", self_controller},
        {2, "IWindowController", window_controller},
        {3, "IAudioController", generic_ok},
        {4, "IDisplayController", generic_ok},
        {10, "IProcessWindingController", generic_ok},
        {11, "ILibraryAppletCreator", library_applet_creator},
        {20, "IApplicationFunctions", application_functions},
        {1000, "IDebugFunctions", generic_ok},
    };
    for (unsigned i = 0; i < sizeof subs / sizeof *subs; i++) {
        if (subs[i].cmd == (int)req->cmd) {
            ipc_return_object(resp, service_new(subs[i].name, subs[i].h, NULL));
            return;
        }
    }
    UNHANDLED("IApplicationProxy");
}

SERVICE("appletOE", applet_oe) {
    (void)s;
    am_init_once();
    if (req->cmd == 0) ipc_return_object(resp, service_new("IApplicationProxy", application_proxy, NULL));
    else UNHANDLED("appletOE");
}

// ------------------------------------------------------------------ apm
static void apm_session(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 1) { OUT(uint32_t, 0) = 0x92220008; resp_size(resp, 4); }  // GetPerformanceConfiguration
}

SERVICE("apm", apm) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("apm:ISession", apm_session, NULL));
    else if (req->cmd == 1) { OUT(uint32_t, 0) = 1; resp_size(resp, 4); }
}

// ------------------------------------------------------------------ lm (guest log output)
static void lm_logger(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s; (void)resp;
    if (req->cmd != 0) return;
    IpcBuf *b = in_buf(req, 0);
    if (!b || !b->size) return;
    // Log packets are TLV chunks after a 0x18-byte header; print the string chunks.
    const uint8_t *p = (const uint8_t *)(uintptr_t)b->addr, *end = p + b->size;
    p += 0x18;
    char line[1024];
    int n = 0;
    while (p + 2 <= end) {
        uint8_t key = p[0], len = p[1];
        p += 2;
        if (p + len > end) break;
        if (key == 2 || key == 4 || key == 5 || key == 6 || key == 7) {  // message, file, function, module, thread
            n += snprintf(line + n, sizeof line - n, "%s%.*s", n ? " | " : "", len, (const char *)p);
            if (n >= (int)sizeof line) break;
        }
        p += len;
    }
    if (n) LOG("[lm] %s\n", line);
}

SERVICE("lm", lm) {
    (void)s;
    if (req->cmd == 0) ipc_return_object(resp, service_new("lm:ILogger", lm_logger, NULL));
}
