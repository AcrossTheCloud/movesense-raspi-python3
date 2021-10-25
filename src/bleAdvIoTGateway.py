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
from DataStore_FirebaseRT import DataStore_FirebaseRT
from BleAdvScanner import BleAdvScanner
import uuid
import base64

import json
import copy
from multiprocessing.dummy import Pool
import asyncio

from BleAdvScanner import COMPANY_ID

pool = Pool(2)
global dataArray
global dataArray2
global lastReceivedPacketNumber
storage = None
data1name = "float1"
data2name = "float2"
data3name = "float3"

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

async def callback(deviceid: str, rssi, manuf_data):
    global dataArray
    global dataArray2
    global lastReceivedPacketNumber
    #print("callback called: ", deviceid, ", rssi: ", rssi)

    payload=deserialized_data(manuf_data)
    if not payload:
        return

    # drop duplicates
    if (deviceid in lastReceivedPacketNumber) and (lastReceivedPacketNumber[deviceid] == payload["counter"]):
        return

    # add deviceId and rssi to payload
    payload["deviceId"]=deviceid
    payload["rssi"]=rssi

    print("payload: ", payload)

    # store last counter value for duplicate rejection:
    lastReceivedPacketNumber[deviceid] = payload["counter"]

    # and append to list
    dataArray.append(payload)
    
    # add to send queue after 10 received packets
    if len(dataArray) >= 10:
        dataArray2.extend(copy.deepcopy(dataArray))
        dataArray = []

    # try to send every 10 received packets
    # If there is a connection, the send happens after 10 received packets.
    if (len(dataArray2) >= 10) and (len(dataArray) == 0):
        print("trying to send ", len(dataArray2), " notifications")
        try:
            # iterate over list copy ([:])
            for d in dataArray2[:]:
                await storage.push_data(d)
                dataArray2.remove(d) # remove if successful

        except Exception as ex:
            print("send failed:", ex )
        


def deserialized_data(manuf_data):
    
    # skip if no Amer companyID in manuf data
    if not COMPANY_ID in manuf_data or manuf_data[COMPANY_ID][0] != 255:
        return None
    
    manuf_data = manuf_data[COMPANY_ID]
    
    #rolling packet count for detecting missing packets
    packet_count = struct.unpack("<I", (manuf_data[1:5]))[0]

    #extract float from the adv packet
    float1 = struct.unpack("<f", (manuf_data[5:9]))[0]

    #exctact float from the adv packet
    float2 = struct.unpack("<f", (manuf_data[9:13]))[0]

    #extract float from the adv packet
    float3 = struct.unpack("<f", (manuf_data[13:17]))[0]

    
    # get the timestamp
    dt = datetime.now()
    ts = datetime.timestamp(dt)
    payload_data = {"counter":packet_count,
                    data1name:float1,
                    data2name:float2,
                    data3name:float3,
                    "Timestamp":ts}
    return payload_data


async def main(argv):
    global dataArray
    global dataArray2
    global lastReceivedPacketNumber

    dataArray = []
    dataArray2 = []
    lastReceivedPacketNumber = {}

    global storage
    storage = None

    parser = argparse.ArgumentParser()
    parser.add_argument('-f', '--settingsFile', default='/etc/blegateway.cnf', help='Settings file (overrides all other parameters)')
    parser.add_argument('-c', '--fbCredsFile', default='service_key.json', help='Firebase credentials file')
    parser.add_argument('-s', '--storageUrl', help='Storage service URL')
    parser.add_argument('-p', '--dbpath', default='/', help='Root path of the storage')
    parser.add_argument('-d1', '--data1name', help='name of 1st float data')
    parser.add_argument('-d2', '--data2name', help='name of 1st float data')
    parser.add_argument('-d3', '--data3name', help='name of 1st float data')
    args = parser.parse_args()

    if args.settingsFile and os.path.exists(args.settingsFile):
        config = configparser.ConfigParser()
        config.read(args.settingsFile)

    else:
        print('Cannot read settings from "%s"' % args.settingsFile)
        print('defaulting to hci=0')
        config = {}
        config['bluetooth']={}
        config['bluetooth']['hci'] = 0

    if "firebaseio" in args.storageUrl:
        if not args.fbCredsFile:
            print("No --fbCredsFile given for firebase storage.")
            return

        # create a storage handler
        storage = await DataStore_FirebaseRT.create(args.fbCredsFile, args.storageUrl, args.dbpath)
    else:
        print("No valid --storageUrl given.")
        return

    # set data field names if given, defaults to floatX
    global data1name
    global data2name
    global data3name
    if args.data1name: 
        data1name = args.data1name
    if args.data2name: 
        data2name = args.data2name
    if args.data3name: 
        data3name = args.data3name
    
    bthci = config['bluetooth']['hci']

    scanner = BleAdvScanner(callback, bt_device_id=int(bthci))
    await scanner.start()

    # Run forever
    #while (1):
    #    time.sleep(10)
    #    print("I'm alive")
    try:
        await asyncio.sleep(9999999999)
    except:
        print("sleep ended by Ctrl+C")
    finally:
        print("Finishing operation")

    await scanner.stop()
    if storage:
        # send remainder of the collected data
        try:
            if len(dataArray) > 0:
                print("Sending remaining ", len(dataArray), " packets.")
                for d in dataArray[:]:
                    await storage.push_data(d)
        finally:
            # and close connection to cloud
            storage.close()
            storage = None

if __name__ == '__main__':
    asyncio.run(main(sys.argv[1:]))
