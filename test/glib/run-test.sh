#! /bin/bash

SCRIPTNAME=$0

function die() 
{
    echo $SCRIPTNAME: $* >&2
    exit 1
}

if test -z "$DBUS_TOP_BUILDDIR" ; then
    die "Must set DBUS_TOP_BUILDDIR"
fi

CONFIG_FILE=./run-test.conf

## create a configuration file based on the standard session.conf
cat $DBUS_TOP_BUILDDIR/bus/session.conf |  \
  sed -e 's/<servicedir>.*$//g'         |  \
  sed -e 's/<include.*$//g'                \
  > $CONFIG_FILE

echo "Created configuration file $CONFIG_FILE"

export PATH=$DBUS_TOP_BUILDDIR/bus:$PATH
## the libtool script found by the path search should already do this, but
export LD_LIBRARY_PATH=$DBUS_TOP_BUILDDIR/dbus/.libs:$LD_LIBRARY_PATH

## will only do anything on Linux
export MALLOC_CHECK_=2 

echo "Using daemon "`type dbus-daemon-1`

eval `$DBUS_TOP_BUILDDIR/tools/dbus-launch --sh-syntax --config-file=$CONFIG_FILE`

if test -z "$DBUS_SESSION_BUS_PID" ; then
    die "Failed to launch message bus for tests to run"
fi

echo "Started test bus pid $DBUS_SESSION_BUS_PID at $DBUS_SESSION_BUS_ADDRESS"

$DBUS_TOP_BUILDDIR/test/glib/test-dbus-glib || die "test-dbus-glib failed"

## we kill -TERM so gcov data can be written out

kill -TERM $DBUS_SESSION_BUS_PID || die "Message bus vanished! should not have happened" && echo "Killed daemon $DBUS_SESSION_BUS_PID"

sleep 2

## be sure it really died 
kill -9 $DBUS_SESSION_BUS_PID > /dev/null 2>&1 || true

exit 0