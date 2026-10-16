/*
 * lab2_ex2.c
 *
 *  Created on: Oct 16, 2026
 *      Author: user
 */


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

#include "etimer.h"

process_event_t coordinator_event;
#define MAX_PERIOD 30

struct command{
    char action; // Will contain L,G,H or T. This way, I can use one single process to toggle LEDs and other to read sensors.
    int period;  // If the user selects a period to perform said action.
};



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

    static struct command data_to_next_process;
    struct process *next_proc;
    long user_period;
    char *end;
    char *incoming_data;


    PROCESS_BEGIN();

    coordinator_event = process_alloc_event();

    while(1){

        PROCESS_WAIT_EVENT_UNTIL(ev==serial_line_event_message);

        incoming_data = (char*)data;

        printf("\nMessage received: %s",(char*)data);

        //First, I check if the message is valid: It must have 2 characters and an optional number:
        if(strlen(data)<2){
            printf(" too short. Expected format: LG|LR|ST|SH [+ optional time]");
            continue;
        }

        //If the length is right, now I check if the content is valid (LR,LR,ST or SH)
        if(incoming_data[0] == 'L' && (incoming_data[1]=='R'|| incoming_data[1]=='G')) next_proc = &toggle_leds;

        else if(incoming_data[0] == 'S' &&(incoming_data[1]=='H'||incoming_data[1]=='T')) next_proc = &read_sensors;

        else{
            printf(" unknown. Valid messages are: LG|LR|ST|SH [+ optional time]");
            continue; //To stop running this process.
        }


        //The length and content are valid so far. Now I check if the received time is valid:
        user_period = strtol(&incoming_data[2],&end,10);
        if(*end != '\0'){
            printf(" invalid period. Format expected is a number from 0 to %d\n",MAX_PERIOD);
            continue;
        }

        //If the code reaches here, the message is totally valid. Now I unpack it and send it to the right next process

        data_to_next_process.action = incoming_data[1];
        data_to_next_process.period = (int)user_period;

        process_post_synch(next_proc,coordinator_event,&data_to_next_process);

    }
    PROCESS_END();
}


PROCESS_THREAD(toggle_leds,ev, data){

    struct command *incoming_data;
    static struct etimer red_timer, green_timer;

    PROCESS_BEGIN();

    while(1){


        PROCESS_WAIT_EVENT_UNTIL(ev==coordinator_event||ev==PROCESS_EVENT_TIMER);


            if(ev==coordinator_event){
                //New data received from coordinator. I check the target LED and its periodicity
                incoming_data = (struct command*)data;
                printf("\nAction received: %c. Period: %d\n",incoming_data->action,incoming_data->period);

                if(incoming_data->action == 'R'){
                    leds_toggle(LEDS_RED);
                    if(incoming_data->period >0)etimer_reset_with_new_interval(&red_timer,(clock_time_t)incoming_data->period * CLOCK_SECOND);
                    else etimer_stop(&red_timer);
                }

                else if(incoming_data->action=='G'){
                    leds_toggle(LEDS_GREEN);
                    if(incoming_data->period >0)etimer_reset_with_new_interval(&green_timer,(clock_time_t)incoming_data->period * CLOCK_SECOND);
                    else etimer_stop(&green_timer);
                }
            }

            else if(ev==PROCESS_EVENT_TIMER){
                if(data==&red_timer){
                    leds_toggle(LEDS_RED);
                    etimer_reset(&red_timer);
                }
                else if(data==&green_timer){
                    leds_toggle(LEDS_GREEN);
                    etimer_reset(&green_timer);
                }
            }


    }
    PROCESS_END();
}


PROCESS_THREAD(read_sensors,ev, data){

    static uint32_t hume = 0;
    static int32_t temper = 0;

    struct command *incoming_data;
    static struct etimer temp_timer, humidity_timer;

    PROCESS_BEGIN();

    while(1){

        PROCESS_WAIT_EVENT_UNTIL(ev == coordinator_event||ev==PROCESS_EVENT_TIMER);

        if(ev==coordinator_event){

            incoming_data = (struct command*)data;
            SI7021_measure(&hume, &temper);

            if(incoming_data->action=='H'){
                printf("\nHumidity: %d.%d %%\n", (int)(hume/1000), (int)(hume%1000));
                if(incoming_data->period > 0)etimer_reset_with_new_interval(&humidity_timer,(clock_time_t)incoming_data->period * CLOCK_SECOND);
                else etimer_stop(&humidity_timer);
            }

            else if(incoming_data->action=='T'){
                printf("\nTemperature: %d.%d C\n", (int)(temper/1000), (int)(temper%1000));

                if(incoming_data->period > 0)etimer_reset_with_new_interval(&temp_timer,(clock_time_t)incoming_data->period * CLOCK_SECOND);
                else etimer_stop(&temp_timer);
            }

        }

        else if(ev==PROCESS_EVENT_TIMER){

            if(data==&temp_timer){
                printf("\n%d Temperature: %d.%d C\n",(int)clock_time(), (int)(temper/1000), (int)(temper%1000));
                etimer_reset(&temp_timer);
            }

            else if(data==&humidity_timer){
                printf("\n%dHumidity: %d.%d %%\n", (int)clock_time(),(int)(hume/1000), (int)(hume%1000));

                etimer_reset(&humidity_timer);
            }
        }


    }
    PROCESS_END();
}