/*
 * Created on Sun Mar 12 2023
 *
 * Project: International Elephant Project (Wildlife Conservation International)
 *
 * The MIT License (MIT)
 * Copyright (c) 2023 Fabian Lindner
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software
 * and associated documentation files (the "Software"), to deal in the Software without restriction,
 * including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED
 * TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef BUZZER_H_
#define BUZZER_H_

#include <stdint.h>
#include <stdbool.h>
#include <vector>
#include <tuple>
#include "hal/ledc_types.h"
#include "esp_err.h"

typedef enum {
    NOTE_C, NOTE_Cs, NOTE_D, NOTE_Eb, NOTE_E, NOTE_F, NOTE_Fs, NOTE_G, NOTE_Gs, NOTE_A, NOTE_Bb, NOTE_B, NOTE_MAX
} note_t;

typedef struct Sound {
      note_t note;
      uint32_t octave;
      uint32_t duration;
      uint32_t pauseDuration;
      Sound() : note(NOTE_C), octave(4), duration(0), pauseDuration(0){};
      Sound(note_t Note, uint32_t Duration): note(Note), octave(4), duration(Duration), pauseDuration(0) {};
      Sound(note_t Note, uint32_t Duration, uint32_t PauseDuration): note(Note), octave(4), duration(Duration), pauseDuration(PauseDuration) {};
      Sound(std::tuple<note_t, int> Note, uint32_t Duration): note(std::get<0>(Note)), octave(std::get<1>(Note)), duration(Duration) {};
}Sound;

typedef std::vector<Sound> Melody;

class BuzzerBase
{
private:
   esp_err_t init();
   uint32_t CalcDutyCycle();
   ledc_mode_t GetSpeedMode();
   void start();
   void stop();
   void cfgFrequency(unsigned int freq);

	unsigned int mPin;
	unsigned int mChannel;
	unsigned int mResolution;
	unsigned int mFreq;
	bool mSpeedModeHighLow;

public:



   esp_err_t ledcWriteTone(uint32_t freq,uint32_t duration); 
   esp_err_t ledcWriteNote(note_t note, uint8_t octave, uint32_t duration);
   BuzzerBase(unsigned int pin);
   BuzzerBase(unsigned int channel, unsigned int resolution, unsigned int pin, unsigned int defaultFreq, bool speedModeHighLow);
   ~BuzzerBase();
};







#endif // BUZZER_H_