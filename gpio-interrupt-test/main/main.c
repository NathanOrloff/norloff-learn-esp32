#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "sdkconfig.h"

/**
 * Constants for timer interrupt period 
 */
#define BLINK_GPIO              CONFIG_BLINK_GPIO
#define BLINK_PERIOD            500000
#define SLEEP_PERIOD            2000000

/**
 * Constants for GPIO interrupt
 */
#define GPIO_OUTPUT_IO          CONFIG_GPIO_OUTPUT
#define GPIO_INPUT_IO           CONFIG_GPIO_INPUT
#define GPIO_OUTPUT_PIN_SEL     (1ULL<<CONFIG_GPIO_OUTPUT)
#define GPIO_INPUT_PIN_SEL      (1ULL<<GPIO_INPUT_IO)
#define ESP_INTR_FLAG_DEFAULT   0

static const char *TAG = "GPIO Interrupt";
static QueueHandle_t gpio_evt_queue = NULL;
static uint8_t s_led_state = 0;
static uint8_t s_output_state = 0;

static void configure_led(void)
{
    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT);
}

static void toggle_gpio(uint32_t io_num, uint8_t state)
{
    /* Set the GPIO level according to the state (LOW or HIGH)*/
    ESP_LOGI(TAG, "toggle_gpio: gpio level = %d", state);
    gpio_set_level(io_num, state);
}

static void timer_callback(void* arg)
{
    // ESP_LOGI(TAG, "timer_callback: Turning the LED %s!", gpio_get_level(GPIO_OUTPUT_IO) == true ? "ON" : "OFF");
    toggle_gpio(GPIO_OUTPUT_IO, s_output_state);
    s_output_state = !s_output_state;
}

static void create_timer(void)
{
    const esp_timer_create_args_t periodic_timer_args = {
                .callback = &timer_callback,
                .name = "periodic"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, BLINK_PERIOD));
}

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
}

static void gpio_task(void* arg)
{
    uint32_t io_num;
    for (;;) {
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            ESP_LOGI(TAG, "GPIO[%"PRIu32"] intr, val: %d", io_num, gpio_get_level(io_num));
            toggle_gpio(BLINK_GPIO, s_led_state);
            s_led_state = !s_led_state;

        }
    }
}


static void configure_gpio(void)
{
    gpio_config_t io_conf_out = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = GPIO_OUTPUT_PIN_SEL,
        .pull_down_en = 0,
        .pull_up_en = 0
    };
    gpio_config(&io_conf_out);

    gpio_config_t io_conf_in = {
        .intr_type = GPIO_INTR_POSEDGE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = GPIO_INPUT_PIN_SEL,
        .pull_up_en = 1
    };
    gpio_config(&io_conf_in);
}

static void configure_gpio_interrupt(void)
{
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    gpio_isr_handler_add(GPIO_INPUT_IO, gpio_isr_handler, (void*) GPIO_INPUT_IO);
}

static void create_task_queue(void)
{
    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(uint32_t));
    xTaskCreate(gpio_task, "gpio_task", 2048, NULL, 10, NULL);
}



void app_main(void)
{
    // setup
    create_task_queue();
    configure_gpio();
    configure_led();
    configure_gpio_interrupt();
    create_timer();
    
    while (1) {
        usleep(SLEEP_PERIOD);
    }
}
