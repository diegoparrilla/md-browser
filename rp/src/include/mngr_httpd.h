/**
 * File: mngr_httpd.h
 * Author: Diego Parrilla Santamaría
 * Date: December 2025
 * Copyright: 2024-2025 - GOODDATA LABS SL
 * Description: Header file for the manager mode httpd server.
 */

#ifndef MNGR_HTTPD_H
#define MNGR_HTTPD_H

#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "constants.h"
#include "debug.h"
#include "download.h"
#include "lwip/apps/httpd.h"
#include "mbedtls/base64.h"
#include "mngr.h"
#include "network.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"

typedef enum {
  MNGR_HTTPD_RESPONSE_OK = 200,
  MNGR_HTTPD_RESPONSE_BAD_REQUEST = 400,
  MNGR_HTTPD_RESPONSE_NOT_FOUND = 404,
  MNGR_HTTPD_RESPONSE_INTERNAL_SERVER_ERROR = 500
} mngr_httpd_response_status_t;

void mngr_httpd_start(int sdcard_err);

#endif  // MNGR_HTTPD_H
