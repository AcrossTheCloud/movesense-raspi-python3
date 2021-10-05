import os
import sys
import time
from datetime import datetime
import argparse
import configparser
import requests
import io
import struct
import json
from bleAdvScanner import bleAdvScanner
import uuid
import base64
import datetime
import json
import copy
from multiprocessing.dummy import Pool

pool = Pool(2)
global dataArray

def myCommandCallback(cmd):
    print('Got:', cmd.event, cmd, type(cmd))
    #payload = json.loads(cmd.payload)
    #command = payload["command"]
    #print command

def on_success(r):
    if r.status_code == 200:
        print(r)
    else:
        # Add handling for unsuccessful post here
        print(r)

def callback(raw):
    global dataArray

    payload=deserialized_data(raw)
    dataArray.append(payload)
    dataArray2 = []

    if len(dataArray) > 10:
        dataArray2 = copy.deepcopy(dataArray)
        dataArray = []

    if len(dataArray2) > 10:
        #accesscode = "?apikey"
        #url = 'service url here'+code
        #pool.apply_async(requests.post, args=[url], kwds={'json':{"messages": dataArray2}}, callback=on_success)
        print(dataArray2)
        print("send")

def deserialized_data(pkt):

    #rolling packet count for detecting missing packets
    packet_count = (pkt[26])
    #MAC address
    bt_addr = ''.join("{0:02x}".format(x) for x in pkt[12:6:-1])
    #exctact float from the adv packet
    float1 = struct.unpack(">f", (pkt[37:33:-1]))
    #1-wire temperature
    float1_val = float1[0]
    #extract float from the adv packet
    float2 = struct.unpack(">f", (pkt[33:29:-1]))
    #activity level integer
    float2_val = float2[0]
    #extract float from the adv packet
    float3 = struct.unpack(">f", (pkt[41:37:-1]))
    #device temperature in celcius
    float3_val = float3[0]
    #rssi info
    rssi = (pkt[-1])

    ts = (datetime.datetime.now()-datetime.datetime(1970,1,1)).total_seconds()
    payload_data = {"deviceMac":bt_addr,
                    "counter":packet_count,
                    "float1":float1_val,
                    "float2":float2_val,
                    "float3":float3_val,
                    "Timestamp":ts}
    return payload_data


def _main(argv):
    global dataArray
    dataArray = []
    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--settingsFile', default='/etc/blegateway.cnf', help='Settings file (overrides all other parameters)')
    args = parser.parse_args()

    if args.settingsFile and os.path.exists(args.settingsFile):
        config = configparser.ConfigParser()
        config.read(args.settingsFile)

    else:
        print('Cannot read settings from "%s"' % args.settingsFile)
        return

    bthci = config['bluetooth']['hci']

    scanner = bleAdvScanner(callback, bt_device_id=int(bthci))
    scanner.start()

    # Run forever
    while (1):
        time.sleep(60)
        print("I'm alive")

    scanner.stop()
    db.close()

if __name__ == '__main__':
    _main(sys.argv[1:])

