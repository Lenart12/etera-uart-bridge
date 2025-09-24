import asyncio
import ctypes
from enum import Enum
import inspect
import time

import serial

from .MotorQueue import MotorQueue
from .MotorQueueCommand import MotorQueueCommand
from .RelayQueue import RelayQueue
from .TemperatureQueue import TemperatureQueue


class EteraUartBridge:
    _s: serial.Serial

    _motor_queue: list[MotorQueue]
    _motor_queue_lock: list[asyncio.Lock]

    _relay_queue: RelayQueue
    _relay_queue_lock: asyncio.Lock

    _temperature_queue: TemperatureQueue
    _temperature_queue_lock: asyncio.Lock

    Direction = MotorQueueCommand.Direction

    class DeviceException(Exception):
        pass

    class _ParseState(Enum):
        WAIT_READY = 0
        DEVICE_RESET = 1
        IDLE = 2
        READ_ASCII = 3

    _parse_state: _ParseState
    _before_read_state: _ParseState
    _current_read: bytes
    _command_read_buffer: bytes
    _running: bool
    _device_ready: asyncio.Event

    _temp_sensors: list[bytes]
    _temp_sensors_lock: asyncio.Lock

    _on_device_message_handler: callable
    _on_device_reset_handler: callable

    def __init__(
        self, serial_port: str, on_device_message_handler: callable = print, on_device_reset_handler: callable = None
    ):
        self._debug_capture = None  # open('/tmp/etera_debug.hex', 'ab')
        self._debug_lock = asyncio.Lock()
        self._debug_last_channel = None
        asyncio.create_task(self._debug_message(f'EteraUartBridge starting {serial_port}'))

        self._s = serial.Serial(port=serial_port, baudrate=115200, timeout=0.5)

        asyncio.create_task(self._debug_message(f'EteraUartBridge opened {serial_port}'))

        self._motor_queue = [MotorQueue() for _ in range(4)]
        self._motor_queue_lock = [asyncio.Lock() for _ in range(4)]

        self._relay_queue = RelayQueue()
        self._relay_queue_lock = asyncio.Lock()

        self._temperature_queue = TemperatureQueue()
        self._temperature_queue_lock = asyncio.Lock()

        self._parse_state = self._ParseState.WAIT_READY
        self._before_read_state = self._parse_state
        self._current_read = b''
        self._command_read_buffer = b''
        self._running = False
        self._device_ready = asyncio.Event()

        self._temp_sensors = []
        self._temp_sensors_lock = asyncio.Lock()

        self.set_device_message_handler(on_device_message_handler)
        self.set_device_reset_handler(on_device_reset_handler)

    async def _write_debug(self, channel: str, data: bytes):
        if self._debug_capture is None:
            return
        async with self._debug_lock:
            if self._debug_last_channel != channel:
                if self._debug_last_channel is not None and self._debug_last_channel != 'M':
                    self._debug_capture.write(b'\n')
                self._debug_capture.write(f'[{channel}] '.encode())
                self._debug_last_channel = channel
            elif channel != 'M':
                self._debug_capture.write(b' ')
            self._debug_capture.write(data)

    async def _write_debug_hex(self, channel: str, data: bytes):
        await self._write_debug(channel, data.hex().upper().encode('utf-8'))

    async def _debug_s_read(self, data: bytes):
        await self._write_debug_hex('R', data)

    async def _debug_buf_read(self, data: bytes):
        await self._write_debug_hex('BR', data)

    async def _debug_s_write(self, data: bytes):
        await self._write_debug_hex('W', data)

    async def _debug_buf_write(self, data: bytes):
        await self._write_debug_hex('BW', data)

    async def _debug_message(self, msg: str):
        if self._debug_capture is None:
            return
        await self._write_debug('M', f'{time.strftime("%b %d %H:%M:%S")} - {msg}\n'.encode())
        self._debug_capture.flush()

    async def ready(self):
        await self._device_ready.wait()

    async def move_motor(
        self, motor_id: int, direction: MotorQueueCommand.Direction, length_ms: int, override: bool = False
    ):
        if motor_id > 3 or motor_id < 0:
            raise ValueError('Motor ID must be between 0 and 3.')

        if length_ms < 0:
            raise ValueError('Length must be non-negative.')

        if not self._device_ready.is_set():
            await self._debug_message(f'Move motor - device is not ready. (state={self._parse_state})')
            raise self.DeviceException('Device is not ready.')

        await self._debug_message(
            f'Move motor {motor_id} - direction={direction}, length_ms={length_ms}, override={override}'
        )

        move_commands = []

        async with self._motor_queue_lock[motor_id]:
            if override:
                self._motor_queue[motor_id].clear_queue()

            while length_ms > 65535:
                move_commands.append(self._motor_queue[motor_id].add_command(direction, 65535))
                length_ms -= 65535

            if length_ms > 0:
                move_commands.append(self._motor_queue[motor_id].add_command(direction, length_ms))

        for i, command in enumerate(move_commands):
            await command.finished.wait()
            if not command.successful:
                await self._debug_message(f'Move motor {motor_id} - failed. (seq. {i}/{len(move_commands)})')
                raise self.DeviceException(f'Failed to fully move motor (seq. {i}/{len(move_commands)}).')

        await self._debug_message(f'Move motor {motor_id} - finished. (seq. {len(move_commands)})')

    async def set_relay(self, relay_id: int, state: bool):
        if relay_id > 7 or relay_id < 0:
            raise ValueError('Relay ID must be between 0 and 7.')

        if not self._device_ready.is_set():
            await self._debug_message(f'Set relay - device is not ready. (state={self._parse_state})')
            raise self.DeviceException('Device is not ready.')

        await self._debug_message(f'Set relay {relay_id} - state={state}')

        async with self._relay_queue_lock:
            command = self._relay_queue.add_command(relay_id, state)

        await command.finished.wait()
        if not command.successful:
            await self._debug_message(f'Set relay {relay_id} - failed.')
            raise self.DeviceException('Failed to switch relay.')
        await self._debug_message(f'Set relay {relay_id} - finished.')

    async def get_sensors(self):
        if not self._device_ready.is_set():
            await self._debug_message(f'Get sensors - device is not ready. (state={self._parse_state})')
            raise self.DeviceException('Device is not ready.')

        await self._debug_message(f'Get sensors - count={len(self._temp_sensors)}')

        async with self._temp_sensors_lock:
            sensors = [a[:] for a in self._temp_sensors]
            await self._debug_message('Get sensors - finished.')
            return sensors

    _invalid_temperature = ctypes.c_int16.from_buffer_copy(b'\xff\x7f').value / 128.0

    async def get_temperatures(self):
        if not self._device_ready.is_set():
            await self._debug_message(f'Get temperatures - device is not ready. (state={self._parse_state})')
            raise self.DeviceException('Device is not ready.')

        await self._debug_message(f'Get temperatures - count={len(self._temp_sensors)}')

        async with self._temperature_queue_lock:
            command = self._temperature_queue.add_command()

        await command.finished.wait()
        if not command.successful:
            await self._debug_message('Get temperatures - failed.')
            raise self.DeviceException('Failed to get temperature.')
        if any(temp == self._invalid_temperature for temp in command.temperatures):
            await self._debug_message('Get temperatures - invalid temperature received.')
            raise self.DeviceException('Invalid temperature received.')
        await self._debug_message('Get temperatures - finished.')
        return command.temperatures

    async def run_forever(self):
        if self._running:
            await self._debug_message(f'EteraUartBridge is already running on {self._s.port}')
            raise self.DeviceException(f'EteraUartBridge is already running on {self._s.port}')

        self._running = True

        while True:
            while self._s.in_waiting != 0 or len(self._command_read_buffer) != 0:
                c = None
                if len(self._command_read_buffer) > 0:
                    c = self._command_read_buffer[0:1]
                    self._command_read_buffer = self._command_read_buffer[1:]
                    await self._debug_buf_read(c)
                else:
                    c = self._s.read(1)
                    await self._debug_s_read(c)

                match c:
                    # Device ready!
                    case b'\xe0':
                        await self._debug_message(f'Device is ready (state={self._parse_state})')
                        await self._device_message(
                            f'Device is ready on {self._s.port} (state={self._parse_state})'.encode()
                        )
                        if (
                            self._parse_state not in [self._ParseState.WAIT_READY, self._ParseState.DEVICE_RESET]
                            and self._on_device_reset_handler is not None
                        ):
                            asyncio.create_task(self._on_device_reset_handler())
                        await self._init()
                    case b'\xe1':
                        await self._debug_message(f'Device is resetting (state={self._parse_state})')
                        await self._device_message(f'Device reset unexpectedly in state {self._parse_state}'.encode())
                        await self._reset_device()
                    # Start of ASCII message
                    case b'\xea':
                        await self._debug_message('Ascii start ->')
                        if self._current_read != b'':
                            await self._device_message(self._current_read)
                        self._current_read = b''
                        self._before_read_state = self._parse_state
                        self._parse_state = self._ParseState.READ_ASCII
                    # End of ASCII message
                    case b'\xeb':
                        await self._debug_message(f'Ascii end: {self._current_read}')
                        if self._parse_state != self._ParseState.READ_ASCII:
                            await self._debug_message(f'End of ASCII message in state {self._parse_state}')
                            await self._device_message(
                                'Device reached end of ASCII message in state '
                                f'{self._parse_state} and will try to reset'.encode()
                            )
                            await self._reset_device()
                        self._parse_state = self._before_read_state
                        await self._device_message(self._current_read)
                        self._current_read = b''
                    # Other
                    case _:
                        if self._parse_state == self._ParseState.READ_ASCII:
                            self._current_read += c
                        else:
                            # Stop moving motor
                            if c[0] & 0b11111000 == 0b11010000:
                                motor_id = c[0] & 0b0000011
                                await self._debug_message(f'Motor {motor_id} stopped (state={self._parse_state})')
                                async with self._motor_queue_lock[motor_id]:
                                    command = self._motor_queue[motor_id].get_next_command()
                                    if command is not None:
                                        command.finished.set()
                            else:
                                await self._write_debug('R', b'!')
                                await self._debug_message(f'Unknown input (state={self._parse_state})')
                                await self._device_message(
                                    f'Device reached unknown input `{c}` in state '
                                    f'{self._parse_state} and will try to reset'.encode()
                                )
                                await self._reset_device()

            if self._parse_state == self._ParseState.IDLE:
                # Process motor queue
                for i in range(4):
                    async with self._motor_queue_lock[i]:
                        if not self._motor_queue[i].is_empty():
                            command = self._motor_queue[i].peek_next_command()
                            if not command.started:
                                command.started = True
                                cmd_bytes = command.to_bytes(i)
                                command.successful = await self._send_command(cmd_bytes)
                                if not command.successful:
                                    command.finished.set()

                # Process relay queue
                async with self._relay_queue_lock:
                    if not self._relay_queue.is_empty():
                        command = self._relay_queue.get_next_command()
                        cmd_bytes = command.to_bytes()
                        command.successful = await self._send_command(cmd_bytes)
                        command.finished.set()

                # Process temperature queue
                async with self._temperature_queue_lock:
                    if not self._temperature_queue.is_empty():
                        command = self._temperature_queue.get_next_command()
                        cmd_bytes = command.to_bytes()
                        command.successful = await self._send_command(cmd_bytes)
                        async with self._temp_sensors_lock:
                            for _ in range(len(self._temp_sensors)):
                                c = self._s.read(2)
                                await self._debug_s_read(c)
                                if len(c) != 2:
                                    command.successful = False
                                    break
                                command.temperatures.append(ctypes.c_int16.from_buffer_copy(c).value / 128.0)

                        command.finished.set()

            await asyncio.sleep(0.05)

    def set_device_reset_handler(self, handler: callable):
        if inspect.iscoroutinefunction(handler) or handler is None:
            self._on_device_reset_handler = handler
        else:

            async def async_handler():
                handler()

            self._on_device_reset_handler = async_handler

    def set_device_message_handler(self, handler: callable):
        if inspect.iscoroutinefunction(handler) or handler is None:
            self._on_device_message_handler = handler
        else:

            async def async_handler(message):
                handler(message)

            self._on_device_message_handler = async_handler

    async def _init(self):
        await self._debug_message(f'Initializing device (state={self._parse_state})')
        self._device_ready.clear()

        if not await self._send_command(b'c'):
            raise self.DeviceException('Failed to send get temperature count command')
        c = self._s.read(1)
        await self._debug_s_read(c)
        if len(c) != 1:
            raise self.DeviceException('Failed to get temperature count')

        temp_sensor_count = ctypes.c_uint8.from_buffer_copy(c).value

        if not await self._send_command(b'a'):
            raise self.DeviceException('Failed to send get temperature sensors command')
        async with self._temp_sensors_lock:
            self._temp_sensors.clear()
            for _ in range(temp_sensor_count):
                c = self._s.read(8)
                await self._debug_s_read(c)
                if len(c) != 8:
                    raise self.DeviceException('Failed to get temperature sensors')
                self._temp_sensors.append(c)

        self._parse_state = self._ParseState.IDLE
        self._device_ready.set()
        await self._debug_message(f'Device initialized (state={self._parse_state})')

    async def _reset_device(self):
        await self._debug_message(f'Resetting device (state={self._parse_state})')
        self._device_ready.clear()
        if self._parse_state != self._ParseState.WAIT_READY and self._on_device_reset_handler is not None:
            asyncio.create_task(self._on_device_reset_handler())
        self._command_read_buffer = b''
        self._parse_state = self._ParseState.DEVICE_RESET
        self._s.close()
        for i in range(4):
            await self._debug_message(f'Clearing motor {i} queue')
            async with self._motor_queue_lock[i]:
                self._motor_queue[i].clear_queue()
        async with self._relay_queue_lock:
            await self._debug_message('Clearing relay queue')
            self._relay_queue.clear_queue()
        async with self._temperature_queue_lock:
            await self._debug_message('Clearing temperature queue')
            self._temperature_queue.clear_queue()
        self._s = serial.Serial(port=self._s.port, baudrate=self._s.baudrate, timeout=self._s.timeout)
        await self._debug_message(f'EteraUartBridge re-opened {self._s.port}')
        await self._debug_message(f'Device reset (state={self._parse_state})')

    async def _send_command(self, command: bytes, expected_byte: bytes | None = None):
        if expected_byte is None:
            expected_byte = command[0:1]

        # Read any pending data before sending the command
        if self._s.in_waiting != 0:
            c = self._s.read(self._s.in_waiting)
            await self._debug_s_read(c)
            await self._debug_buf_write(c)
            self._command_read_buffer += c

        for _retries in range(3):
            self._s.write(command)
            await self._debug_s_write(command)
            # print(f"Sending command {command}")
            if await self._confirm_command(expected_byte):
                # print(f"Command {command} successful")
                return True
        # print(f"Command {command} failed")
        await self._debug_message(f'Failed to send command {command} (state={self._parse_state})')
        return False

    async def _confirm_command(self, expected_byte: bytes):
        assert len(expected_byte) == 1
        while True:
            c = self._s.read(1)
            await self._debug_s_read(c)

            if len(c) == 0:
                await self._debug_message(f'Failed to read confirmation byte (state={self._parse_state})')
                return False

            if c == expected_byte:
                await self._write_debug('R', b'*')
                return True
            else:
                await self._debug_buf_write(c)
                self._command_read_buffer += c

    async def _device_message(self, message: bytes):
        if self._on_device_message_handler is not None:
            asyncio.create_task(self._on_device_message_handler(message))
