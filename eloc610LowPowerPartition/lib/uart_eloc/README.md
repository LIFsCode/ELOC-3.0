# UART

## Introduction
This library adds a UART interface to the ELOC. It is primarily used for testing purposes.

## Usage
The UART is enabled in project_config.h
```
ENABLE_TEST_UART
```


## Lower power/ Sleep Considerations
The ELOC makes considerable use of power management. Depending on configuration the UART may become inaccessible.
To enable the UART to 'wake-up' the ELOC, set the following
In project_config.h
```
ENABLE_UART_WAKE_FROM_SLEEP
```
In the relevant SDK, ensure the following is commented out
```
CONFIG_PM_SLP_DISABLE_GPIO
```

## Pin selection
Note for the UART to wake the ELOC from sleep:
 "the wakeup signal can only be input via IO_MUX (i.e. GPIO3 should be configured as function_1 to wake up UART0, GPIO9 should be configured as function_5 to wake up UART1), UART2 does not support light sleep wakeup feature. the UART pins must be the default pins, as these bypass the GPIO controller."

[Further details](https://docs.espressif.com/projects/esp-idf/en/v4.4.7/esp32/api-reference/peripherals/uart.html#_CPPv425uart_set_wakeup_threshold11uart_port_ti)