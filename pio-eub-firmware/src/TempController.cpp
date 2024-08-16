#if !defined(__TEMP_CONTROLLER_CPP__)
#define __TEMP_CONTROLLER_CPP__

#include "TempController.hpp"
#include "ApplicationDefines.h"

void TempController::Process() {
    switch (state)
    {
    case State::SETUP: {
        // Free the memory if it was allocated before
        if (results) delete[] results;
        if (devices) {
            for (int i = 0; i < device_count; i++)
                delete[] devices[i];
            delete[] devices;
        }
        device_count = 0;
        last_read_millis = 0;

        if (!ds.reset()) {
            static bool shown_error = false;
            if (!shown_error) {
                TC_PRINTLN("1-Wire bus not found!");
                shown_error = true;
            }
            return;
        }

        // Get the number of devices on the bus
        byte addr[8];
        ds.reset_search();
        while (ds.search(addr)) device_count++;

        if (device_count == 0) {
            static bool shown_error = false;
            if (!shown_error) {
                TC_PRINTLN("No 1-Wire devices found!");
                shown_error = true;
            }
            return;
        }

        // Allocate memory for the results
        results = new uint16_t[device_count];
        // Allocate memory for the addresses
        devices = new uint8_t*[device_count];
        for (int i = 0; i < device_count; i++) {
            results[i] = 0xFFFF;
            devices[i] = new uint8_t[8];
        }

        // Get the addresses of the devices
        ds.reset_search();
        for (int i = 0; i < device_count; i++)
        {
            ds.search(addr);
            for (int j = 0; j < 8; j++) devices[i][j] = addr[j];
        }

	TC_PRINT_START();
	Serial.print("Temperature Controller setup found ");
	Serial.print(device_count, DEC);
	Serial.print(" sensors.");
	TC_PRINT_END();

        state = State::START_CONVERSION;
        break;
    }
    case State::START_CONVERSION: {
        ds.reset();
        ds.skip();
        ds.write(0x44); // Start temperature conversion

        state = State::WAIT_CONVERSION;
        last_wait_millis = millis() + 500;
        break;
    }
    case State::WAIT_CONVERSION: {
        // Check if the conversion is done every 5ms
        unsigned long new_millis = millis();
        unsigned long ellapsed = new_millis - last_wait_millis;
        if (ellapsed < 5) return;
        last_wait_millis = new_millis;

        if (!ds.read_bit()) return;

        current_device = 0;
        crc_error_timeout = 0;
        state = State::READ;
        break;
    }
    case State::READ: {
        if (current_device >= device_count) {
            last_read_millis = millis();
            state = State::START_CONVERSION;
            return;
        }

        const uint8_t* addr = devices[current_device];
        ds.reset();
        ds.select(addr);
        ds.write(0xBE); // Read scratchpad
        // Read temperature
        byte data[9];
        for (int j = 0; j < 9; j++)
	  data[j] = ds.read();
        // Check CRC
        if (OneWireFet::crc8(data, 8) != data[8]) {
            if (++crc_error_timeout > 10) {
                state = State::SETUP;
                TC_PRINTLN("CRC check error!");
            }
            return;
        }

        // Convert the data to actual temperature
        int16_t raw = (data[1] << 8) | data[0];
        if (addr[0] == 0x10) { // DS18S20 or old DS1820 returns temperature in 1/128 degrees 
	  // Note that count_per_c register data[7] is not hardcoded to 16 as stated
	  // in http://myarduinotoy.blogspot.com/2013/02/12bit-result-from-ds18s20.html
	  // and usually ranges from 80 to 108. Thefore, we multiply by 128 in order 
	  // to get sufficient precission. Similarly as in
	  // https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/master/DallasTemperature.cpp
	  // https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/65112b562fd37af68ed113c9a3925c09c4529e14/DallasTemperature.cpp#L712
	  uint16_t  dt = (data[7]-data[6]) << 7; // multiply by 128
	  dt /= data[7]; 
	  raw = (raw&0xFFFE)*64 - 32 + dt; // 0.5*128=64 == 1 << 6; 0.25*128=32
        } else {
            byte cfg = (data[4] & 0x60);
            if (cfg == 0x00) raw = raw & ~7;  // 9 bit resolution, 93.75 ms
            else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
            else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
	    raw = raw << 3; // 16*8=128 -> 1<<3=8
        }
        results[current_device] = raw;

        crc_error_timeout = 0;
        current_device++;
        break;
    }
    default:
        state = State::SETUP;
    }
}

#endif // __TEMP_CONTROLLER_CPP__
