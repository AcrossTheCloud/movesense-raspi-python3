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

import uuid
import base64

import json
import copy
from multiprocessing.dummy import Pool
import asyncio


from BleAdvScanner_BlueZ import BleAdvScanner_BlueZ
from BleAdvScanner_Bleak import BleAdvScanner_Bleak

from GpsdDataSource import GpsdDataSource

BLE_COMPANY_ID = 159

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

async def gps_callback(timestamp, lat, lon, speed, alt):
    global dataArray
    global dataArray2
    global lastReceivedPacketNumber
    #print("ble_callback called: ", deviceid, ", rssi: ", rssi)

    payload = {}
    payload["source"]="gps"

    # add values to payload
    payload["Timestamp"] = timestamp
    payload["lat"]=lat
    payload["lon"]=lon
    if speed:
        payload["speed"]=speed
    if alt:
        payload["alt"]=alt

    #print("GPS data update: ", payload)

    # try to send the gps update.
    try:
        # Send the data payload to storage
        await storage.push_data(payload)
    except Exception as ex:
        print("GPS send failed:", ex )


async def ble_callback(deviceid: str, rssi, manuf_data):
    global dataArray
    global dataArray2
    global lastReceivedPacketNumber
    #print("ble_callback called: ", deviceid, ", rssi: ", rssi)

    payload=deserialize_manuf_data(manuf_data)
    if not payload:
        return

    # drop duplicates
    if (deviceid in lastReceivedPacketNumber) and (lastReceivedPacketNumber[deviceid] == payload["counter"]):
        return

    # add deviceId and rssi to payload
    payload["deviceId"]=deviceid
    payload["rssi"]=rssi
    #print("BLE payload: ", payload)

    # store last counter value for duplicate rejection:
    lastReceivedPacketNumber[deviceid] = payload["counter"]

    try:
        # Send data payload to storage
        await storage.push_data(payload)

    except Exception as ex:
        print("BLE send failed:", ex )
    


def deserialize_manuf_data(manuf_data):
    #print("deserialize_manuf_data: ", manuf_data)
    
    # skip if no Amer companyID in manuf data
    if not BLE_COMPANY_ID in manuf_data or manuf_data[BLE_COMPANY_ID][0] != 255:
        #print("skipping... ")
        return None

    #print("manuf_data[BLE_COMPANY_ID]: ", manuf_data[BLE_COMPANY_ID])

    manuf_data = manuf_data[BLE_COMPANY_ID]

    # Only handle packets with at least 17 bytes (4 x 32bit + 255 byte in beginning)
    if len(manuf_data) < 17:
        #print("skipping short data:", len(manuf_data))
        return None

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
global bthci

def createBleAdvScanner():
    # Choose optimal BLE scanner API. Linux Bleak seems to drop packets so use BlueZ implementation
    if sys.platform.startswith('linux'):
        ble_scanner = BleAdvScanner_BlueZ(ble_callback, bt_device_id=int(bthci))
        print("BlueZ ble scanner created: ", ble_scanner)
    else:
        ble_scanner = BleAdvScanner_Bleak(ble_callback)
        print("Bleak ble scanner created: ", ble_scanner)

    return ble_scanner

async def main(argv):
    global dataArray
    global dataArray2
    global lastReceivedPacketNumber

    dataArray = []
    dataArray2 = []
    lastReceivedPacketNumber = {}

    global storage
    storage = None

    global bthci
    bthci = 0


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

    bthci = config['bluetooth']['hci']
    if "firebase" in args.storageUrl:
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

    ble_scanner = createBleAdvScanner()
    await ble_scanner.start()

    # Create GPS datasource
    global gps_datasource
    gps_datasource = GpsdDataSource(gps_callback)
    gps_datasource.start()

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

    await ble_scanner.stop()
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
