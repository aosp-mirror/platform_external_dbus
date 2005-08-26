#!/usr/bin/env python
import sys
import os

builddir = os.environ["DBUS_TOP_BUILDDIR"]
pydir = builddir + "/python"

sys.path.insert(0, pydir)
sys.path.insert(0, pydir + "/.libs")

import dbus
import dbus_bindings

if not dbus.__file__.startswith(pydir):
    raise Exception("DBus modules are not being picked up from the package")

bus = dbus.SessionBus()
remote_object = bus.get_object("org.freedesktop.DBus.TestSuitePythonService", "/org/freedesktop/DBus/TestSuitePythonObject")
iface = dbus.Interface(remote_object, "org.freedesktop.DBus.TestSuiteInterface")

try:
    #test dbus_interface parameter 
    print remote_object.Echo("dbus_interface test Passed", dbus_interface = "org.freedesktop.DBus.TestSuiteInterface")

    #test introspection
    print "\n********* Introspection Test ************"
    print remote_object.Introspect(dbus_interface="org.freedesktop.DBus.Introspectable")
    print "Introspection test passed"

    #test sending python types and getting them back
    print "\n********* Testing Python Types ***********"
    test_vals = [1, 12323231, 3.14159265, 99999999.99,
                 "dude", "123", "What is all the fuss about?", "gob@gob.com",
                 [1,2,3], ["how", "are", "you"], [1.23,2.3], [1], ["Hello"],
                 (1,2,3), (1,), (1,"2",3), ("2", "what"), ("you", 1.2),
                 {1:"a", 2:"b"}, {"a":1, "b":2}, {1:1.1, 2:2.2}, {1.1:"a", 1.2:"b"}, 
                 [[1,2,3],[2,3,4]], [["a","b"],["c","d"]],
                 ([1,2,3],"c", 1.2, ["a","b","c"], {"a": (1,"v"), "b": (2,"d")})]
                 
    for send_val in test_vals:
        print "Testing %s"% str(send_val)
        recv_val = iface.Echo(send_val)
        #TODO: is this right in python - construct a better comparison
        #      method
        if send_val != recv_val:
            raise Exception("Python type tests: %s does not equal %s"%(str(send_val), str(recv_val)))
    
    
    
except Exception, e:
    print e
    sys.exit(1)

sys.exit(0)
