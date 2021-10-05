## Overview

This is Movesense BLE advertisement message scanner for Raspbian.


## Dependencies

* Python 3.x
* [PyBluez](https://github.com/pybluez/pybluez)


## Installation

Open terminal on Raspbian, then follow instructions by typing commands to terminal.

* Install needed libs


    `sudo apt install libbluetooth-dev`
    
    `sudo python3 -m pip install pybluez`
    
    `sudo python3 -m pip instal configparser`
    

* Clone movesense-raspi-gateway repository
    `git clone https://bitbucket.org/suunto/private-movesense-raspi-python3`

* Enter to movesense-raspi-gateway's setup directory
    `cd movesense-raspi-gateway/setup/`

* On setup directory, run installation script:
    `sudo ./install.sh`


## Configuration

Movesense-raspi-gateway's configuration file will be write by install.sh to /etc/blegateway.cnf
Define configuration with your favorit editor.


## Running

* After installation and configuration, gateway can be started and stoped by:


    `sudo service blegateway start`

    `sudo service blegateway stop`


