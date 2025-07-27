/**
 * File: mngr.c
 * Author: Diego Parrilla Santamaría
 * Date: December 2024
 * Copyright: 2023-25 - GOODDATA LABS SL
 * Description: C file with the main loop of the manager module
 */

#include "mngr.h"

static bool startBooster =
    false;  // Flag to indicate if the booster should start

static TransmissionProtocol lastProtocol;
static bool lastProtocolValid = false;

static uint32_t memorySharedAddress = 0;
static uint32_t memoryRandomTokenAddress = 0;
static uint32_t memoryRandomTokenSeedAddress = 0;

/**
 * @brief Callback that handles the protocol command received.
 *
 * This callback copy the content of the protocol to the last_protocol
 * structure. The last_protocol_valid flag is set to true to indicate that the
 * last_protocol structure contains a valid protocol. We return to the
 * dma_irq_handler_lookup function to continue asap with the next
 *
 * @param protocol The TransmissionProtocol structure containing the protocol
 * information.
 */
static inline void __not_in_flash_func(handle_protocol_command)(
    const TransmissionProtocol *protocol) {
  // Copy the content of protocol to last_protocol
  // Copy the 8-byte header directly
  lastProtocol.command_id = protocol->command_id;
  lastProtocol.payload_size = protocol->payload_size;
  lastProtocol.bytes_read = protocol->bytes_read;
  lastProtocol.final_checksum = protocol->final_checksum;

  // Sanity check: clamp payload_size to avoid overflow
  uint16_t size = protocol->payload_size;
  if (size > MAX_PROTOCOL_PAYLOAD_SIZE) {
    size = MAX_PROTOCOL_PAYLOAD_SIZE;
  }

  // Copy only used payload bytes
  memcpy(lastProtocol.payload, protocol->payload, size);

  lastProtocolValid = true;
};

static inline void __not_in_flash_func(handle_protocol_checksum_error)(
    const TransmissionProtocol *protocol) {
  DPRINTF("Checksum error detected (ID=%u, Size=%u)\n", protocol->command_id,
          protocol->payload_size);
}

// Interrupt handler for DMA completion
void __not_in_flash_func(mngr_dma_irq_handler_lookup)(void) {
  // Read the rom3 signal and if so then process the command
  dma_hw->ints1 = 1U << 2;

  // Read once to avoid redundant hardware access
  uint32_t addr = dma_hw->ch[2].al3_read_addr_trig;

  // Check ROM3 signal (bit 16)
  // We expect that the ROM3 signal is not set very often, so this should help
  // the compilar to run faster
  if (__builtin_expect(addr & 0x00010000, 0)) {
    // Invert highest bit of low word to get 16-bit address
    uint16_t addr_lsb = (uint16_t)(addr ^ ADDRESS_HIGH_BIT);

    tprotocol_parse(addr_lsb, handle_protocol_command,
                    handle_protocol_checksum_error);
  }
}

void mngr_preinit() {
  // Memory shared address
  memorySharedAddress = (unsigned int)&__rom_in_ram_start__;
  memoryRandomTokenAddress = memorySharedAddress + TERM_RANDOM_TOKEN_OFFSET;
  memoryRandomTokenSeedAddress =
      memorySharedAddress + TERM_RANDON_TOKEN_SEED_OFFSET;
  SET_SHARED_VAR(TERM_HARDWARE_TYPE, 0, memorySharedAddress,
                 TERM_SHARED_VARIABLES_OFFSET);  // Clean the hardware type
  SET_SHARED_VAR(TERM_HARDWARE_VERSION, 0, memorySharedAddress,
                 TERM_SHARED_VARIABLES_OFFSET);  // Clean the hardware version

  // Initialize the random seed (add this line)
  srand(time(NULL));
  // Init the random token seed in the shared memory for the next command
  uint32_t newRandomSeedToken = rand();  // Generate a new random 32-bit value
  TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenSeedAddress, newRandomSeedToken);
}

// Invoke this function to process the commands from the active loop in the
// main function
void __not_in_flash_func(mngr_loop)() {
  if (lastProtocolValid) {
    // Shared by all commands
    // Read the random token from the command and increment the payload
    // pointer to the first parameter available in the payload
    uint32_t randomToken = TPROTO_GET_RANDOM_TOKEN(lastProtocol.payload);
    uint16_t *payloadPtr = ((uint16_t *)(lastProtocol).payload);
    uint16_t commandId = lastProtocol.command_id;
    DPRINTF(
        "Command ID: %d. Size: %d. Random token: 0x%08X, Checksum: 0x%04X\n",
        lastProtocol.command_id, lastProtocol.payload_size, randomToken,
        lastProtocol.final_checksum);

#if defined(_DEBUG) && (_DEBUG != 0)
    // Jump the random token
    TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);

    // Read the payload parameters
    uint16_t payloadSizeTmp = 4;
    if ((lastProtocol.payload_size > payloadSizeTmp) &&
        (lastProtocol.payload_size <= TERM_PARAMETERS_MAX_SIZE)) {
      DPRINTF("Payload D3: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }
    payloadSizeTmp += 4;
    if ((lastProtocol.payload_size > payloadSizeTmp) &&
        (lastProtocol.payload_size <= TERM_PARAMETERS_MAX_SIZE)) {
      DPRINTF("Payload D4: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }
    payloadSizeTmp += 4;
    if ((lastProtocol.payload_size > payloadSizeTmp) &&
        (lastProtocol.payload_size <= TERM_PARAMETERS_MAX_SIZE)) {
      DPRINTF("Payload D5: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }
    payloadSizeTmp += 4;
    if ((lastProtocol.payload_size > payloadSizeTmp) &&
        (lastProtocol.payload_size <= TERM_PARAMETERS_MAX_SIZE)) {
      DPRINTF("Payload D6: 0x%04X\n", TPROTO_GET_PAYLOAD_PARAM32(payloadPtr));
      TPROTO_NEXT32_PAYLOAD_PTR(payloadPtr);
    }
#endif

    // Handle the command
    switch (lastProtocol.command_id) {
      case APP_BOOSTER_START: {
        SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_BOOSTER);
        startBooster = true;  // Set the flag to start the booster
        DPRINTF("Send command to display: DISPLAY_COMMAND_BOOSTER\n");
      } break;
      default:
        // Unknown command
        DPRINTF("Unknown command\n");
        break;
    }
    if (memoryRandomTokenAddress != 0) {
      // Set the random token in the shared memory
      TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenAddress, randomToken);

      // Init the random token seed in the shared memory for the next command
      uint32_t newRandomSeedToken =
          rand();  // Generate a new random 32-bit value
      TPROTO_SET_RANDOM_TOKEN(memoryRandomTokenSeedAddress, newRandomSeedToken);
    } else {
      DPRINTF("Memory random token address is not set.\n");
    }
  }
  lastProtocolValid = false;
}

int mngr_init() {
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_NOP);

  // Initialize the network
  int err = network_initChipOnly();
  if (err != 0) {
    DPRINTF("Error initializing the network: %i\n", err);
    blink_error();
    return err;
  }

  // Initialize the SD card
  FATFS fs;
  int sdcard_err = sdcard_initFilesystem(&fs, "");
  if (sdcard_err != SDCARD_INIT_OK) {
    DPRINTF("Error initializing the SD card: %i\n", sdcard_err);
  } else {
    DPRINTF("SD card found & initialized\n");
  }

  // Deinit the network
  DPRINTF("Deinitializing the network\n");
  network_deInit();

  // Set hostname
  char *hostname =
      settings_find_entry(gconfig_getContext(), PARAM_HOSTNAME)->value;
  char url_host[128] = {0};
  if ((hostname != NULL) && (strlen(hostname) > 0)) {
    snprintf(url_host, sizeof(url_host), "http://%s", hostname);
  } else {
    snprintf(url_host, sizeof(url_host), "http://%s", "sidecart");
  }

  char url_ip[128] = {0};
  snprintf(url_ip, sizeof(url_ip), "http://%s", "127.0.0.1");

  // Set SSID
  char ssid[128] = {0};
  SettingsConfigEntry *ssid_param =
      settings_find_entry(gconfig_getContext(), PARAM_WIFI_SSID);
  if (strlen(ssid_param->value) == 0) {
    DPRINTF("No SSID found in config.\n");
    snprintf(ssid, sizeof(ssid), "%s", "No SSID found");
  } else {
    snprintf(ssid, sizeof(ssid), "%s", ssid_param->value);
  }

  display_mngr_start(ssid, url_host, url_ip);

  wifi_mode_t wifi_mode_value = WIFI_MODE_STA;
  err = network_wifiInit(wifi_mode_value);
  if (err != 0) {
    DPRINTF("Error initializing the network: %i\n", err);
    blink_error();
    return err;
  }

  // Connect to the WiFi network
  int numRetries = 3;
  err = NETWORK_WIFI_STA_CONN_ERR_TIMEOUT;
  while (err != NETWORK_WIFI_STA_CONN_OK) {
    err = network_wifiStaConnect();
    if (err < 0) {
      DPRINTF("Error connecting to the WiFi network: %i\n", err);
      DPRINTF("Number of retries left: %i\n", numRetries);
      if (--numRetries <= 0) {
        DPRINTF("Max retries reached. Exiting...\n");
        display_mngr_wifi_change_status(2, NULL, NULL,
                                        "Max retries reached. Exiting...");
        display_refresh();
        blink_error();
        sleep_ms(1000);
        return err;
      } else {
        display_mngr_wifi_change_status(
            2, NULL, NULL, network_wifiConnStatusStr(err));  // Error
        display_refresh();
      }
      sleep_ms(3000);  // Wait before retrying
      display_mngr_wifi_change_status(0, NULL, NULL,
                                      NULL);  // Reset to connecting status
      display_refresh();
    }
  }
  DPRINTF("WiFi connected\n");

  // Disable the SELECT button
  select_coreWaitPushDisable();

  // Enable the SELECT button again, but only to reset the BOOSTER
  select_coreWaitPush(reset_device,
                      reset_deviceAndEraseFlash);  // Wait for the SELECT
                                                   // button to be pushed
  ip4_addr_t ip = network_getCurrentIp();
  DPRINTF("IP address: %s\n", ip4addr_ntoa(&ip));

  snprintf(url_ip, sizeof(url_ip), "http://%s", ip4addr_ntoa(&ip));

  display_mngr_wifi_change_status(1, url_host, url_ip, NULL);
  display_refresh();

  mngr_preinit();

  // Start the HTTP server
  mngr_httpd_start(sdcard_err);

  absolute_time_t start_download_time =
      make_timeout_time_ms(86400 * 1000);  // 3 seconds to start the download

  bool usbInitialized = false;  // USB not initialized yet
  while (!startBooster) {
#if PICO_CYW43_ARCH_POLL
    network_safePoll();
#else
    sleep_ms(100);
#endif

    // Check remote commands
    mngr_loop();

    // Disable the USB if nothing is mounted
    if (!cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN) && usbInitialized) {
      // Disconnect the USB mass storage
      DPRINTF("Disconnecting the USB mass storage...\n");
      tud_disconnect();

      irq_set_enabled(USBCTRL_IRQ, false);
      // Reset the USB controller
      reset_block(RESETS_RESET_USBCTRL_BITS);
      // Deassert the reset to re-enable the USB controller
      unreset_block_wait(RESETS_RESET_USBCTRL_BITS);

      usbInitialized = false;
      display_mngr_usb_change_status(false);
      display_refresh();
      DPRINTF("USB mass storage disconnected\n");
    }

    if (usbInitialized) {
      tud_task();  // tinyusb device task
    }

    if (cyw43_arch_gpio_get(CYW43_WL_GPIO_VBUS_PIN) && (!usbInitialized)) {
      DPRINTF("USB VBUS is high, starting USB mass storage...\n");
      usb_mass_init();

      display_mngr_usb_change_status(true);
      display_refresh();
      usbInitialized = true;
      DPRINTF("USB mass storage initialized\n");
    }

    switch (download_getStatus()) {
      case DOWNLOAD_STATUS_STARTED:
      case DOWNLOAD_STATUS_IN_PROGRESS: {
        download_poll();
        break;
      }
      case DOWNLOAD_STATUS_REQUESTED: {
        start_download_time =
            make_timeout_time_ms(3 * 1000);  // 3 seconds to start the download
        download_setStatus(DOWNLOAD_STATUS_NOT_STARTED);
        break;
      }
      case DOWNLOAD_STATUS_NOT_STARTED: {
        if ((absolute_time_diff_us(get_absolute_time(), start_download_time) <
             0)) {
          err = download_start();
          if (err != DOWNLOAD_OK) {
            DPRINTF("Error downloading app. Drive to error page.\n");
          }
        }
        break;
      }
      case DOWNLOAD_STATUS_COMPLETED: {
        // Save the app info to the SD card
        download_err_t err = download_finish();
        if (err != DOWNLOAD_OK) {
          DPRINTF("Error finishing download app\n");
          download_setStatus(DOWNLOAD_STATUS_FAILED);
          break;
        }
        download_setStatus(DOWNLOAD_STATUS_IDLE);
        DPRINTF("Download completed successfully.\n");
        break;
      }
    }
  }

#define SLEEP_LOOP_MS 1000
  select_setResetCallback(NULL);      // Disable the reset callback
  select_setLongResetCallback(NULL);  // Disable the long reset callback
  select_coreWaitPushDisable();       // Disable the SELECT button
  sleep_ms(SLEEP_LOOP_MS);
  // We must reset the computer
  SEND_COMMAND_TO_DISPLAY(DISPLAY_COMMAND_RESET);
  sleep_ms(SLEEP_LOOP_MS);

  // Jump to the booster app
  DPRINTF("Jumping to the booster app...\n");
  reset_jump_to_booster();

  while (1) {
    // Wait for the computer to start
    sleep_ms(SLEEP_LOOP_MS);
  }
}
