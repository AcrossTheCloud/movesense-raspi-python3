#!/bin/bash
#
# Movesense BLE advertisement message gateway
#
# Raspbian installer for Movesense-raspi-gateway (blegateway) service
#
# See https://bitbucket.org/suunto/private-movesense-raspi-python3/README.md
#

servicename="blegateway"
servicefile="/lib/systemd/system/$servicename.service"
destpath="/usr/share/$servicename"
servicelauncher="$destpath/blegatewaykickstart.sh"
servicesettingsfile="/etc/blegateway.cnf"
srcpath=`pwd`

if [ -f "$servicefile" ]; then
    echo "Remove existing service"
    service $servicename stop
    systemctl disable $servicename
fi

if [ ! -d "$destpath" ]; then
    echo "Make dir $destpath"
    mkdir $destpath
fi

echo "Copy scripts to destination path $destpath"
cp ../src/bleAdvIoTGateway.py $destpath/
cp ../src/bleAdvScanner.py $destpath/


if [ ! -f "$servicesettingsfile" ]; then
    # Settings file doesn't exists, write some defaults:
    echo "#
# Settings for Movesense BLE advertisement message gateway to IBM Watson IoT Cloud
#
[bluetooth]
# Raspberry's bluetooth interface
hci=0
" > $servicesettingsfile 
fi

echo "Write service file $serviceFile"
echo "[Unit]
Description=$servicename, Movesense BLE advertisement message gateway service
After=bluetooth.target

[Service]
Type=simple
Restart=always
RestartSec=15
User=pi
ExecStart=$servicelauncher

[Install]
WantedBy=default.target" > $servicefile

echo "Write launcher file $servicelauncher"

echo "#!/bin/bash

# Not so fine way to init system and startup router, but here's some to test what
# we need for it.

hciline=\`cat $servicesettingsfile | grep hci\`
hci="hci"\`cut -d "=" -f 2 <<< "\$hciline"\`

# Restart bluetooth
sudo service bluetooth stop
sudo service bluetooth start
sleep 2

# Set bluetooth available
sudo rfkill unblock bluetooth
sleep 2

# Reset interface
sudo hciconfig \$hci reset

# Turn adv mode on and off
sudo hciconfig \$hci leadv
sleep 5
sudo hciconfig \$hci noleadv
sleep 2

# Scan a while (looks like this is important somehow)
sudo hcitool -i \$hci lescan --duplicates &
sleep 2
# Then kill scanning
sudo killall -9 hcitool

# Finally launch ble gateway
cd $destpath
sudo /usr/bin/env python3 $destpath/bleAdvIoTGateway.py
" > $servicelauncher
sudo chmod +x $servicelauncher

echo "Enable service"
systemctl enable $servicename

echo ""
echo "If everything went well, service can be started, stoped and status checked"
echo "by normal service commands, like:"
echo "   sudo service $servicename start"
echo "   sudo service $servicename status"
echo "   sudo service $servicename stop"
echo ""
echo "Be sure to define settings on $servicesettingsfile"
echo ""
