#ifndef FLUXDROP_CORE_H
#define FLUXDROP_CORE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ----------------------------------------------------------------------------
// Types and Callbacks
// ----------------------------------------------------------------------------

typedef struct {
    uint32_t session_id;
    int port;
    const char* ip;
} fd_device_t;

// Server Callbacks
typedef void (*fd_server_ready_cb)(const char* ip, int port, int pin);
typedef void (*fd_server_status_cb)(const char* message);
typedef void (*fd_server_error_cb)(const char* error);
typedef void (*fd_server_progress_cb)(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps);
typedef void (*fd_server_complete_cb)();

// Client Callbacks
typedef void (*fd_client_device_found_cb)(const fd_device_t* device);
typedef void (*fd_client_status_cb)(const char* message);
typedef void (*fd_client_error_cb)(const char* error);
// Return true to accept the file, false to reject
typedef bool (*fd_client_file_request_cb)(const char* filename, uint64_t file_size);
typedef void (*fd_client_progress_cb)(const char* filename, uint64_t transferred, uint64_t total, double speed_mbps);
typedef void (*fd_client_complete_cb)();

// ----------------------------------------------------------------------------
// Core API
// ----------------------------------------------------------------------------

// Initialization and Cleanup
void fd_init();
void fd_cleanup();

// Server/Sender Functions
void fd_start_server(const char** file_paths, int num_files,
                     fd_server_ready_cb ready_cb,
                     fd_server_status_cb status_cb,
                     fd_server_error_cb error_cb,
                     fd_server_progress_cb progress_cb,
                     fd_server_complete_cb complete_cb);

// Blocking cancel — stops server, joins thread, resets state
void fd_cancel_server();
// Non-blocking cancel — signals stop and closes sockets, thread exits on its own
void fd_request_cancel_server();

// Client/Receiver Functions
void fd_start_discovery(uint32_t room_id, fd_client_device_found_cb found_cb);
void fd_stop_discovery();

void fd_connect(const char* ip, int port, const char* pin, const char* save_dir,
                fd_client_status_cb status_cb,
                fd_client_error_cb error_cb,
                fd_client_file_request_cb file_request_cb,
                fd_client_progress_cb progress_cb,
                fd_client_complete_cb complete_cb);

// Blocking cancel — stops client, joins thread, resets state
void fd_cancel_client();
// Non-blocking cancel — signals stop and closes sockets, thread exits on its own
void fd_request_cancel_client();

#ifdef __cplusplus
}
#endif

#endif // FLUXDROP_CORE_H
