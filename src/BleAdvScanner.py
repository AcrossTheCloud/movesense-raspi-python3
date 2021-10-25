"""
 Bluetooth BLE advertisement message scanner.

 Scanner uses BlueZ stack (Official Linux Bluetooth protocol stack).
 http://www.bluez.org/
 
 Scanner bases to examples took from beacontools by Citruz.
 https://github.com/citruz/beacontools
"""
import threading
from importlib import import_module
import array
from binascii import hexlify
import struct
import time
import sys


import asyncio
import sys
import concurrent.futures

from bleak import BleakScanner

COMPANY_ID = 159
packagesProcessed = {}


executor = concurrent.futures.ThreadPoolExecutor(max_workers=10)

def detection_callback(device, advertisement_data):
    #print(device.address, "RSSI:", device.rssi, advertisement_data)
    # make async call to given async callback function
    asyncio.get_running_loop().create_task(BleAdvScanner.callback_func(device.address, device.rssi, advertisement_data.manufacturer_data))

class BleAdvScanner(object):
    callback_func = None

    def __init__(self, callback, bt_device_id=0):
        self.scanner = BleakScanner()
        self.scanner.register_detection_callback(detection_callback)
        BleAdvScanner.callback_func = callback
        self.loop = asyncio.get_event_loop()

    async def start(self):
        await self.scanner.start()


    async def stop(self):
        self.scanner.stop()


