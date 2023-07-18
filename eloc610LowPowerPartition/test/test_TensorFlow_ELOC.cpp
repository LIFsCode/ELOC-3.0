/**
 * @file test_TensorFlow_ELOC.cpp
 * 
 * @brief Unit tests for the TensorFlow_ELOC class.
 * 
 * @version 0.1
 * 
 * @date 2021-04-06
*/

#include <unity.h>

#include "TensorFlow_ELOC.hpp"

void setUp(void) {

}

void tearDown(void) {
}

void test_TensorFlow_ELOC_GetInstance() {

    TEST_ASSERT(nullptr == nullptr);

}


void test_TensorFlow_ELOC_after_init() {


    //TEST_ASSERT_TRUE(cbuffer_empty(&buff));
}

extern "C"
{
  void app_main(void);
}

void app_main(void) {


    UNITY_BEGIN();

    TensorFlow_ELOC tf_eloc;


   // test_TensorFlow_ELOC_GetInstance();


    UNITY_END();
 
}