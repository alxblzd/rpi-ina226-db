# INA226 Power Monitoring for Raspberry Pi 4B

This project utilizes the INA226 sensor to monitor power consumption and lithium-ion battery voltage in a Raspberry Pi 4B setup. The data collected will be connected to a database to generate basic charts.

## Available Options

- `-h`           Show this help message and exit.
- `-p`           Display the power value.
- `-c`           Display the current value.
- `-v`           Display the Li-Ion battery voltage.
- `-s`           Display the shunt voltage.
- `-a`           Display all available values (power, current, voltage).
- `-sql <file>`  Use an SQLite database for data storage. Specify the database file path.

## SQLite Implementation

The SQLite implementation is still in progress.

## Power Calculation Details

The power value is not derived from the module’s internal calculations, even though this is technically possible. Instead, power calculations are based on the internal voltage sensor used for monitoring the battery. To keep track of the 5V bus from the UPS, the voltage is fixed at 5V in the program. You can manually adjust this value in the `ina226.c` file using the following directive:

```c
#define PWR_CALC_VOLTAGE 5
