#if !defined(__TEMP_CONTROLLER_HPP__)
#define __TEMP_CONTROLLER_HPP__

#include <OneWireFet.hpp>
#include "ApplicationDefines.h"
#include <Arduino.h>

#define DEBUG_TEMP

class TempController
{
public:
    void Process();

    uint8_t GetDeviceCount()
    { return device_count; }
    int16_t GetTemperature(uint8_t device)
    {
        if (device < device_count)
            return results[device];
        return -1;
    }
#ifdef DEBUG_TEMP  
    uint16_t GetRawTemperature(uint8_t device)
    {
        if (device < device_count)
            return raw_temp[device];
        return -1;
    }
   uint16_t GetCountRemain(uint8_t device)
    {
        if (device < device_count)
            return count_remain[device];
        return -1;
    }
#endif  
    uint8_t* GetAddress(uint8_t device)
    {
        if (device < device_count)
            return devices[device];
        return nullptr;
    }

    unsigned long GetLastReadMillis()
    { return last_read_millis; }

    ~TempController()
    {
        for (int i = 0; i < device_count; i++)
            delete[] devices[i];
        if (devices) delete[] devices;
        if (results) delete[] results;
#ifdef DEBUG_TEMP
	if (raw_temp) delete[] raw_temp;
	if (count_remain) delete [] count_remain;
#endif	
    }

private:
    OneWireFet ds {ONEWIRE_BUS_READ_PIN, ONEWIRE_BUS_WRITE_PIN};
    enum class State
    {
        SETUP,
        START_CONVERSION,
        WAIT_CONVERSION,
        READ,
        WAIT_SWITCH_STATE,
    } state = State::SETUP;

    //! Total number of temperature sensors on the bus
    uint8_t device_count = 0;
    //! Array of addresses of the devices on the bus
    uint8_t** devices = nullptr;
    //! Current device being read
    uint8_t current_device = -1;
    //! All conversion results
    int16_t* results = nullptr;
#ifdef DEBUG_TEMP
  //! Raw temperature reading (hi+lo) registers (0.5C precision)
    uint16_t* raw_temp = nullptr;
  //! Counts per Celsius (hi) and counts remaining for -0.25 truncated raw_temp offset
    uint16_t* count_remain = nullptr;
#endif
    //! Last read temperature
    unsigned long last_read_millis = 0;

    //! Wait conversion timeout
    unsigned long last_wait_millis = 0;
    //! Read CRC error timeout
    uint8_t crc_error_timeout = 0;

    void switch_state(State next_state, unsigned long timeout = 0);
    //! Next state to switch to
    State next_state = State::SETUP;
    //! Next state timeout
    unsigned long next_state_millis = 0;

};

#endif // __TEMP_CONTROLLER_HPP__
