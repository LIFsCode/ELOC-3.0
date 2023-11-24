/**
 * @file ei_inference.h
 * @brief Inference structure
 * 
 * @date 2021-05-05
 * 
*/

#ifndef _EI_INFERENCE_H_
#define _EI_INFERENCE_H_

#include <stdint.h>



/** Audio buffers, pointers and selectors 
 *  Allocate two buffers for continuous inference
*/
typedef struct {
    signed short *buffers[2];
    unsigned char buf_select;
    unsigned char buf_ready;
    unsigned int buf_count;
    unsigned int n_samples;
} inference_t;


#endif // _EI_INFERENCE_H_