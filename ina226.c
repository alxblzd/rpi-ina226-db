#include <stdio.h>
#include <errno.h>
#include <stdint.h>
#include <time.h>
#include <math.h>
#include <unistd.h>
#include <wiringPiI2C.h>
#include "ina226.h"
#include <string.h>

#define INA226_ADDRESS 0x40
#define PWR_CALC_VOLTAGE 5
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

// R of shunt resistor in ohm. Max current in Amp
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

inline void ina226_reset()
{
	write16(fd, INA226_REG_CONFIGURATION, config = INA226_RESET);
}

inline void ina226_disable()
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
    printf("  -sql <file>  Use an SQLite database for data storage. Specify the database file path.\n");
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

void log_values_to_sql(const char *sql_file) {
    float voltage, current, power, shunt;
    char buffer[80];
    time_t rawtime;
    
    for (;;) {
		    
        ina226_wait(); 
        ina226_read(&voltage, &current, &power, &shunt);

        time(&rawtime);
        struct tm *info = localtime(&rawtime);
        strftime(buffer, 80, "%Y-%m-%d,%H:%M:%S", info);

        printf("%s,%d,%.3f,%.2f,%.0f,%.0f\n", buffer, (int)rawtime, voltage, current, current * PWR_CALC_VOLTAGE, shunt);

        usleep(1000000);
    }
}

int main(int argc, char* argv[]) {
    char *sql_file = NULL;
    int show_power = 0, show_current = 0, show_voltage = 0, show_all = 0, show_shunt = 0;

    //If no arguments are passed
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
        } else {
            fprintf(stderr, "Error: Unknown option '%s'.\n", argv[i]);
            show_usage(argv[0]);
            return -1;
        }
    }




    // Setup the INA226 sensor if any data is requested
    if (show_power || show_current || show_voltage || show_shunt || show_all || sql_file) {
        setup_ina226();
    
    }
   

    // Handle the case where -a is specified
    if (show_all) {
        show_power = show_current = show_voltage = show_shunt = 1;
    }

    // Log values to SQL file if specified, otherwise display values
    if (sql_file) {
        log_values_to_sql(sql_file);
    } else {
        read_and_display_values(show_power, show_current, show_voltage, show_shunt);
    }

    return 0;
}