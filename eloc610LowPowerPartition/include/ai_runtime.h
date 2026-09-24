/**
 * @file ai_runtime.h
 * @brief Selects the AI runtime of this build
 *
 * The shared firmware code (sampler hookup, detection rules, status, LoRa events, duty cycle) talks
 * to "aiRuntime" through the names both runtimes provide:
 *   - EdgeImpulse   (esp32dev-ei, EDGE_IMPULSE_ENABLED): the Edge Impulse library, lib/edge-impulse
 *   - ElocDetector  (esp32dev-tflm, ELOC_TFLM_ENABLED): TFLite Micro running the device model package
 *     from ELOC Model Training, lib/eloc_ml
 * Both set ELOC_AI_ENABLED. See README-ai.md.
 */

#pragma once

#if defined(EDGE_IMPULSE_ENABLED) && defined(ELOC_TFLM_ENABLED)
    #error "EDGE_IMPULSE_ENABLED and ELOC_TFLM_ENABLED are exclusive: pick one AI runtime per build"
#endif

#if defined(EDGE_IMPULSE_ENABLED)
    #include "EdgeImpulse.hpp"
    using AiRuntime = EdgeImpulse;
    /// Most labels one window can report
    #define AI_LABEL_CAPACITY EI_CLASSIFIER_LABEL_COUNT
#elif defined(ELOC_TFLM_ENABLED)
    #include "ElocDetector.hpp"
    using AiRuntime = ElocDetector;
    #define AI_LABEL_CAPACITY AI_MAX_LABELS
#endif

#if defined(ELOC_AI_ENABLED)
    #if !defined(EDGE_IMPULSE_ENABLED) && !defined(ELOC_TFLM_ENABLED)
        #error "ELOC_AI_ENABLED needs an AI runtime: EDGE_IMPULSE_ENABLED or ELOC_TFLM_ENABLED"
    #endif
    extern AiRuntime aiRuntime;
#endif
