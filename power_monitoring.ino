#include "call_and_sms.hpp"
#include "RTC_clock.hpp"
#include "data_logger.hpp"
#include "read_ADC_and_VI.hpp"

#define proximity_pin 14

int16_t read_watts[60];
int counter_array[10];

TaskHandle_t read_vi;
TaskHandle_t flush_to_db;
TaskHandle_t send_sms;
TaskHandle_t check_for_calls, check_proximity;

SemaphoreHandle_t batton = NULL;
SemaphoreHandle_t sms_wait = NULL;

uint8_t hour, mins, sec, mday, mon, wday;
uint16_t year;
std::string current_month;

unsigned long last_detected;
int counter = 0;
bool flag = true;

void read_vi_task(void *pvParameters)
{
    int16_t counter = 0;
    TickType_t xLastWakeTime = xTaskGetTickCount();
    //used pdMS_TO_TICKS(x)->a macro that converts ticks to milliseconds irrespective of the CPU frequency
    const TickType_t read_frequency = pdMS_TO_TICKS(1000);
    vSemaphoreCreateBinary( batton );
    for(;;)
    {
        if(counter >= 60)
        {
            xSemaphoreTake( batton, portMAX_DELAY);
            xTaskCreatePinnedToCore(flush_to_db_task,
                                    "flush read watts to db after 10 minutes",
                                    10000, NULL, 2, &flush_to_db, 0);
            xSemaphoreGive(batton);
            counter = 0;
        }
        // int sampleV = read_voltage_raw();
        // Serial.println("got here but no power");
        // if ((sampleV < (ADC_COUNTS*0.55)) && (sampleV > (ADC_COUNTS*0.45))) {
            read_watts[counter]= calc_VI(10, 200);
            Serial.printf("%i\n", read_watts[counter]);
            ++counter;
        // }
        xTaskDelayUntil(&xLastWakeTime, read_frequency);
    }
}

void flush_to_db_task(void *pvParameters)
{
    int16_t copied_read_watts[60];
    xSemaphoreTake( batton, portMAX_DELAY);
    memcpy(copied_read_watts, read_watts, (60*sizeof(int16_t)));
    xSemaphoreGive(batton);
    // wait for 8 minutes if the sms_task is running, for the sms to be sent, this is because only one file can be opened at a time for operation.
    // xSemaphoreTake( sms_wait, pdMS_TO_TICKS(480000));
    xSemaphoreTake( sms_wait, pdMS_TO_TICKS(55000));    //todo: remove this line
    
    check_and_update_month_file();

    int16_t peak_watt = copied_read_watts[0];
    int16_t least_watt = copied_read_watts[0];
    int32_t sum = 0;
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
        Serial.printf("sum power: %i\n",sum);
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
        delay(500);
        Serial.println("about to send sms");
        Serial.println(message.c_str());
        send_SMS(recipient_no, message);

        xSemaphoreGive(sms_wait);
        vTaskDelete(NULL);
    }
}

void check_for_calls_task( void *pvParameters )//this will always be running to listen, I don't know if interrupts can work though, I don't even know how it works.
{
    SerialAT.write("AT\r\n");
    for(;;)
    {
        if( recieved_call(&recipient_no) )
            xTaskCreatePinnedToCore(send_data_task, "send sms message", 10000, NULL, 2, &send_sms, 1);
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
    filename += mon < 10 ? ("0" + std::to_string(mon)) : std::to_string(mon);
    filename += "-";
    filename += std::to_string(year + 1900) + "_logs.DB";

    proxi_db_name = "/sd/";
    proxi_db_name += mon < 10 ? ("0" + std::to_string(mon)) : std::to_string(mon);
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
    counter++;
    Serial.printf("Counter value: %i\n", counter);
    unsigned long start = millis();
    while(digitalRead(proximity_pin) ==  LOW)
    {
        Serial.println("still low");
        delay(2500);
        flag = false;
    }
    unsigned long duration = millis() - start;
    if(duration > 5000)
    {
        Serial.printf("logging proximity [duration]: %fs\n", (duration/1000.0));
        log_proximity_data(((duration/1000)));   //the duration in minutes
    }
    else
    {
        Serial.println("wasn't up to 5s");
    }
    flag = true;
    vTaskDelete(NULL);
}

void IRAM_ATTR isr_function()
{
    Serial.println("object detected");
    if((millis() - last_detected > 3000) && flag)
        xTaskCreatePinnedToCore(check_proximity_task, "proximity", 4096, NULL, 1, &check_proximity, 0);
    last_detected = millis();  
}

void setup()
{
    Serial.begin(115200);
    SPI.begin(sd_sclk, sd_miso, sd_mosi, sd_cs);
    if(!SD.begin(sd_cs))
    {
        Serial.println("Card Mount Failed");
        return;
    }
    else
    {
        uint32_t cardSize = SD.cardSize() / (1024 * 1024);
        std::string str = "SDCard Size: " + std::to_string(cardSize) + "MB";
        Serial.println(str.c_str());
    }

    current_month.clear();
    check_for_month(SD, "/current_date.txt", &current_month);
    check_and_update_month_file();
    Serial.println(filename.c_str());

    init_ADC();

    vSemaphoreCreateBinary( sms_wait );
    xTaskCreatePinnedToCore(read_vi_task, "read real power from ADC", 4096, NULL, 3, &read_vi, 1);

    init_SIM800_serial();
    init_SIM800();
    setup_SIM800();
    delay(5000);

    pinMode(proximity_pin, INPUT);
    attachInterrupt(proximity_pin, isr_function, FALLING);
    delay(2500);
    xTaskCreatePinnedToCore(check_for_calls_task, "check for incoming calls", 2048, NULL, 1, &check_for_calls, 0);
}

void loop()
{
    delay(1000);
}