#if PLATFORM_ID>=3

#include "application.h"
#include "unit-test/unit-test.h"
#include "pwm_hal.h"
#include <cmath>

#ifndef ABS
#define ABS(x) (x < 0 ? -x : x)
#endif // ABS


static const uint32_t maxPulseSamples = 100;
static const uint32_t minimumFrequency = 100;

uint8_t pwm_pins[] = {
#if (PLATFORM_ID == 0) // Core
        A0, A1, A4, A5, A6, A7, D0, D1
#elif (PLATFORM_ID == 6) // Photon
        D0, D1, D2, D3, A4, A5, WKP, RX, TX
#elif (PLATFORM_ID == 8) // P1
        D0, D1, D2, D3, A4, A5, WKP, RX, TX, P1S0, P1S1, P1S6
#elif (PLATFORM_ID == 10) // Electron
        D0, D1, D2, D3, A4, A5, WKP, RX, TX, B0, B1, B2, B3, C4, C5
#elif (PLATFORM_ID == PLATFORM_ARGON_SOM) || (PLATFORM_ID == PLATFORM_BORON_SOM) || (PLATFORM_ID == PLATFORM_XENON_SOM)
        D4, D5, D6, D7, A0, A1, A7 /* , RGBR, RGBG, RGBB */
# if PLATFORM_ID != PLATFORM_BORON_SOM || !HAL_PLATFORM_POWER_MANAGEMENT
        ,
        A6
# endif // PLATFORM_ID != PLATFORM_BORON_SOM || !HAL_PLATFORM_POWER_MANAGEMENT
#elif (PLATFORM_ID == PLATFORM_ARGON) || (PLATFORM_ID == PLATFORM_BORON) || (PLATFORM_ID == PLATFORM_XENON)
        D2, D3, D4, D5, D6, /* D7, */ D8, A0, A1, A2, A3, A4, A5 /* , RGBR, RGBG, RGBB */
#else
#error "Unsupported platform"
#endif
};

// static pin_t pin = pwm_pins[0];

template <typename F> void for_all_pwm_pins(F callback)
{
    for (uint8_t i = 0; i<arraySize(pwm_pins); i++)
    {
        callback(pwm_pins[i]);
    }
}

#if (PLATFORM_ID == 8) // P1
test(PWM_00_P1S6SetupForP1) {
    // disable POWERSAVE_CLOCK on P1S6
    System.disableFeature(FEATURE_WIFI_POWERSAVE_CLOCK);
}
#endif

test(PWM_01_CompherensiveResolutionFrequency) {
    for_all_pwm_pins([&](uint16_t pin) {
        // when
        pinMode(pin, OUTPUT);

        uint8_t resolution = 2;

        for (resolution = 2; ; resolution++) {
            // Set resolution
            analogWriteResolution(pin, resolution);
            if (resolution <= 15) {
                // All PWM pins should support resolution of up to 15 bits
                assertEqual(resolution, analogWriteResolution(pin));
            } else {
                // Some PWM pins (which utilize 32-bit timers) may support higher resolution
                if (resolution != analogWriteResolution(pin)) {
                    break;
                } else {
                    assertEqual(resolution, analogWriteResolution(pin));
                }
            }

            uint32_t maxVal = (1 << resolution) - 1;

            uint32_t freq = 1;
            uint32_t freqStep = 9;

            // Test all frequencies up to analogWriteMaxFrequency() with logarithmic step
            for (freq = minimumFrequency; freq <= analogWriteMaxFrequency(pin);) {
                float fp = floor(log10((float)freq));
                if (fp < 1)
                    fp = 1;
                freqStep = ((uint32_t)pow(10.0, fp + 1.0)) / 3;
                if (freqStep < 33)
                    freqStep = 33;

                // Test all analog values with logarithmic step
                uint32_t valueStep = 1;
                float vp = floor(log2((float)maxVal));
                if (vp < 2.0)
                    vp = 2.0;
                valueStep = ((uint32_t)pow(2.0, vp - 2.0)) / 3;
                if (valueStep < 1)
                    valueStep = 1;
                for (uint32_t value = 1; value < maxVal;) {
                    //Serial.printf("val=%d\r\n", value);
                    analogWrite(pin, value, freq);
                    uint32_t period = 1000 / freq;
                    delay(period ? period : 1);

                    // Check if the write resulted in correct analog value
                    assertEqual(HAL_PWM_Get_AnalogValue_Ext(pin), value);

                    float duty = (float)value/(float)maxVal;
                    float refPulseWidthUs = duty * (1000000.0 / (double)freq);
                    // pulseIn cannot measure pulses shorter than 10us reliably, limit to 20us
                    if (refPulseWidthUs < 20.0) {
                        // Skip
                    } else if ((1.0 - duty) * refPulseWidthUs < 20.0) {
                        // pulseIn will not be able to accurately measure high pulse that is followed by <20us low pulse
                    } else {
                        uint32_t pulseAcc = 0;
                        uint32_t pulseSamples = freq < 1000 ? maxPulseSamples / 10 : maxPulseSamples;
                        for (uint32_t i = 0; i < pulseSamples; i++) {
                            ATOMIC_BLOCK() {
                                pulseAcc += pulseIn(pin, HIGH);
                            }
                            // 0 and maxVal should result in a timeout
                            if (value == 0 || value == maxVal) {
                                assertEqual(pulseAcc, 0);
                                assertEqual(digitalRead(pin), 0);
                                break;
                            }
                        }
                        double avgPulse = (double)pulseAcc / pulseSamples;
                        double err = ABS(avgPulse - refPulseWidthUs);

                        float errPulseWidth = 0.05 * refPulseWidthUs;
                        // Pulse width should be witin 5% error margin
                        assertLessOrEqual(err, errPulseWidth);
                    }

                    // Clamp to maxVal
                    if (value < maxVal && (value + valueStep) > maxVal)
                        value = maxVal;
                    else
                        value += valueStep;
                }

                // Clamp to analogWriteMaxFrequency(pin)
                if (freq < analogWriteMaxFrequency(pin) && (freq + freqStep) > analogWriteMaxFrequency(pin))
                    freq = analogWriteMaxFrequency(pin);
                else if (freq < analogWriteMaxFrequency(pin))
                    freq += freqStep;
                else
                    break;
            }
        }

        // At least 15-bit maximum resolution
        assertMoreOrEqual(resolution, 15);
    });
}

#if (PLATFORM_ID == 8) // P1
test(PWM_99_P1S6CleanupForP1) {
    // enable POWERSAVE_CLOCK on P1S6
    System.enableFeature(FEATURE_WIFI_POWERSAVE_CLOCK);
}
#endif

#endif // PLATFORM_ID >= 3
