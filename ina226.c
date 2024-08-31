#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <wiringPiI2C.h>
#include "ina226.h"
#include <string.h>
#include <sqlite3.h>

#define INA226_ADDRESS 0x40
#define PWR_CALC_VOLTAGE 5 //fixed value for power calculation
#define SHUNT_OHM_VALUE 0.01
#define MAX_AMP_POSSIBLE 4

int fd;
uint64_t config;
float current_lsb;

uint16_t read16(int fd, uint8_t ad){
	uint16_t result = wiringPiI2CReadReg16(fd,ad);
	return  (result<<8) | (result>>8);
}

void write16(int fd, uint8_t ad, uint16_t value){
	wiringPiI2CWriteReg16(fd,ad,(value<<8)|(value>>8));
}

void ina226_calibrate(float r_shunt, float max_current)
{
	current_lsb = max_current / (1 << 15);
	float calib = 0.00512 / (current_lsb * r_shunt);
	uint16_t calib_reg = (uint16_t) floorf(calib);
	current_lsb = 0.00512 / (r_shunt * calib_reg);

	//printf("LSB %f\n",current_lsb);
	//printf("Calib %f\n",calib);
	//printf("Calib R%#06x / %d\n",calib_reg,calib_reg);

	write16(fd,INA226_REG_CALIBRATION, calib_reg);
}

void ina226_configure(uint8_t bus, uint8_t shunt, uint8_t average, uint8_t mode)
{
	config = (average<<9) | (bus<<6) | (shunt<<3) | mode;
	write16(fd,INA226_REG_CONFIGURATION, config);
}

static uint16_t ina226_conversion_ready()
{
	return read16(fd,INA226_REG_MASK_ENABLE) & INA226_MASK_ENABLE_CVRF;
}

void ina226_wait(){
	uint8_t average = (config>>9) & 7;
	uint8_t bus = (config>>6) & 7;
	uint8_t shunt = (config>>3) & 7;

	uint32_t total_wait = (wait[bus] + wait[shunt] + (average ? avgwaits[bus>shunt ? bus : shunt] : 0)) * averages[average];

	usleep(total_wait);

	int count=0;
	while(!ina226_conversion_ready()){
		count++;
	}
}

void ina226_read(float *voltage, float *current, float *power, float* shunt_voltage)
{
	if (voltage) {
		uint16_t voltage_reg = read16(fd,INA226_REG_BUS_VOLTAGE);
		*voltage = (float) voltage_reg * 1.25e-3;
        //printf("setup_ina226 returned fd: %f\n", *voltage);
	}

	if (current) {
		int16_t current_reg = (int16_t) read16(fd,INA226_REG_CURRENT);
		*current = (float) current_reg * 1000.0 * current_lsb;
        //printf("setup_ina226 returned fd: %f\n", *current);
	}

	if (power) {
		int16_t power_reg = (int16_t) read16(fd,INA226_REG_POWER);
		*power = (float) power_reg * 25000.0 * current_lsb;
	}

	if (shunt_voltage) {
		int16_t shunt_voltage_reg = (int16_t) read16(fd,INA226_REG_SHUNT_VOLTAGE);
		*shunt_voltage = (float) shunt_voltage_reg * 2.5e-3;
	}
}

void ina226_reset()
{
	write16(fd, INA226_REG_CONFIGURATION, config = INA226_RESET);
}

void ina226_disable()
{
	write16(fd, INA226_REG_CONFIGURATION, config = INA226_MODE_OFF);
}

void show_usage(const char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("This program reads values from an INA226 module on an I2C bus. The power value doesnt come from the Module\n");
    printf("Available options:\n");
    printf("  -h           Show this help message and exit.\n");
    printf("  -p           Display the power value.\n");
    printf("  -c           Display the current value.\n");
    printf("  -v           Display the Li-Ion battery voltage.\n");
	printf("  -s           Display the shunt voltage.\n");
    printf("  -a           Display all available values (power, current, voltage).\n");
    printf("  -sql <file> -t <delay> -n <iteration> Use an SQLite database for data storage. Specify the database file path.\n");
}


int setup_ina226() {
    fd = wiringPiI2CSetup(INA226_ADDRESS);
    if (fd < 0) {
        printf("Error: INA226 device not found.\n");
        return -1;
    } 

    ina226_calibrate(SHUNT_OHM_VALUE, MAX_AMP_POSSIBLE);
    ina226_configure(INA226_TIME_8MS, INA226_TIME_8MS, INA226_AVERAGES_16, INA226_MODE_SHUNT_BUS_CONTINUOUS);

    return fd;  
}

void read_and_display_values(int show_power, int show_current, int show_voltage, int show_shunt) {
    float voltage, current, power, shunt;
    ina226_read(&voltage, &current, &power, &shunt);
    
    if (show_power) {
        printf("%.3f mW\n", current * PWR_CALC_VOLTAGE);
    }
    if (show_current) {
        printf("%.3f mA\n", current);
    }
    if (show_voltage) {
        printf("%.3f V\n", voltage);
    }
	if (show_shunt) {
        printf("%.3f mV\n", shunt);
    }
}

void log_values_to_sql(const char *sql_file, int num_iterations, int delay) {
    float voltage, current, power, shunt;
    char buffer[80];
    time_t rawtime;
    int rc;
    char *err_msg = 0;
    sqlite3 *db;

    rc = sqlite3_open(sql_file, &db);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "Cannot open database: %s\n", sqlite3_errmsg(db));
        sqlite3_close(db);
        return;
    }

    const char *sql_create_table = "CREATE TABLE IF NOT EXISTS SensorData(Timestamp INT, Voltage REAL, Current REAL, Power REAL, Shunt REAL);";
    rc = sqlite3_exec(db, sql_create_table, 0, 0, &err_msg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "SQL error: %s\n", err_msg);
        sqlite3_free(err_msg);
        sqlite3_close(db);
        return;
    }

    char sql_insert_data[256];
    int i = 0;

    while (num_iterations == -1 || i < num_iterations) {
        ina226_wait();
        ina226_read(&voltage, &current, &power, &shunt);

        time(&rawtime);
        struct tm *timeinfo = localtime(&rawtime);
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", timeinfo);

        printf("%s,%d,%.3f,%.2f,%.0f,%.0f\n", buffer, (int)rawtime, voltage, current, current * PWR_CALC_VOLTAGE, shunt);

        snprintf(sql_insert_data, sizeof(sql_insert_data),
                 "INSERT INTO SensorData (Timestamp, Voltage, Current, Power, Shunt) VALUES (%d, %.3f, %.2f, %.0f, %.0f);",
                 (int)rawtime, voltage, current, power, shunt);

        rc = sqlite3_exec(db, sql_insert_data, 0, 0, &err_msg);
        if (rc != SQLITE_OK) {
            fprintf(stderr, "SQL error: %s\n", err_msg);
            sqlite3_free(err_msg);
            break;
        }

        if (delay > 0) {
            sleep(delay);
        }

        if (num_iterations != -1) {
            i++;
        }
    }

    sqlite3_close(db);
}


int main(int argc, char* argv[]) {
    char *sql_file = NULL;
    int show_power = 0, show_current = 0, show_voltage = 0, show_all = 0, show_shunt = 0;
    int num_iterations = 1; 
    int delay = 0;           


    if (argc == 1) {
        show_usage(argv[0]);
        return 0;
    }
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0) {
            show_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-p") == 0) {
            show_power = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            show_current = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            show_voltage = 1;
        } else if (strcmp(argv[i], "-s") == 0) {
            show_shunt = 1;
        } else if (strcmp(argv[i], "-a") == 0) {
            show_all = 1;
        } else if (strcmp(argv[i], "-sql") == 0) {
            if (i + 1 < argc) {
                sql_file = argv[++i];
            } else {
                fprintf(stderr, "Error: Missing argument for -sql option.\n");
                show_usage(argv[0]);
                return -1;
            }
        } else if (strcmp(argv[i], "-n") == 0) {
            if (i + 1 < argc) {
                num_iterations = atoi(argv[++i]);  // Convert string to integer
            } else {
                fprintf(stderr, "Error: Missing argument for -n option.\n");
                show_usage(argv[0]);
                return -1;
            }
        } else if (strcmp(argv[i], "-t") == 0) {
            if (i + 1 < argc) {
                delay = atoi(argv[++i]);  // Convert string to integer
            } else {
                fprintf(stderr, "Error: Missing argument for -t option.\n");
                show_usage(argv[0]);
                return -1;
            }
        } else {
            fprintf(stderr, "Error: Unknown option '%s'.\n", argv[i]);
            show_usage(argv[0]);
            return -1;
        }
    }

    if (show_power || show_current || show_voltage || show_shunt || show_all || sql_file) {
        setup_ina226();
    }
   
    if (show_all) {
        show_power = show_current = show_voltage = show_shunt = 1;
    }


    if (sql_file) {
        ina226_wait();
        log_values_to_sql(sql_file, num_iterations, delay);
    } else {
        ina226_wait(); // Ensure conversion is complete inside the ina module

        read_and_display_values(show_power, show_current, show_voltage, show_shunt);
        //ina226_reset();
        //ina226_disable();  

    }

    return 0;
}