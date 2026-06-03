"""Standalone BPIO2 connectivity probe — confirms the SDK can talk to the BP6
on a Windows COM port before we patch the MCP's device enumeration.

Run with the MCP venv python so deps (pyserial, cobs, flatbuffers) resolve:
  .venv\Scripts\python.exe bp_probe.py COM9
"""
import os
import sys

VENDOR = r"C:\Users\stig\Documents\GitHub\buspirate-mcp\vendor\bpio2\python"
sys.path.insert(0, VENDOR)

from pybpio.bpio_client import BPIOClient  # noqa: E402

port = sys.argv[1] if len(sys.argv) > 1 else "COM9"
print(f"Probing {port} ...")
client = BPIOClient(port)
st = client.status_request()
client.close()
if not st:
    print("NO STATUS — wrong port or not in BPIO2 mode")
    sys.exit(1)
print("OK — BPIO2 responded")
print(f"  fw {st.get('version_firmware_major')}.{st.get('version_firmware_minor')} "
      f"git {st.get('version_firmware_git_hash')} {st.get('version_firmware_date')}")
print(f"  hw {st.get('version_hardware_major')} rev {st.get('version_hardware_minor')}")
print(f"  current mode: {st.get('mode_current')}")
print(f"  modes available: {st.get('modes_available')}")
print(f"  ADC mV: {st.get('adc_mv')}")
