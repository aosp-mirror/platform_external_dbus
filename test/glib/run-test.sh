#! /bin/bash

SCRIPTNAME=$0
MODE=$1

function die() 
{
    if ! test -z "$DBUS_SESSION_BUS_PID" ; then
        echo "killing message bus "$DBUS_SESSION_BUS_PID
        kill -9 $DBUS_SESSION_BUS_PID
    fi
    echo $SCRIPTNAME: $* >&2
    exit 1
}

if test -z "$DBUS_TOP_BUILDDIR" ; then
    die "Must set DBUS_TOP_BUILDDIR"
fi

## convenient to be able to ctrl+C without leaking the message bus process
trap 'die "Received SIGINT"' SIGINT

CONFIG_FILE=./run-test.conf
SERVICE_DIR="$DBUS_TOP_BUILDDIR/test/data/valid-service-files"
ESCAPED_SERVICE_DIR=`echo $SERVICE_DIR | sed -e 's/\//\\\\\\//g'`
echo "escaped service dir is: $ESCAPED_SERVICE_DIR"

## create a configuration file based on the standard session.conf
cat $DBUS_TOP_BUILDDIR/bus/session.conf |  \
    sed -e 's/<servicedir>.*$/<servicedir>'$ESCAPED_SERVICE_DIR'<\/servicedir>/g' |  \
    sed -e 's/<include.*$//g'                \
  > $CONFIG_FILE

echo "Created configuration file $CONFIG_FILE"

export PATH=$DBUS_TOP_BUILDDIR/bus:$PATH
## the libtool script found by the path search should already do this, but
export LD_LIBRARY_PATH=$DBUS_TOP_BUILDDIR/dbus/.libs:$LD_LIBRARY_PATH

## will only do anything on Linux
export MALLOC_CHECK_=2 

unset DBUS_SESSION_BUS_ADDRESS
unset DBUS_SESSION_BUS_PID

echo "Using daemon "`type dbus-daemon-1`

eval `$DBUS_TOP_BUILDDIR/tools/dbus-launch --sh-syntax --config-file=$CONFIG_FILE`

if test -z "$DBUS_SESSION_BUS_PID" ; then
    die "Failed to launch message bus for tests to run"
fi

echo "Started test bus pid $DBUS_SESSION_BUS_PID at $DBUS_SESSION_BUS_ADDRESS"

## so the tests can complain if you fail to use the script to launch them
export DBUS_TEST_GLIB_RUN_TEST_SCRIPT=1

if test x$MODE = xprofile ; then
  sleep 2 ## this lets the bus get started so its startup time doesn't affect the profile too much
  if test x$PROFILE_TYPE = x ; then
      PROFILE_TYPE=all
  fi
  $DEBUG $DBUS_TOP_BUILDDIR/test/glib/test-profile $PROFILE_TYPE || die "test-profile failed"
else
  $DEBUG $DBUS_TOP_BUILDDIR/test/glib/test-dbus-glib || die "test-dbus-glib failed"
fi

## we kill -TERM so gcov data can be written out

kill -TERM $DBUS_SESSION_BUS_PID || die "Message bus vanished! should not have happened" && echo "Killed daemon $DBUS_SESSION_BUS_PID"

sleep 2

## be sure it really died 
kill -9 $DBUS_SESSION_BUS_PID > /dev/null 2>&1 || true

exit 0
