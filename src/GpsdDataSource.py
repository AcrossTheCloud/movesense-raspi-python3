"""
 GPS Datasource

 Scanner uses python "gps" module for reading and parsing gpsd data
"""
import asyncio
import threading
import time
from gps import *

import concurrent.futures
from datetime import datetime

executor = concurrent.futures.ThreadPoolExecutor(max_workers=10)

class GpsdDataSource(threading.Thread):


    def __init__(self, callback):

        # store callback function
        GpsdDataSource.callback_func = callback
        self.event_loop = asyncio.get_running_loop()
        try:
            # Start gps thread
            threading.Thread.__init__(self)
            # init gpsd client
            self.session = gps(mode=WATCH_ENABLE | WATCH_NEWSTYLE)
            print("GPSD initialized.")
        except Exception as ex:
            self.session = None
            print("Error initializing GPSD: ", ex)

        self.current_value = None

    def get_current_value(self):
        return self.current_value

    def call_callback(self, position_fix):
        if "lat" in position_fix:
            #print("GPS position_fix: ", position_fix)
            lat = position_fix['lat']
            lon = position_fix['lon']
            alt = None
            speed = None

            # convert "Z" to timezone that fromisoformat understands
            timestr = position_fix['time'].replace("Z","+00:00")
            timestamp = datetime.fromisoformat(timestr).timestamp()

            if "alt" in position_fix:
                alt = position_fix["alt"]
            if "speed" in position_fix:
                speed = position_fix["speed"]
            
            self.event_loop.create_task(GpsdDataSource.callback_func(timestamp, lat, lon, speed, alt))

    def run(self):
        print("GpsdDataSource.run() called.")
        try:
            while self.session:
                pos_fix = self.session.next()
                #print("GPSD pos fix: ", pos_fix)
                self.call_callback(pos_fix)
                time.sleep(1.0)  # tune this, you might not get values that quickly
        except StopIteration:
            pass
