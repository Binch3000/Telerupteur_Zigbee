// Version 3.3.0 ESP32 par Espressif Systems

#ifndef ZIGBEE_MODE_ZCZR
#error "Zigbee end device mode is not selected in Tools->Zigbee mode"
#endif

#include "esp_zigbee_core.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "ha/esp_zigbee_ha_standard.h"

// ----------------------  OTA  ------------------------
#include "esp_check.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#if CONFIG_ZB_DELTA_OTA
#include "esp_delta_ota_ops.h"
#endif
#include "esp_timer.h"
#include "esp_zigbee_cluster.h"
#include "nvs_flash.h"
// -------------------- OTA ----------------------

//Attribution des END_POINT Configuration ZigBee
#define END_POINT_1 0x01

//--------Sortie ----------//
//Attribution des pin de sortie
static const byte pinSortie1 = 5;  // RGB_BUILTIN Test LED, Pin 5 sortie de commande
static const byte pinIdentify = 13;

//Tableau des Pin de sorties
#define NB_PIN_OUT 2
const uint8_t PIN_OUT[NB_PIN_OUT] = { pinSortie1, pinIdentify };            //Attribution des PINs de sorties
const uint8_t END_POINT_OUT_ID[NB_PIN_OUT] = { END_POINT_1, END_POINT_1 };  //Attribution des END_POINT en fonction des PIN_OUT
bool ETAT_LED_OUT[NB_PIN_OUT];


//-------- Entrée ----------//
//Attribution des pin d'entrée
static const byte pinEntre1 = 0;


//Tableau des Pin de entrées
#define NB_PIN_IN 1
const uint8_t PIN_IN[NB_PIN_IN] = { pinEntre1 };             //Attribution des PINs de entrées
const uint8_t END_POINT_IN_ID[NB_PIN_IN] = { END_POINT_1 };  //Attribution des END_POINT en fonction des PIN_INT
bool ETAT_LED_IN[NB_PIN_IN];
bool VAL_COUR_IN = false;  //Valeur Courante Entrante

#define ESP_ZB_ZR_CONFIG() \
  { \
    .esp_zb_role = ESP_ZB_DEVICE_TYPE_ROUTER, \
    .install_code_policy = INSTALLCODE_POLICY_ENABLE, \
    .nwk_cfg = { \
      .zczr_cfg = { \
        .max_children = MAX_CHILDREN, \
      }, \
    }, \
  }

#define ESP_ZB_DEFAULT_RADIO_CONFIG() \
  { .radio_mode = ZB_RADIO_MODE_NATIVE, }

#define ESP_ZB_DEFAULT_HOST_CONFIG() \
  { .host_connection_mode = ZB_HOST_CONNECTION_MODE_NONE, }

/* Zigbee configuration */
#define MAX_CHILDREN 10                                                  /* the max amount of connected devices */
#define INSTALLCODE_POLICY_ENABLE false                                  /* enable the install code policy for security */
#define ESP_ZB_PRIMARY_CHANNEL_MASK ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK /* Zigbee primary channel mask use in the example */
#define OTA_UPGRADE_RUNNING_FILE_VERSION 0x20250917                      /* The attribute indicates the file version of the running firmware image on the device */

/********************* Zigbee functions **************************/

static void Entree_Binaire() {
  for (uint8_t IN = 0; IN < NB_PIN_IN; IN++) {
    // Lire l'état de la broche
    VAL_COUR_IN = digitalRead(PIN_IN[IN]);

    // Si l'état a changé
    if (VAL_COUR_IN != ETAT_LED_IN[IN]) {
      ETAT_LED_IN[IN] = VAL_COUR_IN;  // Mettre à jour l'état local

      // Afficher le changement d'état
      if (VAL_COUR_IN) {
        Serial.print("Activation pin : ");
      } else {
        Serial.print("Désactivation pin : ");
      }
      Serial.println(PIN_IN[IN]);

      // Vérification explicite des endpoints
      esp_err_t err = ESP_OK;  // Initialiser la variable d'erreur
      esp_zb_lock_acquire(portMAX_DELAY);
      esp_zb_zcl_ias_zone_status_change_notif_cmd_t cmd = {
        cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000,
        cmd.zcl_basic_cmd.dst_endpoint = END_POINT_IN_ID[IN],
        cmd.zcl_basic_cmd.src_endpoint = END_POINT_IN_ID[IN],
        cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
        cmd.zone_status = ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1 ? VAL_COUR_IN : 0,
        cmd.zone_id = 0,
        cmd.delay = 0,
      };
      esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);
      err = esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);
      esp_zb_lock_release();

      // Controle du résultat de l'opération
      if (err != ESP_OK) {
        printf("Erreur lors de l'envoi de la commande pour EndPoint %d : %d\n", END_POINT_IN_ID[IN], err);
      } else {
        printf("Commande envoyée avec succès pour EndPoint %d.\n", END_POINT_IN_ID[IN]);
      }
    }
  }
}

static void bdb_start_top_level_commissioning_cb(uint8_t mode_mask) {
  ESP_ERROR_CHECK(esp_zb_bdb_start_top_level_commissioning(mode_mask));
}

void esp_zb_app_signal_handler(esp_zb_app_signal_t *signal_struct) {
  uint32_t *p_sg_p = signal_struct->p_app_signal;
  esp_err_t err_status = signal_struct->esp_err_status;
  esp_zb_app_signal_type_t sig_type = (esp_zb_app_signal_type_t)*p_sg_p;
  switch (sig_type) {
    case ESP_ZB_ZDO_SIGNAL_SKIP_STARTUP:
      log_i("Zigbee stack initialized");
      esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_INITIALIZATION);
      break;
    case ESP_ZB_BDB_SIGNAL_DEVICE_FIRST_START:
    case ESP_ZB_BDB_SIGNAL_DEVICE_REBOOT:
      if (err_status == ESP_OK) {
        log_i("Device started up in %s factory-reset mode", esp_zb_bdb_is_factory_new() ? "" : "non");
        if (esp_zb_bdb_is_factory_new()) {
          log_i("Start network formation");
          esp_zb_bdb_start_top_level_commissioning(ESP_ZB_BDB_MODE_NETWORK_STEERING);
        } else {
          log_i("Device rebooted");
        }
      } else {
        /* commissioning failed */
        log_w("Failed to initialize Zigbee stack (status: %s)", esp_err_to_name(err_status));
      }
      break;
    case ESP_ZB_BDB_SIGNAL_STEERING:
      if (err_status == ESP_OK) {
        esp_zb_ieee_addr_t extended_pan_id;
        esp_zb_get_extended_pan_id(extended_pan_id);
        log_i("Joined network successfully (Extended PAN ID: %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x, PAN ID: 0x%04hx, Channel:%d, Short Address: 0x%04hx)",
              extended_pan_id[7], extended_pan_id[6], extended_pan_id[5], extended_pan_id[4], extended_pan_id[3], extended_pan_id[2], extended_pan_id[1],
              extended_pan_id[0], esp_zb_get_pan_id(), esp_zb_get_current_channel(), esp_zb_get_short_address());
      } else {
        log_i("Network steering was not successful (status: %s)", esp_err_to_name(err_status));
        esp_zb_scheduler_alarm((esp_zb_callback_t)bdb_start_top_level_commissioning_cb, ESP_ZB_BDB_MODE_NETWORK_STEERING, 1000);
      }
      break;
    default:
      log_i("ZDO signal: %s (0x%x), status: %s", esp_zb_zdo_signal_to_string(sig_type), sig_type, esp_err_to_name(err_status));

      //Mise à jour lors de la reconnection de l'etat des contacts Attention idem que lors de la routine
      if (sig_type == 0x36) {
        for (uint8_t IN = 0; IN < NB_PIN_IN; IN++) {
          // Lire l'état de la broche
          VAL_COUR_IN = digitalRead(PIN_IN[IN]);
          Serial.println(PIN_IN[IN]);
          // Vérification explicite des endpoints
          esp_err_t err = ESP_OK;  // Initialiser la variable d'erreur
          esp_zb_lock_acquire(portMAX_DELAY);
          esp_zb_zcl_ias_zone_status_change_notif_cmd_t cmd = {
            cmd.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000,
            cmd.zcl_basic_cmd.dst_endpoint = END_POINT_IN_ID[IN],
            cmd.zcl_basic_cmd.src_endpoint = END_POINT_IN_ID[IN],
            cmd.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT,
            cmd.zone_status = ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1 ? VAL_COUR_IN : 0,
            cmd.zone_id = 0,
            cmd.delay = 0,
          };
          esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);
          err = esp_zb_zcl_ias_zone_status_change_notif_cmd_req(&cmd);
          esp_zb_lock_release();

          // Controle du résultat de l'opération
          if (err != ESP_OK) {
            printf("Erreur lors de l'envoi de la commande pour EndPoint %d : %d\n", END_POINT_IN_ID[IN], err);
          } else {
            printf("Commande envoyée avec succès pour EndPoint %d.\n", END_POINT_IN_ID[IN]);
          }
        }
      }  // Attention idem que lors de la routine
      break;
  }
}

static esp_err_t zb_action_handler(esp_zb_core_action_callback_id_t callback_id, const void *message) {
  esp_err_t ret = ESP_OK;
  switch (callback_id) {
    case ESP_ZB_CORE_SET_ATTR_VALUE_CB_ID:
      ret = zb_attribute_handler((esp_zb_zcl_set_attr_value_message_t *)message);
      break;
    default:
      log_w("Receive Zigbee action(0x%x) callback", callback_id);
      break;
  }
  return ret;
}

static void esp_zb_task(void *pvParameters) {
  esp_zb_cfg_t zb_nwk_cfg = ESP_ZB_ZR_CONFIG();
  esp_zb_init(&zb_nwk_cfg);
  /* Set default allowed network channels */
  esp_zb_set_channel_mask(ESP_ZB_TRANSCEIVER_ALL_CHANNELS_MASK);

  // ------------------------------ Cluster BASIC ------------------------------
  esp_zb_basic_cluster_cfg_t basic_cluster_cfg = {
    .zcl_version = ESP_ZB_ZCL_BASIC_ZCL_VERSION_DEFAULT_VALUE,
  };

  uint8_t StackVersion = 0x0002;
  uint8_t HWVersion = 0x0002;
  uint8_t ManufacturerName[] = { 9, 'B', 'i', 'n', 'c', 'h', '&', 'C', 'i', 'e' };
  uint8_t ModelIdentifier[] = { 18, 'T', 'e', 'l', 'e', 'r', 'u', 'p', 't', 'e', 'u', 'r', ' ', 'Z', 'i', 'g', 'B', 'e', 'e' };
  uint8_t DateCode[] = { 8, '2', '0', '2', '5', '0', '9', '2', '3' };

  esp_zb_attribute_list_t *esp_zb_basic_cluster = esp_zb_basic_cluster_create(&basic_cluster_cfg);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_STACK_VERSION_ID, &StackVersion);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_HW_VERSION_ID, &HWVersion);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MANUFACTURER_NAME_ID, (void *)&ManufacturerName);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_MODEL_IDENTIFIER_ID, (void *)&ModelIdentifier);
  esp_zb_basic_cluster_add_attr(esp_zb_basic_cluster, ESP_ZB_ZCL_ATTR_BASIC_DATE_CODE_ID, (void *)&DateCode);

  // ------------------------------------Cluster OTA ---------------------------------
  esp_zb_ota_cluster_cfg_t ota_cluster_cfg = {};
  ota_cluster_cfg.ota_upgrade_file_version = OTA_UPGRADE_RUNNING_FILE_VERSION;
  esp_zb_attribute_list_t *ota_cluster = esp_zb_ota_cluster_create(&ota_cluster_cfg);

  // ------------------------------ Cluster IDENTIFY ------------------------------
  esp_zb_identify_cluster_cfg_t identify_cluster_cfg = {};
  esp_zb_attribute_list_t *esp_zb_identify_cluster = esp_zb_identify_cluster_create(&identify_cluster_cfg);

  // ------------------------------ Cluster ON_OFF (Commande Telerupteur) ------------------------------
  esp_zb_on_off_cluster_cfg_t on_off_cluster_cfg = {};
  esp_zb_attribute_list_t *esp_zb_on_off_cluster = esp_zb_on_off_cluster_create(&on_off_cluster_cfg);

  // ------------------------------ Cluster IAS ZONE Entrée (Relecture Lumière) ------------------------------
  esp_zb_ias_zone_cluster_cfg_t ias_zone_cluster_cfg = {
    .zone_state = ESP_ZB_ZCL_IAS_ZONE_ZONESTATE_NOT_ENROLLED,
    .zone_type = ESP_ZB_ZCL_IAS_ZONE_ZONETYPE_CONTACT_SWITCH,
    .zone_status = ESP_ZB_ZCL_IAS_ZONE_ZONE_STATUS_ALARM1,
    .zone_id = 0,
  };
  esp_zb_attribute_list_t *esp_zb_ias_zone_cluster = esp_zb_ias_zone_cluster_create(&ias_zone_cluster_cfg);

  // ------------------------------ Create cluster list ------------------------------
  esp_zb_cluster_list_t *esp_zb_cluster_list_1 = esp_zb_zcl_cluster_list_create();
  esp_zb_cluster_list_add_basic_cluster(esp_zb_cluster_list_1, esp_zb_basic_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_ota_cluster(esp_zb_cluster_list_1, ota_cluster, ESP_ZB_ZCL_CLUSTER_CLIENT_ROLE);
  esp_zb_cluster_list_add_identify_cluster(esp_zb_cluster_list_1, esp_zb_identify_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_ias_zone_cluster(esp_zb_cluster_list_1, esp_zb_ias_zone_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);
  esp_zb_cluster_list_add_on_off_cluster(esp_zb_cluster_list_1, esp_zb_on_off_cluster, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE);

  // ------------------------------ Create endpoint list ------------------------------
  esp_zb_endpoint_config_t endpoint_config_1 = {
    .endpoint = END_POINT_1,
    .app_profile_id = ESP_ZB_AF_HA_PROFILE_ID,
    .app_device_id = ESP_ZB_HA_ON_OFF_SWITCH_DEVICE_ID,
    .app_device_version = 0,
  };

  esp_zb_ep_list_t *esp_zb_ep_list = esp_zb_ep_list_create();
  esp_zb_ep_list_add_ep(esp_zb_ep_list, esp_zb_cluster_list_1, endpoint_config_1);

  // ------------------------------ Register Device ------------------------------
  esp_zb_device_register(esp_zb_ep_list);
  esp_zb_core_action_handler_register(zb_action_handler);
  esp_zb_set_primary_network_channel_set(ESP_ZB_PRIMARY_CHANNEL_MASK);

  //Erase NVRAM before creating connection to new Coordinator
  esp_zb_nvram_erase_at_start(false);  //Comment out this line to erase NVRAM data if you are conneting to new Coordinator

  ESP_ERROR_CHECK(esp_zb_start(false));
  esp_zb_main_loop_iteration();
}

/* Handle the light attribute */

static esp_err_t zb_attribute_handler(const esp_zb_zcl_set_attr_value_message_t *message) {
  esp_err_t ret = ESP_OK;

  if (!message) {
    log_e("Empty message");
  }
  if (message->info.status != ESP_ZB_ZCL_STATUS_SUCCESS) {
    log_e("Received message: error status(%d)", message->info.status);
  }

  log_i("Received message: EndPoint(%d), Cluster(0x%x), Attribute(0x%x), Data size(%d)", message->info.dst_endpoint, message->info.cluster, message->attribute.id, message->attribute.data.size);
  // Traitement des messages
  for (uint8_t OUT = 0; OUT < NB_PIN_OUT; OUT++) {
    // Vérifier si le message correspond au endpoint actuel
    if (message->info.dst_endpoint == END_POINT_OUT_ID[OUT]) {
      // Gestion du cluster ON/OFF
      if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_ON_OFF) {
        if (message->attribute.id == ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID && message->attribute.data.type == ESP_ZB_ZCL_ATTR_TYPE_BOOL) {
          // Si ce n'est pas la broche d'identification
          if (PIN_OUT[OUT] != pinIdentify) {
            // Allumer la LED et attendre
            log_i("Lumière sur ON");
            digitalWrite(PIN_OUT[OUT], HIGH);
            vTaskDelay(pdMS_TO_TICKS(250));  //Temps d'activation du relais ms
            // Éteindre la LED
            ETAT_LED_OUT[OUT] = false;
            log_i("Lumière sur OFF");
            digitalWrite(PIN_OUT[OUT], LOW);
            esp_err_t err = ESP_OK;  // Initialiser la variable d'erreur
            if (END_POINT_OUT_ID[OUT] == END_POINT_1) {
              // Envoyer une commande OFF
              esp_zb_zcl_on_off_cmd_t cmd_req_off;

              // Configurer la commande OFF
              cmd_req_off.zcl_basic_cmd.dst_addr_u.addr_short = 0x0000;         // Adresse Zigbee courte
              cmd_req_off.zcl_basic_cmd.dst_endpoint = END_POINT_1;             // Endpoint cible
              cmd_req_off.zcl_basic_cmd.src_endpoint = END_POINT_1;             // Endpoint source
              cmd_req_off.address_mode = ESP_ZB_APS_ADDR_MODE_16_ENDP_PRESENT;  // Mode adresse
              cmd_req_off.on_off_cmd_id = ESP_ZB_ZCL_CMD_ON_OFF_OFF_ID;         // Commande OFF

              // Envoyer la commande
              esp_err_t err;
              esp_zb_lock_acquire(portMAX_DELAY);  // Acquisition du verrou Zigbee
              err = esp_zb_zcl_on_off_cmd_req(&cmd_req_off);
              esp_zb_lock_release();  // Libération du verrou Zigbee
              // Vérifier si la commande a été envoyée avec succès
              if (err == ESP_OK) {
                log_i("Commande OFF envoyée avec succès");
              } else {
                log_e("Erreur lors de l'envoi de la commande OFF : %d", err);
              }

              int off_value = 0x00;
              esp_zb_lock_acquire(portMAX_DELAY);
              err = esp_zb_zcl_set_attribute_val(END_POINT_1, ESP_ZB_ZCL_CLUSTER_ID_ON_OFF, ESP_ZB_ZCL_CLUSTER_SERVER_ROLE, ESP_ZB_ZCL_ATTR_ON_OFF_ON_OFF_ID, &off_value, sizeof(off_value));
              esp_zb_lock_release();
              // Vérifier si la commande a été envoyée avec succès
              if (err == ESP_OK) {
                log_i("Commande2 OFF envoyée avec succès");
              } else {
                log_e("Erreur lors de l'envoi de la commande2 OFF : %d", err);
              }
            }
          }
        }
      }
      // Gestion du cluster IDENTIFY
      else if (message->info.cluster == ESP_ZB_ZCL_CLUSTER_ID_IDENTIFY) {
        if (message->attribute.id == ESP_ZB_ZCL_ATTR_IDENTIFY_IDENTIFY_TIME_ID) {
          // Si la broche correspond à la broche d'identification
          if (PIN_OUT[OUT] == pinIdentify) {
            bool newState = message->attribute.data.value ? *(bool *)message->attribute.data.value : ETAT_LED_OUT[OUT];
            ETAT_LED_OUT[OUT] = newState;
            // Mettre à jour l'état de la LED
            log_i("Light sets to %s", ETAT_LED_OUT[OUT] ? "On" : "Off");
            digitalWrite(PIN_OUT[OUT], HIGH * ETAT_LED_OUT[OUT]);
          }
        }
      }
    }
  }
  return ret;
}

/********************* Arduino functions **************************/

void setup() {
  //Init dialogue
  Serial.begin(115200);
  //Init Sorties
  for (uint8_t INIT_OUT = 0; INIT_OUT < NB_PIN_OUT; INIT_OUT++) {
    pinMode(PIN_OUT[INIT_OUT], OUTPUT);
    digitalWrite(PIN_OUT[INIT_OUT], LOW);
    ETAT_LED_OUT[INIT_OUT] = false;
  }

  //Init Entrées
  for (uint8_t INIT_IN = 0; INIT_IN < NB_PIN_IN; INIT_IN++) {
    pinMode(PIN_IN[INIT_IN], INPUT_PULLUP);
    ETAT_LED_IN[INIT_IN] = false;
  }

  // Init Zigbee
  esp_zb_platform_config_t config = {
    .radio_config = ESP_ZB_DEFAULT_RADIO_CONFIG(),
    .host_config = ESP_ZB_DEFAULT_HOST_CONFIG(),
  };
  ESP_ERROR_CHECK(esp_zb_platform_config(&config));
  // Start Zigbee task
  xTaskCreate(esp_zb_task, "Zigbee_main", 4096, NULL, 5, NULL);
  Serial.println("Démarrage OK");
}

void loop() {
  Entree_Binaire();
}
