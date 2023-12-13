## Overview

This is Movesense BLE advertisement message scanner for the Raspberry Pi (Raspbian / Ubuntu).


## Dependencies

* Python 3.x
* [PyBluez fork with bug fixes](https://github.com/AcrossTheCloud/pybluez)
* [Firebase Admin](https://firebase.google.com/docs/reference/admin/python/firebase_admin)
* [gpsd](https://gpsd.gitlab.io/gpsd/) and the [corresponding Python module](https://pypi.org/project/gps/): needed even if a GPS receiver isn't attached, if one is attached the GPS data will be uploaded to the cloud too

## Configuration

The movesense-raspi-python3 gateway service talks to a [Google Firebase Realtime Database](https://firebase.google.com/docs/database) in the cloud, so set one up, then:
1. In the Firebase console, open Settings > [Service Accounts](https://console.firebase.google.com/project/_/settings/serviceaccounts/adminsdk).
2. Select the project, then click Generate New Private Key, then confirm by clicking Generate Key.
3. Copy the downloaded key to the Raspberry Pi as (you will need sudo to copy it to) `/etc/firebase_creds.json`, and `sudo chmod 0600 /etc/firebase_creds.json`.

After cloning this repository, edit the `install.sh` script to set the BLEADVIOTGATEWAY_STORAGEURL environment variable to point to the Firebase Realtime Database URL, and optionally set the BLEADVIOTGATEWAY_DBPATH

Movesense-raspi-python3's configuration file will be written by install.sh to /etc/blegateway.cnf and the cloud variables to /etc/blegateway_cloud.cnf


## Installation

Open a terminal on Raspbian / Ubuntu, then follow instructions by typing commands to terminal.

* Install needed libs
** Note that the `--break-system-packages` flag may be required to install using `sudo python3 -m pip install <package>`. 

    `sudo apt install libbluetooth-dev gpsd`

    `sudo python3 -m pip install  git+https://github.com/AcrossTheCloud/pybluez.git#egg=pybluez`

    `sudo python3 -m pip install configparser firebase_admin gps`

    `sudo python3 -m pip install bleak`


* Clone movesense-raspi-gateway repository
    `git clone https://bitbucket.org/suunto/movesense-raspi-python3`

* Enter to movesense-raspi-python3's setup directory
    `cd movesense-raspi-python3/setup/`

* Edit the install.sh script (per configuration section)

* In the setup directory, run installation script:
    `sudo ./install.sh`

## Running

* After installation and configuration, the gateway can be started and stoped by:

    `sudo service blegateway start`

    `sudo service blegateway stop`

or manually started by running:
    `sudo /usr/share/blegateway/blegatewaykickstart.sh`

