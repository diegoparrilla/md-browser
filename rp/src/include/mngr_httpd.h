/**
 * File: mngr_httpd.h
 * Author: Diego Parrilla Santamaría
 * Date: December 2025
 * Copyright: 2024-2025 - GOODDATA LABS SL
 * Description: Header file for the manager mode httpd server.
 */

#ifndef MNGR_HTTPD_H
#define MNGR_HTTPD_H

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

#include "constants.h"
#include "copy.h"
#include "debug.h"
#include "download.h"
#include "include/aconfig.h"
#include "lwip/apps/fs.h"
#include "lwip/apps/httpd.h"
#include "mbedtls/base64.h"
#include "mngr.h"
#include "network.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "settings/settings.h"

#ifndef MAX_JSON_PAYLOAD_SIZE
#define MAX_JSON_PAYLOAD_SIZE 4096
#endif

#ifndef MNGR_HTTPD_ALLOWED_ROOT
#define MNGR_HTTPD_ALLOWED_ROOT "/"
#endif

#ifndef MNGR_HTTPD_MAX_PATH_LEN
#define MNGR_HTTPD_MAX_PATH_LEN 512
#endif
#ifndef MNGR_HTTPD_MAX_FOLDER_LEN
#define MNGR_HTTPD_MAX_FOLDER_LEN 256
#endif
#ifndef MNGR_HTTPD_MAX_NAME_LEN
#define MNGR_HTTPD_MAX_NAME_LEN 128
#endif
#ifndef MNGR_HTTPD_MAX_TOKEN_LEN
#define MNGR_HTTPD_MAX_TOKEN_LEN 32
#endif
#ifndef MNGR_HTTPD_ERROR_URL_LEN
#define MNGR_HTTPD_ERROR_URL_LEN 128
#endif
#ifndef MNGR_HTTPD_RESPONSE_MSG_LEN
#define MNGR_HTTPD_RESPONSE_MSG_LEN 128
#endif
#ifndef MNGR_HTTPD_SSI_JSON_CHUNK_SIZE
#define MNGR_HTTPD_SSI_JSON_CHUNK_SIZE 128
#endif

#ifndef HTTPD_JSON_STATE_SLOTS
#if defined(FMANAGER_DOWNLOAD_HTTPS) && (FMANAGER_DOWNLOAD_HTTPS == 1)
// TLS increases RAM usage; cap the per-file JSON snapshot slots at the
// number of parallel httpd connections (MEMP_NUM_PARALLEL_HTTPD_CONNS = 2)
// when HTTPS is enabled — more slots than connections can never be used.
#define HTTPD_JSON_STATE_SLOTS 2
#else
#define HTTPD_JSON_STATE_SLOTS 4
#endif
#endif

// Upload cost is dominated by per-CHUNK overhead, not per-byte: the client
// round trip plus this chunk's connection setup/teardown measured a fixed
// ~19ms regardless of size, so fewer/bigger chunks amortise it away. It also
// cuts the TIME-WAIT population proportionally (tw ~= connections/s * 4s,
// D-08), which is what keeps the heap off its limit. The SD card does NOT get
// more efficient with bigger writes (measured 3.93us/byte at 4KB vs 4.11 at
// 16KB, i.e. a flat ~245KB/s ceiling), so this only buys back overhead.
//
// The ceiling is NOT TCP_WND, it is main-loop blocking. cyw43 runs in poll
// mode, so the radio is only serviced between poll cycles; a whole chunk can
// arrive inside one cycle and the handler then blocks in f_write for the whole
// of it. Measured: 4KB => ~16ms burst (fine), 16KB => ~67ms burst, which
// reproducibly starved the driver ("[CYW43] do_ioctl: timeout" every run).
// 8KB halves that to ~33ms. Do not raise this without re-checking for cyw43
// timeouts on hardware -- throughput is not the binding constraint here.
// Note the tempting fix (polling cyw43 between writes) is unsafe:
// httpd_post_receive_data is itself called from the poll, so it would recurse
// into lwIP. The structural fix is LWIP_HTTPD_POST_MANUAL_WND (EPIC-14).
//
// NOTE: this size assumes UPLOAD_CHUNK_METHOD "POST" (binary body). The legacy
// base64 GET path inflates by 4/3 into a query parameter and cannot carry more
// than a few KB, so it must not be re-enabled at this size.
#ifndef UPLOAD_CHUNK_SIZE
#define UPLOAD_CHUNK_SIZE 8192
#endif

// Default upload chunk method: "GET" (base64) or "POST" (binary)
#ifndef UPLOAD_CHUNK_METHOD
#define UPLOAD_CHUNK_METHOD "POST"
#endif

// Default download chunk size (raw bytes) to fit JSON buffer after base64.
#ifndef DOWNLOAD_CHUNK_SIZE
#define DOWNLOAD_CHUNK_SIZE 2048
#endif

typedef enum {
  MNGR_HTTPD_RESPONSE_OK = 200,
  MNGR_HTTPD_RESPONSE_BAD_REQUEST = 400,
  MNGR_HTTPD_RESPONSE_NOT_FOUND = 404,
  MNGR_HTTPD_RESPONSE_INTERNAL_SERVER_ERROR = 500
} mngr_httpd_response_status_t;

#if defined(LWIP_HTTPD_FILE_STATE) && LWIP_HTTPD_FILE_STATE
typedef struct {
  bool in_use;
  char json_snapshot[MAX_JSON_PAYLOAD_SIZE];
} httpd_json_state_t;
#endif

void mngr_httpd_start(int sdcard_err);

#endif  // MNGR_HTTPD_H
