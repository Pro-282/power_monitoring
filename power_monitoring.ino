#include "call_and_sms.hpp"
#include "RTC_clock.hpp"
#include "data_logger.hpp"
#include "read_ADC_and_VI.hpp"
#include <stdio.h>

#define proximity_pin 14

int16_t read_watts[60];
int counter_array[10];

TaskHandle_t read_vi;
TaskHandle_t flush_to_db;
TaskHandle_t send_sms;
TaskHandle_t check_for_calls, check_proximity;

// TaskHandle_t task2;
// TaskHandle_t task1;

SemaphoreHandle_t batton = NULL;
SemaphoreHandle_t sms_wait = NULL;

uint8_t hour, mins, sec, mday, mon, wday;
uint16_t year;
std::string current_month;

void read_vi_task(void *pvParameters)
{
    int16_t counter = 0;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    //used pdMS_TO_TICKS(x)->a macro that converts ticks to milliseconds irrespective of the CPU frequency
    const TickType_t read_frequency = pdMS_TO_TICKS(1000);
    vSemaphoreCreateBinary( batton );
    for(;;)
    {
        if(counter >= 600)
        {
            xSemaphoreTake( batton, portMAX_DELAY);
            xTaskCreatePinnedToCore(flush_to_db_task,
                                    "flush read watts to db after 10 minutes",
                                    20000, NULL, 2, &flush_to_db, 0);
            xSemaphoreGive(batton);
            counter = 0;
        }
        // if(read_voltage_raw() > 100) //I should change this value after calibration, and the one on line 33
        // {
            read_watts[counter]= calc_VI(10, 200);
            Serial.printf("%i\n", read_watts[counter]);
            ++counter;
        // }
        xTaskDelayUntil(&xLastWakeTime, read_frequency);
    }
}

void flush_to_db_task(void *pvParameters)
{
    int16_t copied_read_watts[600];
    xSemaphoreTake( batton, portMAX_DELAY);
    memcpy(copied_read_watts, read_watts, (60*sizeof(int16_t)));
    xSemaphoreGive(batton);
    // wait for 8 minutes if the sms_task is running, for the sms to be sent, this is because only one file can be opened at a time for operation.
    xSemaphoreTake( sms_wait, pdMS_TO_TICKS(480000));
    
    check_and_update_month_file();

    int16_t peak_watt = copied_read_watts[0];
    int16_t least_watt = copied_read_watts[0];
    int16_t sum = 0;
    for(;;)
    {
        for( int i=0; i<(sizeof(copied_read_watts)/sizeof(int16_t)); i++)
        {
            if( copied_read_watts[i] > peak_watt )
                peak_watt = copied_read_watts[i];
            else if( copied_read_watts[i] < least_watt )
                least_watt = copied_read_watts[i];
            sum += copied_read_watts[i]; //* from 0-599
        }
        sum = ( sum / 3600) * ( 1 / 1000 ); //* converting sum of W reading to kWh
        if( log_data(sum, peak_watt, least_watt) )
        {
            xSemaphoreGive(sms_wait);
            vTaskDelete(NULL);
        }
    }
}

void send_data_task( void *pvParameters )
{
    xSemaphoreTake( sms_wait, portMAX_DELAY);
    std::string message;
    message.clear();
    for(;;)
    {
        retrieve_monthly_data(&message);
        retrieve_monthly_data(&message, 1);
        retrieve_monthly_data(&message, 2);
        send_SMS(recipient_no, message);
        xSemaphoreGive(sms_wait);
        vTaskDelete(NULL);
    }
}

void check_for_calls_task( void *pvParameters )//this will always be running to listen, I don't know if interrupts can work though, I don't even know how it works.
{
    int counter = 0;
    for(;;)
    {
        if( recieved_call(&recipient_no) )
            Serial.printf("got a call\n");
            // xTaskCreatePinnedToCore(send_data_task, "send sms message", 10000, NULL, 2, &send_sms, 1);
        delay(500);
    }
}

void initiate_db(std::string filename)
{
    myFile = fopen(filename.c_str(), "w+b");
	if(myFile)
	{
		struct dblog_write_context ctx;
		ctx.buf = buf;
		ctx.col_count = 4;
		ctx.page_resv_bytes = 0;
		ctx.page_size_exp = 12;
		ctx.max_pages_exp = 0;
		ctx.read_fn = read_fn_wctx;
		ctx.flush_fn = flush_fn;
		ctx.write_fn = write_fn;
		int res = dblog_write_init(&ctx);

        res = dblog_append_empty_row(&ctx);
        if( res ){ print_error(res); fclose(myFile); return; }

        res = dblog_set_col_val(&ctx, 0, DBLOG_TYPE_TEXT, " ", 1);
        if( res ){ print_error(res); fclose(myFile); return; }

        res = dblog_set_col_val(&ctx, 1, DBLOG_TYPE_INT, 0, sizeof(int16_t));
        if( res ){ print_error(res); fclose(myFile); return; }

        res = dblog_set_col_val(&ctx, 2, DBLOG_TYPE_INT, 0, sizeof(int16_t));
        if( res ){ print_error(res); fclose(myFile); return; }

        res = dblog_set_col_val(&ctx, 3, DBLOG_TYPE_INT, 0, sizeof(int16_t));
        if( res ){ print_error(res); fclose(myFile); return; }
                
        res = dblog_append_empty_row(&ctx);
        Serial.print(F("Logging completed. Finalizing...\n"));
        res = dblog_finalize(&ctx);
        fclose(myFile);
    }
    else Serial.print(F("Open Error\n"));
}

void check_and_update_month_file()
{
    getDateTime(&hour, &mins, &sec, &mday, &mon, &year, &wday);
    filename = "/sd/";
    filename += mon < 10 ? ("0" + std::to_string(mon)) : std::to_string(mon), 0, 2;
    filename += "-";
    filename += std::to_string(year + 1900) + "_logs.DB";

    proxi_db_name = "/sd/";
    proxi_db_name += mon < 10 ? ("0" + std::to_string(mon)) : std::to_string(mon), 0, 2;
    proxi_db_name += "-";
    proxi_db_name += std::to_string(year + 1900) + "_proxi_logs.DB";

    if( current_month != std::to_string(mon) )
    {
        initiate_db(filename);
        initiate_db(proxi_db_name);        
        writeFile(SD, "/current_date.txt", std::to_string(mon).c_str());
        current_month = std::to_string(mon);
    } 
}

void check_proximity_task( void *pvParameters )
{
    unsigned long start, duration;
    for(;;)
    {
        start = millis();
        while(digitalRead(proximity_pin) == LOW){}
        duration = millis() - start;
        log_proximity_data(((duration/1000)/60));   //the duration in minutes
        vTaskDelete(NULL);
    }
}

void IRAM_ATTR isr_function()
{
    xTaskCreatePinnedToCore(check_proximity_task, "proximity", 4096, NULL, 1, &check_proximity, 0);
    Serial.println("object detected");
}

void setup()
{
    Serial.begin(115200);

    init_SIM800_serial();
    init_SIM800();
    setup_SIM800();
    init_ADC();

    SPI.begin();
    if(!SD.begin())
    {
        Serial.println("Card Mount Failed");
        return;
    }

    current_month.clear();
    check_for_month(SD, "/current_date.txt", &current_month);
    check_and_update_month_file();

    pinMode(proximity_pin, INPUT);
    // attachInterrupt(proximity_pin, isr_function, FALLING);

    vSemaphoreCreateBinary( sms_wait );
    xTaskCreatePinnedToCore(check_for_calls_task, "check for incoming calls", 1000, NULL, 1, &check_for_calls, 0);
    xTaskCreatePinnedToCore(read_vi_task, "read real power from ADC", 4096, NULL, 3, &read_vi, 1);
}

void loop()
{
    delay(1000);
}