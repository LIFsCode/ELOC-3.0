# Resources for use by all tests

*** Settings ***
Library           SerialLibrary    encoding=ascii

*** Variables ***
${SERIAL_PORT_LINUX}=    /dev/ttyUSB0
${SERIAL_PORT_WINDOWS}=  COM3
${NEWLINE}        ${\n}

*** Keywords ***
