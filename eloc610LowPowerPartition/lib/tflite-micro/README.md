# tflite-micro
TensorFlow Lite library files for microcontrollers

## Introduction  
This is a set of TensorFlow Lite library files for microcontrollers that should build for most devices (known to work for ESP32). Its just tfiles from TF before the dependencies were complicated in later versions

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

## Build library from source
Follow directions from [here](https://github.com/tensorflow/tflite-micro/blob/main/tensorflow/lite/micro/docs/new_platform_support.md#step-1-build-tflm-static-library-with-reference-kernels)

python3 tensorflow/lite/micro/tools/project_generation/create_tflm_tree.py \
  -e hello_world \
  -e micro_speech \
  /tmp/tflm-tree