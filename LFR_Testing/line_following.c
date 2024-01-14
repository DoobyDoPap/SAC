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

/*
 * weights given to respective line sensor
 */
const int weights[5] = {-5, -3, 1, 3, 5};

/*
 * Motor value boundts
 */
int optimum_duty_cycle = 56;
int lower_duty_cycle = 43;
int higher_duty_cycle = 69;
float left_duty_cycle = 0, right_duty_cycle = 0;
bool is_left = 0, is_right = 0, is_end = 0, is_inverted = 0;

/*
 * Line Following PID Variables
 */
float error=0, prev_error=0, difference, cumulative_error, correction;

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
    float weighted_sum = 0, sum = 0; 
    float pos = 0; int k = 0;

    for(int i = 0; i < 5; i++)
    {
        if(line_sensor_readings.adc_reading[i] > BLACK_BOUNDARY)
        {
            all_black_flag = 0;
        }
        if(line_sensor_readings.adc_reading[i] > 700)
        {
            k = 1;
        }
        if(line_sensor_readings.adc_reading[i] < 700)
        {
            k = 0;
        }
        weighted_sum += (float)(weights[i]) * k;
        sum = sum + k;
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
int i=0;
int node=0;
int test_run[]={};
test_run[i]={1};

void calculate_nodes(){
    bool node_flag;
    if (node_flag) {
        if ((sensor_now[0] && !sensor_now[1] && !sensor_now[2] && !sensor_now[3] && sensor_now[4])&&(!sensor_prev[1] && !sensor_prev[2] && !sensor_prev[3] && (!sensor_prev[4] || !sensor_prev[0] ) )) {
            // test_run[i+1]={i};
            node_flag=false;
            node+=1;
            i+=1;
        }
    }
}
int final_run[]={};
void simplify_path(test_run){
    for (a=0;a<=len;a++){
        temp=test_run[a];
        for (a<=len;a++){
            if (temp % test_run[a] >= 2){
                break;
            }
            final_run[a]=test_run[a];
        }
    }
}

void line_follow_task(void* arg)
{
    ESP_ERROR_CHECK(enable_motor_driver(a, NORMAL_MODE));
    ESP_ERROR_CHECK(enable_line_sensor());
    //ESP_ERROR_CHECK(enable_bar_graph());
#ifdef CONFIG_ENABLE_OLED
    // Initialising the OLED
    // ESP_ERROR_CHECK(init_oled());
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
            test_run[i+1]=(test_run[i] - 1) % 4;
            if ( test_run[i+1]==0) { 
                test_run[i+!]={4};
            }
            i+=1;
        }
        else if (sensor_now[0] && !sensor_now[1] && !sensor_now[2] && !sensor_now[3] && sensor_now[4]){
            is_straight = true;
            test_run[i+1]=test_run[i];
            i+=1;
        }
        else if (sensor_now[0] && sensor_now[1]&& sensor_now[2] && sensor_now[3] && sensor_now[4] && sensor_prev[0] && !sensor_prev[1]&& !sensor_prev[2] && !sensor_prev[3] && !sensor_prev[4]){
            is_right = true;
            test_run[i+1]=(test_run[i] + 1) % 4;
            if (test_run[i+1]==0) {
                test_run[i+1]={4};
            }
            i+=1;
        }
        else if (sensor_now[0] && sensor_now[1]&& sensor_now[2] && sensor_now[3] && sensor_now[4]){
            is_end = true;
            test_run[i+1] =( test_run[i] - 2) % 4;
            if (test_run[i+1]==0) { 
                test_run[i+1]=4;
            }
            if (test_run[i+!]==-1) { 
                test_run[i+!]=3;
            }
            i+=1;
        }
        else if (!sensor_now[0] && sensor_now[2] && !sensor_now[4] && sensor[1] && sensor[3]){
            is_inverted = true;
        }
        if (is_straight){
            left_duty_cycle = optimum_duty_cycle ;
            right_duty_cycle = optimum_duty_cycle ;
            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_FORWARD, right_duty_cycle);
            // vTaskDelay(500 / portTICK_PERIOD_MS);
            is_straight = false;
            
        }
        if (is_left){
            left_duty_cycle = optimum_duty_cycle + 4 ;
            right_duty_cycle = optimum_duty_cycle + 4 ;
            set_motor_speed(MOTOR_A_0, MOTOR_BACKWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_FORWARD, right_duty_cycle);
            // vTaskDelay(500 / portTICK_PERIOD_MS);
            is_left = false;
            
        }
        else if (is_right){
            left_duty_cycle = optimum_duty_cycle + 4;
            right_duty_cycle = optimum_duty_cycle + 4;
            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_BACKWARD, right_duty_cycle);
            // vTaskDelay(500 / portTICK_PERIOD_MS);
            is_right = false;
            
        }
        else if (is_end){
            left_duty_cycle = higher_duty_cycle ;
            right_duty_cycle = higher_duty_cycle ;
            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_BACKWARD, right_duty_cycle);
            // vTaskDelay(500 / portTICK_PERIOD_MS);
            is_end = false;

        }
        else if (is_inverted){
            left_duty_cycle = optimum_duty_cycle ;
            right_duty_cycle = optimum_duty_cycle ;
            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_FORWARD, right_duty_cycle);
            vTaskDelay(500 / portTICK_PERIOD_MS);
            is_inverted = false;
        }
        else {
            calculate_error();
            calculate_correction();
            lsa_to_bar();

            left_duty_cycle = bound((optimum_duty_cycle + correction), lower_duty_cycle, higher_duty_cycle);
            right_duty_cycle = bound((optimum_duty_cycle - correction), lower_duty_cycle, higher_duty_cycle);

            set_motor_speed(MOTOR_A_0, MOTOR_FORWARD, left_duty_cycle);
            set_motor_speed(MOTOR_A_1, MOTOR_FORWARD, right_duty_cycle);
        }
        for (int i = 0; i < 5; i++ ){
            sensor_prev[i] = sensor_now[i]; 
        }


       // ESP_LOGI("debug","left_duty_cycle:  %f    ::  right_duty_cycle :  %f  :: error :  %f  correction  :  %f  \n",left_duty_cycle, right_duty_cycle, error, correction);
    //    ESP_LOGI("debug", "KP: %f ::  KI: %f  :: KD: %f", read_pid_const().kp, read_pid_const().ki, read_pid_const().kd);
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