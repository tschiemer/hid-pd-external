
PDLIBBUILDER_DIR=${CURDIR}/deps/pd-lib-builder
LIBUSB_DIR=${CURDIR}/deps/libusb
HIDAPI_DIR=${CURDIR}/deps/hidapi
HIDMAP_DIR=${CURDIR}/deps/USB-HID-Report-Parser

cflags += -I${LIBUSB_DIR}/libusb -I${HIDAPI_DIR}/hidapi -I${HIDMAP_DIR}
ldflags += -L${LIBUSB_DIR}/libusb -lusb -L${HIDAPI_DIR}/local-install/lib -lhidapi -L${HIDMAP_DIR} -lusbhid_map

export PDDIR
export PDLIBBUILDER_DIR
export LIBUSB_DIR
export HIDAPI_DIR
export HIDMAP_DIR
export cflags
export ldflags


all: libusb hidapi usbhid_map
	$(MAKE) -C src/hid

install:
	$(MAKE) -C src/hid install

clean:
	$(MAKE) -C src/hid clean
	$(MAKE) -C ${LIBUSB_DIR} clean
	$(MAKE) -C ${HIDAPI_DIR} clean
	$(MAKE) -C ${HIDMAP_DIR} clean


### libusbb

libusb: ${LIBUSB_DIR}/libusb/.libs/libusb-1.0.a

${LIBUSB_DIR}/libusb/.libs/libusb-1.0.a: ${LIBUSB_DIR}/Makefile
	$(MAKE) -C ${LIBUSB_DIR}

${LIBUSB_DIR}/Makefile: ${LIBUSB_DIR}/configure
	cd ${LIBUSB_DIR}; ./configure

${LIBUSB_DIR}/configure:
	cd ${LIBUSB_DIR}; ./autogen.sh

### hidapi

hidapi: ${HIDAPI_DIR}/local-install/libhidapi.la

${HIDAPI_DIR}/local-install/libhidapi.la: ${HIDAPI_DIR}/Makefile
	$(MAKE) -C ${HIDAPI_DIR}
	$(MAKE) -C ${HIDAPI_DIR} install

${HIDAPI_DIR}/Makefile: ${HIDAPI_DIR}/configure
	cd ${HIDAPI_DIR}; ./configure --prefix=${HIDAPI_DIR}/local-install

${HIDAPI_DIR}/configure:
	cd ${HIDAPI_DIR}; ./bootstrap
	
	
### usbhid_map

# usbhid_map: ${HIDMAP_DIR}/libusbhid_map.a
# 	$(MAKE) -C ${HIDMAP_DIR}

# ${HIDMAP_DIR}/libusbhid_map.a: ${HIDMAP_DIR}/Makefile
usbhid_map: ${HIDMAP_DIR}/Makefile
	$(MAKE) -C ${HIDMAP_DIR}

${HIDMAP_DIR}/Makefile:
	cd ${HIDMAP_DIR}; cmake .
