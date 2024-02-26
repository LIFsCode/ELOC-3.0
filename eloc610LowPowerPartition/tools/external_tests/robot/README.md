#  Testing framework for ELOC hardware

## Introduction
This is hardware-in-the-loop (HIL) test cases for ELOC hardware. It is based on the [Robot Framework](https://robotframework.org/). It allows both manual & automated testing of the ELOC hardware.

## Installation
Follow the steps below:
1. Install [Python](https://www.python.org/downloads/)
2. Install the framework and the serial library using pip:
```
pip install robotframework
pip install robotframework-seriallibrary
```

## Usage
To run the tests, execute the following command:
```
robot bt_serial.robot
```

## Output
The results of the tests will be displayed in the terminal & a detailed report will be generated (report.html).
```
==============================================================================
Bt Serial :: Test ELOC Bluetooth serial communication
==============================================================================
Verify getHelp                                                        | PASS |
------------------------------------------------------------------------------
Verify getStatus                                                      | PASS |
------------------------------------------------------------------------------
Verify getBattery                                                     | PASS |
------------------------------------------------------------------------------
Bt Serial :: Test ELOC Bluetooth serial communication                 | PASS |
3 tests, 3 passed, 0 failed
==============================================================================
```

## Workarounds
The ESP32 Bluetooth serial port does not (currently) connect to Linux devices (known issue).

As a workaround use an ESP32 supporting Bluetooth Classic as a [serial to Bluetooth bridge](https://github.com/OOHehir/esp32_bt_serial).

## Using tests in CI/CD
The tests can be used in Python applications for CI/CD. As an example:
```
import robot

logFile = open('log_results.txt', 'w')
robot.run('tests.robot',stdout=logFile)
```
For more information, see the [robot package](https://robot-framework.readthedocs.io/en/latest/autodoc/robot.html).
