
LIBUSB_DIR=${CURDIR}/deps/libusb
PDLIBBUILDER_DIR=${CURDIR}/deps/pd-lib-builder

cflags += -I${LIBUSB_DIR}
ldflags += -L${LIBUSB_DIR}/libusb -lusb

export PDDIR
export PDLIBBUILDER_DIR
export LIBUSB_DIR
export cflags
export ldflags


all: libusb
	$(MAKE) -C src/hid

install:
	$(MAKE) -C src/hid install

clean:
	$(MAKE) -C src/hid clean
	$(MAKE) -C ${LIBUSB_DIR} cleanpd


libusb: ${LIBUSB_DIR}/libusb/.libs/libusb-1.0.a

${LIBUSB_DIR}/libusb/libusb-1.0.la: ${LIBUSB_DIR}/Makefile
	cd ${LIBUSB_DIR}; make

${LIBUSB_DIR}/Makefile: ${LIBUSB_DIR}/configure
	cd ${LIBUSB_DIR}; ./configure

${LIBUSB_DIR}/configure:
	cd ${LIBUSB_DIR}; ./autogen.sh
