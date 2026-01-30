#include <stdio.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp32/rom/ets_sys.h"

void ultrasonic_test(void *pvParameters)
{

gpio_set_direction(GPIO_NUM_27,GPIO_MODE_OUTPUT);
gpio_set_direction(GPIO_NUM_26, GPIO_MODE_OUTPUT);
gpio_set_direction(GPIO_NUM_25,GPIO_MODE_OUTPUT);
gpio_set_direction(GPIO_NUM_33,GPIO_MODE_OUTPUT);

// 512 in this configuration is exactly one full rotation
for (int i=0; i < 512; i++){
    gpio_set_level(GPIO_NUM_27,1);
    gpio_set_level(GPIO_NUM_26,0) ;
    gpio_set_level(GPIO_NUM_25,0);
    gpio_set_level(GPIO_NUM_33,0);
    ets_delay_us(2000);
    gpio_set_level(GPIO_NUM_27,0) ;
    gpio_set_level(GPIO_NUM_26,1);
    gpio_set_level(GPIO_NUM_25,0) ;
    gpio_set_level(GPIO_NUM_33,0);
    ets_delay_us(2000);
    gpio_set_level(GPIO_NUM_27,0) ;
    gpio_set_level(GPIO_NUM_26,0) ;
    gpio_set_level(GPIO_NUM_25,1);
    gpio_set_level(GPIO_NUM_33,0);
    ets_delay_us(2000);
    gpio_set_level(GPIO_NUM_27,0) ;
    gpio_set_level(GPIO_NUM_26,0) ;
    gpio_set_level(GPIO_NUM_25,0);
    gpio_set_level(GPIO_NUM_33,1);
    ets_delay_us(2000);
}

ets_delay_us(2000000);


}

void app_main(void) {

    xTaskCreate(ultrasonic_test, "ultrasonic_test", configMINIMAL_STACK_SIZE * 3, NULL, 5, NULL);
}