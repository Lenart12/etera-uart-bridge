#if !defined(__TEMP_CONTROLLER_HPP__)
#define __TEMP_CONTROLLER_HPP__

#include <OneWireFet.hpp>
#include "ApplicationDefines.h"
#include <Arduino.h>

//#define DEBUG_TEMP

// Legacy DS1820 sensors are experiencing glitches to x.75 or to x.76
// few times a day. With this fix we compare previous reading and decide
// it it is a clitch or not depending on deviation that should not be more
// than 0.1 K. Typically the glitch is upward from +0.2 to +0.9 K
#define DOT765

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
#ifdef DOT765
	if (previous_temperature) delete [] previous_temperature;
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
#ifdef DOT765
  int16_t* previous_temperature = nullptr;
#endif
    //! Last read temperature
    unsigned long last_read_millis = 0;

    //! Read CRC error timeout
    uint8_t crc_error_timeout = 0;

    void switch_state(State next_state, unsigned long timeout = 0);
    //! Next state to switch to
    struct NextState {
        State state;
        // Seperated start and delay to prevent overflow errors
        unsigned long timeout_start;
        unsigned long timeout_delay;
        //! Switch to the next state
        void switch_state(State& current, State next, unsigned long timeout);
        //! Process the wait state
        bool process_wait(State& current);
    } next_state;
};

#endif // __TEMP_CONTROLLER_HPP__
