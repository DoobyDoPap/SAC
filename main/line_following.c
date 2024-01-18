#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sra_board.h"
#include "tuning_http_server.h"

#define MODE NORMAL_MODE
#define BLACK_MARGIN 4095
#define WHITE_MARGIN 0
#define bound_LSA_LOW 0
#define bound_LSA_HIGH 1000
#define BLACK_BOUNDARY  850    // Boundary value to distinguish between black and white readings

#define left 0
#define right 1
#define straight 2
#define deadend 3
/*
 * weights given to respective line sensor
 */
const int weights[5] = {-5, -3, 1, 3, 5};
const int weights_inverted[5] = {-5, -3, 1, 3, 5 }; 
/*
 * Motor value boundts
 */
int optimum_duty_cycle = 57;
int lower_duty_cycle = 44;
int higher_duty_cycle = 70;
float left_duty_cycle = 0, right_duty_cycle = 0;
bool is_left = 0, is_right = 0, is_end = 0, is_stop = 0;
int idx = 0, dry_run_flag = 0;
int counter = 0; 
bool switch_left = 0, switch_end = 0, switch_straight = 0;
/*
 * Line Following PID Variables
 */
float error=0, prev_error=0, difference, cumulative_error, correction;
int nodes_array[100];
/*
 * Union containing line sensor readings
 */
line_sensor_array line_sensor_readings;

void lsa_to_bar()
{   
    uint8_t var = 0x00;                     
    bool number[8] = {0,0,0,0,0,0,0,0};
    for(int i = 0; i < 5; i++)
    {
        number[7-i] = (line_sensor_readings.adc_reading[i] < BLACK_BOUNDARY) ? 0 : 1; //If adc value is less than black margin, then set that bit to 0 otherwise 1.
        var = bool_to_uint8(number);  //A helper function to convert bool array to unsigned int.
        ESP_ERROR_CHECK(set_bar_graph(var)); //Setting bar graph led with unsigned int value.
    }
}

void calculate_correction()
{
    error = error*10;  // we need the error correction in range 0-100 so that we can send it directly as duty cycle paramete
    difference = error - prev_error;
    cumulative_error += error;

    cumulative_error = bound(cumulative_error, -30, 30);

    correction = read_pid_const().kp*error + read_pid_const().ki*cumulative_error + read_pid_const().kd*difference;
    prev_error = error;
}

void calculate_error()
{
    int all_black_flag = 1; // assuming initially all black condition
    // int all_white_flag = 0;
    float weighted_sum = 0, sum = 0; 
    float pos = 0; int k = 0;

    if (line_sensor_readings.adc_reading[0]  > BLACK_BOUNDARY && line_sensor_readings.adc_reading[4]  > BLACK_BOUNDARY && (line_sensor_readings.adc_reading[2]  < BLACK_BOUNDARY || line_sensor_readings.adc_reading[1]  < BLACK_BOUNDARY || line_sensor_readings.adc_reading[3]  < BLACK_BOUNDARY) ) {
        for (int i = 0; i < 5; i++)
        {
            // if (line_sensor_readings.adc_reading[i] > BLACK_BOUNDARY)
            // {
            //     all_white_flag = 1;
            // }
            if (line_sensor_readings.adc_reading[i] < 850)
            {
                k = 1;
            }
            if (line_sensor_readings.adc_reading[i] > 850)
            {
                k = 0;
            }
            weighted_sum += (float)(weights_inverted[i]) * k;
            sum = sum + k;
        }
    }
    else {
        for (int i = 0; i < 5; i++)
        {
            if (line_sensor_readings.adc_reading[i] > BLACK_BOUNDARY)
            {
                all_black_flag = 0;
            }
            if (line_sensor_readings.adc_reading[i] > 850)
            {
                k = 1;
            }
            if (line_sensor_readings.adc_reading[i] < 850)
            {
                k = 0;
            }
            weighted_sum += (float)(weights[i]) * k;
            sum = sum + k;
        }
    }
        
        

    if(sum != 0) // sum can never be 0 but just for safety purposes
    {
        pos = (weighted_sum - 1) / sum; // This will give us the position wrt line. if +ve then bot is facing left and if -ve the bot is facing to right.
    }

    if(all_black_flag == 1)  // If all black then we check for previous error to assign current error.
    {
        if(prev_error > 0)
        {
            error = 2.5;
        }
        else
        {
            error = -2.5;
        }
    }
    else
    {
        error = pos;
    }
}

void line_follow_task(void* arg)
{
    ESP_ERROR_CHECK(enable_motor_driver(a, NORMAL_MODE));
    ESP_ERROR_CHECK(enable_line_sensor());
    //ESP_ERROR_CHECK(enable_bar_graph());
#ifdef CONFIG_ENABLE_OLED
    // Initialising the OLED
    ESP_ERROR_CHECK(init_oled());
    vTaskDelay(100);

    // Clearing the screen
    lv_obj_clean(lv_scr_act());

#endif

    while(true)
    {
        line_sensor_readings = read_line_sensor();
        bool sensor_now[5] = {0,0,0,0,0};
        bool sensor_prev[5] = {0,0,0,0,0};
        for(int i = 0; i < 5; i++)
        {
            line_sensor_readings.adc_reading[i] = bound(line_sensor_readings.adc_reading[i], WHITE_MARGIN, BLACK_MARGIN);
            line_sensor_readings.adc_reading[i] = map(line_sensor_readings.adc_reading[i], WHITE_MARGIN, BLACK_MARGIN, bound_LSA_LOW, bound_LSA_HIGH);
            line_sensor_readings.adc_reading[i] = 1000 - (line_sensor_readings.adc_reading[i]);
        }
        for (int i = 0; i < 5; i++ ){
            sensor_now[i] = (line_sensor_readings.adc_reading[i] > BLACK_BOUNDARY ) ? 0 : 1; 
        }

        if (!sensor_now[0] && !sensor_now[1]&& !sensor_now[2] && !sensor_now[3] ){
                is_left = true;
                if (counter  >= 50){ 
                    is_stop = true;
                    is_left = false;
                }
            // else{
            //     if ((nodes_array[idx + 1] == deadend) || (nodes_array[idx + 2] == right && nodes_array[idx + 1] == right) ){ 
            //         is_left = 0;
            //     }else {
            //         is_left = 1;
            //     }
            // }
            // if(!switch_left){
            //     if(!dry_run_flag) nodes_array[idx] = left;
            //     idx += 1;
            //     switch_left = 1;
            
        }
        else if (sensor_now[0] && sensor_now[1]&& sensor_now[2] && sensor_now[3] && sensor_now[4] && sensor_prev[0] && !sensor_prev[1]&& !sensor_prev[2] && !sensor_prev[3] && !sensor_prev[4]){
            is_right = true;
        }
        // else if (sensor_now[0] && !sensor_now[1]&& !sensor_now[2] && !sensor_now[3] && sensor_now[4] && sensor_prev[0] && !sensor_prev[1]&& !sensor_prev[2] && !sensor_prev[3] && !sensor_prev[4] ){
        //     if (!switch_straight) {
        //         if(dry_run_flag){
        //             if ((nodes_array[idx + 1] == deadend) ){ 
        //                 is_right = 1;
        //             }else {
        //                 is_right = 0;
        //             }
        //             switch_straight = 1;
        //             idx += 1;
        //         }
        //         else {
        //             nodes_array[idx] = straight;
        //             idx += 1;
        //             switch_straight = 1;
        //             is_right = 0;
        //         }
        //     }
        // }
        else if (sensor_now[0] && sensor_now[1]&& sensor_now[2] && sensor_now[3] && sensor_now[4]){
            is_end = true;
            // nodes_array[idx] = deadend;
            // idx += 1;
        }
        // else if (!sensor_now[0] && sensor_now[2] && !sensor_now[4]){
        //     is_inverted = true;
        // }
        if(!sensor_now[0] && !sensor_now[1] && !sensor_now[2] && !sensor_now[3] && !sensor_now[4]){
           counter += 1;
        }
        // if (sensor_now[0] && !sensor_now[1] && !sensor_now[2] && !sensor_now[3] && sensor_now[4]){
        //     if(counter != 0) counter = 0;
        //     // is_left = false;
        //     // is_right = false;
        //     // is_end = false;
        //     switch_left = 0;
        //     switch_end = 0;
        //     switch_straight = 0;r
        // }

        if (is_left){
            left_duty_cycle = optimum_duty_cycle +  5;
            right_duty_cycle = optimum_duty_cycle + 5;
            set_motor_speed(MOTOR_A_0, MOTOR_BACKWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_FORWARD, right_duty_cycle);
            // vTaskDelay(500 / portTICK_PERIOD_MS);
            is_left = false;
        }
        else if (is_right){
            left_duty_cycle = optimum_duty_cycle + 4;
            right_duty_cycle = optimum_duty_cycle + 4   ;
            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_BACKWARD, right_duty_cycle);
            // nodes_array[idx] = right;
            // idx += 1;
            is_right = false;
            // vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        else if (is_end){
            left_duty_cycle = higher_duty_cycle ;
            right_duty_cycle = higher_duty_cycle ;
            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_BACKWARD, right_duty_cycle);
            nodes_array[idx] = deadend;
            idx += 1;
            is_end = false;
            // vTaskDelay(500 / portTICK_PERIOD_MS);
        }
        else if (is_stop){
            left_duty_cycle = 0;
            right_duty_cycle  = 0;
            dry_run_flag = 1;
            idx = 0;
            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_FORWARD, right_duty_cycle);
            for (int i = 0; i < 5; i++)
            {
                ESP_ERROR_CHECK(set_bar_graph(0xFF));
                //0xFF = 1111 1111(all leds are on)
                // setting values of all 8 leds to 1
                vTaskDelay(1000 / portTICK_RATE_MS);
                //delay of 1s
                ESP_ERROR_CHECK(set_bar_graph(0x00));
                //0x00 = 0000 0000(all leds are off)
                // setting values of all 8 leds to 0
                vTaskDelay(1000 / portTICK_RATE_MS);
                //delay of 1s
            }
        }
        else {
            calculate_error();
            calculate_correction();
            counter = 0;
            left_duty_cycle = bound((optimum_duty_cycle + correction), lower_duty_cycle, higher_duty_cycle);
            right_duty_cycle = bound((optimum_duty_cycle - correction), lower_duty_cycle, higher_duty_cycle);

            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_FORWARD, right_duty_cycle);
            // is_inverted = false;
        }
        for (int i = 0; i < 5; i++ ){
            sensor_prev[i] = sensor_now[i]; 
        }

    //ESP_LOGI("debug", "node:  %f    ::  right_duty_cycle :  %f  :: error :  %f  correction  :  %f  \n",(float)set, right_duty_cycle, error, correction);
    //  ESP_LOGI("debug", "KP: %f ::  KI: %f  :: KD: %f", read_pid_const().kp, read_pid_const().ki, read_pid_const().kd);
#ifdef CONFIG_ENABLE_OLED
        // Diplaying kp, ki, kd values on OLED 
        if (read_pid_const().val_changed)
        {
            display_pid_values(read_pid_const().kp, read_pid_const().ki, read_pid_const().kd);
            reset_val_changed_pid_const();
        }
#endif

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }

    vTaskDelete(NULL);
}

void app_main()
{
    xTaskCreate(&line_follow_task, "line_follow_task", 4096, NULL, 1, NULL);
    start_tuning_http_server();
}