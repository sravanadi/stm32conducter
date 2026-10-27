#!/usr/bin/env python3
"""
RS-485 Communication & Diagnostics Tool for Mirrant TDS 4002
Hardware: STM32G030K8T6 on USART1 (PB6=TX, PB7=RX, PB5=RE/DE)
Adapter:  FTDI USB-to-RS485 on COM6
"""

import sys
import time
import argparse
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[ERROR] 'pyserial' is required. Run: pip install pyserial")
    sys.exit(1)


def calc_crc16_modbus(data: bytes) -> int:
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x0001:
                crc = (crc >> 1) ^ 0xA001
            else:
                crc >>= 1
    return crc


def build_modbus_read(slave_id=1, start_reg=0, num_regs=2) -> bytes:
    pdu = bytes([slave_id, 0x04, (start_reg >> 8) & 0xFF, start_reg & 0xFF, (num_regs >> 8) & 0xFF, num_regs & 0xFF])
    crc = calc_crc16_modbus(pdu)
    return pdu + bytes([crc & 0xFF, (crc >> 8) & 0xFF])


def read_until_idle(ser, max_wait=4.0, idle_time=0.35) -> bytes:
    """Reads incoming data from RS-485 bus until line goes silent for idle_time, or max_wait expires."""
    buf = b""
    t_start = time.time()
    last_byte_time = None

    while time.time() - t_start < max_wait:
        n = ser.in_waiting
        if n > 0:
            chunk = ser.read(n)
            if chunk:
                buf += chunk
                last_byte_time = time.time()
        else:
            if last_byte_time and (time.time() - last_byte_time) >= idle_time:
                break
            time.sleep(0.02)
    return buf


def run_diagnostics(port="COM6", baud=9600):
    print("=" * 70)
    print(f" MIRRANT TDS 4002 - RS-485 INTERFACE DIAGNOSTICS")
    print(f" Port: {port} | Baud: {baud} (8-N-1)")
    print("=" * 70)

    try:
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.1, write_timeout=1.0)
    except Exception as e:
        print(f"[ERROR] Could not open port {port}: {e}")
        return

    print(f"[OK] Port {port} opened successfully.")
    print("Flushing buffers and checking bus silence...")
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    time.sleep(0.2)

    print("\n--- TEST 1: Sending ASCII Diagnostic Command ('?') ---")
    print("Sending: '?' (0x3F) to trigger meter diagnostic banner...")
    ser.reset_input_buffer()
    ser.write(b"?\r\n")
    ser.flush()
    resp = read_until_idle(ser, max_wait=2.0, idle_time=0.3)
    if resp:
        print(f"[SUCCESS] Received {len(resp)} bytes from meter!")
        print("Raw text response:\n" + resp.decode("utf-8", errors="replace").strip())
    else:
        print("[NO RESPONSE] Meter did not respond to ASCII '?' command.")

    time.sleep(0.15)  # Half-duplex turnaround delay

    print("\n--- TEST 2: Sending Live Math Calculation Command ('m') ---")
    print("Sending: 'm' (0x6D) to trigger 5-step electrochemical calculation breakdown...")
    ser.reset_input_buffer()
    ser.write(b"m\r\n")
    ser.flush()
    # At 9600 baud, 2500 bytes takes ~2.6 seconds. Wait up to 5.0 seconds until idle.
    resp = read_until_idle(ser, max_wait=5.0, idle_time=0.4)
    if resp:
        print(f"[SUCCESS] Received {len(resp)} bytes math breakdown from meter!")
        print(resp.decode("utf-8", errors="replace").strip())
    else:
        print("[NO RESPONSE] Meter did not respond to 'm' command.")

    time.sleep(0.15)  # Half-duplex turnaround delay

    print("\n--- TEST 3: Sending Modbus RTU Read Registers Request ---")
    req = build_modbus_read(slave_id=1, start_reg=0, num_regs=2)
    print(f"Sending Modbus Frame: {req.hex(' ').upper()} (Read Regs 0-1, FC=04, Slave=1)...")
    ser.reset_input_buffer()
    ser.write(req)
    ser.flush()
    time.sleep(0.1)
    resp = read_until_idle(ser, max_wait=1.0, idle_time=0.2)
    if resp:
        print(f"[SUCCESS] Received Modbus Response ({len(resp)} bytes): {resp.hex(' ').upper()}")
    else:
        print("[NO RESPONSE] Meter did not respond to Modbus RTU query.")

    time.sleep(0.1)

    print("\n--- TEST 4: Passive Raw Bus Listener (Listening for 5 seconds) ---")
    print("The meter is programmed to auto-stream telemetry every 200 ms.")
    print("Listening for any raw bytes on the RS-485 bus...")
    t0 = time.time()
    total_bytes = 0
    all_data = b""
    while time.time() - t0 < 10.0:
        b = ser.read(ser.in_waiting or 1)
        if b:
            total_bytes += len(b)
            all_data += b
            sys.stdout.write(b.decode("utf-8", errors="replace"))
            sys.stdout.flush()

    if total_bytes > 0:
        print(f"\n[SUCCESS] Captured {total_bytes} bytes from RS-485 stream!")
    else:
        print("\n[RESULT] 0 bytes captured during the 5-second listen.")
        print("\n" + "=" * 70)
        print(" TROUBLESHOOTING CHECKLIST:")
        print("=" * 70)
        print(" 1. RS-485 WIRING POLARITY (MOST COMMON):")
        print("    Try SWAPPING the two data wires:")
        print("    - Swap Adapter A and Adapter B on the meter terminal block.")
        print("    - If A and B are inverted, communication is completely blocked.")
        print(" 2. COMMON GROUND:")
        print("    - Ensure Adapter GND is connected to Meter J3 Header Pin 4 (GND).")
        print(" 3. METER POWER:")
        print("    - Ensure the meter board is powered ON (LCD is lit and running).")
        print(" 4. FIRMWARE FLASH:")
        print("    - Ensure latest firmware has been flashed to the STM32.")
        print("=" * 70)

    ser.close()
    print("\n[OK] Port closed.")


def interactive_mode(port="COM6", baud=9600):
    print("=" * 70)
    print(f" RS-485 INTERACTIVE SENDER & RECEIVER ({port} @ {baud})")
    print(" Enter any text or command to send to the meter:")
    print("   '?'     : Send Help request")
    print("   'm'     : Print Step-by-Step Mathematical Calculation breakdown")
    print("   'd'     : Request live reading dump")
    print("   'c'     : View meter configuration & settings")
    print("   's'     : Toggle streaming on/off")
    print("   'mb'    : Send Modbus query (Slave 1, FC 04, Regs 0-1)")
    print("   'exit'  : Quit")
    print("=" * 70)

    try:
        ser = serial.Serial(port=port, baudrate=baud, timeout=0.5)
    except Exception as e:
        print(f"[ERROR] Could not open {port}: {e}")
        return

    try:
        while True:
            cmd = input("\nEnter command to send: ").strip()
            if not cmd:
                continue
            if cmd.lower() in ["exit", "quit", "q"]:
                break

            # Flush any stale incoming data from buffer before sending command
            ser.reset_input_buffer()

            if cmd.lower() == "mb":
                req = build_modbus_read(slave_id=1, start_reg=0, num_regs=2)
                print(f"TX (Modbus Hex): {req.hex(' ').upper()}")
                ser.write(req)
            else:
                tx_data = (cmd + "\r\n").encode("utf-8")
                print(f"TX (ASCII)     : {repr(cmd)}")
                ser.write(tx_data)
            ser.flush()

            # Wait and read reply
            time.sleep(0.3)
            reply = b""
            t0 = time.time()
            while time.time() - t0 < 2.5:
                chunk = ser.read(ser.in_waiting or 1)
                if chunk:
                    reply += chunk
                else:
                    if reply:
                        break

            if reply:
                print(f"\n[RX - {len(reply)} bytes]:")
                text = reply.decode("utf-8", errors="replace").strip()
                print(text)
            else:
                print("RX (0 bytes): No response from meter.")
    except KeyboardInterrupt:
        print("\nExiting...")
    finally:
        ser.close()


def main():
    parser = argparse.ArgumentParser(description="Mirrant RS-485 Diagnostic & Transfer Tool")
    parser.add_argument("-p", "--port", default="COM6", help="Serial port (default: COM6)")
    parser.add_argument("-b", "--baud", type=int, default=9600, help="Baud rate (default: 9600)")
    parser.add_argument("-i", "--interactive", action="store_true", help="Launch interactive send/receive mode")
    args = parser.parse_args()

    if args.interactive:
        interactive_mode(args.port, args.baud)
    else:
        run_diagnostics(args.port, args.baud)


if __name__ == "__main__":
    main()