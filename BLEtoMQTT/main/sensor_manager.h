#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include <stdio.h>
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"
#include <math.h>


#define LIGHT_CHANNEL ADC_CHANNEL_6  // GPIO34
#define MQ2_CHANNEL ADC_CHANNEL_7    // GPIO35


void sensor_manager_init();
int light_sensor_read();
int mq2_sensor_read();
float mq2_get_ppm(int raw_value);
void sensor_manager_deinit();

#endif 