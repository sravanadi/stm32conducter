#!/usr/bin/env python3
"""
Mirrant TDS 4002 - Real-Time Serial Monitor & Data Logger
Hardware: STM32G030K8T6 on USART1 (PB6=TX, PB7=RX, PB5=RS485 DE/RE)
Ground/Debug: J3 Header (Pin 4 = GND)
"""

import sys
import time
import argparse
from datetime import datetime

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    print("[!] 'pyserial' package is not installed.")
    print("    To use the Python monitor, run: pip install pyserial")
    print("    Alternatively, run 'serial_monitor.bat' directly which has zero dependencies!")
    sys.exit(1)

def discover_ports():
    ports = list(serial.tools.list_ports.comports())
    return ports

def main():
    parser = argparse.ArgumentParser(description="Mirrant TDS 4002 Serial Monitor")
    parser.add_argument("-p", "--port", help="Serial COM port (e.g. COM3)")
    parser.add_argument("-b", "--baud", type=int, default=9600, help="Baud rate (default: 9600)")
    parser.add_argument("-l", "--log", help="Optional CSV/Text log filename")
    args = parser.parse_args()

    port = args.port
    if not port:
        ports = discover_ports()
        if not ports:
            print("[!] No COM ports detected. Please connect your USB-to-RS485 adapter.")
            port = input("Enter COM port manually (e.g. COM3): ").strip()
        elif len(ports) == 1:
            port = ports[0].device
            print(f"[*] Auto-selected port: {port} ({ports[0].description})")
        else:
            print("Available Serial Ports:")
            for idx, p in enumerate(ports):
                print(f"  [{idx + 1}] {p.device} - {p.description}")
            choice = input(f"Select port (1-{len(ports)}, default 1): ").strip()
            if choice.isdigit() and 1 <= int(choice) <= len(ports):
                port = ports[int(choice) - 1].device
            elif choice:
                port = choice.upper()
            else:
                port = ports[0].device

    print("=" * 70)
    print(f" Connecting to {port} @ {args.baud} bps (8-N-1)...")
    print(" RS-485 Lines : PB6 (TX), PB7 (RX), PB5 (DE/RE)")
    print(" Common Ground: J3 Header Pin 4 (GND)")
    print(" Press Ctrl+C to exit")
    print("=" * 70)

    log_file = None
    if args.log:
        log_file = open(args.log, "a", encoding="utf-8")
        print(f"[*] Logging data to {args.log}")

    try:
        ser = serial.Serial(port=port, baudrate=args.baud, timeout=1.0)
    except Exception as e:
        print(f"[ERROR] Failed to open {port}: {e}")
        return 1

    try:
        last_heartbeat = time.time()
        waiting_reported = False
        while True:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if line:
                ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
                print(f"[{ts}] {line}")
                if log_file:
                    log_file.write(f"[{ts}] {line}\n")
                    log_file.flush()
                waiting_reported = False
            else:
                now = time.time()
                if now - last_heartbeat >= 3.0:
                    last_heartbeat = now
                    if not waiting_reported:
                        print("[*] Listening on RS-485 bus... (Waiting for data from meter)")
                        print("    Tip: If no data appears, try swapping the A and B wires on the adapter.")
                        waiting_reported = True
                    else:
                        sys.stdout.write(".")
                        sys.stdout.flush()
    except KeyboardInterrupt:
        print("\n[*] Exiting serial monitor...")
    finally:
        ser.close()
        if log_file:
            log_file.close()
        print("[OK] Serial port closed.")

if __name__ == "__main__":
    sys.exit(main() or 0)
