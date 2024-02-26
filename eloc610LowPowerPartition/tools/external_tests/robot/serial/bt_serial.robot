*** Settings ***
Documentation   Test ELOC Bluetooth serial communication
Library           String
Library           platform
Resource          resource.robot
Suite Setup       Custom Setup
Suite Teardown    Custom Teardown
Test Tags         boot has-serial

*** Variables ***


*** Test cases ***
# System Boots
#     [Timeout]     3m
#     Check ELOC Boots
#     Sleep         5s

Verify getHelp
    [Timeout]     5s
    Check getHelp
    Sleep         2s

Verify getStatus
    [Timeout]     5s
    Check getStatus
    Sleep         2s

Verify getBattery
    [Timeout]     5s
    Check getBattery
    Sleep         2s

*** Keywords ***
Custom Setup
    # Setup port depending on the OS
    [Arguments]    ${SERIAL_PORT}=
    ${system}=    Evaluate    platform.system()    platform
    IF  $system == 'win32'
        ${PORT}=    Set Variable   ${SERIAL_PORT_WINDOWS}
    ELSE
        ${PORT}=    Set Variable   ${SERIAL_PORT_LINUX}
    END
    Set Suite Variable    ${SERIAL_PORT}  ${PORT}

    Add Port   ${SERIAL_PORT}
    ...        baudrate=115200
    ...        bytesize=8
    ...        parity=N
    ...        stopbits=1
    ...        timeout=999

Custom Teardown
    Delete All Ports

Clear Between Tests
# Make sure previous test case doesn't affect the next one
    SerialLibrary.Write Data  ${NEWLINE}
    SerialLibrary.reset_output_buffer
    SerialLibrary.reset_input_buffer

Read Until Single
    [Arguments]       ${expected}
    ${read} =         Read Until  terminator=${expected}
    Should Contain    ${read}     ${expected}
    #Log               ${read}     console=yes
    RETURN            ${read}

Check Boots
    Clear Between Tests
    Read Until Single   device ELOC
    Read Until Single   cmdVersion

Check getHelp
    Clear Between Tests
    SerialLibrary.Write Data  gethelp
    ${read} =  Read Until Single  \r\n
    ${length}=  Get length  ${read}
    Should Be True		${length}>15

Check getStatus
    Clear Between Tests
    SerialLibrary.Write Data  getstatus
    ${read} =  Read Until Single  \r\n
    ${length}=  Get length  ${read}
    Should Be True  ${length}>18

Check getBattery
    Clear Between Tests
    SerialLibrary.Write Data  getbattery : raw
    ${read} =  Read Until Single  \r\n
    ${length} =  Get length  ${read}
    Should Be True	${length}>10
