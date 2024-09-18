#if !defined(__TEMP_CONTROLLER_CPP__)
#define __TEMP_CONTROLLER_CPP__

#include "TempController.hpp"
#include "ApplicationDefines.h"

void TempController::switch_state(State next, unsigned long timeout) {
    next_state.switch_state(state, next, timeout);
}

void TempController::NextState::switch_state(State& current, State next, unsigned long timeout) {
    if (timeout == 0) {
        current = next;
        return;
    }
    current = State::WAIT_SWITCH_STATE;
    state = next;
    timeout_start = millis();
    timeout_delay = timeout;
}

bool TempController::NextState::process_wait(State& current) {
    if (millis() - timeout_start < timeout_delay) return false;
    current = state;
    return true;
}

void TempController::Process() {
    switch (state)
    {
    case State::SETUP: {
        // Free the memory if it was allocated before
        if (results) delete[] results;

        #ifdef DEBUG_TEMP
        if (raw_temp) delete[] raw_temp;
        if (count_remain) delete [] count_remain;
        #endif

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
        results = new int16_t[device_count];
        #ifdef DEBUG_TEMP
        raw_temp = new uint16_t[device_count];
        count_remain = new uint16_t[device_count];
        #endif
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

        switch_state(State::START_CONVERSION);
        break;
    }
    case State::START_CONVERSION: {
        ds.reset();
        ds.skip();
        ds.write(0x44); // Start temperature conversion

        switch_state(State::WAIT_CONVERSION, 500);
        break;
    }
    case State::WAIT_CONVERSION: {
        // Check if the conversion is done every 5ms
        if (!ds.read_bit())
            return switch_state(state, 5);

        current_device = 0;
        crc_error_timeout = 0;
        switch_state(State::READ);
        break;
    }
    case State::READ: {
        if (current_device >= device_count) {
            last_read_millis = millis();
            switch_state(State::START_CONVERSION);
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

        #ifdef DEBUG_TEMP
        raw_temp[current_device] = raw;
        #endif

        if (addr[0] == 0x10) {
            // DS18S20 or old DS1820 returns temperature in 1/128 degrees
            // Note that count_per_c register data[7] is not hardcoded to 16 for legacy DC1820 as stated
            // in http://myarduinotoy.blogspot.com/2013/02/12bit-result-from-ds18s20.html
            // byte 6: DS18S20: COUNT_REMAIN
            // byte 7: DS18S20: COUNT_PER_C
            // 	                                  COUNT_PER_C - COUNT_REMAIN
            //     TEMPERATURE = TEMP_READ - 0.25 + --------------------------
            //                                      COUNT_PER_C
            // and usually ranges from 78 to 108. Thefore, we multiply by 128 in order
            // to get sufficient precission. Similarly as in
            // https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/master/DallasTemperature.cpp
            // https://github.com/milesburton/Arduino-Temperature-Control-Library/blob/65112b562fd37af68ed113c9a3925c09c4529e14/DallasTemperature.cpp#L712

            if (data[7] == 0) {
                raw = raw << 6;
                TC_PRINTLN("COUNT_PER_C=0");
            } else {
                int16_t  dt = 128*(data[7]-data[6]); // multiply by 128

                #ifdef DEBUG_TEMP
                count_remain[current_device] = (data[7] << 8) | data[6];
                #endif

                dt /= data[7];

                #if 0
                if (dt > 128) {
                    TC_PRINT_START();
                    Serial.print("DT > 1 occured! COUNT_REMAIN=");
                    Serial.print(data[6], DEC);
                    Serial.print(" COUNT_PER_C=");
                    Serial.print(data[7], DEC);
                    Serial.print(" temp raw = ");
                    Serial.print(raw, DEC);
                    Serial.print(" dt = ");
                    Serial.print(dt, DEC);
                    TC_PRINT_END();
                }
                #endif

                raw = 64*(raw&0xFFFE) - 32 + dt; // 0.5*128=64 == (1<<6); 0.25*128=32

                #if 0
                if ((raw & 0x007F) == 97) {
                    TC_PRINT_START();
                    Serial.print("Measurement ?.76 occured! COUNT_REMAIN=");
                    Serial.print(data[6], DEC);
                    Serial.print(" COUNT_PER_C=");
                    Serial.print(data[7], DEC);
                    Serial.print(" temp raw = ");
                    Serial.print(raw, DEC);
                    Serial.print(" dt = ");
                    Serial.print(dt, DEC);
                    TC_PRINT_END();
                }

                if ( data[6] > data[7]) {
                    TC_PRINT_START();
                    Serial.print("Measurement data[6] > data[7] occured! COUNT_REMAIN=");
                    Serial.print(data[6], DEC);
                    Serial.print(" COUNT_PER_C=");
                    Serial.print(data[7], DEC);
                    TC_PRINT_END();
                }
                #endif
            }
        } else {
            byte cfg = (data[4] & 0x60);
            if      (cfg == 0x00) raw = raw & ~7; // 9  bit res, 93.75 ms
            else if (cfg == 0x20) raw = raw & ~3; // 10 bit res, 187.5 ms
            else if (cfg == 0x40) raw = raw & ~1; // 11 bit res, 375 ms
            raw = raw << 3; // multiply by 8
        }
        results[current_device] = raw;

        crc_error_timeout = 0;
        current_device++;
        break;
    }
    case State::WAIT_SWITCH_STATE: {
        next_state.process_wait(state);
        break;
    }
    default:
        state = State::SETUP;
    }
}

#endif // __TEMP_CONTROLLER_CPP__
