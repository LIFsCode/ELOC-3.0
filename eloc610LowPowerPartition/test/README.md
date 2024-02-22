# Introduction
This directory is for Unit Testing and project tests.

## References
More information about PlatformIO Unit Testing:
- https://docs.platformio.org/page/plus/unit-testing.html
- https://piolabs.com/blog/insights/cicd-testing-coverage-versioning.html

### Generic Tests (Desktop)
C++ files placed in ```/test/test_generic_*``` will be run as native (desktop) tests.\
To build & run desktop/ generic tests go to Project Tasks > generic_unit_tests > Advanced > Test\
No hardware is required for these tests.\
Probably limited in scope to simple functions, classes, etc.\
However it is possible to mock hardware. Quick to compile & run.

### Target Tests (Embedded)
C++ files placed in ```/test/test_target_*``` will be run as embedded tests.\
The target hardware must be available and connected.

There are two options to run target tests.
1. The first is to run 'target_unit_selected_test' which will run the test defined in platformio.ini under ```selected_tests = test_target_test_name```\
PlatformIO will automatically build, upload (to the target hardware), run & collect the results of the test.\
For this to occur the target hardware **must** be either must be ready to be programmed OR capable of programming without user input.\
To build & run a single embedded/ target test go to Project Tasks > target_unit_selected_tests > Advanced > Test
2. The second option is to run 'target_unit_all_tests' which will run all tests in the ```/test/test_target_*``` folder.\
PlatformIO will automatically build, upload (to the target hardware), run & collect the results of each test **separately**.\
For this to occur the target hardware **must** be capable of programming without user input.\
To build & run all embedded/ target tests go to Project Tasks > target_unit_all_tests > Advanced > Test

### Both Tests (Desktop & Embedded)
C++ files placed in ```/test/test_*``` are availbe to run as **both** native (desktop) & embedded tests.\
Inclusion guards facilitate this.

### Components for testing
Ideally place files in /lib for testing according to this path:

```
|--lib
|  |
|  |--Bar
|  |  |--docs
|  |  |--examples
|  |  |--src
|  |     |- Bar.c
|  |     |- Bar.h
|  |  |- library.json (optional, custom build options, etc) https://docs.platformio.org/page/librarymanager/config.html
|  |
```
### Test Results
After running the results are generated:
```
-------------------------------------------------------- target_unit_tests:test_target_edge-impulse [PASSED] Took 21.61 seconds --------------------------------------------------------

======================================================================================= SUMMARY =======================================================================================
Environment        Test                      Status    Duration
-----------------  ------------------------  --------  ------------
target_unit_tests  test_target_audio_input   PASSED    00:00:22.584
target_unit_tests  test_target_buzzer        PASSED    00:00:25.340
target_unit_tests  test_both_demo            PASSED    00:00:21.701
target_unit_tests  test_target_edge-impulse  PASSED    00:00:21.608
====================================================================== 5 test cases: 5 succeeded in 00:01:31.234 ======================================================================
 ```


 ### Mock Class
 A mock class is contained in lib/mock. This could be added upon over time to mock components for testing.