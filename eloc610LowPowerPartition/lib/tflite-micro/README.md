# tflite-micro
TensorFlow Lite for Microcontrollers library

## Introduction  
This is a set of TensorFlow Lite library files for microcontrollers that should build for most devices (known to work for ESP32)

## To use  
Modify the path as required but something like
```
#include "../tflite-micro/tensorflow/lite/micro/micro_interpreter.h"
#include "../tflite-micro/tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "../tflite-micro/tensorflow/lite/micro/system_setup.h"
#include "../tflite-micro/tensorflow/lite/schema/schema_generated.h"
#include "../tflite-micro/tensorflow/lite/micro/micro_error_reporter.h"
#include "../tflite-micro/tensorflow/lite/micro/micro_interpreter.h"
#include "../tflite-micro/tensorflow/lite/micro/all_ops_resolver.h"
```

## To do
Need to investigate method of building automatically from [source](https://github.com/tensorflow/tflite-micro).  
