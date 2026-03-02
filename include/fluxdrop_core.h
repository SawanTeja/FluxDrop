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

// Initialization and Cleanup (Good practice for libraries)
void fd_init();
void fd_cleanup();

// Server/Sender Functions
// Starts the server sharing a list of exact file paths.
// file_paths is an array of absolute paths to files/directories
// num_files is the length of the array
void fd_start_server(const char** file_paths, int num_files,
                     fd_server_ready_cb ready_cb,
                     fd_server_status_cb status_cb,
                     fd_server_error_cb error_cb,
                     fd_server_progress_cb progress_cb,
                     fd_server_complete_cb complete_cb);

// Cancels the active server session
void fd_cancel_server();

// Client/Receiver Functions
// Starts listening for UDP broadcasts with a specific room ID
void fd_start_discovery(uint32_t room_id, fd_client_device_found_cb found_cb);

// Stops listening for UDP broadcasts
void fd_stop_discovery();

// Connects to a specific IP/port with a PIN, saving accepted files to save_dir
void fd_connect(const char* ip, int port, const char* pin, const char* save_dir,
                fd_client_status_cb status_cb,
                fd_client_error_cb error_cb,
                fd_client_file_request_cb file_request_cb,
                fd_client_progress_cb progress_cb,
                fd_client_complete_cb complete_cb);

// Cancels the active client session/transfer
void fd_cancel_client();

#ifdef __cplusplus
}
#endif

#endif // FLUXDROP_CORE_H
