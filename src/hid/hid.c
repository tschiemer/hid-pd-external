

#include "m_pd.h"
#include "libusb/libusb.h"
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <math.h>

#define UNUSED(x) (void)(x)

static t_class *hid_class;

typedef struct {
    libusb_device * dev;
    struct libusb_device_descriptor desc;
//    struct libusb_config_descriptor *config;
    uint8_t interface_num;

    char * serial_string;
    char * manufacturer_string;
    char * product_string;

    uint8_t report_desc[];
#define hid_usage_page  report_desc[1]
#define hid_usage       report_desc[3]
} hid_device_t;

typedef struct {
    t_object  x_obj;
    t_outlet *out;

    libusb_context * context;

    // currently open device
    hid_device_t * hiddev;
    libusb_device_handle * handle;


//    t_inlet *in;
//    t_outlet *dbg_out;
//    bool runningStatusEnabled;
//    uint8_t runningStatusState;
} hid_t;



void hid_setup(void);
static void * hid_new();
static void hid_free(hid_t *hid);

//static void hid_anything(hid_t *hid, t_symbol *s, int argc, t_atom *argv);
//void midimessage_gen_anything(t_midimessage_gen *self, t_symbol *s, int argc, t_atom *argv);
//void midimessage_gen_generatorError(int code, uint8_t argc, uint8_t ** argv);


static void hid_cmd_list(hid_t *hid, t_symbol *s, int argc, t_atom *argv);
static void hid_cmd_open(hid_t *hid, t_symbol *s, int argc, t_atom *argv);
static void hid_cmd_close(hid_t *hid, t_symbol *s, int argc, t_atom *argv);


static int hid_get_device_list(hid_t *hid, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage, uint8_t max);
static int hid_filter_device_list(libusb_device **devs, ssize_t count, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage, uint8_t max);
static void hid_free_device_list(hid_device_t ** hiddevs);

static char *get_usb_string(libusb_device_handle *dev, uint8_t idx);


/**
 * define the function-space of the class
 */
void hid_setup(void) {
    hid_class = class_new(gensym("hid"),
                          (t_newmethod)hid_new,
                          (t_method)hid_free,
                          sizeof(hid_t),
                          CLASS_DEFAULT,
                          0);

    /* call a function when object gets banged */
//    class_addbang(midimessage_gen_class, midimessage_gen_bang);


    class_addmethod(hid_class, (t_method)hid_cmd_list, gensym("list"), A_GIMME, 0);
    class_addmethod(hid_class, (t_method)hid_cmd_open, gensym("open"), A_GIMME, 0);
    class_addmethod(hid_class, (t_method)hid_cmd_close, gensym("close"), A_GIMME, 0);

//    class_addmethod(midi)
//    class_addlist(hid_class, hid_anything);
//    class_addanything(hid_class, hid_anything);


}


static void *hid_new()
{
    hid_t *hid = (hid_t *)pd_new(hid_class);

    int r = libusb_init(&hid->context);
    if (r < 0){
        error("failed to init libusb: %d", r);
        return NULL;
    }

    hid->handle = NULL;

    // generic outlet
    hid->out = outlet_new(&hid->x_obj, 0);

//    self->in = inlet_new(&self->x_obj, &self->x_obj.ob_pd,
//              gensym(""), gensym("list"));

    /* create a new outlet for floating-point values */
//    self->byte_out = outlet_new(&self->x_obj, &s_float);
//    self->dbg_out = outlet_new(&self->x_obj, &s_symbol);

//    self->runningStatusEnabled = false;
//    self->runningStatusState = MidiMessage_RunningStatusNotSet;

    return hid;
}

static void hid_free(hid_t *hid)
{
    libusb_exit(hid->context);
}

//static void hid_anything(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
//{
//    if (s == gensym("list")){
//        hid_cmd_list(hid, s, argc, argv);
//    } else {
//        error("not a recognized command");
//    }
//}


/* This function returns a newly allocated wide string containing the USB
   device string numbered by the index. The returned string must be freed
   by using free(). */
static char *get_usb_string(libusb_device_handle *dev, uint8_t idx)
{
    char buf[512];
    int len;
    char *str = NULL;


    /* Determine which language to use. */
    uint16_t lang = 0;
//    lang = get_usb_code_for_current_locale();
//    if (!is_language_supported(dev, lang))
//        lang = get_first_language(dev);

    /* Get the string from libusb. */
    len = libusb_get_string_descriptor(dev,
                                       idx,
                                       lang,
                                       (unsigned char*)buf,
                                       sizeof(buf));
    if (len < 0)
        return NULL;

	len -= 2;
	str = (char*) malloc((len / 2 + 1) * sizeof(wchar_t));
	int i;
	for (i = 0; i < len / 2; i++) {
		str[i] = buf[i * 2 + 2];// | (buf[i * 2 + 3] << 8);
	}
	str[len / 2] = 0x00000000;


    return str;
}


static int hid_get_device_list(hid_t *hid, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage, uint8_t max)
{
    libusb_device **devs;
    ssize_t cnt;


    cnt = libusb_get_device_list(hid->context, &devs);
    if (cnt < 0){
        error("Failed to get device list: %zd", cnt);
        return -1;
    }

    cnt = hid_filter_device_list(devs, cnt, hiddevs, vendor, product, serial, usage_page, usage, max);

    libusb_free_device_list(devs, 1);

    if (cnt < 0){
        error("Error during filter operation: %zd", cnt);
        return -1;
    }

    return cnt;
}

static int hid_filter_device_list(libusb_device **devs, ssize_t count, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage, uint8_t max)
{
    libusb_device *dev;
    int i = 0, o = 0;

    if (count <= 0){
        return count;
    }

    if (max <= 0){
        max = count;
    }

    // let's assume that there will be fewer HID devices (interfaces) than original usb devices...
    *hiddevs = calloc(1+max,sizeof(hid_device_t));
    if (!*hiddevs){
        error("failed to allocate memory for HID devices list");
        return -1;
    }

    while ((dev = devs[i++]) != NULL && o < max) {
        struct libusb_device_descriptor desc;
        int r = libusb_get_device_descriptor(dev, &desc);
        if (r < 0) {
            error("failed to get device descriptor");
            continue;
        }

        // actually this only works for simple devices...
        if (desc.bDeviceClass != 0 || desc.bDeviceSubClass != 0){
            continue;
        }

        // if filter bby vendor/product id
        if ((vendor != 0 && vendor != desc.idVendor) ||
            (product != 0 && product != desc.idProduct)){
            continue;
        }

        struct libusb_config_descriptor *config;
        r = libusb_get_active_config_descriptor(dev, &config);
        if (r < 0){
            libusb_get_config_descriptor(dev, 0, &config);
        }
        if (r < 0){
            error("Failed to get config descriptor: %d", r);
            continue;
        }

        for(int k = 0; k < config->bNumInterfaces && o < max; k++) {
//            printf("\t if %d / num_altsetting = %d\n", k, config->interface[k].num_altsetting);

            for (int l = 0; l < config->interface[k].num_altsetting && o < max; l++) {

                if (config->interface[k].altsetting[l].bInterfaceClass != LIBUSB_CLASS_HID) {
                    continue;
                }

                libusb_device_handle *handle;
                r = libusb_open(dev, &handle);

                if (r < 0) {
                    error("failed to open dev: %d", r);
                    continue;
                }


                uint8_t interface_num = config->interface[k].altsetting[l].bInterfaceNumber;

                uint8_t report_desc[256];
                r = libusb_control_transfer(handle, LIBUSB_ENDPOINT_IN | LIBUSB_RECIPIENT_INTERFACE,
                                            LIBUSB_REQUEST_GET_DESCRIPTOR, (LIBUSB_DT_REPORT << 8) | interface_num, 0,
                                            report_desc, sizeof(report_desc), 5000);
                if (r < 4) {
                    error("control_transfer() fail for report descriptor (%04x:%04x): %d", desc.idVendor, desc.idProduct, r);
                } else if (report_desc[0] != 0x05 || report_desc[2] != 0x09) {
                    error("Notice: report descriptor not starting with usage page and usage bytes: %02x %02x %02x %02x",
                          report_desc[0], report_desc[1], report_desc[2], report_desc[3]);
                } else if ((usage_page != 0 && usage_page != report_desc[1]) ||
                           (usage != 0 && usage != report_desc[3]) ||
                           (serial != NULL && desc.iSerialNumber == 0)) {
                    // filter out unwanted usages
                    // ie, do nothing
//                } else if (o >= count) {
//                    error("Too many HID interfaces, skipping device %04x:%04x usage %d %d", desc.idVendor,
//                          desc.idProduct, report_desc[1], report_desc[3]);
                } else {

                    char * serial_string = NULL;

                    if (desc.iSerialNumber > 0 && (serial_string = get_usb_string(handle, desc.iSerialNumber)) == NULL){
                        error("failed to load serial for device %04x:%04x", desc.idVendor, desc.idProduct);
                    } else if (serial != NULL && strcmp(serial, serial_string) != 0){
                        free(serial_string);
                    } else {

                        hid_device_t * hiddev = calloc(1, sizeof(hid_device_t) + r);

                        hiddev->dev = dev;
                        libusb_ref_device(dev); // increase reference count

                        memcpy(&hiddev->desc, &desc, sizeof(struct libusb_device_descriptor));

//                    hiddev->config = config;
//                    config = NULL;

                        hiddev->interface_num = interface_num;

                        memcpy(hiddev->report_desc, report_desc, r);

                        /* Serial Number */
                        hiddev->serial_string = serial_string;

                        /* Manufacturer and Product strings */
                        if (desc.iManufacturer > 0)
                            hiddev->manufacturer_string =
                                    get_usb_string(handle, desc.iManufacturer);
                        if (desc.iProduct > 0)
                            hiddev->product_string =
                                    get_usb_string(handle, desc.iProduct);

                        (*hiddevs)[o++] = hiddev;

//                    post("usage (page) = %d (%d)", report_desc[3], report_desc[1]);
                    }
                }

                libusb_close(handle);
            }
        }

        if (config != NULL){
            libusb_free_config_descriptor(config);
        }
    }

    // if not found any devices free list;
    if (o == 0){
        free(*hiddevs);
        *hiddevs = NULL;
    }

    return o;
}

static void hid_free_device(hid_device_t * hiddev)
{
    if (!hiddev)
        return;

    if (hiddev->serial_string)
        free(hiddev->serial_string);

    if (hiddev->manufacturer_string)
        free(hiddev->manufacturer_string);

    if (hiddev->product_string)
        free(hiddev->product_string);
//
//        if (dev->config) {
//            libusb_free_config_descriptor(dev->config);
//        }

    libusb_unref_device(hiddev->dev);
    free(hiddev);
}

static void hid_free_device_list(hid_device_t ** hiddevs)
{
    if (!hiddevs){
        return;
    }

    hid_device_t *dev;
    int i = 0;

    while( (dev = hiddevs[i++]) != NULL){
        hid_free_device(dev);
    }
}

typedef struct {
    uint16_t vendor;
    uint16_t product;
    char serial[256];
    char * serialptr;
    uint8_t usage_page;
    uint8_t usage;
} filter_args_t;

static void get_filter_args(filter_args_t * args, int argc, t_atom *argv)
{
    assert(args);
    assert(argv);

    args->vendor = 0;
    args->product = 0;
    args->serialptr = NULL;
    args->usage_page = 0;
    args->usage = 0;

    if (argc > 0){
        for(int i = 0, inc; i < argc; i += inc){
            char opt[256];
            inc = 0;

            atom_string(argv + i, opt, sizeof(opt));

            if (strcmp(opt, "vendorid") == 0){
                if (i+1 >= argc){
                    error("missing argument");
                    return;
                }
                inc = 2;

                t_float f = atom_getfloat(argv + i + 1);

                if (f <= 0.0 || fmodf(f, 1.0) > 0.0){
                    error("vendorid must be integer > 0");
                    return;
                }
                args->vendor = f;

            } else if (strcmp(opt, "productid") == 0){
                if (i+1 >= argc){
                    error("missing argument");
                    return;
                }
                inc = 2;

                t_float f = atom_getfloat(argv + i + 1);

                if (f <= 0.0 || fmodf(f, 1.0) > 0.0){
                    error("productidid must be integer > 0");
                    return;
                }
                args->product = f;

            } else if (strcmp(opt, "serial") == 0){
                if (i+1 >= argc){
                    error("missing argument");
                    return;
                }
                inc = 2;

                atom_string(argv + i + 1, args->serial, sizeof(args->serial));

                if (strlen(args->serial)){
                    args->serialptr = args->serial;
                }

            } else if (strcmp(opt, "usage_page") == 0){
                if (i+1 >= argc){
                    error("missing argument");
                    return;
                }
                inc = 2;

                t_float f = atom_getfloat(argv + i + 1);

                if (f <= 0.0 || fmodf(f, 1.0) > 0.0){
                    error("usage_page must be integer > 0");
                    return;
                }
                args->usage_page = f;
            } else if (strcmp(opt, "usage") == 0){
                if (i+1 >= argc){
                    error("missing argument");
                    return;
                }
                inc = 2;

                t_float f = atom_getfloat(argv + i + 1);

                if (f <= 0.0 || fmodf(f, 1.0) > 0.0){
                    error("usage must be integer > 0");
                    return;
                }
                args->usage = f;
            } else if (strcmp(opt, "mouse") == 0) {
                inc = 1 ;

                args->usage_page = 1;
                args->usage = 2;

            } else if (strcmp(opt, "joystick") == 0) {
                inc = 1 ;

                args->usage_page = 1;
                args->usage = 4;
            }  else {
                error("Unrecognized option %s", opt);
                return;
            }

            assert(inc > 0); // so you forgot to set inc(rement)
        }
    }
}

static void hid_cmd_list(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);

    filter_args_t args;

    get_filter_args(&args, argc, argv);

    hid_device_t ** hiddevs;
    ssize_t cnt = hid_get_device_list(hid, &hiddevs, args.vendor, args.product, args.serialptr, args.usage_page, args.usage, 0);

    if (cnt < 0){
        return;
    }

//    post("list all devices now: %d", cnt);

    for(int i = 0; i < cnt; i++){
        t_atom orgv[7];
        int orgc = sizeof(orgv) / sizeof(t_atom);

        SETFLOAT(orgv, hiddevs[i]->hid_usage_page);
        SETFLOAT(orgv+1, hiddevs[i]->hid_usage);
        SETFLOAT(orgv+2, hiddevs[i]->desc.idVendor);
        SETFLOAT(orgv+3, hiddevs[i]->desc.idProduct);

        if (hiddevs[i]->serial_string) {
            SETSYMBOL(orgv + 4, gensym(hiddevs[i]->serial_string));
        } else {
            SETSYMBOL(orgv + 4, gensym("-"));
        }

        if (hiddevs[i]->manufacturer_string) {
            SETSYMBOL(orgv + 5, gensym(hiddevs[i]->manufacturer_string));
        } else {
            SETSYMBOL(orgv + 5, gensym("-"));
        }

        if (hiddevs[i]->product_string) {
            SETSYMBOL(orgv + 6, gensym(hiddevs[i]->product_string));
        } else {
            SETSYMBOL(orgv + 6, gensym("-"));
        }

        outlet_anything(hid->out, gensym("device"), orgc, orgv);
    }

    hid_free_device_list(hiddevs);
}


static void hid_cmd_open(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);

    if (hid->handle){
        error("already open (%s %s %s), please first close explicitly",
              hid->hiddev->manufacturer_string ? hid->hiddev->manufacturer_string : "",
              hid->hiddev->product_string ? hid->hiddev->product_string : "",
              hid->hiddev->serial_string ? hid->hiddev->serial_string : ""
              );
        return;
    }

    filter_args_t args;

    get_filter_args(&args, argc, argv);

    hid_device_t ** hiddevs;
    ssize_t cnt = hid_get_device_list(hid, &hiddevs, args.vendor, args.product, args.serialptr, args.usage_page, args.usage, 1);

    if (cnt < 0){
        return;
    }

    if (cnt == 0){
        error("No such device");
        return;
    }


    int r = libusb_open(hiddevs[0]->dev, &hid->handle);
    if (r < 0){
        error("failed to open");

        return;
    }

    hid->hiddev = hiddevs[0];

    // just free list (not found device)
    free(hiddevs);

//    outlet_symbol(hid->out, gensym("opened"));
    outlet_anything(hid->out, gensym("opened"), 0, NULL);
}

static void hid_cmd_close(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);
    UNUSED(argc);
    UNUSED(argv);

    if (hid->handle == NULL) {
        post("already closed");
    } else {

        libusb_close(hid->handle);
        hid->handle = NULL;

        if (hid->hiddev) {
            hid_free_device(hid->hiddev);
            hid->hiddev = NULL;
        }
    }

    outlet_anything(hid->out, gensym("closed"), 0, NULL);
}