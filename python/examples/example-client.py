#!/usr/bin/env python

import dbus

bus = dbus.Bus()
remote_service = bus.get_service("org.designfu.SampleService")
remote_object = remote_service.get_object("/MyObject", "org.designfu.SampleInterface")

remote_object.HelloWorld("Hello from example-client.py!")
