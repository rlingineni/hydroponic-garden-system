/* UART Echo Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include <stdio.h>
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "driver/adc.h" // Include ADC driver header
#include "esp_adc/adc_oneshot.h"
#include "ds18b20.h"
#include "onewire_sensor.h"

/**
 * This is an example which echos any data it receives on configured UART back to the sender,
 * with hardware flow control turned off. It does not use UART driver event queue.
 *
 * - Port: configured UART
 * - Receive (Rx) buffer: on
 * - Transmit (Tx) buffer: off
 * - Flow control: off
 * - Event queue: off
 * - Pin assignment: see defines below (See Kconfig)
 */

#define ECHO_TEST_TXD (CONFIG_EXAMPLE_UART_TXD)
#define ECHO_TEST_RXD (CONFIG_EXAMPLE_UART_RXD)
#define ECHO_TEST_RTS (UART_PIN_NO_CHANGE)
#define ECHO_TEST_CTS (UART_PIN_NO_CHANGE)

#define ECHO_UART_PORT_NUM (CONFIG_EXAMPLE_UART_PORT_NUM)
#define ECHO_UART_BAUD_RATE (CONFIG_EXAMPLE_UART_BAUD_RATE)
#define ECHO_TASK_STACK_SIZE (CONFIG_EXAMPLE_TASK_STACK_SIZE)

const int LED_BLINK_PIN = 13;

const int TEMP_SENSOR_PIN = 13;
const int PH_SENSOR_PIN = ADC1_CHANNEL_6;

const int WATER_LEVEL_PIN = 14;
const int LIGHT_CHECK_PIN = 27;
const int PH_UP_PIN = 33;
const int PH_DOWN_PIN = 32;

const int PLANT_FOOD_PIN = 32;

const int FLOW_DURATION = 1000; // 2 seconds

bool should_dispense_ph_up = false;
bool should_dispense_ph_down = false;
bool should_auto_ph = false;
bool should_dispense_plant_food = false;
bool should_toggle_leds = false;

bool leds_on = false;

#define MIN_VAL 0
#define MAX_VAL 4095

#define DEFAULT_PERIOD 1000

#define BUF_SIZE (1024)

static uint8_t s_led_state = 1;
static float PH_VALUE_NOW = 7.0;
static float set_ph_value = 7.0;
static float error_ph = 0.20;
static uint8_t START_VALUE = 0;
static uint32_t flash_period = DEFAULT_PERIOD;
static uint32_t flash_period_dec = DEFAULT_PERIOD / 10;

TaskHandle_t getTemperatureHandle = NULL;
TaskHandle_t getWaterLevelHandle = NULL;
TaskHandle_t getPHValueHandle = NULL;
TaskHandle_t autoPHValueHandle = NULL;
TaskHandle_t dispensePhUpHandle = NULL;
TaskHandle_t dispensePhDownHandle = NULL;
TaskHandle_t dispensePlantFoodHandle = NULL;
TaskHandle_t lightCheckHandle = NULL;
TaskHandle_t toggleLedsHandle = NULL;

static void get_temperature(void *pvParameters)
{
    while (1)
    {
        float a = sensor_read();
        // convert float value to string before sending
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%.2f", a);

        char result[50];                             // Ensure the array is large enough to hold the concatenated string
        strcpy(result, "T:");                        // Copy the initial string into the result
        char *return_value = strcat(result, buffer); // Append buffer to result and get the return value

        uart_write_bytes(ECHO_UART_PORT_NUM, return_value, strlen(return_value));

        vTaskDelay(flash_period / portTICK_PERIOD_MS);
    }
}

static void dispense_ph_up(void *arg)
{
    ESP_LOGI("Motor", "Dispensing PH_UP");
    gpio_reset_pin(PH_UP_PIN);
    gpio_set_direction(PH_UP_PIN, GPIO_MODE_OUTPUT);

    while (1)
    {
        if (should_dispense_ph_up)
        {
            ESP_LOGI("Motor", "Activating PH_UP_PIN");
            gpio_set_level(PH_UP_PIN, 1);
            vTaskDelay(FLOW_DURATION / portTICK_PERIOD_MS);
            gpio_set_level(PH_UP_PIN, 0);
            should_dispense_ph_up = false;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

static void dispense_ph_down(void *arg)
{
    ESP_LOGI("Motor", "Dispensing PH_DOWN");
    gpio_reset_pin(PH_DOWN_PIN);
    gpio_set_direction(PH_DOWN_PIN, GPIO_MODE_OUTPUT);

    while (1)
    {
        if (should_dispense_ph_down)
        {
            ESP_LOGI("Motor", "Activating PH_UP_PIN");
            gpio_set_level(PH_DOWN_PIN, 1);
            vTaskDelay(FLOW_DURATION / portTICK_PERIOD_MS);
            gpio_set_level(PH_DOWN_PIN, 0);
            should_dispense_ph_down = false;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

static void dispense_plant_food(void *arg)
{
    ESP_LOGI("Motor", "Dispensing PLANT_FOOD");

    gpio_reset_pin(PLANT_FOOD_PIN);
    gpio_set_direction(PLANT_FOOD_PIN, GPIO_MODE_OUTPUT);

    while (1)
    {
        if (should_dispense_plant_food)
        {
            ESP_LOGI("Motor", "Activating PLANT_FOOD_PIN");
            gpio_set_level(PLANT_FOOD_PIN, 1);
            vTaskDelay(FLOW_DURATION / portTICK_PERIOD_MS);
            gpio_set_level(PLANT_FOOD_PIN, 0);
            should_dispense_plant_food = false;
        }
        vTaskDelay(100 / portTICK_PERIOD_MS); // Add a small delay to prevent busy looping
    }
}

static void get_water_level(void *arg)
{
    // read the value from pin 5
    while (1)
    {
        int water_level = gpio_get_level(WATER_LEVEL_PIN);

        if (water_level == 0)
        {
            uart_write_bytes(ECHO_UART_PORT_NUM, "WL: LOW", strlen("WL: LOW"));
        }
        else
        {
            uart_write_bytes(ECHO_UART_PORT_NUM, "WL: HIGH", strlen("WL: HIGH"));
        }

        vTaskDelay(flash_period / portTICK_PERIOD_MS);
    }
}

static void get_ph_value(void *arg)
{
    // Configure ADC1 to read from channel 7 (GPIO25 / A1)
    adc1_config_width(ADC_WIDTH_BIT_12);                        // Set ADC resolution to 12 bits
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); // Set attenuation for full-scale voltage
    float ph_value = 0;
    while (1)
    {
        // Read the analog value from GPIO25 (ADC1_CHANNEL_7)
        ph_value = adc1_get_raw(ADC1_CHANNEL_6);

        float ph_value_calibrated = -0.00476 * ph_value + 15.28; // Calibrate the value to get the pH level

        // convert float value to string before sending
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%.2f", ph_value_calibrated);

        char result[50];                             // Ensure the array is large enough to hold the concatenated string
        strcpy(result, "PH:");                       // Copy the initial string into the result
        char *return_value = strcat(result, buffer); // Append buffer to result and get the return value

        uart_write_bytes(ECHO_UART_PORT_NUM, return_value, strlen(return_value));
        // Delay for 1 second before reading again

        PH_VALUE_NOW = ph_value_calibrated;
        vTaskDelay(2200 / portTICK_PERIOD_MS); // Delay for 1 second
    }
}

static void light_control_led(int val)
{
    val = (val < 0) ? 0 : (val > 4096) ? 4096
                                       : val;
    gpio_reset_pin(LIGHT_CHECK_PIN);
    gpio_set_direction(LIGHT_CHECK_PIN, GPIO_MODE_OUTPUT);

    if (val > 1000)
    {
        leds_on = true;
        gpio_set_level(LIGHT_CHECK_PIN, 1);
    }
    else
    {
        leds_on = false;
        gpio_set_level(LIGHT_CHECK_PIN, 0);
    }

    vTaskDelay(10 / portTICK_PERIOD_MS);
}

static void toggle_led(void *arg)
{
    while (1)
    {
        if (should_toggle_leds)
        {
            if (leds_on)
            {
                light_control_led(0);
            }
            else
            {
                light_control_led(4000);
            }
        }
    }
}

static void light_check(void *arg)
{
    // Configure ADC1 to read from channel 3 (GPIO39 / A3)
    adc1_config_width(ADC_WIDTH_BIT_12);                        // Set ADC resolution to 12 bits
    adc1_config_channel_atten(ADC1_CHANNEL_3, ADC_ATTEN_DB_11); // Set attenuation for full-scale voltage

    int light_value = 0;
    while (1)
    {

        
        // Read the analog value from GPIO25 (ADC1_CHANNEL_7)
        light_value = adc1_get_raw(ADC1_CHANNEL_3);

        // Print the ADC to the console
        light_control_led(light_value);

        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%.2d", light_value);

        char result[50];                             // Ensure the array is large enough to hold the concatenated string
        strcpy(result, "L:");                        // Copy the initial string into the result
        char *return_value = strcat(result, buffer); // Append buffer to result and get the return value

        uart_write_bytes(ECHO_UART_PORT_NUM, return_value, strlen(return_value));

        // Delay for 1 second before reading again
        vTaskDelay(250 / portTICK_PERIOD_MS); // Delay for 1 second
    }
}

static void auto_PH(void *arg)
{
    set_ph_value = 6.0;
    int cnt = 1;
    while (1)
    {
        if (should_auto_ph)
        {
            if (PH_VALUE_NOW < set_ph_value - error_ph)
            {
                should_dispense_ph_up = true;
            }
            else if (PH_VALUE_NOW > set_ph_value + error_ph)
            {
                should_dispense_ph_down = true;
            }
            else
            {
                should_dispense_ph_up = false;
                should_dispense_ph_down = false;
            }
            cnt += 1;
            if (cnt == 50)
            {
                cnt = 1;
                should_dispense_ph_down = false;
                should_dispense_ph_up = false;
            }
            vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
        }
    }
}

static void echo_task(void *arg)
{
    /* Configure parameters of an UART driver,
     * communication pins and install the driver */
    uart_config_t uart_config = {
        .baud_rate = ECHO_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
    intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif

    ESP_ERROR_CHECK(uart_driver_install(ECHO_UART_PORT_NUM, BUF_SIZE * 2, 0, 0, NULL, intr_alloc_flags));
    ESP_ERROR_CHECK(uart_param_config(ECHO_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(ECHO_UART_PORT_NUM, ECHO_TEST_TXD, ECHO_TEST_RXD, ECHO_TEST_RTS, ECHO_TEST_CTS));

    // Configure a temporary buffer for the incoming data
    uint8_t *data = (uint8_t *)malloc(BUF_SIZE);

    uart_write_bytes(ECHO_UART_PORT_NUM, "Commands", strlen("Commands"));
    while (1)
    {
        // Read data from the UART
        int len = uart_read_bytes(ECHO_UART_PORT_NUM, data, (BUF_SIZE - 1), 20 / portTICK_PERIOD_MS);
        // Write data back to the UART
        uart_write_bytes(ECHO_UART_PORT_NUM, (const char *)data, len);
        if (len)
        {
            data[len] = '\0';
            switch (data[0])
            {
            case 'I':
                uart_write_bytes(ECHO_UART_PORT_NUM, "ESP32-Hydroponic garden system", strlen("ESP32-Hydroponic garden system"));
                break;
            case 'F':
                should_dispense_plant_food = true;
                break;
            case 'U':
                should_dispense_ph_up = true;
                break;
            case 'D':
                should_dispense_ph_down = true;
                break;
            case 'R':
                should_dispense_ph_up = false;
                should_dispense_ph_down = false;
                break;
            case 'P':
                should_auto_ph = true;
                if (should_dispense_ph_down == false && should_dispense_ph_up == false)
                {
                    should_auto_ph = false;
                }
                break;
            case 'L':
            {
                should_toggle_leds = true;
            }
            break;
            case 'S':
                vTaskResume(getPHValueHandle);
                vTaskResume(lightCheckHandle);
                vTaskResume(getWaterLevelHandle);
                vTaskResume(getTemperatureHandle);
                vTaskResume(dispensePhUpHandle);
                vTaskResume(dispensePhDownHandle);
                vTaskResume(autoPHValueHandle);
                break;
            case 'Q':
                vTaskSuspend(getPHValueHandle);
                vTaskSuspend(lightCheckHandle);
                vTaskSuspend(getWaterLevelHandle);
                vTaskSuspend(getTemperatureHandle);
                vTaskSuspend(dispensePhUpHandle);
                vTaskSuspend(dispensePhDownHandle);
                vTaskSuspend(autoPHValueHandle);
                break;
            default:
                break;
            }
        }
    }
}

void app_main(void)
{

    // // LED Blinking
    // gpio_set_direction(LED_BLINK_PIN, GPIO_MODE_OUTPUT);

    // // GPIO for temp sensor, water level
    // gpio_set_direction(TEMP_SENSOR_PIN, GPIO_MODE_INPUT);
    // gpio_set_direction(WATER_LEVEL_PIN, GPIO_MODE_INPUT);

    // Plant_Food dispense

    // blink_led();
    // printf("LED Blinking\n");

    // Detect the DS18B20 sensor in the bus
    sensor_detect();

    xTaskCreate(echo_task, "uart_echo_task", ECHO_TASK_STACK_SIZE, NULL, 10, NULL);

    // Start task to read the temperature from DS18B20 sensor
    xTaskCreate(get_temperature, "temperature_detect", 4096, NULL, 5, &getTemperatureHandle);
    xTaskCreate(get_water_level, "get_water_level", 2000, NULL, 5, &getWaterLevelHandle);

    // Create the get_ph_value task
    xTaskCreate(get_ph_value, "get_ph_value", 2000, NULL, 5, &getPHValueHandle);

    xTaskCreate(dispense_ph_up, "dispense_ph_up", 4096, NULL, 5, &dispensePhUpHandle);

    xTaskCreate(dispense_ph_down, "dispense_ph_down", 4096, NULL, 5, &dispensePhDownHandle);

    xTaskCreate(light_check, "light_check", 4096, NULL, 5, &lightCheckHandle);

    xTaskCreate(toggle_led, "toggle_led", 4096, NULL, 5, &toggleLedsHandle);

    // xTaskCreate(auto_PH, "auto_PH", 2000, NULL, 5, &autoPHValueHandle);
}