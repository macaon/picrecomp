// Small services: parental control, add-on content, and other mostly-stubbed interfaces.
#include <string.h>

#include "../hle.h"
#include "../ipc.h"

static void ok_logger(Service *s, IpcReq *req, IpcResp *resp) {
    LOG("[svc] %s cmd %u (ok)\n", s->name, req->cmd);
    resp_size(resp, 0x10);
}

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

// ------------------------------------------------------------------ pctl
static void pctl_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 1031: case 1035: case 1018: OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;  // restrictions off
    case 1032: OUT(uint32_t, 0) = 0; resp_size(resp, 4); break;
    default: LOG("[pctl] IParentalControlService cmd %u (ok)\n", req->cmd); resp_size(resp, 0x10);
    }
}

static void pctl_root(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 0 || req->cmd == 1)
        ipc_return_object(resp, service_new("IParentalControlService", pctl_service, NULL));
}
SERVICE("pctl", pctl) { pctl_root(s, req, resp); }
SERVICE("pctl:s", pctl_s) { pctl_root(s, req, resp); }
SERVICE("pctl:a", pctl_a) { pctl_root(s, req, resp); }
SERVICE("pctl:r", pctl_r) { pctl_root(s, req, resp); }

// ------------------------------------------------------------------ aoc:u (add-on content)
// The game's DLC are entitlements: the puzzles are in its own RomFS and unlock when the game is told the add-on is
// there. title.json lists the indices of the packages the importer found (settings.c); the DLC's title id is the
// game's with the low 12 bits cleared, plus one in bit 12, plus the index.
extern int settings_addons(const uint32_t **list);

SERVICE("aoc:u", aoc_u) {
    (void)s;
    const uint32_t *list;
    int n = settings_addons(&list);
    switch (req->cmd) {
    case 2: OUT(uint32_t, 0) = (uint32_t)n; resp_size(resp, 4); break;   // CountAddOnContent
    case 3: {  // ListAddOnContent(offset, count) -> indices, as many as there are
        uint32_t offset = IN(uint32_t, 0), count = IN(uint32_t, 4), out = 0;
        IpcBuf *buf = out_buf(req, 0);
        for (uint32_t i = offset; buf && i < (uint32_t)n && out < count && (out + 1) * 4 <= buf->size; i++)
            ((uint32_t *)(uintptr_t)buf->addr)[out++] = list[i];
        LOG("[aoc] ListAddOnContent(%u, %u): %u of %d\n", offset, count, out, n);
        OUT(uint32_t, 0) = out; resp_size(resp, 4); break;
    }
    case 5: OUT(uint64_t, 0) = ((g_program_id & ~0xfffull) + 0x1000); resp_size(resp, 8); break;  // GetAddOnContentBaseId
    case 6: break;  // PrepareAddOnContent(index): nothing to prepare, the data is in the RomFS
    case 7: case 8: resp->copy_handles[resp->ncopy++] = new_event_handle(); break;
    default: ok_logger(s, req, resp);
    }
}
