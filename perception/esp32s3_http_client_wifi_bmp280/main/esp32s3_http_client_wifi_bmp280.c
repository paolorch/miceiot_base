#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include <bmp280.h>

#include "esp_http_client.h"

#include "esp_adc/adc_oneshot.h"

#include "esp_mac.h"

#include "esp_heap_caps.h"

#include "esp_timer.h"

#include "wifi_secrets.h"

// 1 = tres potenciometros ADC
// 0 = sensor BMP280/BME280 original por I2C
#define USE_POTENTIOMETERS 1


#define POT_PRESSURE    ADC_CHANNEL_3
#define POT_TEMPERATURE ADC_CHANNEL_4
#define POT_HUMIDITY    ADC_CHANNEL_5

static adc_oneshot_unit_handle_t adc_handle;

#ifndef APP_CPU_NUM
#define APP_CPU_NUM PRO_CPU_NUM
#endif

#define SERVER_URL      "http://192.168.68.54:8080/measurement"

#define DEVICE_ID "01"

#define WIFI_CONNECTED_BIT BIT0

#define I2C_MASTER_SDA 8
#define I2C_MASTER_SCL 9

static const char *TAG = "HTTP_TEST";
static EventGroupHandle_t wifi_event_group;


/* -----------------------------------------------------------
 * SIMULACION TEMPORAL: 3 POTENCIOMETROS ADC
 * --------------------------------------------------------- */

#if USE_POTENTIOMETERS

static void pots_init(void)
{
    adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = ADC_UNIT_1,
    };

    ESP_ERROR_CHECK(adc_oneshot_new_unit(&unit_cfg, &adc_handle));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle, POT_PRESSURE, &chan_cfg));

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle, POT_TEMPERATURE, &chan_cfg));

    ESP_ERROR_CHECK(adc_oneshot_config_channel(
        adc_handle, POT_HUMIDITY, &chan_cfg));
}

static void pots_read(float *pressure, float *temperature, float *humidity)
{
    int raw_p = 0;
    int raw_t = 0;
    int raw_h = 0;

    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, POT_PRESSURE, &raw_p));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, POT_TEMPERATURE, &raw_t));
    ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, POT_HUMIDITY, &raw_h));

    // Conversiones simuladas, no mediciones ambientales reales
*pressure    = 90000.0f + (raw_p / 4095.0f) * 20000.0f;
*temperature = 10.0f + (raw_t / 4095.0f) * 35.0f;
*humidity    = 10.0f + (raw_h / 4095.0f) * 90.0f;

    ESP_LOGI(TAG, "ADC GPIO4=%d, GPIO5=%d, GPIO6=%d",
             raw_p, raw_t, raw_h);

    ESP_LOGI(TAG, "SIMULADO: P=%.2f Pa, T=%.2f C, H=%.2f%%",
             *pressure, *temperature, *humidity);
}

#endif



/* -----------------------------------------------------------
 * WiFi
 * --------------------------------------------------------- */
void bmp280_test(bmp280_t *dev)
{
    bmp280_params_t params;
    bmp280_init_default_params(&params);
    memset(dev, 0, sizeof(bmp280_t));

    ESP_ERROR_CHECK(bmp280_init_desc(dev, BMP280_I2C_ADDRESS_0, 0, I2C_MASTER_SDA, I2C_MASTER_SCL));
    ESP_ERROR_CHECK(bmp280_init(dev, &params));

    bool bme280p = dev->id == BME280_CHIP_ID;
    printf("BMP280: found %s\n", bme280p ? "BME280" : "BMP280");
}

void bmp280_read(bmp280_t *dev, float *pressure, float *temperature, float *humidity) {

    if (bmp280_read_float(dev, temperature, pressure, humidity) != ESP_OK) {
        printf("Temperature/pressure reading failed\n");
        return;
    }

    printf("Pressure: %.2f Pa, Temperature: %.2f C", *pressure, *temperature);

    bool bme280p = dev->id == BME280_CHIP_ID;
    if (bme280p)
        printf(", Humidity: %.2f\n", *humidity);
    else
        printf("\n");
}


/* -----------------------------------------------------------
 * WiFi
 * --------------------------------------------------------- */

static void wifi_event_handler(void *arg,
                               esp_event_base_t event_base,
                               int32_t event_id,
                               void *event_data)
{
    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START) {

        esp_wifi_connect();

    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {

        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);

    } else if (event_base == IP_EVENT &&
               event_id == IP_EVENT_STA_GOT_IP) {

        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;

        ESP_LOGI(TAG, "Got IP: " IPSTR,
                 IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(
        esp_event_loop_create_default()
    );

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(
        esp_wifi_init(&cfg)
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            WIFI_EVENT,
            ESP_EVENT_ANY_ID,
            &wifi_event_handler,
            NULL
        )
    );

    ESP_ERROR_CHECK(
        esp_event_handler_register(
            IP_EVENT,
            IP_EVENT_STA_GOT_IP,
            &wifi_event_handler,
            NULL
        )
    );

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(
        esp_wifi_set_mode(WIFI_MODE_STA)
    );

    ESP_ERROR_CHECK(
        esp_wifi_set_config(
            WIFI_IF_STA,
            &wifi_config
        )
    );

    ESP_ERROR_CHECK(
        esp_wifi_start()
    );

    ESP_LOGI(TAG, "Connecting to WiFi...");
}

/* -----------------------------------------------------------
 * HTTP event
 * --------------------------------------------------------- */

static esp_err_t http_event_handler(
    esp_http_client_event_t *evt)
{
    switch (evt->event_id) {

    case HTTP_EVENT_ON_DATA:

        printf("%.*s",
               evt->data_len,
               (char *)evt->data);

        break;

    default:
        break;
    }

    return ESP_OK;
}

/* -----------------------------------------------------------
 * GET
 * --------------------------------------------------------- */

static void http_get(void)
{
    ESP_LOGI(TAG, "GET %s", SERVER_URL);

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    esp_http_client_set_method(
        client,
        HTTP_METHOD_GET
    );

    esp_err_t err =
        esp_http_client_perform(client);

    if (err == ESP_OK) {

        ESP_LOGI(TAG,
                 "GET status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));

    } else {

        ESP_LOGE(TAG,
                 "GET failed: %s",
                 esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

/* -----------------------------------------------------------
 * POST
 * --------------------------------------------------------- */

static char *POST_DATA_TEMPLATE = "id="DEVICE_ID"&key=234567&t=%.2f&h=%.2f&p=%.2f&error=%d";

static void diagnostico(float p, float t, float h, int error, int http)
{
    uint8_t mac[6];
    wifi_ap_record_t ap = {0};

    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    esp_wifi_sta_get_ap_info(&ap);

    printf("\n========== DIAGNOSTICO ESP32-S3 ==========\n");
    printf("MAC:              %02X:%02X:%02X:%02X:%02X:%02X\n",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    printf("RAM libre:        %lu KB\n",
           (unsigned long)(esp_get_free_heap_size() / 1024));
    printf("WiFi RSSI:        %d dBm\n", ap.rssi);
    printf("Tiempo activo:    %lld segundos\n",
           (long long)(esp_timer_get_time() / 1000000));
#if USE_POTENTIOMETERS
    printf("Sensores:         3 canales ADC\n");
#else
    printf("Sensores:         BMP280/BME280 por I2C\n");
#endif
    printf("Presion:          %.2f Pa\n", p);
    printf("Temperatura:      %.2f C\n", t);
    printf("Humedad:          %.2f %%\n", h);
    printf("Estado medicion:  %s\n",
           error ? "FUERA DE RANGO" : "NORMAL");
    printf("HTTP:             %d\n", http);
    printf("==========================================\n\n");
}



static void http_post(float *pressure, float *temperature, float *humidity) {

    char post_data[128];

    int error = (*temperature < 15 || *temperature > 40 ||
                 *humidity < 20 || *humidity > 90 ||
                 *pressure < 95000 || *pressure > 105000);

    snprintf(post_data, sizeof(post_data), POST_DATA_TEMPLATE,
             *temperature, *humidity, *pressure, error);

    ESP_LOGI(TAG, "POST %s", SERVER_URL);
    ESP_LOGI(TAG, "POST_DATA %s", post_data);

    esp_http_client_config_t config = {
        .url = SERVER_URL,
        .event_handler = http_event_handler,
    };

    esp_http_client_handle_t client =
        esp_http_client_init(&config);

    esp_http_client_set_method(
        client,
        HTTP_METHOD_POST
    );

    esp_http_client_set_header(
        client,
        "Content-Type",
        "application/x-www-form-urlencoded"
    );

    esp_http_client_set_post_field(
        client,
        post_data,
        strlen(post_data)
    );

    esp_err_t err =
        esp_http_client_perform(client);

    int http_status = (err == ESP_OK)
        ? esp_http_client_get_status_code(client) : 0;

    diagnostico(*pressure, *temperature, *humidity, error, http_status);

    if (err == ESP_OK) {

        ESP_LOGI(TAG,
                 "POST status = %d",
                 esp_http_client_get_status_code(client));

    } else {

        ESP_LOGE(TAG,
                 "POST failed: %s",
                 esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
}

/* -----------------------------------------------------------
 * Main
 * --------------------------------------------------------- */

void app_main(void)
{
    ESP_ERROR_CHECK(
        nvs_flash_init()
    );

    #if USE_POTENTIOMETERS
        ESP_LOGI(TAG, "Modo ADC: tres potenciometros");
        pots_init();
    #else
        bmp280_t dev;

        ESP_ERROR_CHECK(i2cdev_init());

        ESP_LOGI(TAG, "bmp280 init...");
        bmp280_test(&dev);
    #endif

    wifi_init();

    /* Wait for WiFi */
    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY
    );

    ESP_LOGI(TAG, "WiFi connected");

    vTaskDelay(pdMS_TO_TICKS(1000));

    float pressure, temperature, humidity;
    while (1) {
        #if USE_POTENTIOMETERS
                pots_read(&pressure, &temperature, &humidity);
        #else
                bmp280_read(&dev, &pressure, &temperature, &humidity);
        #endif
       http_post(&pressure, &temperature, &humidity);
       vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
