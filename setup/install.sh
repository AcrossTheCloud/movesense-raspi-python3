#!/bin/bash
#
# Movesense BLE advertisement message gateway
#
# Raspbian installer for Movesense-raspi-gateway (blegateway) service
#
# See https://bitbucket.org/suunto/private-movesense-raspi-python3/README.md
#

# Set configuration tag if given
if [ -z "$1" ]
then
    config_tag=""
else
    config_tag="_$1"
fi


servicename="blegateway$config_tag"
servicefile="/lib/systemd/system/$servicename.service"
destpath="/usr/share/$servicename"
servicelauncher="$destpath/blegatewaykickstart.sh"
#servicesettingsfile="/etc/blegateway.cnf"
srcpath=`pwd`


cloudconfig_file="/etc/blegateway_cloud$config_tag.cnf"
cloud_creds_file="/etc/firebase_creds$config_tag.json"

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
cp ../src/*.py $destpath/


# if [ ! -f "$servicesettingsfile" ]; then
#     # Settings file doesn't exists, write some defaults:
#     echo "#
# # Settings for Movesense BLE advertisement message gateway to IBM Watson IoT Cloud
#
# [bluetooth]
# # Raspberry's bluetooth interface
# hci=0
# " > $servicesettingsfile 
# fi

# create cloud config file
echo "Write cloud config file $cloudconfig_file"
echo "
# Cloud settings for bleScanner
export BLEADVIOTGATEWAY_STORAGEURL=https://XXXXXXXX.firebaseio.com/
export BLEADVIOTGATEWAY_DBPATH=/RootPathHere/
export BLEADVIOTGATEWAY_FBCREDSFILE=$cloud_creds_file
export BLEADVIOTGATEWAY_DATA1NAME=
export BLEADVIOTGATEWAY_DATA2NAME=
export BLEADVIOTGATEWAY_DATA3NAME=

" > $cloudconfig_file

# create service description file
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

# read cloud config variables
source $cloudconfig_file

# build command line parameters for the script
service_launch_cmd_line=\"--storageUrl \$BLEADVIOTGATEWAY_STORAGEURL --fbCredsFile \$BLEADVIOTGATEWAY_FBCREDSFILE --dbpath \$BLEADVIOTGATEWAY_DBPATH\"
if [ ! -z \$BLEADVIOTGATEWAY_DATA1NAME ]; then
    service_launch_cmd_line=\"\$service_launch_cmd_line -d1 \$BLEADVIOTGATEWAY_DATA1NAME\"
fi
if [ ! -z \$BLEADVIOTGATEWAY_DATA2NAME ]; then
    service_launch_cmd_line=\"\$service_launch_cmd_line -d2 \$BLEADVIOTGATEWAY_DATA2NAME\"
fi
if [ ! -z \$BLEADVIOTGATEWAY_DATA3NAME ]; then
    service_launch_cmd_line=\"\$service_launch_cmd_line -d3 \$BLEADVIOTGATEWAY_DATA3NAME\"
fi

# Not so fine way to init system and startup router, but here's some to test what
# we need for it.

# Restart bluetooth
sudo service bluetooth stop
sudo service bluetooth start
sleep 2

# Set bluetooth available
sudo rfkill unblock bluetooth
sleep 2

# OLD STUFF
# Reset interface
# sudo hciconfig \$hci reset

# Turn adv mode on and off
#sudo hciconfig \$hci leadv
#sleep 5
#sudo hciconfig \$hci noleadv
#sleep 2

# Scan a while (looks like this is important somehow)
#sudo hcitool -i \$hci lescan --duplicates &
#sleep 2
# Then kill scanning
#sudo killall -9 hcitool
# OLD STUFF ENDS

# Finally launch ble gateway
cd $destpath
sudo /usr/bin/env python3 $destpath/bleAdvIoTGateway.py \$service_launch_cmd_line
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
echo "Be sure to define cloud settings on $cloudconfig_file"
echo "and the cloud credentials in $cloud_creds_file"
echo ""
