package org.freedesktop.dbus;

import gnu.gcj.RawData;

public class Message {
    private RawData message;

    public Message (String name, String destService) {
	this.message = dbus_message_new (name, destService);
    }

    private static native RawData dbus_message_new (String name, String destService);
}
