#ifndef FLUXDROP_CORE_H
#define FLUXDROP_CORE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Types and Callbacks

typedef struct {
    uint32_t session_id;
    int port;
    const char* ip;
} fd_device_t;

typedef void (*fd_server_ready_cb)(const char* ip, int port, int pin);
typedef void (*fd_server_status_cb)(const char* message);
typedef void (*fd_server_error_cb)(const char* error);
typedef void (*fd_server_progress_cb)(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps);
typedef void (*fd_server_complete_cb)();

typedef void (*fd_client_device_found_cb)(const fd_device_t* device);
typedef void (*fd_client_status_cb)(const char* message);
typedef void (*fd_client_error_cb)(const char* error);
typedef bool (*fd_client_file_request_cb)(const char* filename, uint64_t file_size);
typedef void (*fd_client_progress_cb)(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps);
typedef void (*fd_client_complete_cb)();

// Core API

void fd_init();
void fd_cleanup();

void fd_start_server(const char** file_paths, int num_files, fd_server_ready_cb ready_cb, fd_server_status_cb status_cb,
                     fd_server_error_cb error_cb, fd_server_progress_cb progress_cb, fd_server_complete_cb complete_cb);

void fd_cancel_server();
void fd_request_cancel_server();

void fd_start_discovery(uint32_t room_id, fd_client_device_found_cb found_cb);
void fd_stop_discovery();

void fd_connect(const char* ip, int port, const char* pin, const char* save_dir, fd_client_status_cb status_cb,
                fd_client_error_cb error_cb, fd_client_file_request_cb file_request_cb,
                fd_client_progress_cb progress_cb, fd_client_complete_cb complete_cb);

void fd_cancel_client();
void fd_request_cancel_client();

// ──── Session-Based API (bidirectional) ────

typedef struct {
    uint32_t session_id;
    const char* peer_ip;
    int peer_port;
    int role; // 0 = HOST, 1 = GUEST
} fd_session_info_t;

typedef void (*fd_session_ready_cb)(const char* ip, int port, int pin);
typedef void (*fd_session_established_cb)(const fd_session_info_t* info);
typedef void (*fd_session_ended_cb)();
typedef void (*fd_session_status_cb)(const char* message);
typedef void (*fd_session_error_cb)(const char* error);
typedef bool (*fd_session_file_offer_cb)(const char* filename, uint64_t file_size);
typedef void (*fd_session_progress_cb)(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps);
typedef void (*fd_session_file_complete_cb)(const char* filename);

void fd_session_host(fd_session_ready_cb ready_cb, fd_session_established_cb established_cb,
                     fd_session_ended_cb ended_cb, fd_session_status_cb status_cb, fd_session_error_cb error_cb,
                     fd_session_file_offer_cb file_offer_cb, fd_session_progress_cb progress_cb,
                     fd_session_file_complete_cb file_complete_cb);

void fd_session_join(const char* ip, int port, const char* pin, const char* save_dir,
                     fd_session_established_cb established_cb, fd_session_ended_cb ended_cb,
                     fd_session_status_cb status_cb, fd_session_error_cb error_cb,
                     fd_session_file_offer_cb file_offer_cb, fd_session_progress_cb progress_cb,
                     fd_session_file_complete_cb file_complete_cb);

void fd_session_send_files(const char** file_paths, int num_files);

void fd_session_set_save_dir(const char* dir);

void fd_session_disconnect();

int fd_session_get_pin();
int fd_session_get_port();
const char* fd_session_get_ip();
bool fd_session_is_connected();

#ifdef __cplusplus
}
#endif

#endif // FLUXDROP_CORE_H
