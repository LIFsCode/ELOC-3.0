

# To build the ai 'branch'/ version of this project:
Enter the folder containing the project (e.g. cd ~/Documents/PlatformIO/Projects/eloc610LowPowerPartition)

1. git checkout ai
2. git pull

Build using 'esp32dev-ei' in the 'Project Tasks':
1.  'Full Clean'   **(Very important, otherwise changes to any files in lib/edge-impulse/src/ will not be pulled into .pio build folder)**
2.  'Build'
3.  'Upload'

# To update the Edge Impulse model follow these steps:
1. Open the Edge Impulse Studio and navigate to the project containing the model you want to use.
2. Ensure the correct target device is selected (i.e. ESP32).
3. Click on the "Deployment" tab.
4. Select the deployment you want to update (i.e. "Arduino Library").
5. Click on the "Builde" button & download the zip file.
6. Delete the following folders from this project. This may not be strictly necessary as some fo these folders may be unchanged but it's a good idea to ensure that the latest versions are used:  
    `lib/edge-impulse/src/edge-impulse-sdk`  
    `lib/edge-impulse/src/model-parameters`  
    `lib/edge-impulse/src/tflite-model`  
    `lib/edge-impulse/src/<model-header-file.h>`  
7. Copy the three new folders (edge-impulse-sdk, model-parameters & tflite-model) & model header file from the downloaded model into 'lib/src/'. 
8. Amend the following line (currently #10) in /lib/edge-impulse/src/EdgeImpulse.cppn.cpp with the correct model header file name (if necessary)
    `#include "trumpet_inferencing.h"`
9. Under 'esp32dev-ei' in the 'Project Tasks' menu run:
    'Full Clean' **(Very important, otherwise the new model will not be pulled into .pio build folder)**
    'Build'
    'Upload'

## Troubleshooting
1. I've noticed that at startup there are errors about failing to run the inference model (or similar). When the Bluetooth task is suspended (after 30sec?) the problem seems to resolve itself & predictions will be visible.
2. esp32dev-ei might not appear under 'Project Tasks'. The refresh button above will do the trick.
3. Compile error:
    ```lib/edge-impulse/src/edge-impulse-sdk/tensorflow/lite/micro/kernels/select.cpp:157:21: 
    error: 'output_size' may be used uninitialized in this function [-Werror=maybe-uninitialized]
    TfLiteIntArrayFree(output_size);
    ~~~~~~~~~~~~~~~~~~^~~~~~~~~~~~~
    cc1plus: some warnings being treated as errors
    [.pio/build/esp32dev-ei/lib7f0/edge-impulse/edge-impulse-sdk/tensorflow/lite/micro/kernels/select.cpp.o] Error 1   
    
    Modify line 120 of lib/edge-impulse/src/edge-impulse-sdk/tensorflow/lite/micro/kernels/select.cpp to read:  
    TfLiteIntArray* output_size = nullptr;
4. Compile error:
    ```collect2.exe: error: ld returned 1 exit status
    *** [.pio\build\esp32dev-ei-windows\firmware.elf] Error 1```
    Solution: Comment out this line in platform.io: -DEI_CLASSIFIER_ALLOCATION_STATIC=1
    It's a RAM issue. More info here: https://github.com/LIFsCode/ELOC-3.0/issues/79


