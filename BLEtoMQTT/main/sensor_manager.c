#include "sensor_manager.h"

// Biến toàn cục để lưu handle ADC
static adc_oneshot_unit_handle_t adc1_handle;
static const char *TAG_SENSOR = "SENSOR_MANAGER";

void sensor_manager_init() {
    if (adc1_handle != NULL) {
        ESP_LOGW(TAG_SENSOR, "ADC1 is already initialized, skipping initialization");
        return;
    }

    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
    };

    esp_err_t ret = adc_oneshot_new_unit(&init_config, &adc1_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SENSOR, "Error initializing ADC unit: %s", esp_err_to_name(ret));
        return;
    }

    adc_oneshot_chan_cfg_t config_light = {
        .atten = ADC_ATTEN_DB_6,
        .bitwidth = ADC_BITWIDTH_12,
    };

    adc_oneshot_chan_cfg_t config_mq2 = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_12,
    };

    esp_err_t config_light_ret = adc_oneshot_config_channel(adc1_handle, LIGHT_CHANNEL, &config_light);
    esp_err_t config_mq2_ret = adc_oneshot_config_channel(adc1_handle, MQ2_CHANNEL, &config_mq2);

    if (config_light_ret != ESP_OK || config_mq2_ret != ESP_OK) {
        ESP_LOGE(TAG_SENSOR, "Error configuring ADC channels");
    }

    ESP_LOGI(TAG_SENSOR, "Sensor Manager Initialized");
}

int light_sensor_read() {
    if (adc1_handle == NULL) {
        ESP_LOGE(TAG_SENSOR, "ADC handle is NULL");
        return -1;
    }

    int raw_value = 0;
    esp_err_t ret = adc_oneshot_read(adc1_handle, LIGHT_CHANNEL, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SENSOR, "Error reading Light Sensor ADC value");
        return -1;
    }

    int light_intensity = (int)((float)raw_value / 4095.0f * 100);
    ESP_LOGI(TAG_SENSOR, "Light Intensity: %d%%", light_intensity);
    return light_intensity;
}

int mq2_sensor_read() {
    if (adc1_handle == NULL) {
        ESP_LOGE(TAG_SENSOR, "ADC handle is NULL");
        return -1;
    }

    int raw_value = 0;
    esp_err_t ret = adc_oneshot_read(adc1_handle, MQ2_CHANNEL, &raw_value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG_SENSOR, "Error reading MQ2 Sensor ADC value");
        return -1;
    }

    ESP_LOGI(TAG_SENSOR, "MQ2 Raw ADC Value: %d", raw_value);
    return raw_value;
}

float mq2_get_ppm(int raw_value) {
    float voltage = ((float)raw_value / 4095.0f) * 3.3f;
    float A = 1000.0f;  
    float B = -1.5f;
    float ppm = A * powf(voltage, B);

    ESP_LOGI(TAG_SENSOR, "Gas Concentration: %.2f ppm", ppm);
    return ppm;
}

void sensor_manager_deinit() {
    if (adc1_handle != NULL) {
        esp_err_t ret = adc_oneshot_del_unit(adc1_handle);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG_SENSOR, "ADC unit freed successfully");
            adc1_handle = NULL;
        } else {
            ESP_LOGE(TAG_SENSOR, "Failed to free ADC unit: %s", esp_err_to_name(ret));
        }
    }
}