"""
 Bluetooth BLE advertisement message scanner
 uses BlueZ library.
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

packagesProcessed = {}


executor = concurrent.futures.ThreadPoolExecutor(max_workers=10)

def detection_callback(device_addr, device_rssi, manuf_data):
    #print("BlueZ!!!", device_addr, "RSSI:", device_rssi, manuf_data)
    # make async call to given async callback function
    BleAdvScanner_BlueZ.loop.create_task(BleAdvScanner_BlueZ.callback_func(device_addr, device_rssi, manuf_data))

class BleAdvScanner_BlueZ(object):
    callback_func = None
    loop = None
    
    def __init__(self, callback, bt_device_id=0):
        BleAdvScanner_BlueZ.callback_func = callback
        BleAdvScanner_BlueZ.loop = asyncio.get_event_loop()
        self._mon = BlueZMonitor(bt_device_id)


    async def start(self):
        await BleAdvScanner_BlueZ.loop.run_in_executor(None, self._mon.start)


    async def stop(self):
        await BleAdvScanner_BlueZ.loop.run_in_executor(None, self._mon.terminate)


LE_META_EVENT = 0x3e
OGF_LE_CTL = 0x08
OCF_LE_SET_SCAN_ENABLE = 0x000C
EVT_LE_ADVERTISING_REPORT = 0x02
MOVESENSE_MAC_START = b'\xdc\x8c\x0c'
COMPANY_ID = b'\x9f\x00'

class BlueZMonitor(threading.Thread):
    def __init__(self, bt_device_id):
        self.bluez = import_module('bluetooth._bluetooth')
        threading.Thread.__init__(self)
        self.daemon = False
        self.keep_going = True
        self.bt_device_id = bt_device_id # hciN
        self.socket = None

    def run(self):
        while 1:
            try:
                self.socket = self.bluez.hci_open_dev(self.bluez.hci_get_route('%s' % self.bt_device_id))

                filtr = self.bluez.hci_filter_new()
                self.bluez.hci_filter_all_events(filtr)
                self.bluez.hci_filter_set_ptype(filtr, self.bluez.HCI_EVENT_PKT)
                self.socket.setsockopt(self.bluez.SOL_HCI, self.bluez.HCI_FILTER, filtr)
                self.socket.settimeout(15)
                self.toggle_scan(True)

                while self.keep_going:
                    pkt = self.socket.recv(255)
                    event = _to_int(pkt[1])
                    subevent = _to_int(pkt[3])
                    if event == LE_META_EVENT and subevent == EVT_LE_ADVERTISING_REPORT:
                        bt_addr = _bt_addr_to_string(pkt[7:13])
                        #device.address, device.rssi, advertisement_data.manufacturer_data
                        rssi=_bin_to_int(pkt[-1])
                        
                        adv_pkt = pkt[14:-1]
                        adv_data = _parse_adv_packet(adv_pkt)
                        #print("bt_addr:",bt_addr,"rssi:",rssi,"adv_pkt: ", adv_pkt, "adv_data:",adv_data)
                        if 'manuf_data' in adv_data: 
                            BleAdvScanner_BlueZ.loop.run_in_executor(executor, detection_callback, bt_addr, rssi, adv_data['manuf_data'])

            except OSError as e:
                if e.errno == 19:
                    print('hci%d not available, but waiting for it..' % self.bt_device_id)
                    time.sleep(10)
                else:
                    print('Error', sys.exc_info())
                    time.sleep(1)
            except:
                print('Error', sys.exc_info())
                time.sleep(1)

    def toggle_scan(self, enable):
        if enable:
            command = "\x01\x00"
        else:
            command = "\x00\x00"
        self.bluez.hci_send_cmd(self.socket, OGF_LE_CTL, OCF_LE_SET_SCAN_ENABLE, command)

    def terminate(self):
        self.toggle_scan(False)
        self.keep_going = False
        self.join()

# internal helper funcs

def _parse_adv_packet(pkt):
    pos = 0
    data = {}
    while pos < len(pkt):
        chunk_len = _to_int(pkt[pos])
        chunk_type = _to_int(pkt[pos+1])
        chunk_data = pkt[pos+2:pos+1+chunk_len]
        #print("BLE data chunk type:", chunk_type, "len: ", chunk_len, "data: ", chunk_data)
        if chunk_type == 255:
            manuf_id, = struct.unpack("H",chunk_data[0:2])
            manuf_data = chunk_data[2:]
            #print("manuf_id:", manuf_id, "manuf_data: ", manuf_data)
            data['manuf_data'] = {manuf_id:manuf_data}

        pos += chunk_len+1
        
    return data    

def _bt_addr_to_string(addr):
    """Convert a binary string to the hex representation."""
    addr_str = array.array('B', addr)
    addr_str.reverse()
    hex_str = hexlify(addr_str.tobytes()).decode('ascii')
    # insert ":" seperator between the bytes
    return ':'.join(a+b for a, b in zip(hex_str[::2], hex_str[1::2]))

def _to_int(string):
    """Convert a one element byte string to int for python 2 support."""
    if isinstance(string, str):
        return ord(string[0])
    else:
        return string

def _bin_to_int(string):
        """Convert a one element byte string to signed int for python 2 support."""
        if isinstance(string, str):
            return struct.unpack("b", string)[0]
        else:
            return struct.unpack("b", bytes([string]))[0]
