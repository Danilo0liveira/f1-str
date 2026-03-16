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


#define NUM_OF_SPIN_TASKS   6
#define SPIN_ITER           500000  //Actual CPU cycles used will depend on compiler optimization
#define SPIN_TASK_PRIO      2
#define STATS_TASK_PRIO     3
#define STATS_TICKS         pdMS_TO_TICKS(1000)
#define ARRAY_SIZE_OFFSET   5   //Increase this if print_real_time_stats returns ESP_ERR_INVALID_SIZE

#define INJECTION_STANDARD_VALUE 100
#define INJECTION_HACK_VALUE 120
#define SENSOR_SAMPLE_TIME 30

#define ALERT_CHEAT_DETECTED_LED GPIO_NUM_13
#define ALERT_SYSTEM_GOOD GPIO_NUM_12
#define ALERT_CHEAT_ON_LED GPIO_NUM_14

#define START_CHEAT_BUTTON GPIO_NUM_25
#define FORCE_FAULT_BUTTON GPIO_NUM_27

static char task_names[NUM_OF_SPIN_TASKS][configMAX_TASK_NAME_LEN];

static SemaphoreHandle_t sync_spin_task;
static SemaphoreHandle_t sync_stats_task;
static SemaphoreHandle_t xSemaphoreSensorRead;
static SemaphoreHandle_t xStartButtonPressed;

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
static esp_err_t print_real_time_stats(TickType_t xTicksToWait)
{
    TaskStatus_t *start_array = NULL, *end_array = NULL;
    UBaseType_t start_array_size, end_array_size;
    configRUN_TIME_COUNTER_TYPE start_run_time, end_run_time;
    esp_err_t ret;

    //Allocate array to store current task states
    start_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    start_array = malloc(sizeof(TaskStatus_t) * start_array_size);
    if (start_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get current task states
    start_array_size = uxTaskGetSystemState(start_array, start_array_size, &start_run_time);
    if (start_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    vTaskDelay(xTicksToWait);

    //Allocate array to store tasks states post delay
    end_array_size = uxTaskGetNumberOfTasks() + ARRAY_SIZE_OFFSET;
    end_array = malloc(sizeof(TaskStatus_t) * end_array_size);
    if (end_array == NULL) {
        ret = ESP_ERR_NO_MEM;
        goto exit;
    }
    //Get post delay task states
    end_array_size = uxTaskGetSystemState(end_array, end_array_size, &end_run_time);
    if (end_array_size == 0) {
        ret = ESP_ERR_INVALID_SIZE;
        goto exit;
    }

    //Calculate total_elapsed_time in units of run time stats clock period.
    uint32_t total_elapsed_time = (end_run_time - start_run_time);
    if (total_elapsed_time == 0) {
        ret = ESP_ERR_INVALID_STATE;
        goto exit;
    }

    printf("| Task | Run Time | Percentage\n");
    //Match each task in start_array to those in the end_array
    for (int i = 0; i < start_array_size; i++) {
        int k = -1;
        for (int j = 0; j < end_array_size; j++) {
            if (start_array[i].xHandle == end_array[j].xHandle) {
                k = j;
                //Mark that task have been matched by overwriting their handles
                start_array[i].xHandle = NULL;
                end_array[j].xHandle = NULL;
                break;
            }
        }
        //Check if matching task found
        if (k >= 0) {
            uint32_t task_elapsed_time = end_array[k].ulRunTimeCounter - start_array[i].ulRunTimeCounter;
            uint32_t percentage_time = (task_elapsed_time * 100UL) / (total_elapsed_time * CONFIG_FREERTOS_NUMBER_OF_CORES);
            printf("| %s | %"PRIu32" | %"PRIu32"%%\n", start_array[i].pcTaskName, task_elapsed_time, percentage_time);
        }
    }

    //Print unmatched tasks
    for (int i = 0; i < start_array_size; i++) {
        if (start_array[i].xHandle != NULL) {
            printf("| %s | Deleted\n", start_array[i].pcTaskName);
        }
    }
    for (int i = 0; i < end_array_size; i++) {
        if (end_array[i].xHandle != NULL) {
            printf("| %s | Created\n", end_array[i].pcTaskName);
        }
    }
    ret = ESP_OK;

exit:    //Common return path
    free(start_array);
    free(end_array);
    return ret;
}

static void spin_task(void *arg)
{
    xSemaphoreTake(sync_spin_task, portMAX_DELAY);
    while (1) {
        //Consume CPU cycles
        for (int i = 0; i < SPIN_ITER; i++) {
            __asm__ __volatile__("NOP");
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void stats_task(void *arg)
{
    xSemaphoreTake(sync_stats_task, portMAX_DELAY);

    //Start all the spin tasks
    for (int i = 0; i < NUM_OF_SPIN_TASKS; i++) {
        xSemaphoreGive(sync_spin_task);
    }

    //Print real time stats periodically
    while (1) {
        printf("\n\nGetting real time stats over %"PRIu32" ticks\n", STATS_TICKS);
        if (print_real_time_stats(STATS_TICKS) == ESP_OK) {
            printf("Real time stats obtained\n");
        } else {
            printf("Error getting real time stats\n");
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

static const char *TAG = "SENSOR_TASK";
static const char *TAG2 = "DEBUGS BUTTON";

int injection_value = INJECTION_STANDARD_VALUE;
bool hack_start_up = true;

static void injection_task(void *arg)
{
    if (xSemaphoreTake(xStartButtonPressed, portMAX_DELAY) == pdPASS) 
    {
        gpio_set_level(ALERT_CHEAT_ON_LED, 1);
        while (true)
        {
            injection_value = INJECTION_STANDARD_VALUE;
            if (xSemaphoreTake(xSemaphoreSensorRead, portMAX_DELAY) == pdPASS)
            {
                if (hack_start_up){
                    hack_start_up = false;
                }
                else{
                    injection_value = INJECTION_HACK_VALUE;
                    vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_TIME-1));
                }
                ESP_LOGW(TAG2, "injection hack:  %d", injection_value);
            }
        }
    }
}


static void sensor_task(void *arg)
{
    while(true)
    {
        ESP_LOGW(TAG, "sensor value:  %d", injection_value);
        if (injection_value > INJECTION_STANDARD_VALUE)
            gpio_set_level(ALERT_CHEAT_DETECTED_LED, 1);
        xSemaphoreGive(xSemaphoreSensorRead);
        vTaskDelay(pdMS_TO_TICKS(SENSOR_SAMPLE_TIME));
    }
}

void isr_callback_start_cheat_pressed_button(void *arg)
{
    xSemaphoreGive(xStartButtonPressed);
}

void app_main(void)
{
    gpio_reset_pin(ALERT_CHEAT_DETECTED_LED);
    gpio_reset_pin(ALERT_CHEAT_ON_LED);
    gpio_reset_pin(ALERT_SYSTEM_GOOD);

    gpio_set_direction(ALERT_CHEAT_DETECTED_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(ALERT_CHEAT_ON_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(ALERT_SYSTEM_GOOD, GPIO_MODE_OUTPUT);

    gpio_set_direction(START_CHEAT_BUTTON, GPIO_MODE_INPUT);
    gpio_set_direction(FORCE_FAULT_BUTTON, GPIO_MODE_INPUT);

    gpio_set_pull_mode(START_CHEAT_BUTTON, GPIO_PULLUP_ONLY);
    gpio_set_pull_mode(FORCE_FAULT_BUTTON, GPIO_PULLUP_ONLY);

    gpio_set_intr_type(START_CHEAT_BUTTON, GPIO_INTR_NEGEDGE);
    gpio_set_intr_type(FORCE_FAULT_BUTTON, GPIO_INTR_NEGEDGE);

    gpio_install_isr_service(0);

    gpio_isr_handler_add(
        START_CHEAT_BUTTON,
        isr_callback_start_cheat_pressed_button, 
        NULL);


    gpio_set_level(ALERT_SYSTEM_GOOD, 1);

    xSemaphoreSensorRead = xSemaphoreCreateBinary();
    xStartButtonPressed = xSemaphoreCreateBinary();

    //Allow other core to finish initialization
    vTaskDelay(pdMS_TO_TICKS(100));

    //Create semaphores to synchronize
    sync_spin_task = xSemaphoreCreateCounting(NUM_OF_SPIN_TASKS, 0);
    sync_stats_task = xSemaphoreCreateBinary();
    
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 4096, NULL, 1, NULL, 0);
    xTaskCreatePinnedToCore(injection_task, "injection_task", 4096, NULL, 1, NULL, 1);

    //xTaskCreatePinnedToCore(stats_task, "stats", 4096, NULL, STATS_TASK_PRIO, NULL, tskNO_AFFINITY);
    //xSemaphoreGive(sync_stats_task);
}
