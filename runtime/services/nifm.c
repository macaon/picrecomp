// nifm (network interface manager): a console with no network. The game asks whether it may use the Internet and is
// told no; nothing is connected.
// Piczle Puzzle & Watch Collection (UE4) builds an nn::nifm request at startup (RequestClient's constructor): with the
// logging stub, CreateGeneralService returned no object and the constructor read through a null pointer.
// Commands per Switchbrew (IStaticService, IGeneralService, IRequest); the offline answers follow yuzu's with the network
// disabled: a request is never accepted, its result is "network communication disabled".
#include "../hle.h"
#include "../ipc.h"

#define NIFM_RESULT_DISABLED MAKE_RESULT(110, 1111)
#define NIFM_REQUEST_NOT_SUBMITTED 1u

static uint32_t new_event_handle(void) {
    uint32_t w, r;
    kernel_create_event_pair(&w, &r);
    return r;
}

static void request_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 0: OUT(uint32_t, 0) = NIFM_REQUEST_NOT_SUBMITTED; resp_size(resp, 4); break;  // GetRequestState
    case 1: resp->result = NIFM_RESULT_DISABLED; break;                                // GetResult
    case 2:  // GetSystemEventReadableHandles: the request's two events (never signalled)
        resp->copy_handles[resp->ncopy++] = new_event_handle();
        resp->copy_handles[resp->ncopy++] = new_event_handle();
        break;
    default: break;  // Cancel, Submit, the Set* options: accepted, nothing happens
    }
}

static void general_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    switch (req->cmd) {
    case 4: ipc_return_object(resp, service_new("IRequest", request_service, NULL)); break;  // CreateRequest
    case 5: case 12: case 18: resp->result = NIFM_RESULT_DISABLED; break;  // GetCurrentNetworkProfile / IpAddress / InternetConnectionStatus
    case 21: case 22: case 23: case 24:  // IsWireless / IsEthernetCommunicationEnabled, IsAny{Internet,Foreground}RequestAccepted
        OUT(uint8_t, 0) = 0; resp_size(resp, 1); break;
    default: LOG("[nifm] IGeneralService cmd %u (ok)\n", req->cmd); break;
    }
}

static void static_service(Service *s, IpcReq *req, IpcResp *resp) {
    (void)s;
    if (req->cmd == 4 || req->cmd == 5) ipc_return_object(resp, service_new("IGeneralService", general_service, NULL));
    else LOG("[nifm] IStaticService cmd %u (ok)\n", req->cmd);
}
SERVICE("nifm:u", nifm_u) { static_service(s, req, resp); }
SERVICE("nifm:a", nifm_a) { static_service(s, req, resp); }
SERVICE("nifm:s", nifm_s) { static_service(s, req, resp); }
