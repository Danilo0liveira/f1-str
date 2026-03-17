/* FreeRTOS Real Time Stats Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "sdkconfig.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "driver/gptimer.h"

#define NUM_OF_SPIN_TASKS   6
#define SPIN_ITER           500000  //Actual CPU cycles used will depend on compiler optimization
#define SPIN_TASK_PRIO      2
#define STATS_TASK_PRIO     3
#define STATS_TICKS         pdMS_TO_TICKS(1000)
#define ARRAY_SIZE_OFFSET   5   //Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE

#define INJECTION_STANDARD_VALUE 100
#define INJECTION_HACK_VALUE 120
#define SENSOR_SAMPLE_TIME 8000

#define ALERT_CHEAT_DETECTED_LED GPIO_NUM_13
#define ALERT_SYSTEM_GOOD GPIO_NUM_12
#define ALERT_CHEAT_ON_LED GPIO_NUM_14

#define START_CHEAT_BUTTON GPIO_NUM_25
#define FORCE_FAULT_BUTTON GPIO_NUM_27

static SemaphoreHandle_t sync_spin_task;
static SemaphoreHandle_t sync_stats_task;
static SemaphoreHandle_t xSemaphoreSensorRead;
static SemaphoreHandle_t xStartButtonPressed;
static SemaphoreHandle_t xSyncTimerSensorRead;
static SemaphoreHandle_t xSyncTimerCheat;


gptimer_handle_t gptimer_2 = NULL;


/**
 * @brief   Function to print the CPU usage of tasks over a given duration.
 *
 * This function will measure and print the CPU usage of tasks over a specified
 * number of ticks (i.e. real time stats). This is implemented by simply calling
 * uxTaskGetSystemState() twice separated by a delay, then calculating the
 * differences of task run times before and after the delay.
 *
 * @note    If any tasks are added or removed during the delay, the stats of
 *          those tasks will not be printed.
 * @note    This function should be called from a high priority task to minimize
 *          inaccuracies with delays.
 * @note    When running in dual core mode, each core will correspond to 50% of
 *          the run time.
 *
 * @param   xTicksToWait    Period of stats measurement
 *
 * @return
 *  - ESP_OK                Success
 *  - ESP_ERR_NO_MEM        Insufficient memory to allocated internal arrays
 *  - ESP_ERR_INVALID_SIZE  Insufficient array size for uxTaskGetSystemState. Trying increasing ARRAY_SIZE_OFFSET
 *  - ESP_ERR_INVALID_STATE Delay duration too short
 */

// tags for logging
static const char *TAG = "SENSOR_TASK";
static const char *TAG2 = "DEBUGS BUTTON";

// variables to maniuplate the injection, jitter and the cheat start up flag
int injection_value = INJECTION_STANDARD_VALUE;
int jitter = 0;
bool cheat_start_up = true;

static IRAM_ATTR bool timer_isr_callback(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(xSyncTimerSensorRead, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

static IRAM_ATTR bool timer_isr_callback_2(
    gptimer_handle_t timer,
    const gptimer_alarm_event_data_t *edata,
    void *user_ctx)
{
    BaseType_t high_task_awoken = pdFALSE;
    xSemaphoreGiveFromISR(xSyncTimerCheat, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

static void injection_task(void *arg)
{
    // injection task waiting to the START_CHEAT_BUTTON
    if (xSemaphoreTake(xStartButtonPressed, portMAX_DELAY) == pdPASS) 
    {
        // set the io of ALERT_CHEAT_ON_LED to high voltage
        gpio_set_level(ALERT_CHEAT_ON_LED, 1);
        while (true)
        {
            // set the injection to the standard value
            injection_value = INJECTION_STANDARD_VALUE;
            // semaphore to sync the sensor read
            xSemaphoreTake(xSemaphoreSensorRead, portMAX_DELAY);
            // if the first read after the button was clicked, don't cheat yet.  
            if (cheat_start_up){
                cheat_start_up = false;
            }
            // after the second read, we sync with the sensor.
            else{
                injection_value = INJECTION_HACK_VALUE;

                //xSemaphoreTake(xSyncTimerCheat, 0);
                gptimer_set_raw_count(gptimer_2, 0);
                gptimer_start(gptimer_2);

                xSemaphoreTake(xSyncTimerCheat, portMAX_DELAY);
                gptimer_stop(gptimer_2);
                
                vTaskDelay(pdMS_TO_TICKS(jitter));
            }
            //ESP_LOGW(TAG2, "injection hack:  %d", injection_value);
        }
    }
}

static void sensor_task(void *arg)
{
    // running indefinitely 
    while(true)
    {
        if (xSemaphoreTake(xSyncTimerSensorRead, portMAX_DELAY) == pdTRUE)
        {
            // set the LED io to high voltage if cheat detected and good to low
            //ESP_LOGW(TAG, "sensor value:  %d", injection_value);
            if (injection_value > INJECTION_STANDARD_VALUE)
            {
                //ESP_LOGW(TAG, "start:  %d", cheat_start_up);
                gpio_set_level(ALERT_CHEAT_DETECTED_LED, 1);
                gpio_set_level(ALERT_SYSTEM_GOOD, 0);
                //return;
            }
            // give to cheat task and set the delay sample time
            xSemaphoreGive(xSemaphoreSensorRead);
            //vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_TIME));
        }
    }
}

// function to introduce the cheat task
static bool IRAM_ATTR isr_callback_start_cheat_pressed_button(void *arg)
{
    BaseType_t high_task_awoken = pdFALSE;
    // give to sempahore of cheat task
    xSemaphoreGiveFromISR(xStartButtonPressed, &high_task_awoken);
    return (high_task_awoken == pdTRUE);
}

// function to introduce jitter to the cheat task
void isr_callback_add_jitter_pressed_button(void *arg)
{
    jitter=5000;
}


void app_main(void)
{
    // reset the LEDs pins
    gpio_reset_pin(ALERT_CHEAT_DETECTED_LED);
    gpio_reset_pin(ALERT_CHEAT_ON_LED);
    gpio_reset_pin(ALERT_SYSTEM_GOOD);

    // set the direction of LEDs pins to OUTPUT
    gpio_set_direction(ALERT_CHEAT_DETECTED_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(ALERT_CHEAT_ON_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(ALERT_SYSTEM_GOOD, GPIO_MODE_OUTPUT);
    
    // set the direction of BUTTONs pins to INPUT
    gpio_set_direction(START_CHEAT_BUTTON, GPIO_MODE_INPUT);
    gpio_set_direction(FORCE_FAULT_BUTTON, GPIO_MODE_INPUT);
    
    // set the internal PULLUP resistor in the buttons pins
    gpio_set_pull_mode(START_CHEAT_BUTTON, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(FORCE_FAULT_BUTTON, GPIO_PULLUP_ONLY);
    
    // set the functions call to the NEGEDGE dectetion
    gpio_set_intr_type(START_CHEAT_BUTTON, GPIO_INTR_NEGEDGE);
    gpio_set_intr_type(FORCE_FAULT_BUTTON, GPIO_INTR_NEGEDGE);
    
    // initialize the GPIO interrupt service routine (ISR) 
    gpio_install_isr_service(0);

    // add handler to START_CHEAT_BUTTON
    gpio_isr_handler_add(
        START_CHEAT_BUTTON,
        isr_callback_start_cheat_pressed_button, 
        NULL);
        
    // add handler to FORCE_FAULT_BUTTON
    gpio_isr_handler_add(
        FORCE_FAULT_BUTTON,
        isr_callback_add_jitter_pressed_button, 
        NULL);
    
    // setup the ALERT_SYSTEM_GOOD LED to high voltage
    gpio_set_level(ALERT_SYSTEM_GOOD, 1);
    
    // semaphore to sync the read action of the sensor with the cheat task
    xSemaphoreSensorRead = xSemaphoreCreateBinary();
    // semaphore to handle the start cheating interrupting
    xStartButtonPressed = xSemaphoreCreateBinary();
    // semaphore to handle the timer sync
    xSyncTimerSensorRead = xSemaphoreCreateBinary();
    xSyncTimerCheat = xSemaphoreCreateBinary();

    // timer configuration and the handler
    gptimer_handle_t gptimer = NULL;
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000
    };

    gptimer_new_timer(&timer_config, &gptimer);

    // registering the ISR callback  
    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_isr_callback
    };
    gptimer_register_event_callbacks(gptimer, &cbs, NULL);

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = SENSOR_SAMPLE_TIME,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true
    };

    gptimer_set_alarm_action(gptimer, &alarm_config);
    gptimer_enable(gptimer);
    gptimer_start(gptimer);


    // configure the second timer

    gptimer_new_timer(&timer_config, &gptimer_2);

    // registering the ISR callback  
    gptimer_event_callbacks_t cbs_2 = {
        .on_alarm = timer_isr_callback_2
    };
    gptimer_register_event_callbacks(gptimer_2, &cbs_2, NULL);

    gptimer_alarm_config_t alarm_config_2 = {
        .alarm_count = SENSOR_SAMPLE_TIME-1000,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = false
    };

    gptimer_set_alarm_action(gptimer_2, &alarm_config_2);
    gptimer_enable(gptimer_2);

    //Allow other core to finish initialization
    vTaskDelay(pdMS_TO_TICKS(100));

    //Create semaphores to synchronize
    sync_spin_task = xSemaphoreCreateCounting(NUM_OF_SPIN_TASKS, 0);
    sync_stats_task = xSemaphoreCreateBinary();
    
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 5, NULL, 0);
    xTaskCreatePinnedToCore(injection_task, "injection_task", 4096, NULL, 5, NULL, 1);

    //xTaskCreatePinnedToCore(stats_task, "stats", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
    //xSemaphoreGive(sync_stats_task);
}
