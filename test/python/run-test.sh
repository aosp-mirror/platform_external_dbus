#! /bin/bash

function die()
{
    if ! test -z "$DBUS_SESSION_BUS_PID" ; then
        echo "killing message bus "$DBUS_SESSION_BUS_PID >&2
        kill -9 $DBUS_SESSION_BUS_PID
    fi
    echo $SCRIPTNAME: $* >&2

    rm $DBUS_TOP_BUILDDIR/python/dbus
 
    exit 1
}


SCRIPTNAME=$0
MODE=$1

## so the tests can complain if you fail to use the script to launch them
export DBUS_TEST_PYTHON_RUN_TEST_SCRIPT=1

# Rerun ourselves with tmp session bus if we're not already
if test -z "$DBUS_TEST_PYTHON_IN_RUN_TEST"; then
  DBUS_TEST_PYTHON_IN_RUN_TEST=1
  export DBUS_TEST_PYTHON_IN_RUN_TEST
  exec $DBUS_TOP_BUILDDIR/tools/run-with-tmp-session-bus.sh $SCRIPTNAME $MODE
fi  

ln -s $DBUS_TOP_BUILDDIR/python $DBUS_TOP_BUILDDIR/python/dbus
echo "running test-client.py"
#uncomment this if you need to see the output from the service for debugging
#otherwise keep it commented so we can test activation
#libtool --mode=execute $DEBUG $DBUS_TOP_BUILDDIR/test/python/test-service.py &
#libtool --mode=execute $DEBUG $DBUS_TOP_BUILDDIR/test/glib/test-service-glib &
#sleep 1
#ps
#sleep 9

libtool --mode=execute $DEBUG $DBUS_TOP_BUILDDIR/test/python/test-client.py || die "test-client.py failed"
rm $DBUS_TOP_BUILDDIR/python/dbus

