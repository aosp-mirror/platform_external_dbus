LOCAL_PATH:= $(call my-dir)
include $(CLEAR_VARS)

LOCAL_C_INCLUDES:= \
	$(call include-path-for, dbus) \
	$(call include-path-for, dbus)/dbus \
	external/expat/lib/

LOCAL_CFLAGS:=-O3
LOCAL_CFLAGS+=-DDBUS_COMPILATION
#LOCAL_CFLAGS+=-DDBUS_MACHINE_UUID_FILE=\"/etc/machine-id\"
LOCAL_CFLAGS+=-DDAEMON_NAME=\"dbus-daemon\"
LOCAL_CFLAGS+=-DDBUS_SYSTEM_CONFIG_FILE=\"/etc/dbus.conf\"
LOCAL_CFLAGS+=-DDBUS_SESSION_CONFIG_FILE=\"/etc/session.conf\"


LOCAL_SRC_FILES:= \
	activation.c \
	bus.c \
	config-loader-expat.c \
	config-parser.c \
	connection.c \
	desktop-file.c \
	dir-watch-default.c \
	dispatch.c \
	driver.c \
	expirelist.c \
	main.c \
	policy.c \
	selinux.c \
	services.c \
	signals.c \
	utils.c

LOCAL_SHARED_LIBRARIES := \
	libexpat \
	libdbus

LOCAL_MODULE:=dbus-daemon

include $(BUILD_EXECUTABLE)
