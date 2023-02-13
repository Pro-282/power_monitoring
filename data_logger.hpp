#pragma once

#include <ulog_sqlite.h>
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include "RTC_clock.hpp"

#define sd_miso 35
#define sd_mosi 33
#define sd_sclk 32
#define sd_cs 25

#define MAX_FILE_NAME_LEN 100
#define MAX_STR_LEN 500

#define BUF_SIZE 4096
byte buf[BUF_SIZE];
std::string filename;
std::string proxi_db_name = "not_set_yet.DB";
extern const char sqlite_sig[];

FILE *myFile;

int32_t read_fn_wctx(struct dblog_write_context *ctx, void *buf, uint32_t pos, size_t len)
{
	if (fseek(myFile, pos, SEEK_SET))
		return DBLOG_RES_SEEK_ERR;
	size_t ret = fread(buf, 1, len, myFile);
	if (ret != len)
		return DBLOG_RES_READ_ERR;
	return ret;
}

int32_t read_fn_rctx(struct dblog_read_context *ctx, void *buf, uint32_t pos, size_t len)
{
	if (fseek(myFile, pos, SEEK_SET))
		return DBLOG_RES_SEEK_ERR;
	size_t ret = fread(buf, 1, len, myFile);
	if (ret != len)
		return DBLOG_RES_READ_ERR;
	return ret;
}

int32_t write_fn(struct dblog_write_context *ctx, void *buf, uint32_t pos, size_t len)
{
	if (fseek(myFile, pos, SEEK_SET))
		return DBLOG_RES_SEEK_ERR;
	size_t ret = fwrite(buf, 1, len, myFile);
	if (ret != len)
		return DBLOG_RES_ERR;
	if (fflush(myFile))
		return DBLOG_RES_FLUSH_ERR;
	fsync(fileno(myFile));
	return ret;
}

int flush_fn(struct dblog_write_context *ctx)
{
	return DBLOG_RES_OK;
}

void listDir(fs::FS &fs, const char *dirname)
{
	Serial.print(F("Listing directory: "));
	Serial.println(dirname);
	File root = fs.open(dirname);
	if (!root)
	{
		Serial.println(F("Failed to open directory"));
		return;
	}
	if (!root.isDirectory())
	{
		Serial.println("Not a directory");
		return;
	}
	File file = root.openNextFile();
	while (file)
	{
		if (file.isDirectory())
		{
			Serial.print(" Dir : ");
			Serial.println(file.name());
		}
		else
		{
			Serial.print(" File: ");
			Serial.print(file.name());
			Serial.print(" Size: ");
			Serial.println(file.size());
		}
		file = root.openNextFile();
	}
}

void appendFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Appending to file: %s\n", path);

    File file = fs.open(path, FILE_APPEND);
    if(!file){
        Serial.println("Failed to open file for appending");
        return;
    }
    if(file.print(message)){
        Serial.println("Message appended");
    } else {
        Serial.println("Append failed");
    }
    file.close();
}

void writeFile(fs::FS &fs, const char * path, const char * message){
    Serial.printf("Writing file: %s\n", path);

    File file = fs.open(path, FILE_WRITE);
    if(!file){
        Serial.println("Failed to open file for writing");
        return;
    }
    if(file.print(message)){
        Serial.println("File written");
    } else {
        Serial.println("Write failed");
    }
    file.close();
}

void readFile(fs::FS &fs, const char * path){
    Serial.printf("Reading file: %s\n", path);

    File file = fs.open(path);
    if(!file){
        Serial.println("Failed to open file for reading");
        return;
    }
    Serial.print("Read from file: ");
    while(file.available()){
        Serial.write(file.read());
    }
    file.close();
}

void check_for_month(fs::FS &fs, const char * path, std::string *read_month)
{
	File file = fs.open(path, FILE_READ, true);
	if(!file)
	{
		Serial.println("failed to open file for reading");
		return;
	}
	while(file.available())
	{
		*read_month += file.read();
	}
	file.close();
}

void renameFile(fs::FS &fs, const char *path1, const char *path2)
{
	Serial.printf("Renaming file %s to %s\n", path1, path2);
	if (fs.rename(path1, path2))
		Serial.println(F("File renamed"));
	else
		Serial.println(F("Rename failed"));
}

void deleteFile(fs::FS &fs, const char *path)
{
	Serial.printf("Deleting file: %s\n", path);
	if (fs.remove(path))
		Serial.println(F("File deleted"));
	else
		Serial.println(F("Delete failed"));
}

void print_error(int res)
{
	Serial.print(F("Err:"));
	Serial.print(res);
	Serial.print(F("\n"));
}

int pow10(int8_t len)
{
	return (len == 3 ? 1000 : (len == 2 ? 100 : (len == 1 ? 10 : 1)));
}

void set_ts_part(char *s, int val, int8_t len)
{
	while (len--)
	{
		*s++ = '0' + val / pow10(len);
		val %= pow10(len);
	}
}

int get_ts_part(char *s, int8_t len)
{
	int i = 0;
	while (len--)
		i += ((*s++ - '0') * pow10(len));
	return i;
}

int update_ts_part(char *ptr, int8_t len, int limit, int ovflw)
{
	int8_t is_one_based = (limit == 1000 || limit == 60 || limit == 24) ? 0 : 1;
	int part = get_ts_part(ptr, len) + ovflw - is_one_based;
	ovflw = part / limit;
	part %= limit;
	set_ts_part(ptr, part + is_one_based, len);
	return ovflw;
}
// "YYYY-MM-DD HH:MM"
int update_ts_min_and_check_for_new_day_and_month(char *ts, int diff)
{
	uint8_t checker;
	int ovflw = update_ts_part(ts + 14, 2, 60, diff); // minutes
	if (ovflw)
	{
		ovflw = update_ts_part(ts + 11, 2, 24, ovflw); // hours
		if (ovflw)
		{
			int8_t month = get_ts_part(ts + 5, 2);
			int year = get_ts_part(ts, 4);
			int8_t limit = (month == 2 ? (year % 4 ? 28 : 29) : 
			(month == 4 || month == 6 || month == 9 || month == 11 ? 30 : 31));
			ovflw = update_ts_part(ts + 8, 2, limit, ovflw); // day
			checker = 1; //to show that the timestamp has entered another day, switch to the next row of the array
			if (ovflw) 
			{
				ovflw = update_ts_part(ts + 5, 2, 12, ovflw); // month
				checker = 2; //to show another month has been encountered, stop the retrival
				if (ovflw)
					set_ts_part(ts, year + ovflw, 4); // year
			}
		}
	} return checker;
}

void recover_db()
{
	struct dblog_write_context ctx;
	ctx.buf = buf;
	ctx.read_fn = read_fn_wctx;
	ctx.write_fn = write_fn;
	ctx.flush_fn = flush_fn;
	myFile = fopen(filename.c_str(), "r+b");
	if (!myFile)
	{
		print_error(0);
		return;
	}
	int32_t page_size = dblog_read_page_size(&ctx);
	if (page_size < 512)
	{
		Serial.print(F("Error reading page size\n"));
		fclose(myFile);
		return;
	}
	if (dblog_recover(&ctx))
	{
		Serial.print(F("Error during recover\n"));
		fclose(myFile);
		return;
	}
	fclose(myFile);
}

bool log_data(int32_t _sum, int16_t _peak, int16_t _least)
{
	// data from the adc would be read every seconds and stored in an array
	// every second. this data would be accumulated for 10 minutes and sent to the database
	// so the database would contain energy usage for each 10 minute, the peak power in the 10 minutes, the least power in the 10 minutes
	// the current timestamp would accompany the logged data 
	// every 10 min the data is sent to the db, 
	// upon request the system sums up all logs giving the kWh in a month, per day could be calculated first
	// so a daily average, peak and least could be retrieved

	std::string date_time_string;
	date_time_to_string( &date_time_string );
	Serial.println(filename.c_str());
	myFile = fopen(filename.c_str(), "r+b");
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
		int res = dblog_init_for_append(&ctx);
		if( res == DBLOG_RES_NOT_FINALIZED )
			dblog_finalize(&ctx);
		if(!res)
		{
			res = dblog_append_empty_row(&ctx);
			if( res ){ print_error(res); fclose(myFile); return 0; }
			
			res = dblog_set_col_val(&ctx, 0, DBLOG_TYPE_TEXT, date_time_string.c_str(), date_time_string.length());
			if( res ){ print_error(res); fclose(myFile); return 0; }
			
			res = dblog_set_col_val(&ctx, 1, DBLOG_TYPE_INT, &_sum, sizeof(int32_t));
			if( res ){ print_error(res); fclose(myFile); return 0; }
			
			res = dblog_set_col_val(&ctx, 2, DBLOG_TYPE_INT, &_peak, sizeof(int16_t));
			if( res ){ print_error(res); fclose(myFile); return 0; }
			
			res = dblog_set_col_val(&ctx, 3, DBLOG_TYPE_INT, &_least, sizeof(int16_t));
			if( res ){ print_error(res); fclose(myFile); return 0; }
			
			Serial.print(F("Logging completed. Finalizing...\n"));
			if (!res)
				res = dblog_finalize(&ctx);
			fclose(myFile);
			return 1;
		}
		else
		{
			fclose(myFile);
			print_error(res);
			return 0;
		}
	}
	else
	{
		Serial.print(F("Open Error\n"));
		return 0;
	} 
}

int16_t get_int16(const byte *ptr)
{
	return (*ptr << 8) | ptr[1];
}

int32_t read_int32(const byte *ptr) 
{
  int32_t ret;
  ret  = ((int32_t)*ptr++) << 24;
  ret |= ((int32_t)*ptr++) << 16;
  ret |= ((int32_t)*ptr++) << 8;
  ret |= *ptr;
  return ret;
}

void extract_row_values(struct dblog_read_context *ctx, char *first, int32_t *second, int16_t *third, int16_t *fouth)
{
	int16_t i = 0;
	while(i < 4)
	{
		uint32_t col_type;
		const byte *col_val = (const byte *) dblog_read_col_val(ctx, i, &col_type);
		if (!col_val) {
			if (i == 0){ Serial.print(F("Error reading value\n")); }
			return;
		}
		switch (i)
		{
			case 0: {
				uint32_t col_len = dblog_derive_data_len(col_type);
				for (int j = 0; j < col_len; j++){
					*first = (char)col_val[j];
					first++;
				}
				*first = '\0';
			}break;
			case 1:
				*second = read_int32(col_val);
				break;
			case 2:
				*third = get_int16(col_val);
				break;
			case 3:
				*fouth = get_int16(col_val);
				break;
		}
		i++;
	}
}

void extract_proxi_row_values(struct dblog_read_context *ctx, char *first, int16_t *second)
{
	int16_t i = 0;
	while(i < 2)
	{
		uint32_t col_type;
		const byte *col_val = (const byte *) dblog_read_col_val(ctx, i, &col_type);
		if(!col_val) {
			if(i == 0){ Serial.printf("Error reading value\n"); }
			return;
		}
		switch (i)
		{
			case 0: {
				uint32_t col_len = dblog_derive_data_len(col_type);
				for(int j = 0; j < col_len; j++){
					*first = (char)col_val[j];
					first++;
				}
				*first = '\0';
			}break;
			case 1:
				*second = read_int32(col_val);
				break;
		}
		i++;
	}
}

void retrieve_monthly_data(std::string *message, int8_t month_difference = 0)
{
	int daily_summary [31][4];
	int daily_power_sum;
	int daily_peak, daily_least;
	int16_t max_duration;
	uint8_t current_month;
	uint16_t current_year;
	get_current_month_year(&current_month, &current_year);

	struct dblog_read_context rctx;
	rctx.page_size_exp = 12;
	rctx.read_fn = read_fn_rctx;

	std::string temp_filename = filename;
	std::string proxi_temp_filename = proxi_db_name;
	if(month_difference)
	{
		if(month_difference >= current_month)
		{
			current_year -= 1;
			current_month = 12 - month_difference + current_month;
			temp_filename.replace(4, 2, current_month < 10 ? ("0" + std::to_string(current_month)) : std::to_string(current_month));
			temp_filename.replace(7, 4, std::to_string(current_year + 1900));

			proxi_temp_filename.replace(4, 2, current_month < 10 ? ("0" + std::to_string(current_month)) : std::to_string(current_month));
			proxi_temp_filename.replace(7, 4, std::to_string(current_year + 1900));
		}
		else
		{
			current_month -= month_difference;
			temp_filename.replace(4, 2, current_month < 10 ? ("0" + std::to_string(current_month)) : std::to_string(current_month));

			proxi_temp_filename.replace(4, 2, current_month < 10 ? ("0" + std::to_string(current_month)) : std::to_string(current_month));
		}
		myFile = fopen(temp_filename.c_str(), "r+b");
	}
	else
		myFile = fopen(filename.c_str(), "r+b"); //I will have to change the filename to that of a previous month and check if the file exists for the next stint
	if (myFile)
	{
		rctx.buf = buf;
		int res = dblog_read_init(&rctx);
		if ( res ){ print_error(res); fclose(myFile); return; }
		if (memcmp(buf, sqlite_sig, 16) || buf[68] != 0xA5) 
		{
			Serial.print(F("Invalid DB. Try recovery.\n"));
			fclose(myFile);
			return;
		}
		if (BUF_SIZE < (int32_t) 1 << rctx.page_size_exp)
		{
			Serial.print(F("Buffer size less than Page size. Try increasing if enough SRAM\n"));
			fclose(myFile);
			return;
		}
		std::string date_time_string;
		date_time_to_string( &date_time_string ) ;
		*message += "Energy Usage data for (" + std::to_string(current_month); 
		*message += "-" + std::to_string(current_year + 1900) + ") . Retrieved on ";
		*message += date_time_string + " \n";
		*message += "Date, (Time) Int_1, (Time) Int_2, (Time) Int_3, (Time) Int_4, (Time) Int_5,";
		*message += "(Time) Int_6, (Time) Int_7, (Time) Int_8, (Time) Int_9, (Time) Int_10,";
		*message += "(Time) Int_11, (Time) Int_12, (Time) Int_13, (Time) Int_14, (Time) Int_15,";
		*message += "(Time) Int_16, (Time) Int_17, (Time) Int_18\n";

		char row_ts [17];
		int32_t row_power;
		int16_t row_peak_W, row_least_W;
		dblog_read_first_row(&rctx);	//this is the first row that was used to init the db
		res = dblog_read_next_row(&rctx);	//goes to the second row of null values
		res = dblog_read_next_row(&rctx);	//where the logs actually start

		extract_row_values(&rctx, row_ts, &row_power, &row_peak_W, &row_least_W);
		uint16_t row_day = get_ts_part(row_ts + 8, 2);
		daily_power_sum = row_power;
		daily_peak = row_peak_W;
		daily_least = row_least_W;
		int16_t count = 0;
		*message += std::string(row_ts).substr(0, 10) + ", (";	//Date of the row data
		*message += std::string(row_ts).substr(11, 5) + ") ";	//time of the cell data
		*message += std::to_string((row_power / 3600.00) * (1 / 1000.00)) + ", ";
		count++;

		int i = 1; //i is for db row counting, multiply by 10 minutes to get total approximate minutes of power usage
		int j = 0; //iterate through the rows of the 2d array (it shows how many days power was used)
		res = dblog_read_next_row(&rctx);
		while(!res)
		{
			extract_row_values(&rctx, row_ts, &row_power, &row_peak_W, &row_least_W);
			if(count == 18)
			{
				while(get_ts_part(row_ts + 8, 2) == row_day)
				{
					res = dblog_read_next_row(&rctx);
					extract_row_values(&rctx, row_ts, &row_power, &row_peak_W, &row_least_W);
					// delay(5);
				}
				count = 0;
			}
			if( get_ts_part(row_ts + 8, 2) != row_day) // it's a new day, dump that of the previous day
			{
				*message += "\n" + std::string(row_ts).substr(0, 10) + ", ";
				count = 0;
				daily_summary[j][0] = row_day;
				daily_summary[j][1] = daily_power_sum;
				daily_summary[j][2] = daily_peak;
				daily_summary[j][3] = daily_least;
				
				row_day = get_ts_part(row_ts + 8, 2);
				daily_power_sum = 0;	//reset the parameters after dumping
				daily_peak = row_peak_W;
				daily_least = row_least_W;
				j++;
			}
			*message += "(" + std::string(row_ts).substr(11, 5) + ") ";
			*message += std::to_string((row_power / 3600.0) * (1 / 1000.0)) + ", ";
			count++;
			daily_power_sum += row_power;
			if(row_peak_W > daily_peak)
				daily_peak = row_peak_W;
			if(row_least_W < daily_least)
				daily_least = row_least_W;
			i++;
			res = dblog_read_next_row(&rctx);
			if(res) //it's the end of the db
			{
				daily_summary[j][0] = row_day;
				daily_summary[j][1] = daily_power_sum;
				daily_summary[j][2] = daily_peak;
				daily_summary[j][3] = daily_least;
			}
		}
		fclose(myFile);

		//at this point all data would have been extracted into the array: let's roll
		//to get the total energy used sum up all the index 1 column of all rows
		//to get the peak power consumed iterate through the index 2 column to check the max value
		//to get the least power consumed iterate through the index 3 column to check the least value
		//to get the average power consumed divide the sum energy by (i * 10)/60
		//to get the average energy used per day divide the sum energy used by j
		//
		for(int x=0; x<=j; x++)
		{
			for(int y=0; y<4; y++)
			{
				Serial.print(daily_summary[x][y]);
			 	Serial.print("|");
			}
			Serial.printf("\n");
		}
		int total_energy = 0;
		int peak_power = daily_summary[0][2];
		int least_power = daily_summary[0][2];
		float total_energy_kwh;
		float peak_power_kw, least_power_kw;
		for(int x=0; x<=j; x++)
		{
			total_energy = total_energy + daily_summary[x][1];
			if(daily_summary[x][2] > peak_power)
				peak_power = daily_summary[x][2];
			if(daily_summary[x][3] < least_power)
				least_power = daily_summary[x][3];
		}
		total_energy_kwh = (total_energy / 3600.00) * (1 / 1000.00);
		peak_power_kw = peak_power / 1000.0;
		least_power_kw = least_power / 1000.0;

		float average_power_kw;		//todo:
		float average_energy_used_daily;

		*message += "\nTotal Energy used(kWh): ";
		*message += std::to_string(total_energy_kwh);
		*message += "|Peak power used(kW): ";
		*message += std::to_string(peak_power_kw) + "|";
		*message += "Least power used(kW): " + std::to_string(least_power_kw);
	}
	else{
		Serial.print("Error opening file for reading\n");
		return;
	}

	// extract the maximum duration of object detected
	if(month_difference)
		myFile = fopen(proxi_temp_filename.c_str(), "r+b");
	else
		myFile = fopen(proxi_db_name.c_str(), "r+b");
	if(myFile)
	{
		rctx.buf = buf;
		int res = dblog_read_init(&rctx);
		if ( res ){ print_error(res); fclose(myFile); return; }
		if (memcmp(buf, sqlite_sig, 16) || buf[68] != 0xA5) 
		{
			Serial.print(F("Invalid DB. Try recovery.\n")); //todo: try adding a recovery code here
			fclose(myFile);
			return;
		}
		if (BUF_SIZE < (int32_t) 1 << rctx.page_size_exp)
		{
			Serial.print(F("Buffer size less than Page size. Try increasing if enough SRAM\n"));
			fclose(myFile);
			return;
		}
		char row_ts [17];
		char max_duration_ts[17];
		int16_t _duration;
		dblog_read_first_row(&rctx);	//this is the first row that was used to init the db
		res = dblog_read_next_row(&rctx);	//goes to the second row of null values
		res = dblog_read_next_row(&rctx);	//where the logs actually start(a bug-I don't know the source)

		extract_proxi_row_values(&rctx, row_ts, &_duration);
		max_duration = _duration;
		memcpy(max_duration_ts, row_ts, sizeof(row_ts));

		res = dblog_read_next_row(&rctx);
		while(!res)
		{
			extract_proxi_row_values(&rctx, row_ts, &_duration);
			Serial.printf("duration gotten: %i", _duration);
			if ( _duration > max_duration){
				max_duration = _duration;
				memcpy(max_duration_ts, row_ts, sizeof(row_ts));
			}
			res = dblog_read_next_row(&rctx);
		}
		fclose(myFile);
		std::string time_stamp(max_duration_ts);
		*message += "|Maximum duration of motion captured: ";
		*message += std::to_string(max_duration) + "s|time of capture: ";
		*message += std::string(max_duration_ts) + "\n";
		return;
	}
	else{
		Serial.print(F("Proxi_db file open Error\n"));
		*message += "|No motion captured";
		return;
	}
}

bool log_proximity_data(int duration)
{
	std::string date_time_string;
	date_time_to_string( &date_time_string ) ;

	Serial.println(proxi_db_name.c_str());
	myFile = fopen(proxi_db_name.c_str(), "r+b");
	if( myFile )
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
		int res = dblog_init_for_append(&ctx);
		if( res == DBLOG_RES_NOT_FINALIZED)
			dblog_finalize(&ctx);
		if(!res)
		{
			res = dblog_append_empty_row(&ctx);
			if( res ){ print_error(res); fclose(myFile); return 0; }

			res = dblog_set_col_val(&ctx, 0, DBLOG_TYPE_TEXT, date_time_string.c_str(), date_time_string.length());
			if( res ){ print_error(res); fclose(myFile); return 0; }

			res = dblog_set_col_val(&ctx, 1, DBLOG_TYPE_INT, &duration, sizeof(int));
			if( res ){ print_error(res); fclose(myFile); return 0; }

			Serial.print(F("\nLogging completed. Finalizing...\n"));
			if (!res)
				res = dblog_finalize(&ctx);
			fclose(myFile);
			return 1;
		}
		else
		{
			fclose(myFile);
			print_error(res);
			return 0;
		}
	}
	else
	{
		Serial.print(F("Open Error\n"));
		return 0;
	} 
	
}