
#include <stdio.h> 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_nimble_hci.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "sdkconfig.h"
#include "protocol_examples_common.h"
#include "mqtt_client.h"
#include "esp_system.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "esp32-dht11.h"
#include "sensor_manager.h"

#define TAG_DHT11 "DHT11"
#define TAG_LIGHT_SENSOR "LIGHT_SENSOR"
#define TAG_MQ2  "MQ2_SENSOR"

static struct dht11_reading stDht11Reading;
char *TAG_BLE = "BLE-Server";
uint8_t ble_addr_type;
void ble_app_advertise(void);
void ble_app_on_sync(void); 
esp_mqtt_client_handle_t mqtt_client;

void dht11_test(void *pvParameters)
{
    DHT11_init(GPIO_NUM_4);  
    while(1)
    {
        stDht11Reading = DHT11_read();  
        if(DHT11_OK == stDht11Reading.status) 
        {
            ESP_LOGI(TAG_DHT11,"Temperature: %d °C\tHumidity: %d %%",stDht11Reading.temperature,stDht11Reading.humidity);
        // Tạo chuỗi JSON để gửi dữ liệu
        char dht11_str[50];
        snprintf(dht11_str, sizeof(dht11_str), "{\"temperature\": %d, \"humidity\": %d}",stDht11Reading.temperature, stDht11Reading.humidity);

        esp_mqtt_client_publish(mqtt_client, "dht11-sensor", dht11_str, 0, 1, 0);
                
            }else  
        {
            ESP_LOGW(TAG_DHT11,"Cannot read from sensor: %s",(DHT11_TIMEOUT_ERROR == stDht11Reading.status) ? "Timeout" : "Bad CRC");
        }
        vTaskDelay(2500 / portTICK_PERIOD_MS); 
    }
}
//BLE write - gửi dữ liệu từ client đến server
static int device_write(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) 
{
    char *data = (char *)ctxt->om->om_data;
    int data_len = ctxt->om->om_len;

    // Đảm bảo chuỗi đầu vào được kết thúc bằng ký tự null
    if (data_len > 0 && data[data_len - 1] != '\0') {
        data[data_len] = '\0'; 
    }

    if (strcmp(data, "temp") == 0) 
    {
        printf("Received Data Length: %d\n", data_len);
        esp_mqtt_client_publish(mqtt_client, "ble_send_data", data, data_len, 1, 0);
        xTaskCreate(dht11_test, "dht11_test", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
    } 
    else if (strcmp(data, "light") == 0) 
    {
        printf("Received Data Length: %d\n", data_len);
        esp_mqtt_client_publish(mqtt_client, "ble_send_data", data, data_len, 1, 0);

        sensor_manager_init();

        int light_value = light_sensor_read();
        ESP_LOGI(TAG_LIGHT_SENSOR, "Light Intensity: %d%%", light_value);

        sensor_manager_deinit();

        char light_str[50];
        snprintf(light_str, sizeof(light_str), "{\"light\": %d}", light_value);

        os_mbuf_append(ctxt->om, light_str, strlen(light_str));
        esp_mqtt_client_publish(mqtt_client, "light-sensor", light_str, 0, 1, 0);
    } 
    else if (strcmp(data, "air") == 0) 
    {
        printf("Received Data Length: %d\n", data_len);
        esp_mqtt_client_publish(mqtt_client, "ble_send_data", data, data_len, 1, 0);

        sensor_manager_init();

        int air_value = mq2_sensor_read();
        ESP_LOGI(TAG_MQ2, "Air Quality: %d%%", air_value);

        sensor_manager_deinit();

        char air_str[50];
        snprintf(air_str, sizeof(air_str), "{\"air\": %d}", air_value);

        os_mbuf_append(ctxt->om, air_str, strlen(air_str));
        esp_mqtt_client_publish(mqtt_client, "mq2-sensor", air_str, 0, 1, 0);
    } 
    else 
    {
        printf("Received Data Length: %d\n", data_len);
        printf("Data from the client: %.*s\n", ctxt->om->om_len, ctxt->om->om_data);
        esp_mqtt_client_publish(mqtt_client, "ble_send_data", data, data_len, 1, 0);
    }
    return 0;
}


//BLE Read - Gửi dữ liệu từ server đến client
static int device_read(uint16_t con_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt *ctxt, void *arg) {
    os_mbuf_append(ctxt->om, "Data from the server", strlen("Data from the server"));
    return 0;
}


// Các dịch vụ BLE
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY,
     .uuid = BLE_UUID16_DECLARE(0x0180),
     .characteristics = (struct ble_gatt_chr_def[]){
         {.uuid = BLE_UUID16_DECLARE(0xFEF4),
          .flags = BLE_GATT_CHR_F_READ,
          .access_cb = device_read},
         {.uuid = BLE_UUID16_DECLARE(0xDEAD),
          .flags = BLE_GATT_CHR_F_WRITE,
          .access_cb = device_write},
         {0}}},

    {0}
};

// Xử lý sự kiện GAP (kết nối, ngắt kết nối)
static int ble_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT CONNECT %s", event->connect.status == 0 ? "OK!" : "FAILED!");
        if (event->connect.status != 0) {
            ble_app_advertise();
        }
        break;
    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI("GAP", "BLE GAP EVENT DISCONNECTED");
        break;
    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI("GAP", "BLE GAP EVENT ADV COMPLETE");
        ble_app_advertise();
        break;
    default:
        break;
    }
    return 0;
}

// Quảng bá BLE
void ble_app_advertise(void) {
    struct ble_hs_adv_fields fields;
    const char *device_name;
    memset(&fields, 0, sizeof(fields));
    device_name = ble_svc_gap_device_name();
    fields.name = (uint8_t *)device_name;
    fields.name_len = strlen(device_name);
    fields.name_is_complete = 1;
    ble_gap_adv_set_fields(&fields);

    struct ble_gap_adv_params adv_params;
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    ble_gap_adv_start(ble_addr_type, NULL, BLE_HS_FOREVER, &adv_params, ble_gap_event, NULL);
}

void ble_app_on_sync(void)
{
     ble_hs_id_infer_auto(0, &ble_addr_type); 
     ble_app_advertise();                     
}

 // The infinite task
 void host_task(void *param)
 {
     nimble_port_run(); 
 }
// MQTT Event Handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    ESP_LOGD(TAG_BLE, "Event dispatched from event loop base=%s, event_id=%" PRIi32 "", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_BLE, "MQTT_EVENT_CONNECTED");
        msg_id = esp_mqtt_client_publish(client, "esp32", "hello", 0, 1, 0);
        ESP_LOGI(TAG_BLE, "sent publish successful, msg_id=%d", msg_id);
        msg_id = esp_mqtt_client_subscribe(client, "mqttesp32", 0);
        ESP_LOGI(TAG_BLE, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_BLE, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    default:
        ESP_LOGI(TAG_BLE, "Other event id:%d", event->event_id);
        break;
    }
}

static void mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://mqtt.eclipseprojects.io:1883",
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}


void app_main() {
   
    
    ESP_ERROR_CHECK(nvs_flash_init());  // Initialize NVS
    nimble_port_init();                  // Initialize NimBLE stack
    ble_svc_gap_device_name_set("BLE-Server");
    ble_svc_gap_init();                  // Initialize GAP service
    ble_svc_gatt_init();                 // Initialize GATT service
    ble_gatts_count_cfg(gatt_svcs);      // Configure GATT services
    ble_gatts_add_svcs(gatt_svcs);       // Add GATT services
    ble_hs_cfg.sync_cb = ble_app_on_sync;  // Sync callback
    nimble_port_freertos_init(host_task); // Run the task

    ESP_LOGI(TAG_BLE, "[APP] Startup..");
    ESP_LOGI(TAG_BLE, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG_BLE, "[APP] IDF version: %s", esp_get_idf_version());

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(example_connect());



    mqtt_app_start();  // Start MQTT client

    
}
