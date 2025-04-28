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

const int WATER_LEVEL_PIN = 12;

const int PH_UP_PIN = 33;
const int PLANT_FOOD_PIN = 32;

const int PH_DOWN_PIN = 22;

const int FLOW_DURATION = 2000; // 3 seconds

bool should_dispense_ph_up = false;
bool should_dispense_ph_down = false;
bool should_dispense_plant_food = false;

#define DEFAULT_PERIOD 1000

#define BUF_SIZE (1024)

static uint8_t s_led_state = 1;

static uint32_t flash_period = DEFAULT_PERIOD;
static uint32_t flash_period_dec = DEFAULT_PERIOD / 10;

TaskHandle_t getTemperatureHandle = NULL;
TaskHandle_t getWaterLevelHandle = NULL;
TaskHandle_t getPHValueHandle = NULL;

TaskHandle_t dispensePhUpHandle = NULL;
TaskHandle_t dispensePhDownHandle = NULL;
TaskHandle_t dispensePlantFoodHandle = NULL;

static void get_temperature(void *pvParameters)
{
    while (1)
    {
        float a = sensor_read();


        // convert float value to string before sending
        char buffer[20];
        snprintf(buffer, sizeof(buffer), "%.2f", a);

        char result[50];                             // Ensure the array is large enough to hold the concatenated string
        strcpy(result, "T:");              // Copy the initial string into the result
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

    // gpio_reset_pin(PH_DOWN_PIN);
    // gpio_reset_pin(PH_DOWN_REVERSE_PIN);
    // gpio_set_direction(PH_DOWN_REVERSE_PIN, GPIO_MODE_OUTPUT);
    // gpio_set_direction(PH_DOWN_PIN, GPIO_MODE_OUTPUT);

    // gpio_set_level(PH_DOWN_PIN, 0);
    // gpio_set_level(PH_DOWN_REVERSE_PIN, 0);

    // vTaskDelay(FLOW_DURATION / portTICK_PERIOD_MS);

    // gpio_set_level(PH_DOWN_PIN, 0);
    // gpio_set_level(PH_DOWN_REVERSE_PIN, 0);

    // vTaskSuspend(dispensePhDownHandle);
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
            uart_write_bytes(ECHO_UART_PORT_NUM, "Water Level: LOW", strlen("WL: LOW;"));
        }
        else
        {
            uart_write_bytes(ECHO_UART_PORT_NUM, "Water Level: HIGH", strlen("WL: HIGH;"));
        }

        vTaskDelay(flash_period / portTICK_PERIOD_MS);
    }
}

static void blink_led(void)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    gpio_set_level(13, s_led_state);
}

static void blink_task(void *arg)
{
    while (1)
    {
        s_led_state = !s_led_state;
        blink_led();
        vTaskDelay(flash_period / portTICK_PERIOD_MS);
    }
}

// Questions for TA:
// 1. Reading Value from PH
// 2. 5V sensor to 3V reading?
// 3. Docs for ESP Methods?

static void get_ph_value(void *arg)
{
    // Configure ADC1 to read from channel 7 (GPIO25 / A1)
    adc1_config_width(ADC_WIDTH_BIT_12);                        // Set ADC resolution to 12 bits
    adc1_config_channel_atten(ADC1_CHANNEL_6, ADC_ATTEN_DB_11); // Set attenuation for full-scale voltage
    int ph_value = 0;
    while (1)
    {
        // Read the analog value from GPIO25 (ADC1_CHANNEL_7)
        ph_value = adc1_get_raw(ADC1_CHANNEL_6);

        // Print the pH value to the console
        printf("pH Value (ADC Reading): %d\n", ph_value);
        // ESP_LOGI("PH_LEVEL", "PH VALUE %d",ph_value);          // prints a INFO message

        // Delay for 1 second before reading again
        vTaskDelay(1000 / portTICK_PERIOD_MS); // Delay for 1 second
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
                s_led_state = 1;
                blink_led();
                uart_write_bytes(ECHO_UART_PORT_NUM, "ESP32", strlen("ESP32"));
                break;
            case 'T':
                flash_period -= flash_period_dec;
                if (flash_period <= flash_period_dec)
                    flash_period = flash_period_dec;
                break;
            case 'A':
            {
                should_dispense_ph_up = true;
            }
            break;
            case 'B':
            {
                should_dispense_plant_food = true;
            }
            break;
            case 'R':
                flash_period = DEFAULT_PERIOD;
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

    blink_led();
    printf("LED Blinking\n");

    // Detect the DS18B20 sensor in the bus
    sensor_detect();

    // xTaskCreate(echo_task, "uart_echo_task", ECHO_TASK_STACK_SIZE, NULL, 10, NULL);

    // xTaskCreate(blink_task, "blink_task", 1024, NULL, 5, NULL);

    xTaskCreate(get_water_level, "get_water_level", 2000, NULL, 5, &getWaterLevelHandle);

    // Create the get_ph_value task
    xTaskCreate(get_ph_value, "get_ph_value", 2000, NULL, 5, &getPHValueHandle);

    // Start task to read the temperature from DS18B20 sensor
    xTaskCreate(get_temperature, "temperature_detect", 4096, NULL, 5, &getTemperatureHandle);

    xTaskCreate(echo_task, "uart_echo_task", ECHO_TASK_STACK_SIZE, NULL, 10, NULL);

    xTaskCreate(dispense_ph_up, "dispense_ph_up", 4096, NULL, 5, &dispensePhUpHandle);

    // xTaskCreate(dispense_ph_down, "dispense_ph_down", 4096, NULL, 5, &dispensePhDownHandle);
    xTaskCreate(dispense_plant_food, "dispense_plant_food", 4096, NULL, 5, &dispensePlantFoodHandle);

    // vTaskSuspend(myTaskHandle);
}
