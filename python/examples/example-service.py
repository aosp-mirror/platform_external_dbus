import dbus

import pygtk
import gtk

class MyObject(dbus.Object):
    def __init__(self):
        service = dbus.Service("org.designfu.SampleService")
        dbus.Object("/SomeObject", [self.HelloWorld], service)

    def HelloWorld(self, hello_message):
        print (hello_message)
        return "Hello from example-service.py"

object = MyObject()

gtk.main()
