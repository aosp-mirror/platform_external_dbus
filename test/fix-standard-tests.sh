#! /bin/sh

### this script is a lame hack used in the Makefile because 
### I couldn't figure out how to escape @EXPANDED_LOCALSTATEDIR@ etc. 
### inside the Makefile

perl -pi -e "s%\@EXPANDED_LOCALSTATEDIR\@%/var%g" data/valid-config-files/system.conf
perl -pi -e "s%\@DBUS_SYSTEM_SOCKET\@%run/dbus/system_bus_socket%g" data/valid-config-files/system.conf
