/*----------------------------------------------------------------------------*/
/* Copyright (c) FIRST 2016. All Rights Reserved.                             */
/* Open Source Software - may be modified and shared by FRC teams. The code   */
/* must be accompanied by the FIRST BSD license file in the root directory of */
/* the project.                                                               */
/*----------------------------------------------------------------------------*/

#include "HAL/PWM.h"

#include "ConstantsInternal.h"
#include "DigitalInternal.h"
#include "PortsInternal.h"
#include "handles/HandlesInternal.h"

using namespace hal;

static inline int32_t GetMaxPositivePwm(DigitalPort* port) {
  return port->maxPwm;
}
static inline int32_t GetMinPositivePwm(DigitalPort* port) {
  return port->eliminateDeadband ? port->deadbandMaxPwm : port->centerPwm + 1;
}
static inline int32_t GetCenterPwm(DigitalPort* port) {
  return port->centerPwm;
}
static inline int32_t GetMaxNegativePwm(DigitalPort* port) {
  return port->eliminateDeadband ? port->deadbandMinPwm : port->centerPwm - 1;
}
static inline int32_t GetMinNegativePwm(DigitalPort* port) {
  return port->minPwm;
}
static inline int32_t GetPositiveScaleFactor(DigitalPort* port) {
  return GetMaxPositivePwm(port) - GetMinPositivePwm(port);
}  ///< The scale for positive speeds.
static inline int32_t GetNegativeScaleFactor(DigitalPort* port) {
  return GetMaxNegativePwm(port) - GetMinNegativePwm(port);
}  ///< The scale for negative speeds.
static inline int32_t GetFullRangeScaleFactor(DigitalPort* port) {
  return GetMaxPositivePwm(port) - GetMinNegativePwm(port);
}  ///< The scale for positions.

extern "C" {

HalDigitalHandle initializePWMPort(HalPortHandle port_handle, int32_t* status) {
  initializeDigital(status);

  if (*status != 0) return HAL_INVALID_HANDLE;

  int16_t pin = getPortHandlePin(port_handle);
  if (pin == InvalidHandleIndex) {
    *status = PARAMETER_OUT_OF_RANGE;
    return HAL_INVALID_HANDLE;
  }

  uint8_t origPin = static_cast<uint8_t>(pin);

  if (origPin < kNumPWMHeaders) {
    pin += kNumDigitalPins;  // remap Headers to end of allocations
  } else {
    pin = remapMXPPWMChannel(pin) + 10;  // remap MXP to proper channel
  }

  auto handle = digitalPinHandles.Allocate(pin, HalHandleEnum::PWM, status);

  if (*status != 0)
    return HAL_INVALID_HANDLE;  // failed to allocate. Pass error back.

  auto port = digitalPinHandles.Get(handle, HalHandleEnum::PWM);
  if (port == nullptr) {  // would only occur on thread issue.
    *status = HAL_HANDLE_ERROR;
    return HAL_INVALID_HANDLE;
  }

  port->pin = origPin;

  uint32_t bitToSet = 1 << remapMXPPWMChannel(port->pin);
  short specialFunctions = digitalSystem->readEnableMXPSpecialFunction(status);
  digitalSystem->writeEnableMXPSpecialFunction(specialFunctions | bitToSet,
                                               status);

  return handle;
}
void freePWMPort(HalDigitalHandle pwm_port_handle, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }

  if (port->pin > tPWM::kNumHdrRegisters - 1) {
    uint32_t bitToUnset = 1 << remapMXPPWMChannel(port->pin);
    short specialFunctions =
        digitalSystem->readEnableMXPSpecialFunction(status);
    digitalSystem->writeEnableMXPSpecialFunction(specialFunctions & ~bitToUnset,
                                                 status);
  }

  digitalPinHandles.Free(pwm_port_handle, HalHandleEnum::PWM);
}

bool checkPWMChannel(uint8_t pin) { return pin < kNumPWMPins; }

void setPWMConfig(HalDigitalHandle pwm_port_handle, double max,
                  double deadbandMax, double center, double deadbandMin,
                  double min, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }

  // calculate the loop time in milliseconds
  double loopTime =
      getLoopTiming(status) / (kSystemClockTicksPerMicrosecond * 1e3);
  if (*status != 0) return;

  int32_t maxPwm = (int32_t)((max - kDefaultPwmCenter) / loopTime +
                             kDefaultPwmStepsDown - 1);
  int32_t deadbandMaxPwm = (int32_t)(
      (deadbandMax - kDefaultPwmCenter) / loopTime + kDefaultPwmStepsDown - 1);
  int32_t centerPwm = (int32_t)((center - kDefaultPwmCenter) / loopTime +
                                kDefaultPwmStepsDown - 1);
  int32_t deadbandMinPwm = (int32_t)(
      (deadbandMin - kDefaultPwmCenter) / loopTime + kDefaultPwmStepsDown - 1);
  int32_t minPwm = (int32_t)((min - kDefaultPwmCenter) / loopTime +
                             kDefaultPwmStepsDown - 1);

  port->maxPwm = maxPwm;
  port->deadbandMaxPwm = deadbandMaxPwm;
  port->deadbandMinPwm = deadbandMinPwm;
  port->centerPwm = centerPwm;
  port->minPwm = minPwm;
  port->configSet = true;
}

void setPWMConfigRaw(HalDigitalHandle pwm_port_handle, int32_t maxPwm,
                     int32_t deadbandMaxPwm, int32_t centerPwm,
                     int32_t deadbandMinPwm, int32_t minPwm, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }

  port->maxPwm = maxPwm;
  port->deadbandMaxPwm = deadbandMaxPwm;
  port->deadbandMinPwm = deadbandMinPwm;
  port->centerPwm = centerPwm;
  port->minPwm = minPwm;
}

void getPWMConfigRaw(HalDigitalHandle pwm_port_handle, int32_t* maxPwm,
                     int32_t* deadbandMaxPwm, int32_t* centerPwm,
                     int32_t* deadbandMinPwm, int32_t* minPwm,
                     int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }
  *maxPwm = port->maxPwm;
  *deadbandMaxPwm = port->deadbandMaxPwm;
  *deadbandMinPwm = port->deadbandMinPwm;
  *centerPwm = port->centerPwm;
  *minPwm = port->minPwm;
}

void setPWMEliminateDeadband(HalDigitalHandle pwm_port_handle,
                             uint8_t eliminateDeadband, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }
  port->eliminateDeadband = eliminateDeadband;
}

uint8_t getPWMEliminateDeadband(HalDigitalHandle pwm_port_handle,
                                int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return false;
  }
  return port->eliminateDeadband;
}

/**
 * Set a PWM channel to the desired value. The values range from 0 to 255 and
 * the period is controlled
 * by the PWM Period and MinHigh registers.
 *
 * @param channel The PWM channel to set.
 * @param value The PWM value to set.
 */
void setPWMRaw(HalDigitalHandle pwm_port_handle, uint16_t value,
               int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }

  if (port->pin < tPWM::kNumHdrRegisters) {
    pwmSystem->writeHdr(port->pin, value, status);
  } else {
    pwmSystem->writeMXP(port->pin - tPWM::kNumHdrRegisters, value, status);
  }
}

/**
 * Set a PWM channel to the desired scaled value. The values range from -1 to 1
 * and
 * the period is controlled
 * by the PWM Period and MinHigh registers.
 *
 * @param channel The PWM channel to set.
 * @param value The scaled PWM value to set.
 */
void setPWMSpeed(HalDigitalHandle pwm_port_handle, float speed,
                 int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }
  if (!port->configSet) {
    *status = INCOMPATIBLE_STATE;
    return;
  }

  DigitalPort* dPort = port.get();

  if (speed < -1.0) {
    speed = -1.0;
  } else if (speed > 1.0) {
    speed = 1.0;
  }

  // calculate the desired output pwm value by scaling the speed appropriately
  int32_t rawValue;
  if (speed == 0.0) {
    rawValue = GetCenterPwm(dPort);
  } else if (speed > 0.0) {
    rawValue = (int32_t)(speed * ((float)GetPositiveScaleFactor(dPort)) +
                         ((float)GetMinPositivePwm(dPort)) + 0.5);
  } else {
    rawValue = (int32_t)(speed * ((float)GetNegativeScaleFactor(dPort)) +
                         ((float)GetMaxNegativePwm(dPort)) + 0.5);
  }

  if (!((rawValue >= GetMinNegativePwm(dPort)) &&
        (rawValue <= GetMaxPositivePwm(dPort))) ||
      rawValue == kPwmDisabled) {
    *status = HAL_PWM_SCALE_ERROR;
    return;
  }

  setPWMRaw(pwm_port_handle, rawValue, status);
}

/**
 * Set a PWM channel to the desired position value. The values range from 0 to 1
 * and
 * the period is controlled
 * by the PWM Period and MinHigh registers.
 *
 * @param channel The PWM channel to set.
 * @param value The scaled PWM value to set.
 */
void setPWMPosition(HalDigitalHandle pwm_port_handle, float pos,
                    int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }
  if (!port->configSet) {
    *status = INCOMPATIBLE_STATE;
    return;
  }
  DigitalPort* dPort = port.get();

  if (pos < 0.0) {
    pos = 0.0;
  } else if (pos > 1.0) {
    pos = 1.0;
  }

  // note, need to perform the multiplication below as floating point before
  // converting to int
  uint16_t rawValue = (int32_t)((pos * (float)GetFullRangeScaleFactor(dPort)) +
                                GetMinNegativePwm(dPort));

  if (rawValue == kPwmDisabled) {
    *status = HAL_PWM_SCALE_ERROR;
    return;
  }

  setPWMRaw(pwm_port_handle, rawValue, status);
}

void setPWMDisabled(HalDigitalHandle pwm_port_handle, int32_t* status) {
  setPWMRaw(pwm_port_handle, kPwmDisabled, status);
}

/**
 * Get a value from a PWM channel. The values range from 0 to 255.
 *
 * @param channel The PWM channel to read from.
 * @return The raw PWM value.
 */
uint16_t getPWMRaw(HalDigitalHandle pwm_port_handle, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return 0;
  }

  if (port->pin < tPWM::kNumHdrRegisters) {
    return pwmSystem->readHdr(port->pin, status);
  } else {
    return pwmSystem->readMXP(port->pin - tPWM::kNumHdrRegisters, status);
  }
}

/**
 * Get a scaled value from a PWM channel. The values range from -1 to 1.
 *
 * @param channel The PWM channel to read from.
 * @return The scaled PWM value.
 */
float getPWMSpeed(HalDigitalHandle pwm_port_handle, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return 0;
  }
  if (!port->configSet) {
    *status = INCOMPATIBLE_STATE;
    return 0;
  }

  int32_t value = getPWMRaw(pwm_port_handle, status);
  if (*status != 0) return 0;
  DigitalPort* dPort = port.get();

  if (value == kPwmDisabled) {
    return 0.0;
  } else if (value > GetMaxPositivePwm(dPort)) {
    return 1.0;
  } else if (value < GetMinNegativePwm(dPort)) {
    return -1.0;
  } else if (value > GetMinPositivePwm(dPort)) {
    return (float)(value - GetMinPositivePwm(dPort)) /
           (float)GetPositiveScaleFactor(dPort);
  } else if (value < GetMaxNegativePwm(dPort)) {
    return (float)(value - GetMaxNegativePwm(dPort)) /
           (float)GetNegativeScaleFactor(dPort);
  } else {
    return 0.0;
  }
}

/**
 * Get a position value from a PWM channel. The values range from 0 to 1.
 *
 * @param channel The PWM channel to read from.
 * @return The scaled PWM value.
 */
float getPWMPosition(HalDigitalHandle pwm_port_handle, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return 0;
  }
  if (!port->configSet) {
    *status = INCOMPATIBLE_STATE;
    return 0;
  }

  int32_t value = getPWMRaw(pwm_port_handle, status);
  if (*status != 0) return 0;
  DigitalPort* dPort = port.get();

  if (value < GetMinNegativePwm(dPort)) {
    return 0.0;
  } else if (value > GetMaxPositivePwm(dPort)) {
    return 1.0;
  } else {
    return (float)(value - GetMinNegativePwm(dPort)) /
           (float)GetFullRangeScaleFactor(dPort);
  }
}

void latchPWMZero(HalDigitalHandle pwm_port_handle, int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }

  pwmSystem->writeZeroLatch(port->pin, true, status);
  pwmSystem->writeZeroLatch(port->pin, false, status);
}

/**
 * Set how how often the PWM signal is squelched, thus scaling the period.
 *
 * @param channel The PWM channel to configure.
 * @param squelchMask The 2-bit mask of outputs to squelch.
 */
void setPWMPeriodScale(HalDigitalHandle pwm_port_handle, uint32_t squelchMask,
                       int32_t* status) {
  auto port = digitalPinHandles.Get(pwm_port_handle, HalHandleEnum::PWM);
  if (port == nullptr) {
    *status = HAL_HANDLE_ERROR;
    return;
  }

  if (port->pin < tPWM::kNumPeriodScaleHdrElements) {
    pwmSystem->writePeriodScaleHdr(port->pin, squelchMask, status);
  } else {
    pwmSystem->writePeriodScaleMXP(port->pin - tPWM::kNumPeriodScaleHdrElements,
                                   squelchMask, status);
  }
}

/**
 * Get the loop timing of the PWM system
 *
 * @return The loop time
 */
uint16_t getLoopTiming(int32_t* status) {
  return pwmSystem->readLoopTiming(status);
}
}
