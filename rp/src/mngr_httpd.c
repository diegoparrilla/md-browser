/**
 * File: mngr_httpd.c
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2024-2025 - GOODDATA LABS SL
 * Description: HTTPD server functions for manager httpd
 */

#include "mngr_httpd.h"

#include "network.h"

// Add includes for download and settings
#include "download.h"
#include "include/aconfig.h"
#include "settings/settings.h"

#define MAX_JSON_PAYLOAD_SIZE 4096
static mngr_httpd_response_status_t response_status = MNGR_HTTPD_RESPONSE_OK;
static char json_buff[MAX_JSON_PAYLOAD_SIZE] = {0};  // Buffer for JSON payload
static char httpd_response_message[128] = {0};
static void *current_connection;
static int sdcard_status = SDCARD_INIT_ERROR;

#define UPLOAD_CHUNK_SIZE 4096
// Default upload chunk method: "GET" (base64) or "POST" (binary)
#ifndef UPLOAD_CHUNK_METHOD
#define UPLOAD_CHUNK_METHOD "POST"
#endif

// Default download chunk size (raw bytes) to fit JSON buffer after base64
// (~4KB)
#ifndef DOWNLOAD_CHUNK_SIZE
#define DOWNLOAD_CHUNK_SIZE 2048
#endif

// Decodes a URI-escaped string (percent-encoded) into its original value.
// E.g., "My%20SSID%21" -> "My SSID!"
// Returns true on success. Always null-terminates output.
// Output buffer must be at least outLen bytes.
// Returns false if input is NULL, output is NULL, or output buffer is too
// small.
static bool url_decode(const char *in, char *out, size_t outLen) {
  if (!in || !out || outLen == 0) return false;

  DPRINTF("Decoding '%s'. Length=%d\n", in, outLen);
  size_t o = 0;
  for (size_t i = 0; in[i] && o < outLen - 1; ++i) {
    if (in[i] == '%' && isxdigit((unsigned char)in[i + 1]) &&
        isxdigit((unsigned char)in[i + 2])) {
      // Decode %xx
      char hi = in[i + 1], lo = in[i + 2];
      int high = (hi >= 'A' ? (toupper(hi) - 'A' + 10) : (hi - '0'));
      int low = (lo >= 'A' ? (toupper(lo) - 'A' + 10) : (lo - '0'));
      out[o++] = (char)((high << 4) | low);
      i += 2;
    } else if (in[i] == '+') {
      // Optionally decode + as space (common in x-www-form-urlencoded)
      out[o++] = ' ';
    } else {
      out[o++] = in[i];
    }
  }
  out[o] = '\0';
  DPRINTF("Decoded to '%s'. Size: %d\n", out, o + 1);
  return true;
}

/**
 * @brief Array of SSI tags for the HTTP server.
 *
 * This array contains the SSI tags used by the HTTP server to dynamically
 * insert content into web pages. Each tag corresponds to a specific piece of
 * information that can be updated or retrieved from the server.
 */
static const char *ssi_tags[] = {
    // Max size of SSI tag is 8 chars
    "HOMEPAGE",  // 0 - Redirect to the homepage
    "SDCARDST",  // 1 - SD card status
    "APPSFLDR",  // 2 - Apps folder
    "SSID",      // 3 - SSID
    "IPADDR",    // 4 - IP address
    "SDCARDB",   // 5 - SD card status true or false
    "APPSFLDB",  // 6 - Apps folder status true or false
    "JSONPLD",   // 7 - JSON payload
    "DWNLDSTS",  // 8 - Download status
    "TITLEHDR",  // 9 - Title header
    "WIFILST",   // 10 - WiFi list
    "WLSTSTP",   // 11 - WiFi list stop
    "RSPSTS",    // 12 - Response status
    "RSPMSG",    // 13 - Response message
    "BTFTR",     // 14 - Boot feature
    "SFCNFGRB",  // 15 - Safe Config Reboot
    "SDBDRTKB",  // 16 - SD Card Baud Rate
    "APPSURL",   // 17 - Apps Catalog URL
    "PLHLDR9",   // 18 - Placeholder 9
    "PLHLDR10",  // 19 - Placeholder 10
    "PLHLDR11",  // 20 - Placeholder 11
    "PLHLDR12",  // 21 - Placeholder 12
    "PLHLDR13",  // 22 - Placeholder 13
    "PLHLDR14",  // 23 - Placeholder 14
    "PLHLDR15",  // 24 - Placeholder 15
    "PLHLDR16",  // 25 - Placeholder 16
    "PLHLDR17",  // 26 - Placeholder 17
    "PLHLDR18",  // 27 - Placeholder 18
    "PLHLDR19",  // 28 - Placeholder 19
    "PLHLDR20",  // 29 - Placeholder 20
    "PLHLDR21",  // 30 - Placeholder 21
    "PLHLDR22",  // 31 - Placeholder 22
    "PLHLDR23",  // 32 - Placeholder 23
    "PLHLDR24",  // 33 - Placeholder 24
    "PLHLDR25",  // 34 - Placeholder 25
    "PLHLDR26",  // 35 - Placeholder 26
    "PLHLDR27",  // 36 - Placeholder 27
    "PLHLDR28",  // 37 - Placeholder 28
    "PLHLDR29",  // 38 - Placeholder 29
    "PLHLDR30",  // 39 - Placeholder 30
    "WDHCP",     // 40 - WiFi DHCP
    "WIP",       // 41 - WiFi IP
    "WNTMSK",    // 42 - WiFi Netmask
    "WGTWY",     // 43 - WiFi Gateway
    "WDNS",      // 44 - WiFi DNS
    "WCNTRY",    // 45 - WiFi Country
    "WHSTNM",    // 46 - WiFi Hostname
    "WPWR",      // 47 - WiFi Power
    "WRSS",      // 48 - WiFi RSSI
};

/**
 * @brief
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to after the floppy disk image is
 * selected.
 */
static const char *cgi_test(int iIndex, int iNumParams, char *pcParam[],
                            char *pcValue[]) {
  DPRINTF("TEST CGI handler called with index %d\n", iIndex);
  return "/test.shtml";
}

/**
 * @brief Show the folder content
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to after the floppy disk image is
 * selected.
 */
static const char *cgi_folder(int iIndex, int iNumParams, char *pcParam[],
                              char *pcValue[]) {
  DPRINTF("FOLDER CGI handler called with index %d\n", iIndex);
  /* Parse 'folder' query parameter */
  const char *req_folder = NULL;
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "folder") == 0) {
      req_folder = pcValue[i];
      bool valid = url_decode(req_folder, pcValue[i], strlen(pcValue[i]) + 1);
      if (!valid) {
        DPRINTF("Invalid folder parameter: %s\n", pcValue[i]);
        /* Return empty JSON array */
        strcpy(json_buff, "[]");
        return "/json.shtml";
      }
      DPRINTF("Folder parameter: %s\n", req_folder);
      break;
    }
  }
  if (req_folder == NULL) {
    DPRINTF("No folder parameter provided\n");
    /* Return empty JSON array */
    strcpy(json_buff, "[]");
    return "/json.shtml";
  }
  DPRINTF("Listing subfolders of: %s\n", req_folder);
  /* Prepare JSON array */
  json_buff[0] = '\0';
  strcat(json_buff, "[");
  /* FatFS list directories in the given folder */
  DIR dir;
  FILINFO fno;
  FRESULT fr;
  bool apps_installed_found = false;
  fr = f_opendir(&dir, req_folder);
  if (fr != FR_OK) {
    DPRINTF("Failed to open directory %s, error %d\n", req_folder, fr);
    /* Return empty JSON array */
    strcpy(json_buff, "[]");
    return "/json.shtml";
  }
  for (;;) {
    fr = f_readdir(&dir, &fno);
    if (fr != FR_OK || fno.fname[0] == '\0') break;
    if (fno.fattrib & AM_DIR) {
      DPRINTF("DIR: %s/%s\n", req_folder, fno.fname);
      /* Append folder name to JSON array */
      char tmp[256];
      snprintf(tmp, sizeof(tmp), "\"%s\",", fno.fname);
      strcat(json_buff, tmp);
      apps_installed_found = true;
    }
  }
  f_closedir(&dir);
  /* Close JSON array */
  if (apps_installed_found) {
    size_t len = strlen(json_buff);
    if (len > 1 && json_buff[len - 1] == ',') {
      json_buff[len - 1] = ']';
      json_buff[len] = '\0';
    } else {
      strcat(json_buff, "]");
    }
  } else {
    /* Empty array */
    strcat(json_buff, "]");
  }
  if (!apps_installed_found) {
    DPRINTF("No subfolders found in %s\n", req_folder);
  }
  /* Return JSON page (json_buff contains array or empty) */
  return "/json.shtml";
}
// CGI: make directory
static const char *cgi_mkdir(int iIndex, int iNumParams, char *pcParam[],
                             char *pcValue[]) {
  const char *folder = NULL, *src = NULL;
  char df[256] = {0}, ds[128] = {0}, path[512];
  for (int i = 0; i < iNumParams; i++) {
    if (!strcmp(pcParam[i], "folder")) folder = pcValue[i];
    if (!strcmp(pcParam[i], "src")) src = pcValue[i];
  }
  if (!folder || !src) {
    strcpy(json_buff, "{\"error\":\"missing parameters\"}");
    return "/json.shtml";
  }
  if (!url_decode(folder, df, sizeof(df)) || !url_decode(src, ds, sizeof(ds))) {
    strcpy(json_buff, "{\"error\":\"invalid encoding\"}");
    return "/json.shtml";
  }
  snprintf(path, sizeof(path), "%s/%s", df, ds);
  FRESULT r = f_mkdir(path);
  if (r != FR_OK)
    snprintf(json_buff, sizeof(json_buff), "{\"error\":\"mkdir failed %d\"}",
             r);
  else
    strcpy(json_buff, "{\"status\":\"created\"}");
  return "/json.shtml";
}

/**
 * @brief Download the selected app from the folder and URL parameters.
 */
const char *cgi_download(int iIndex, int iNumParams, char *pcParam[],
                         char *pcValue[]) {
  DPRINTF("cgi_download called with index %d\n", iIndex);

  char decoded_folder[256] = {0};
  char decoded_url[DOWNLOAD_BUFFLINE_SIZE] = {0};
  bool has_folder = false, has_url = false;
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "folder") == 0) {
      if (!url_decode(pcValue[i], decoded_folder, sizeof(decoded_folder))) {
        DPRINTF("Invalid folder parameter: %s\n", pcValue[i]);
        static char error_url[128];
        snprintf(error_url, sizeof(error_url),
                 "/error.shtml?error=%d&error_msg=Invalid%%20folder",
                 MNGR_HTTPD_RESPONSE_BAD_REQUEST);
        return error_url;
      }
      has_folder = true;
    } else if (strcmp(pcParam[i], "url") == 0) {
      if (!url_decode(pcValue[i], decoded_url, sizeof(decoded_url))) {
        DPRINTF("Invalid URL parameter: %s\n", pcValue[i]);
        static char error_url[128];
        snprintf(error_url, sizeof(error_url),
                 "/error.shtml?error=%d&error_msg=Invalid%%20url",
                 MNGR_HTTPD_RESPONSE_BAD_REQUEST);
        return error_url;
      }
      has_url = true;
    }
  }
  if (!has_folder || !has_url) {
    DPRINTF("Missing folder or URL parameter\n");
    static char error_url[128];
    snprintf(error_url, sizeof(error_url),
             "/error.shtml?error=%d&error_msg=Bad%%20request",
             MNGR_HTTPD_RESPONSE_BAD_REQUEST);
    return error_url;
  }
  DPRINTF("Download request: folder=%s, url=%s\n", decoded_folder, decoded_url);
  download_setDstFolder(decoded_folder);
  download_setUrl(decoded_url);
  download_setStatus(DOWNLOAD_STATUS_REQUESTED);

  return "/downloading.shtml";
}

/**
 * @brief Execute a ls-like operation to list all content
 *
 * @param iIndex The index of the CGI handler.
 * @param iNumParams The number of parameters passed to the CGI handler.
 * @param pcParam An array of parameter names.
 * @param pcValue An array of parameter values.
 * @return The URL of the page to redirect to after the floppy disk image is
 * selected.
 */
static const char *cgi_ls(int iIndex, int iNumParams, char *pcParam[],
                          char *pcValue[]) {
  DPRINTF("LS CGI handler called with index %d\n", iIndex);
  /* Parse 'folder' query parameter */
  const char *req_folder = NULL;
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "folder") == 0) {
      req_folder = pcValue[i];
      bool valid = url_decode(req_folder, pcValue[i], strlen(pcValue[i]) + 1);
      if (!valid) {
        DPRINTF("Invalid folder parameter: %s\n", pcValue[i]);
        /* Return empty JSON array */
        strcpy(json_buff, "[]");
        return "/json.shtml";
      }
      DPRINTF("Folder parameter: %s\n", req_folder);
      break;
    }
  }
  if (req_folder == NULL) {
    DPRINTF("No folder parameter provided\n");
    /* Return empty JSON array */
    strcpy(json_buff, "[]");
    return "/json.shtml";
  }
  DPRINTF("Listing entries of: %s\n", req_folder);
  // Parse optional pagination start index
  int nextItem = 0;
  for (int j = 0; j < iNumParams; j++) {
    if (strcmp(pcParam[j], "nextItem") == 0) {
      nextItem = atoi(pcValue[j]);
      DPRINTF("cgi_ls: start from item %d\n", nextItem);
      break;
    }
  }
  unsigned idx = 0;
  bool truncated = false;
  /* Prepare JSON array */
  json_buff[0] = '\0';
  strcat(json_buff, "[");
  /* FatFS list directory entries */
  DIR dir;
  FILINFO fno;
  FRESULT fr;
  bool apps_installed_found = false;
  fr = f_opendir(&dir, req_folder);
  if (fr != FR_OK) {
    DPRINTF("Failed to open directory %s, error %d\n", req_folder, fr);
    /* Return empty JSON array */
    strcpy(json_buff, "[]");
    return "/json.shtml";
  }
  for (;;) {
    fr = f_readdir(&dir, &fno);
    if (fr != FR_OK || fno.fname[0] == '\0') break;
    // Skip until reaching nextItem index
    if (idx++ < nextItem) continue;
    // Stop if JSON buffer size approaches limit (buffer size minus 100 bytes)
    if (strlen(json_buff) >= sizeof(json_buff) - 100) {
      DPRINTF("JSON payload too large (%u chars), truncating listing\n",
              (unsigned)strlen(json_buff));
      truncated = true;
      break;
    }
    /* Build JSON object for each entry */
    char tmp[256];
    /* Combine date and time into a single ts field */
    unsigned ts = ((unsigned)fno.fdate << 16) | (unsigned)fno.ftime;
    snprintf(tmp, sizeof(tmp), "{\"n\":\"%s\",\"a\":%u,\"s\":%lu,\"t\":%u},",
             fno.fname, (unsigned)fno.fattrib, (unsigned long)fno.fsize, ts);
    strcat(json_buff, tmp);
    apps_installed_found = true;
  }
  f_closedir(&dir);
  /* Close JSON array, handle truncation sentinel */
  if (truncated) {
    size_t len = strlen(json_buff);
    if (len > 1 && json_buff[len - 1] == ',') {
      json_buff[len - 1] = '\0';
    }
    strcat(json_buff, ",{}]");
  } else if (apps_installed_found) {
    /* Remove trailing comma if any */
    size_t len = strlen(json_buff);
    if (len > 1 && json_buff[len - 1] == ',') {
      json_buff[len - 1] = ']';
      json_buff[len] = '\0';
    } else {
      strcat(json_buff, "]");
    }
  } else {
    /* No entries and not truncated */
    strcpy(json_buff, "[]");
  }
  if (!apps_installed_found) {
    DPRINTF("No subfolders found in %s\n", req_folder);
  }
  /* Return JSON page (json_buff contains array or empty) */
  return "/json.shtml";
}

// Upload context for resumable uploads
#define MAX_UPLOAD_CONTEXTS 4
typedef struct {
  char token[32];
  FIL file;
  bool in_use;
} upload_ctx_t;
static upload_ctx_t upload_contexts[MAX_UPLOAD_CONTEXTS] = {0};
// Context for ongoing POST-based chunk upload
static upload_ctx_t *current_chunk_ctx = NULL;
static unsigned current_chunk_idx = 0;

// Find context by token
static upload_ctx_t *find_upload_ctx(const char *token) {
  for (int i = 0; i < MAX_UPLOAD_CONTEXTS; i++) {
    if (upload_contexts[i].in_use &&
        strcmp(upload_contexts[i].token, token) == 0) {
      return &upload_contexts[i];
    }
  }
  return NULL;
}

// Allocate new context
static upload_ctx_t *alloc_upload_ctx(const char *token) {
  for (int i = 0; i < MAX_UPLOAD_CONTEXTS; i++) {
    if (!upload_contexts[i].in_use) {
      upload_contexts[i].in_use = true;
      strncpy(upload_contexts[i].token, token,
              sizeof(upload_contexts[i].token) - 1);
      upload_contexts[i].token[sizeof(upload_contexts[i].token) - 1] = '\0';
      return &upload_contexts[i];
    }
  }
  return NULL;
}

// Download context for chunked downloads
#define MAX_DOWNLOAD_CONTEXTS 4
typedef struct {
  char token[32];
  FIL file;
  bool in_use;
} download_ctx_t;
static download_ctx_t download_contexts[MAX_DOWNLOAD_CONTEXTS] = {0};

// Find download context by token
static download_ctx_t *find_download_ctx(const char *token) {
  for (int i = 0; i < MAX_DOWNLOAD_CONTEXTS; i++) {
    if (download_contexts[i].in_use &&
        strcmp(download_contexts[i].token, token) == 0) {
      return &download_contexts[i];
    }
  }
  return NULL;
}

// Allocate new download context
static download_ctx_t *alloc_download_ctx(const char *token) {
  for (int i = 0; i < MAX_DOWNLOAD_CONTEXTS; i++) {
    if (!download_contexts[i].in_use) {
      download_contexts[i].in_use = true;
      strncpy(download_contexts[i].token, token,
              sizeof(download_contexts[i].token) - 1);
      download_contexts[i].token[sizeof(download_contexts[i].token) - 1] = '\0';
      return &download_contexts[i];
    }
  }
  return NULL;
}

// Free download context and close file
static void free_download_ctx(download_ctx_t *ctx) {
  if (ctx->in_use) {
    f_close(&ctx->file);
    ctx->in_use = false;
  }
}

// Free context and close file
static void free_upload_ctx(upload_ctx_t *ctx) {
  if (ctx->in_use) {
    f_close(&ctx->file);
    ctx->in_use = false;
  }
}

// CGI: start upload
static const char *cgi_upload_start(int iIndex, int iNumParams, char *pcParam[],
                                    char *pcValue[]) {
  const char *token = NULL, *fullpath = NULL;
  char decoded_path[256];
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
    if (strcmp(pcParam[i], "fullpath") == 0) fullpath = pcValue[i];
  }
  if (!token || !fullpath) {
    strcpy(json_buff, "{\"error\":\"missing parameters\"}");
    return "/json.shtml";
  }
  if (!url_decode(fullpath, decoded_path, sizeof(decoded_path))) {
    strcpy(json_buff, "{\"error\":\"invalid path\"}");
    return "/json.shtml";
  }
  upload_ctx_t *ctx = alloc_upload_ctx(token);
  if (!ctx) {
    strcpy(json_buff, "{\"error\":\"no context available\"}");
    return "/json.shtml";
  }
  // Open file for writing
  FRESULT res = f_open(&ctx->file, decoded_path, FA_WRITE | FA_CREATE_ALWAYS);
  if (res != FR_OK) {
    free_upload_ctx(ctx);
    strcpy(json_buff, "{\"error\":\"cannot open file\"}");
    return "/json.shtml";
  }
  // Return status, chunk size, and preferred method for client
  snprintf(json_buff, sizeof(json_buff),
           "{\"status\":\"started\",\"chunkSize\":%d,\"method\":\"%s\"}",
           UPLOAD_CHUNK_SIZE, UPLOAD_CHUNK_METHOD);

  return "/json.shtml";
}

// CGI: upload chunk (base64 payload)
static const char *cgi_upload_chunk(int iIndex, int iNumParams, char *pcParam[],
                                    char *pcValue[]) {
  const char *token = NULL, *chunkStr = NULL, *payload = NULL;
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
    if (strcmp(pcParam[i], "chunk") == 0) chunkStr = pcValue[i];
    if (strcmp(pcParam[i], "payload") == 0) payload = pcValue[i];
  }
  upload_ctx_t *ctx = token ? find_upload_ctx(token) : NULL;
  if (!ctx || !chunkStr || !payload) {
    strcpy(json_buff, "{\"error\":\"invalid parameters\"}");
    return "/json.shtml";
  }
  int chunk = atoi(chunkStr);
  // URL-decode the base64 payload parameter
  size_t plen = strlen(payload) + 1;
  char *decodedPayload = malloc(plen);
  if (!decodedPayload || !url_decode(payload, decodedPayload, plen)) {
    free(decodedPayload);
    strcpy(json_buff, "{\"error\":\"invalid payload encoding\"}");
    return "/json.shtml";
  }
  // Base64-decode the payload
  size_t outLen = strlen(decodedPayload) * 3 / 4;
  unsigned char *buffer = malloc(outLen);
  size_t decodedLen;
  if (mbedtls_base64_decode(buffer, outLen, &decodedLen,
                            (const unsigned char *)decodedPayload,
                            strlen(decodedPayload)) != 0) {
    free(buffer);
    free(decodedPayload);
    strcpy(json_buff, "{\"error\":\"invalid base64\"}");
    return "/json.shtml";
  }
  free(decodedPayload);
  // Seek to chunk offset using fixed chunk size
  f_lseek(&ctx->file, (DWORD)(chunk * UPLOAD_CHUNK_SIZE));
  UINT written;
  FRESULT res = f_write(&ctx->file, buffer, decodedLen, &written);
  free(buffer);
  if (res != FR_OK || written != decodedLen) {
    strcpy(json_buff, "{\"error\":\"write failed\"}");
    return "/json.shtml";
  }
  strcpy(json_buff, "{\"status\":\"chunk_ok\"}");
  return "/json.shtml";
}

// CGI: end upload
static const char *cgi_upload_end(int iIndex, int iNumParams, char *pcParam[],
                                  char *pcValue[]) {
  const char *token = NULL;
  for (int i = 0; i < iNumParams; i++)
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
  upload_ctx_t *ctx = token ? find_upload_ctx(token) : NULL;
  if (!ctx) {
    strcpy(json_buff, "{\"error\":\"invalid token\"}");
    return "/json.shtml";
  }
  free_upload_ctx(ctx);
  strcpy(json_buff, "{\"status\":\"completed\"}");
  return "/json.shtml";
}

// CGI: cancel upload
static const char *cgi_upload_cancel(int iIndex, int iNumParams,
                                     char *pcParam[], char *pcValue[]) {
  const char *token = NULL;
  for (int i = 0; i < iNumParams; i++)
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
  upload_ctx_t *ctx = token ? find_upload_ctx(token) : NULL;
  if (!ctx) {
    strcpy(json_buff, "{\"error\":\"invalid token\"}");
    return "/json.shtml";
  }
  // Optionally delete partial file
  char path[256];
  f_getcwd(path, sizeof(path));  // stub, adjust if needed
  // Remove file using fullpath stored? Skipped
  free_upload_ctx(ctx);
  strcpy(json_buff, "{\"status\":\"cancelled\"}");
  return "/json.shtml";
}

// CGI: start download
static const char *cgi_download_start(int iIndex, int iNumParams,
                                      char *pcParam[], char *pcValue[]) {
  const char *token = NULL, *fullpath = NULL;
  char decoded_path[256];
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
    if (strcmp(pcParam[i], "fullpath") == 0) fullpath = pcValue[i];
  }
  if (!token || !fullpath) {
    strcpy(json_buff, "{\"error\":\"missing parameters\"}");
    return "/json.shtml";
  }
  if (!url_decode(fullpath, decoded_path, sizeof(decoded_path))) {
    strcpy(json_buff, "{\"error\":\"invalid path\"}");
    return "/json.shtml";
  }
  download_ctx_t *ctx = alloc_download_ctx(token);
  if (!ctx) {
    strcpy(json_buff, "{\"error\":\"no context available\"}");
    return "/json.shtml";
  }
  FRESULT res = f_open(&ctx->file, decoded_path, FA_READ);
  if (res != FR_OK) {
    free_download_ctx(ctx);
    strcpy(json_buff, "{\"error\":\"cannot open file\"}");
    return "/json.shtml";
  }
  DWORD size = f_size(&ctx->file);
  snprintf(json_buff, sizeof(json_buff),
           "{\"status\":\"started\",\"chunkSize\":%d,\"fileSize\":%lu}",
           DOWNLOAD_CHUNK_SIZE, (unsigned long)size);
  return "/json.shtml";
}

// CGI: download chunk
static const char *cgi_download_chunk(int iIndex, int iNumParams,
                                      char *pcParam[], char *pcValue[]) {
  const char *token = NULL, *chunkStr = NULL;
  for (int i = 0; i < iNumParams; i++) {
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
    if (strcmp(pcParam[i], "chunk") == 0) chunkStr = pcValue[i];
  }
  download_ctx_t *ctx = token ? find_download_ctx(token) : NULL;
  if (!ctx || !chunkStr) {
    strcpy(json_buff, "{\"error\":\"invalid parameters\"}");
    return "/json.shtml";
  }
  int chunk = atoi(chunkStr);
  DWORD offset = (DWORD)chunk * DOWNLOAD_CHUNK_SIZE;
  f_lseek(&ctx->file, offset);
  unsigned char rawbuf[DOWNLOAD_CHUNK_SIZE];
  UINT readBytes = 0;
  FRESULT res = f_read(&ctx->file, rawbuf, DOWNLOAD_CHUNK_SIZE, &readBytes);
  if (res != FR_OK) {
    strcpy(json_buff, "{\"error\":\"read failed\"}");
    return "/json.shtml";
  }
  unsigned char b64buf[MAX_JSON_PAYLOAD_SIZE];
  size_t olen = 0;
  if (mbedtls_base64_encode(b64buf, sizeof(b64buf), &olen, rawbuf, readBytes) !=
      0) {
    strcpy(json_buff, "{\"error\":\"base64 encode failed\"}");
    return "/json.shtml";
  }
  // JSON response with base64 data
  snprintf(json_buff, sizeof(json_buff),
           "{\"status\":\"chunk\",\"length\":%u,\"data\":\"%.*s\"}",
           (unsigned)readBytes, (int)olen, b64buf);
  return "/json.shtml";
}

// CGI: end download
static const char *cgi_download_end(int iIndex, int iNumParams, char *pcParam[],
                                    char *pcValue[]) {
  const char *token = NULL;
  for (int i = 0; i < iNumParams; i++)
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
  download_ctx_t *ctx = token ? find_download_ctx(token) : NULL;
  if (!ctx) {
    strcpy(json_buff, "{\"error\":\"invalid token\"}");
    return "/json.shtml";
  }
  free_download_ctx(ctx);
  strcpy(json_buff, "{\"status\":\"completed\"}");
  return "/json.shtml";
}

// CGI: cancel download
static const char *cgi_download_cancel(int iIndex, int iNumParams,
                                       char *pcParam[], char *pcValue[]) {
  const char *token = NULL;
  for (int i = 0; i < iNumParams; i++)
    if (strcmp(pcParam[i], "token") == 0) token = pcValue[i];
  download_ctx_t *ctx = token ? find_download_ctx(token) : NULL;
  if (!ctx) {
    strcpy(json_buff, "{\"error\":\"invalid token\"}");
    return "/json.shtml";
  }
  free_download_ctx(ctx);
  strcpy(json_buff, "{\"status\":\"cancelled\"}");
  return "/json.shtml";
}

// CGI: rename file
static const char *cgi_ren(int iIndex, int iNumParams, char *pcParam[],
                           char *pcValue[]) {
  const char *folder = NULL, *src = NULL, *dst = NULL;
  char df[256] = {0}, ds[128] = {0}, dd[128] = {0};
  for (int i = 0; i < iNumParams; i++) {
    if (!strcmp(pcParam[i], "folder")) folder = pcValue[i];
    if (!strcmp(pcParam[i], "src")) src = pcValue[i];
    if (!strcmp(pcParam[i], "dst")) dst = pcValue[i];
  }
  if (!folder || !src || !dst) {
    strcpy(json_buff, "{\"error\":\"missing parameters\"}");
    return "/json.shtml";
  }
  if (!url_decode(folder, df, sizeof(df)) || !url_decode(src, ds, sizeof(ds)) ||
      !url_decode(dst, dd, sizeof(dd))) {
    strcpy(json_buff, "{\"error\":\"invalid encoding\"}");
    return "/json.shtml";
  }
  char from[512], to[512];
  snprintf(from, sizeof(from), "%s/%s", df, ds);
  snprintf(to, sizeof(to), "%s/%s", df, dd);
  FRESULT r = f_rename(from, to);
  if (r != FR_OK)
    snprintf(json_buff, sizeof(json_buff), "{\"error\":\"rename failed %d\"}",
             r);
  else
    strcpy(json_buff, "{\"status\":\"renamed\"}");
  return "/json.shtml";
}

// CGI: delete file
// Recursively delete path (file or directory)
// Delete file or directory only if directory is empty
static FRESULT delete_path(const char *path) {
  FILINFO fno;
  FRESULT fr = f_stat(path, &fno);
  if (fr != FR_OK) return fr;
  if (fno.fattrib & AM_DIR) {
    // Check if directory is empty
    DIR dir;
    fr = f_opendir(&dir, path);
    if (fr != FR_OK) return fr;
    fr = f_readdir(&dir, &fno);
    f_closedir(&dir);
    if (fr != FR_OK) return fr;
    // If first entry name is not empty, directory has contents
    if (fno.fname[0] != '\0') return FR_DENIED;
    // Empty directory -> remove
    return f_unlink(path);
  }
  // Regular file
  return f_unlink(path);
}
static const char *cgi_del(int iIndex, int iNumParams, char *pcParam[],
                           char *pcValue[]) {
  const char *folder = NULL, *src = NULL;
  char df[256] = {0}, ds[128] = {0};
  for (int i = 0; i < iNumParams; i++) {
    if (!strcmp(pcParam[i], "folder")) folder = pcValue[i];
    if (!strcmp(pcParam[i], "src")) src = pcValue[i];
  }
  if (!folder || !src) {
    strcpy(json_buff, "{\"error\":\"missing parameters\"}");
    return "/json.shtml";
  }
  if (!url_decode(folder, df, sizeof(df)) || !url_decode(src, ds, sizeof(ds))) {
    strcpy(json_buff, "{\"error\":\"invalid encoding\"}");
    return "/json.shtml";
  }
  char path[512];
  snprintf(path, sizeof(path), "%s/%s", df, ds);
  FRESULT r = delete_path(path);
  if (r == FR_DENIED) {
    snprintf(json_buff, sizeof(json_buff),
             "{\"error\":\"directory not empty\"}");
  } else if (r != FR_OK) {
    snprintf(json_buff, sizeof(json_buff), "{\"error\":\"delete failed %d\"}",
             r);
  } else {
    strcpy(json_buff, "{\"status\":\"deleted\"}");
  }
  return "/json.shtml";
}
// CGI: set file attributes (hidden & read-only)
static const char *cgi_attr(int iIndex, int iNumParams, char *pcParam[],
                            char *pcValue[]) {
  const char *folder = NULL, *src = NULL, *hidden_s = NULL, *readonly_s = NULL;
  char df[256] = {0}, ds[128] = {0}, path[512];
  for (int i = 0; i < iNumParams; i++) {
    if (!strcmp(pcParam[i], "folder")) folder = pcValue[i];
    if (!strcmp(pcParam[i], "src")) src = pcValue[i];
    if (!strcmp(pcParam[i], "hidden")) hidden_s = pcValue[i];
    if (!strcmp(pcParam[i], "readonly")) readonly_s = pcValue[i];
  }
  if (!folder || !src || !hidden_s || !readonly_s) {
    strcpy(json_buff, "{\"error\":\"missing parameters\"}");
    return "/json.shtml";
  }
  if (!url_decode(folder, df, sizeof(df)) || !url_decode(src, ds, sizeof(ds))) {
    strcpy(json_buff, "{\"error\":\"invalid encoding\"}");
    return "/json.shtml";
  }
  int hide = atoi(hidden_s);
  int ro = atoi(readonly_s);
  snprintf(path, sizeof(path), "%s/%s", df, ds);
  // Build attribute mask
  BYTE attr = 0;
  if (ro) attr |= AM_RDO;
  if (hide) attr |= AM_HID;
  // Apply only hidden and read-only bits
  FRESULT r = f_chmod(path, attr, AM_RDO | AM_HID);
  if (r != FR_OK)
    snprintf(json_buff, sizeof(json_buff), "{\"error\":\"chmod failed %d\"}",
             r);
  else
    strcpy(json_buff, "{\"status\":\"attributes updated\"}");
  return "/json.shtml";
}

/**
 * @brief Array of CGI handlers for floppy select and eject operations.
 *
 * This array contains the mappings between the CGI paths and the corresponding
 * handler functions for selecting and ejecting floppy disk images for drive A
 * and drive B.
 */
static const tCGI cgi_handlers[] = {
    {"/test.cgi", cgi_test},
    {"/folder.cgi", cgi_folder},
    {"/download.cgi", cgi_download},
    {"/ls.cgi", cgi_ls},
    {"/upload_start.cgi", cgi_upload_start},
    {"/upload_chunk.cgi", cgi_upload_chunk},
    {"/upload_end.cgi", cgi_upload_end},
    {"/upload_cancel.cgi", cgi_upload_cancel},
    {"/ren.cgi", cgi_ren},
    {"/mkdir.cgi", cgi_mkdir},
    {"/del.cgi", cgi_del},
    {"/attr.cgi", cgi_attr},
    {"/download_start.cgi", cgi_download_start},
    {"/download_chunk.cgi", cgi_download_chunk},
    {"/download_end.cgi", cgi_download_end},
    {"/download_cancel.cgi", cgi_download_cancel}};

/**
 * @brief Initializes the HTTP server with optional SSI tags, CGI handlers, and
 * an SSI handler function.
 *
 * This function initializes the HTTP server and sets up the provided Server
 * Side Include (SSI) tags, Common Gateway Interface (CGI) handlers, and SSI
 * handler function. It first calls the httpd_init() function to initialize the
 * HTTP server.
 *
 * The filesystem for the HTTP server is in the 'fs' directory in the project
 * root.
 *
 * @param ssi_tags An array of strings representing the SSI tags to be used in
 * the server-side includes.
 * @param num_tags The number of SSI tags in the ssi_tags array.
 * @param ssi_handler_func A pointer to the function that handles SSI tags.
 * @param cgi_handlers An array of tCGI structures representing the CGI handlers
 * to be used.
 * @param num_cgi_handlers The number of CGI handlers in the cgi_handlers array.
 */
static void httpd_server_init(const char *ssi_tags[], size_t num_tags,
                              tSSIHandler ssi_handler_func,
                              const tCGI *cgi_handlers,
                              size_t num_cgi_handlers) {
  httpd_init();

  // SSI Initialization
  if (num_tags > 0) {
    for (size_t i = 0; i < num_tags; i++) {
      LWIP_ASSERT("tag too long for LWIP_HTTPD_MAX_TAG_NAME_LEN",
                  strlen(ssi_tags[i]) <= LWIP_HTTPD_MAX_TAG_NAME_LEN);
    }
    http_set_ssi_handler(ssi_handler_func, ssi_tags, num_tags);
  } else {
    DPRINTF("No SSI tags defined.\n");
  }

  // CGI Initialization
  if (num_cgi_handlers > 0) {
    http_set_cgi_handlers(cgi_handlers, num_cgi_handlers);
  } else {
    DPRINTF("No CGI handlers defined.\n");
  }

  DPRINTF("HTTP server initialized.\n");
}

err_t httpd_post_begin(void *connection, const char *uri,
                       const char *http_request, u16_t http_request_len,
                       int content_len, char *response_uri,
                       u16_t response_uri_len, u8_t *post_auto_wnd) {
  LWIP_UNUSED_ARG(connection);
  LWIP_UNUSED_ARG(http_request);
  LWIP_UNUSED_ARG(http_request_len);
  LWIP_UNUSED_ARG(content_len);
  LWIP_UNUSED_ARG(post_auto_wnd);
  DPRINTF("POST request for URI: %s\n", uri);
  // Handle binary chunk upload via POST
  if (strncmp(uri, "/upload_chunk.cgi", 17) == 0) {
    // parse token and chunk index from querystring
    const char *qs = strchr(uri, '?');
    if (qs) {
      // find token
      const char *t = strstr(qs, "token=");
      if (t) {
        t += 6;  // skip 'token='
        char buf[32];
        int i = 0;
        while (*t && *t != '&' && i < (int)sizeof(buf) - 1) buf[i++] = *t++;
        buf[i] = '\0';
        current_chunk_ctx = find_upload_ctx(buf);
      }
      // find chunk
      const char *c = strstr(qs, "chunk=");
      if (c) current_chunk_idx = atoi(c + 6);
      if (current_chunk_ctx) {
        // seek to correct offset
        f_lseek(&current_chunk_ctx->file,
                (DWORD)(current_chunk_idx * UPLOAD_CHUNK_SIZE));
        current_connection = connection;
        // allow immediate receive
        *post_auto_wnd = 1;
        return ERR_OK;
      }
    }
  }
  return ERR_VAL;
}

err_t httpd_post_receive_data(void *connection, struct pbuf *p) {
  LWIP_UNUSED_ARG(connection);
  // Write binary chunk data directly to file
  if (connection == current_connection && current_chunk_ctx && p) {
    UINT written;
    // p->payload may be chained; write each segment
    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
      f_write(&current_chunk_ctx->file, q->payload, q->len, &written);
    }
    pbuf_free(p);
    return ERR_OK;
  }
  // If the connection is not valid, return an error
  DPRINTF("POST data received for invalid connection\n");
  return ERR_VAL;
}

void httpd_post_finished(void *connection, char *response_uri,
                         u16_t response_uri_len) {
  DPRINTF("POST finished for connection\n");
  // clear context
  current_chunk_ctx = NULL;
  current_chunk_idx = 0;
  // respond with JSON status
  strcpy(json_buff, "{\"status\":\"chunk_ok\"}");
  // ensure LWIP returns our json
  strncpy(response_uri, "/json.shtml", response_uri_len);
}

/**
 * @brief Server Side Include (SSI) handler for the HTTPD server.
 *
 * This function is called when the server needs to dynamically insert content
 * into web pages using SSI tags. It handles different SSI tags and generates
 * the corresponding content to be inserted into the web page.
 *
 * @param iIndex The index of the SSI handler.
 * @param pcInsert A pointer to the buffer where the generated content should be
 * inserted.
 * @param iInsertLen The length of the buffer.
 * @param current_tag_part The current part of the SSI tag being processed (used
 * for multipart SSI tags).
 * @param next_tag_part A pointer to the next part of the SSI tag to be
 * processed (used for multipart SSI tags).
 * @return The length of the generated content.
 */
static u16_t ssi_handler(int iIndex, char *pcInsert, int iInsertLen
#if LWIP_HTTPD_SSI_MULTIPART
                         ,
                         u16_t current_tag_part, u16_t *next_tag_part
#endif /* LWIP_HTTPD_SSI_MULTIPART */
) {
  DPRINTF("SSI handler called with index %d\n", iIndex);
  size_t printed;
  switch (iIndex) {
    case 0: /* "HOMEPAGE" */
    {
      // Always to the first step of the configuration
      printed = snprintf(
          pcInsert, iInsertLen, "%s",
          "<meta http-equiv='refresh' content='0;url=/browser_home.shtml'>");
      break;
    }
    case 5: /* "SDCARDB"*/
    {
      // SD card status as boolean
      printed = snprintf(pcInsert, iInsertLen, "%s",
                         sdcard_status == SDCARD_INIT_OK ? "true" : "false");
      break;
    }
    case 7: /* JSONPLD */
    {
      // DPRINTF("SSI JSONPLD handler called with index %d\n", iIndex);
      int chunk_size = 128;
      /* The offset into json based on current tag part */
      size_t offset = current_tag_part * chunk_size;
      size_t json_len = strlen(json_buff);

      /* If offset is beyond the end, we have no more data */
      if (offset >= json_len) {
        /* No more data, so no next part */
        printed = 0;
        break;
      }

      /* Calculate how many bytes remain from offset */
      size_t remain = json_len - offset;
      /* We want to send up to chunk_size bytes per part, or what's left if
       * <chunk_size */
      size_t chunk_len = (remain < chunk_size) ? remain : chunk_size;

      /* Also ensure we don't exceed iInsertLen - 1, to leave room for '\0' */
      if (chunk_len > (size_t)(iInsertLen - 1)) {
        chunk_len = iInsertLen - 1;
      }

      /* Copy that chunk into pcInsert */
      memcpy(pcInsert, &json_buff[offset], chunk_len);
      pcInsert[chunk_len] = '\0'; /* null-terminate */

      printed = (u16_t)chunk_len;

      /* If there's more data after this chunk, increment next_tag_part */
      if ((offset + chunk_len) < json_len) {
        *next_tag_part = current_tag_part + 1;
      }
      break;
    }
    case 8: /* DWNLDSTS */
    {
      download_status_t status = download_getStatus();

      switch (status) {
        case DOWNLOAD_STATUS_IDLE:
        case DOWNLOAD_STATUS_COMPLETED:
          printed = snprintf(pcInsert, iInsertLen, "%s",
                             "<meta http-equiv='refresh' "
                             "content='0;url=/browser_home.shtml'>");
          break;
          break;
        case DOWNLOAD_STATUS_FAILED:
          printed = snprintf(
              pcInsert, iInsertLen,
              "<meta http-equiv='refresh' "
              "content='0;url=/"
              "error.shtml?error=%d&error_msg=Download%%20error:%%20%s'>",
              status, download_getErrorString());
          break;
          break;
        default:
          printed = snprintf(
              pcInsert, iInsertLen, "%s",
              "<meta http-equiv='refresh' content='5;url=/downloading.shtml'>");
          break;
      }
      break;
    }
    case 9: /* TITLEHDR */
    {
#if _DEBUG == 0
      printed = snprintf(pcInsert, iInsertLen, "%s (%s)", BROWSER_TITLE,
                         RELEASE_VERSION);
#else
      printed = snprintf(pcInsert, iInsertLen, "%s (%s-%s)", BROWSER_TITLE,
                         RELEASE_VERSION, RELEASE_DATE);
#endif
      break;
    }
    default: {
      // Handle other SSI tags
      DPRINTF("Unknown SSI tag index: %d\n", iIndex);
      printed = snprintf(pcInsert, iInsertLen, "Unknown SSI tag");
      break;
    }
  }
  return (u16_t)printed;
}

// The main function should be as follows:
void mngr_httpd_start(int sdcard_err) {
  // Set the SD card status based on the error code
  sdcard_status = sdcard_err;
  // Initialize the HTTP server with SSI tags and CGI handlers
  httpd_server_init(ssi_tags, LWIP_ARRAYSIZE(ssi_tags), ssi_handler,
                    cgi_handlers, LWIP_ARRAYSIZE(cgi_handlers));
}