#!/usr/bin/env python
import sys
import os
import unittest
import time

builddir = os.environ["DBUS_TOP_BUILDDIR"]
pydir = builddir + "/python"

sys.path.insert(0, pydir)
sys.path.insert(0, pydir + "/.libs")

import dbus
import dbus_bindings
import gobject
import dbus.glib

if not dbus.__file__.startswith(pydir):
    raise Exception("DBus modules are not being picked up from the package")

if not dbus_bindings.__file__.startswith(pydir):
    raise Exception("DBus modules are not being picked up from the package")

test_types_vals = [1, 12323231, 3.14159265, 99999999.99,
                 "dude", "123", "What is all the fuss about?", "gob@gob.com",
                 [1,2,3], ["how", "are", "you"], [1.23,2.3], [1], ["Hello"],
                 (1,2,3), (1,), (1,"2",3), ("2", "what"), ("you", 1.2),
                 {1:"a", 2:"b"}, {"a":1, "b":2}, #{"a":(1,"B")},
                 {1:1.1, 2:2.2}, [[1,2,3],[2,3,4]], [["a","b"],["c","d"]],
                 #([1,2,3],"c", 1.2, ["a","b","c"], {"a": (1,"v"), "b": (2,"d")})
                 ]

class TestDBusBindings(unittest.TestCase):
    def setUp(self):
        self.bus = dbus.SessionBus()
        self.remote_object = self.bus.get_object("org.freedesktop.DBus.TestSuitePythonService", "/org/freedesktop/DBus/TestSuitePythonObject")
        self.iface = dbus.Interface(self.remote_object, "org.freedesktop.DBus.TestSuiteInterface")

    def testInterfaceKeyword(self):
        #test dbus_interface parameter
        print self.remote_object.Echo("dbus_interface on Proxy test Passed", dbus_interface = "org.freedesktop.DBus.TestSuiteInterface")
        print self.iface.Echo("dbus_interface on Interface test Passed", dbus_interface = "org.freedesktop.DBus.TestSuiteInterface")
        self.assert_(True)
        
    def testIntrospection(self):
        #test introspection
        print "\n********* Introspection Test ************"
        print self.remote_object.Introspect(dbus_interface="org.freedesktop.DBus.Introspectable")
        print "Introspection test passed"
        self.assert_(True)

    def testPythonTypes(self):
        #test sending python types and getting them back
        print "\n********* Testing Python Types ***********"
                 
        for send_val in test_types_vals:
            print "Testing %s"% str(send_val)
            recv_val = self.iface.Echo(send_val)
            self.assertEquals(send_val, recv_val)

    def testBenchmarkIntrospect(self):
        print "\n********* Benchmark Introspect ************"
	a = time.time()
	print a
        print self.iface.GetComplexArray()
	b = time.time()
	print b
	print "Delta: %f" % (b - a)
        self.assert_(True)

    def testAsyncCalls(self):
        #test sending python types and getting them back async
        print "\n********* Testing Async Calls ***********"

        
        main_loop = gobject.MainLoop()
        class async_check:
            def __init__(self, test_controler, expected_result, do_exit):
                self.expected_result = expected_result
                self.do_exit = do_exit
                self.test_controler = test_controler

            def callback(self, val):
                if self.do_exit:
                    main_loop.quit()

                self.test_controler.assertEquals(val, self.expected_result)
                
            def error_handler(error):
                print error
                if self.do_exit:
                    main_loop.quit()

                self.test_controler.assert_(val, False)
        
        last_type = test_types_vals[-1]
        for send_val in test_types_vals:
            print "Testing %s"% str(send_val)
            check = async_check(self, send_val, last_type == send_val) 
            recv_val = self.iface.Echo(send_val, 
                                       reply_handler = check.callback,
                                       error_handler = check.error_handler)
            
        main_loop.run()

class TestDBusPythonToGLibBindings(unittest.TestCase):
    def setUp(self):
        self.bus = dbus.SessionBus()
        self.remote_object = self.bus.get_object("org.freedesktop.DBus.TestSuiteGLibService", "/org/freedesktop/DBus/Tests/MyTestObject")
        self.iface = dbus.Interface(self.remote_object, "org.freedesktop.DBus.Tests.MyObject")
			    
    def testIntrospection(self):
	#test introspection
        print "\n********* Introspection Test ************"
        print self.remote_object.Introspect(dbus_interface="org.freedesktop.DBus.Introspectable")
        print "Introspection test passed"
        self.assert_(True)

    def testCalls(self):
        print "\n********* Call Test ************"
        result =  self.iface.ManyArgs(1000, 'Hello GLib', 2)
        print result
        self.assert_(result == [2002.0, 'HELLO GLIB'])
	
        arg0 = {"Dude": 1, "john": "palmieri", "python": 2.4}
        result = self.iface.ManyStringify(arg0)
        print result
       
        print "Call test passed"
        self.assert_(True)

    def testPythonTypes(self):
        print "\n********* Testing Python Types ***********"
                 
        for send_val in test_types_vals:
            print "Testing %s"% str(send_val)
            recv_val = self.iface.EchoVariant(send_val)
            self.assertEquals(send_val, recv_val)

if __name__ == '__main__':
    gobject.threads_init()
    dbus.glib.init_threads()

    unittest.main()
