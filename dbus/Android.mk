ifneq ($(TARGET_SIMULATOR),true)

LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES:= \
dbus-address.c \
dbus-auth-script.c \
dbus-auth-util.c \
dbus-auth.c \
dbus-bus.c \
dbus-connection.c \
dbus-dataslot.c \
dbus-errors.c \
dbus-hash.c \
dbus-internals.c \
dbus-keyring.c \
dbus-list.c \
dbus-mainloop.c \
dbus-marshal-basic.c \
dbus-marshal-byteswap-util.c \
dbus-marshal-byteswap.c \
dbus-marshal-header.c \
dbus-marshal-recursive-util.c \
dbus-marshal-recursive.c \
dbus-marshal-validate-util.c \
dbus-marshal-validate.c \
dbus-memory.c \
dbus-mempool.c \
dbus-message-factory.c \
dbus-message-util.c \
dbus-message.c \
dbus-object-tree.c \
dbus-pending-call.c \
dbus-resources.c \
dbus-server-debug-pipe.c \
dbus-server-socket.c \
dbus-server-unix.c \
dbus-server.c \
dbus-sha.c \
dbus-shell.c \
dbus-signature.c \
dbus-spawn.c \
dbus-string-util.c \
dbus-string.c \
dbus-sysdeps-pthread.c \
dbus-sysdeps-unix.c \
dbus-sysdeps-util-unix.c \
dbus-sysdeps-util.c \
dbus-sysdeps.c \
dbus-test-main.c \
dbus-test.c \
dbus-threads.c \
dbus-timeout.c \
dbus-transport-socket.c \
dbus-transport-unix.c \
dbus-transport.c \
dbus-userdb-util.c \
dbus-userdb.c \
dbus-uuidgen.c \
dbus-watch.c

LOCAL_C_INCLUDES+= \
	$(call include-path-for, dbus)

LOCAL_MODULE:=libdbus

LOCAL_CFLAGS+= \
	-DDBUS_COMPILATION \
	-DANDROID_MANAGED_SOCKET \
	-DDBUS_MACHINE_UUID_FILE=\"/etc/machine-id\"

include $(BUILD_SHARED_LIBRARY)

endif
