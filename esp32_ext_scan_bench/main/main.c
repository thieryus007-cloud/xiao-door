/*
 * Banc de test BLE Extended Advertising -- Étage 1 (Evolution-XIAO-BLE.md §6.5)
 *
 * Scanner passif "observer" pur : pas de connexion, pas de GATT. Reçoit les
 * advertisements étendus (et legacy) et journalise adresse, type, longueur
 * et contenu brut sur la console série -- critères de réussite §6.6
 * étapes 1-3 : adresse du XIAO visible, longueur de payload = 74 octets,
 * octets D2 FC 44 présents (UUID BTHome 0xFCD2 + device info 0x44).
 */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "nvs_flash.h"
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#include "esp_log.h"

#define TAG "EXTSCAN"

/* Continu (0/0 = pas de limite de durée/période, cf. exemples ESP-IDF ble_50). */
#define EXT_SCAN_DURATION 0
#define EXT_SCAN_PERIOD   0

static esp_ble_ext_scan_params_t ext_scan_params = {
    .own_addr_type  = BLE_ADDR_TYPE_PUBLIC,
    .filter_policy  = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE,
    .cfg_mask       = ESP_BLE_GAP_EXT_SCAN_CFG_UNCODE_MASK | ESP_BLE_GAP_EXT_SCAN_CFG_CODE_MASK,
    .uncoded_cfg    = {BLE_SCAN_TYPE_PASSIVE, 40, 40},
    .coded_cfg      = {BLE_SCAN_TYPE_PASSIVE, 40, 40},
};

static void log_adv_report(const esp_ble_gap_ext_adv_report_t *r)
{
    bool legacy = r->event_type & ESP_BLE_GAP_SET_EXT_ADV_PROP_LEGACY;

    ESP_LOGI(TAG, "== advertisement recu ==");
    ESP_LOGI(TAG, "  adresse      : %02x:%02x:%02x:%02x:%02x:%02x (type %d)",
             r->addr[0], r->addr[1], r->addr[2], r->addr[3], r->addr[4], r->addr[5],
             r->addr_type);
    ESP_LOGI(TAG, "  type         : %s (event_type 0x%04x)",
             legacy ? "legacy" : "ETENDU", r->event_type);
    ESP_LOGI(TAG, "  PHY          : primaire %d, secondaire %d", r->primary_phy, r->secondly_phy);
    ESP_LOGI(TAG, "  longueur     : %d octets", r->adv_data_len);

    if (r->adv_data_len > 0) {
        ESP_LOG_BUFFER_HEX(TAG, r->adv_data, r->adv_data_len);
    }

    uint8_t svc_len = 0;
    uint8_t *svc = esp_ble_resolve_adv_data_by_type((uint8_t *)r->adv_data, r->adv_data_len,
                                                      ESP_BLE_AD_TYPE_SERVICE_DATA, &svc_len);
    if (svc != NULL && svc_len >= 3) {
        ESP_LOGI(TAG, "  service data : UUID 0x%02X%02X, device info 0x%02X, %d octets de mesures",
                 svc[1], svc[0], svc[2], svc_len - 3);
    } else {
        ESP_LOGW(TAG, "  aucun AD Service Data (0x16) trouve dans cette trame");
    }
}

static void esp_gap_cb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param)
{
    switch (event) {
    case ESP_GAP_BLE_SET_EXT_SCAN_PARAMS_COMPLETE_EVT:
        if (param->set_ext_scan_params.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "echec set_ext_scan_params, status=0x%x", param->set_ext_scan_params.status);
            break;
        }
        ESP_LOGI(TAG, "parametres de scan etendu OK, demarrage du scan...");
        esp_ble_gap_start_ext_scan(EXT_SCAN_DURATION, EXT_SCAN_PERIOD);
        break;

    case ESP_GAP_BLE_EXT_SCAN_START_COMPLETE_EVT:
        if (param->ext_scan_start.status != ESP_BT_STATUS_SUCCESS) {
            ESP_LOGE(TAG, "echec demarrage scan etendu, status=0x%x", param->ext_scan_start.status);
            break;
        }
        ESP_LOGI(TAG, "scan etendu actif -- en attente d'advertisements");
        break;

    case ESP_GAP_BLE_EXT_ADV_REPORT_EVT:
        log_adv_report(&param->ext_adv_report.params);
        break;

    default:
        break;
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_BLE));

    esp_bluedroid_config_t bd_cfg = BT_BLUEDROID_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bluedroid_init_with_cfg(&bd_cfg));
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    ESP_ERROR_CHECK(esp_ble_gap_register_callback(esp_gap_cb));

    ESP_LOGI(TAG, "init BLE OK -- configuration du scan etendu");
    esp_ble_gap_set_ext_scan_params(&ext_scan_params);
}
