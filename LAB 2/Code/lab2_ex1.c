#include "contiki.h"

#include <stdio.h> /* For printf() */
#include <stdlib.h>
#include "leds.h"
#include "serial-line.h"
#include "string.h"

#include "efr32mg12p432f1024gl125.h"
#include "em_cmu.h"
#include "em_emu.h"
#include "si7021.h"
#include "board_cookie.h"
#include "util.h"
#include "imu.h"
#include "icm20648.h"

process_event_t coordinator_event;


PROCESS(sensors_setup, "Sensors setup");
PROCESS(coordinator, "Coordinator process");
PROCESS(toggle_leds, "Toggle LEDs process");
PROCESS(read_sensors, "Sensors reading");

AUTOSTART_PROCESSES(&sensors_setup,&coordinator,&toggle_leds,&read_sensors);


PROCESS_THREAD(sensors_setup, ev ,data)
{

    PROCESS_BEGIN();

    GPIO_PinModeSet(gpioPortF, 9, gpioModePushPull, 1); // T/H, set as out pin (PushPull)
    GPIO_PinOutSet(gpioPortF, 9); // T/H, enabled

    I2CSPM_Init_TypeDef i2cspmInit = I2CSPM_INIT_DEFAULT; // I2C configuration
    I2CSPM_Init(&i2cspmInit); // I2C configuration
    SI7021_init(); // TEMP/HUM Sensor config

    PROCESS_END();
}



PROCESS_THREAD(coordinator,ev, data){

    PROCESS_BEGIN();

    coordinator_event = process_alloc_event();
    while(1){


        PROCESS_WAIT_EVENT_UNTIL(ev==serial_line_event_message);
        printf("Message received: %s\n",(char*)data);

        if(strcmp(data,"LR") == 0) process_post(&toggle_leds,coordinator_event,"RED");

        else if(strcmp(data,"LG") == 0) process_post(&toggle_leds,coordinator_event,"GREEN");

        else if(strcmp(data,"SH") == 0)process_post(&read_sensors,coordinator_event,"H");

        else if(strcmp(data,"ST") == 0) process_post(&read_sensors,coordinator_event,"T");

        else printf("Message received (%s) not identified.Valid messages: LR, LG, SH, ST\n",(char*)data);

    }
    PROCESS_END();
}


PROCESS_THREAD(toggle_leds,ev, data){

    PROCESS_BEGIN();

    while(1){

        PROCESS_WAIT_EVENT_UNTIL(ev == coordinator_event);
        printf("%s LED toggled\n",(char*)data);
        printf("Waiting for next serial line reception\n");
        if(strcmp(data,"RED")==0) leds_toggle(LEDS_RED);
        else if(strcmp(data,"GREEN")==0) leds_toggle(LEDS_GREEN);

    }
    PROCESS_END();
}

PROCESS_THREAD(read_sensors,ev, data){

    uint32_t hume = 0;
    int32_t temper = 0;


    PROCESS_BEGIN();

    while(1){

        PROCESS_WAIT_EVENT_UNTIL(ev == coordinator_event);

        SI7021_measure(&hume, &temper);

        if((strcmp(data,"H"))==0) printf("Humidity: %d.%d %%\n", (int)(hume/1000), (int)(hume%1000));

        else if((strcmp(data,"T"))==0) printf("Temperature: %d.%d C\n", (int)(temper/1000), (int)(temper%1000));
        printf("Waiting for next serial line reception\n");

    }
    PROCESS_END();
}
