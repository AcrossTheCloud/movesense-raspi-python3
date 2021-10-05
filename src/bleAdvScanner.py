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

LE_META_EVENT = 0x3e
OGF_LE_CTL = 0x08
OCF_LE_SET_SCAN_ENABLE = 0x000C
EVT_LE_ADVERTISING_REPORT = 0x02
MOVESENSE_MAC_START = b'\xdc\x8c\x0c'
COMPANY_ID = b'\x9f\x00'
packagesProcessed = {}

class bleAdvScanner(object):
    def __init__(self, callback, bt_device_id=0):
        self._mon = Monitor(callback, bt_device_id)

    def start(self):
        self._mon.start()

    def stop(self):
        self._mon.terminate()

class Monitor(threading.Thread):
    def __init__(self, callback, bt_device_id):
        self.bluez = import_module('bluetooth._bluetooth')
        threading.Thread.__init__(self)
        self.daemon = False
        self.keep_going = True
        self.callback = callback
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
                        if pkt[10:13] == MOVESENSE_MAC_START and pkt[23:25] == COMPANY_ID:
                            bt_addr = _bt_addr_to_string(pkt[7:13])
                            if bt_addr in packagesProcessed and packagesProcessed[bt_addr] == pkt[13:-2]:
                                # Packet already handled
                                pass
                            else:
                                packagesProcessed[bt_addr] = pkt[13:-2]
                                self.callback(pkt)
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

def _bt_addr_to_string(addr):
        """Convert a binary string to the hex representation."""
        addr_str = array.array('B', addr)
        addr_str.reverse()
        hex_str = hexlify(addr_str.tostring()).decode('ascii')
        # insert ":" seperator between the bytes
        return ':'.join(a+b for a, b in zip(hex_str[::2], hex_str[1::2]))

def _data_to_hexstring(data):
        """Convert an array of binary data to the hex representation as a string."""
        return hexlify(_data_to_binstring(data)).decode('ascii')

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

def _data_to_binstring(data):
    """Convert an array of binary data to a binary string."""
    return array.array('B', data).tostring()
