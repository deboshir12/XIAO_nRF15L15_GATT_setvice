import serial
from serial import SerialTimeoutException
import time

port = "/dev/cu.usbmodemE26121F43"
baud = 115200
chunk_size = 64
chunk_delay_s = 0.008

with open("emf_1000_8000_samples.bin", "rb") as f:
    data = f.read()

try:
    with serial.Serial(
        port,
        baudrate=baud,
        timeout=1,
        write_timeout=2,
        rtscts=False,
        dsrdtr=False,
        xonxoff=False,
    ) as ser:
        ser.reset_output_buffer()
        ser.reset_input_buffer()

        # Give macOS/USB-UART a short moment after opening the port.
        time.sleep(0.2)

        written = 0

        for offset in range(0, len(data), chunk_size):
            chunk = data[offset:offset + chunk_size]
            written += ser.write(chunk)
            time.sleep(chunk_delay_s)

            if written % 1024 == 0:
                print(f"sent {written}/{len(data)} data bytes")

except SerialTimeoutException:
    print(f"write timeout after {written}/{len(data)} data bytes")
    raise
else:
    print(f"sent {written}/{len(data)} data bytes")
