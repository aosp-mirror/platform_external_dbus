import dbus

import pygtk
import gtk

class MyObject(dbus.Object):
    def __init__(self):
        service = dbus.Service("org.designfu.SampleService")
        dbus.Object("/MyObject", [self.HelloWorld], service)

    def HelloWorld(self, arg1):
        print ("Hello World!: %s" % (arg1))

object = MyObject()

gtk.main()
