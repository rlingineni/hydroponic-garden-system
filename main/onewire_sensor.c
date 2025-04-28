#include "onewire_sensor.h"
#include "esp_log.h"
#include "ds18b20.h"
#include "onewire_bus.h"

#define EXAMPLE_ONEWIRE_BUS_GPIO 13
#define EXAMPLE_ONEWIRE_MAX_DS18B20 2

static int s_ds18b20_device_num = 0;
static float s_temperature = 0.0;
static ds18b20_device_handle_t s_ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];

static const char *TAG = "DS18B20";

void sensor_detect(void)
{
    onewire_bus_handle_t bus = NULL;
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));

    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_onewire_device;
    esp_err_t search_result = ESP_OK;

    ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
    ESP_LOGI(TAG, "Device iterator created, start searching...");
    do
    {
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);
        if (search_result == ESP_OK)
        {
            ds18b20_config_t ds_cfg = {};
            onewire_device_address_t address;
            if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &s_ds18b20s[s_ds18b20_device_num]) == ESP_OK)
            {
                ds18b20_get_device_address(s_ds18b20s[s_ds18b20_device_num], &address);
                ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX", s_ds18b20_device_num, address);
                s_ds18b20_device_num++;
            }
            else
            {
                ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);
            }
        }
    } while (search_result != ESP_ERR_NOT_FOUND);
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));
    ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found", s_ds18b20_device_num);
}

float sensor_read(void)
{
    for (int i = 0; i < s_ds18b20_device_num; i++)
    {
        ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion(s_ds18b20s[i]));
        ESP_ERROR_CHECK(ds18b20_get_temperature(s_ds18b20s[i], &s_temperature));
        ESP_LOGI(TAG, "Temperature read from DS18B20[%d]: %.2fC", i, s_temperature);
        return s_temperature;
    }

    ESP_LOGE(TAG, "No DS18B20 device found");
    return -1.0;
    

}