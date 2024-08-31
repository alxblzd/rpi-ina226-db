# INA226 Power Monitoring for Raspberry Pi 4B

This project utilizes the INA226 sensor to monitor power consumption and lithium-ion battery voltage in a Raspberry Pi 4B setup. The data collected will be connected to a database to generate basic charts.

based on this github repository : https://github.com/MarioAriasGa/raspberry-pi-ina226

## Available Options

- `-h`           Show this help message and exit.
- `-p`           Display the power value.
- `-c`           Display the current value.
- `-v`           Display the Li-Ion battery voltage.
- `-s`           Display the shunt voltage.
- `-a`           Display all available values (power, current, voltage).
- `-sql <file> -t <delay> -n <iteration>`  Use an SQLite database for data storage. Specify the database file path.

## SQLite Implementation

The SQLite is working but still in progress, I need to make the databse more resilient for time serie, 2 tables, 2 differrent primary key then join them together if I need to read data for a specific datetime

## Power Calculation Details


The power value is not coming from registry that contain calcul made with the module, despite it being possible. 

Because, power calculations from the module are based on the internal voltage sensor that I instead use for monitoring the battery,
I want the power being calculated the 5V bus coming out of the UPS, so I fixed it at 5V within the program to keep a reding on my li-ion battery. 

This value can be set manually in the ina226.c file with the #define PWR_CALC_VOLTAGE 5 


```c
#define INA226_ADDRESS 0x40 // I2C address
#define PWR_CALC_VOLTAGE 5 //fixed value for power calculation
#define SHUNT_OHM_VALUE 0.01 
#define MAX_AMP_POSSIBLE 4 // Max amp to measure on your circuit, used for calibration
```


## dependencies :
- libsqlite3-dev
- and more to precises
