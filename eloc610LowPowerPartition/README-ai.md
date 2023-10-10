

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
2. Click on the "Deployment" tab.
3. Select the deployment you want to update (i.e. "Arduino Library").
4. Click on the "Builde" button & download the zip file.
5. Delete the following folders from this project. This may not be strictly necessary as some fo these folders may be unchanged but it's a good idea to ensure that the latest versions are used:  
    `lib/edge-impulse/src/edge-impulse-sdk`  
    `lib/edge-impulse/src/model-parameters`  
    `lib/edge-impulse/src/tflite-model`  
    `lib/edge-impulse/src/<model-header-file.h>`  
6. Copy the three new folders (edge-impulse-sdk, model-parameters & tflite-model) & model header file from the downloaded model into 'lib/src/'. 
7. Amend the following line (currently #736) in main.cpp with the correct model header file name (if necessary)
    `#include "trumpet_inferencing.h"`
8. Under 'esp32dev-ei' in the 'Project Tasks' menu run:
    'Full Clean' **(Very important, otherwise the new model will not be pulled into .pio build folder)**
    'Build'
    'Upload'

## Troubleshooting
1. I've noticed that at startup there are errors about failing to run the inference model (or similar). When the Bluetooth task is suspended (after 30sec?) the problem seems to resolve itself & predictions will be visible.

