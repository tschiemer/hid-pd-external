

#include "m_pd.h"
#include "libusb.h"
#include "hidapi.h"
#include "usbhid_map.h"
//#include "report_item.h"
//#include "report_usage.h"

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <math.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>

#define UNUSED(x) (void)(x)

static t_class *hid_class;
static int obj_counter;

typedef struct {
    libusb_device * dev;
    struct libusb_device_descriptor desc;
//    struct libusb_config_descriptor *config;
    uint8_t interface_num;

    int input_endpoint;
    int output_endpoint;
    int input_ep_max_packet_size;

    wchar_t * serial_string;
    wchar_t * manufacturer_string;
    wchar_t * product_string;

    size_t report_desc_len;
    uint8_t report_desc[];
#define hid_usage_page  report_desc[1]
#define hid_usage       report_desc[3]
} hid_device_t;

typedef struct {
    t_object  x_obj;
    t_outlet *out;

    libusb_context * usb_context;

    //// currently open device
    // local type
    hid_device_t * hiddev;
    // hidapi type
    hid_device * handle;

    // parsed hid report
    usbhid_map_ref_t hid_map;

    uint8_t report_id;
    size_t report_size;
    uint8_t * last_report;

    size_t value_count;
    struct usbhid_map_item_st ** report_items;
    int32_t * last_values;
    int32_t * tmp_values;

    // polling option/logic
    volatile int poll_ms;
    pthread_t polling_thread;

} hid_t;



void hid_setup(void);
static void * hid_new();
static void hid_free(hid_t *hid);

//static void hid_anything(hid_t *hid, t_symbol *s, int argc, t_atom *argv);

static void hid_cmd_list(hid_t *hid, t_symbol *s, int argc, t_atom *argv);
static void hid_cmd_open(hid_t *hid, t_symbol *s, int argc, t_atom *argv);
static void hid_cmd_close(hid_t *hid, t_symbol *s, int argc, t_atom *argv);

static void hid_cmd_bang(hid_t *hid);
static void hid_cmd_poll(hid_t *hid, t_symbol *s, int argc, t_atom *argv);

static void hid_cmd_report_id(hid_t *hid, t_symbol *s, int argc, t_atom *argv);

static int hid_get_device_list(hid_t *hid, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage, uint8_t max);
static int hid_filter_device_list(libusb_device **devs, ssize_t count, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage, uint8_t max);

static void hid_free_device(hid_device_t * hiddev);
static void hid_free_device_list(hid_device_t ** hiddevs);

static int hid_read_report(hid_t * hid);
//static char *get_usb_wstring(libusb_device_handle *dev, uint8_t idx);


static void hid_shutdown(hid_t *hid);

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

    class_addmethod(hid_class, (t_method)hid_cmd_list, gensym("list"), A_GIMME, 0);

    class_addmethod(hid_class, (t_method)hid_cmd_open, gensym("open"), A_GIMME, 0);
    class_addmethod(hid_class, (t_method)hid_cmd_close, gensym("close"), A_GIMME, 0);

    class_addbang(hid_class, hid_cmd_bang);
    class_addmethod(hid_class, (t_method)hid_cmd_poll, gensym("poll"), A_GIMME, 0);

    class_addmethod(hid_class, (t_method)hid_cmd_report_id, gensym("report_id"), A_GIMME, 0);

    obj_counter = 0;
}


static void *hid_new()
{
    hid_t *hid = (hid_t *)pd_new(hid_class);

    int r = libusb_init(&hid->usb_context);
    if (r < 0){
        error("failed to init libusb: %d", r);
        return NULL;
    }

    if (obj_counter == 0){
        if (hid_init()){
            error("failed to init hid");
            return NULL;
        }
    }
    obj_counter++;

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
    hid_shutdown(hid);

    if (obj_counter){
        obj_counter--;
        if (!obj_counter){
            hid_exit();
        }
    }

    libusb_exit(hid->usb_context);
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
static wchar_t *get_usb_wstring(libusb_device_handle *dev, uint8_t idx)
{
    char buf[512];
    int len;
    wchar_t *str = NULL;


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
	str = (wchar_t*) malloc((len / 2 + 1) * sizeof(wchar_t));
	int i;
	for (i = 0; i < len / 2; i++) {
		str[i] = buf[i * 2 + 2] | (buf[i * 2 + 3] << 8);
	}
	str[len / 2] = 0x00000000;


    return str;
}

static int wchar2char(wchar_t * wstr, char * cstr, size_t count)
{
    if (!count--){
        return 0;
    }

    int len = 0;
    for(;*wstr && count--; wstr++, cstr++, len++){
        *cstr = *wstr;
    }
    *cstr = '\0';

    return len;
}

static void hid_shutdown(hid_t *hid)
{
    // first shutdown threads
    if (hid->poll_ms){
        hid->poll_ms = 0;
        pthread_join(hid->polling_thread, NULL);
    }

    // then close handle
    if (hid->handle){
        hid_close(hid->handle);
        hid->handle = NULL;
    }

    // and free store device
    if (hid->hiddev){
        hid_free_device(hid->hiddev);
        hid->hiddev = NULL;
    }

    if (hid->hid_map){
        usbhid_map_free(hid->hid_map);
        hid->hid_map = NULL;
    }

    if (hid->last_report){
        free(hid->last_report);
        hid->last_report = NULL;
    }

    if (hid->last_values){
        free(hid->last_values);
        hid->last_values = NULL;
    }

    if (hid->tmp_values){
        free(hid->tmp_values);
        hid->tmp_values = NULL;
    }

    if (hid->report_items){
        free(hid->report_items);
        hid->report_items = NULL;
    }
}


static int hid_get_device_list(hid_t *hid, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage, uint8_t max)
{
    libusb_device **devs;
    ssize_t cnt;


    cnt = libusb_get_device_list(hid->usb_context, &devs);
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

                    wchar_t * serial_wstring = NULL;
                    char serial_cstring[256];


                    if (desc.iSerialNumber > 0 && (serial_wstring = get_usb_wstring(handle, desc.iSerialNumber)) == NULL){
                        error("failed to load (required) serial for device %04x:%04x", desc.idVendor, desc.idProduct);
                    } else if (serial != NULL && wchar2char(serial_wstring, serial_cstring, sizeof(serial_cstring)) && strcmp(serial, serial_cstring) != 0){
                        free(serial_wstring);
                    } else {


                        hid_device_t * hiddev = calloc(1, sizeof(hid_device_t) + r);

                        hiddev->dev = dev;
                        libusb_ref_device(dev); // increase reference count

                        memcpy(&hiddev->desc, &desc, sizeof(struct libusb_device_descriptor));

                        // on macos causes a fault...
//                    hiddev->config = config;
//                    config = NULL;

                        hiddev->interface_num = interface_num;

                        hiddev->report_desc_len = r;
                        memcpy(hiddev->report_desc, report_desc, r);

                        /* Serial Number */
                        hiddev->serial_string = serial_wstring;

                        /* Manufacturer and Product strings */
                        if (desc.iManufacturer > 0)
                            hiddev->manufacturer_string =
                                    get_usb_wstring(handle, desc.iManufacturer);
                        if (desc.iProduct > 0)
                            hiddev->product_string =
                                    get_usb_wstring(handle, desc.iProduct);


                        /* Find the INPUT and OUTPUT endpoints. An
						   OUTPUT endpoint is not required. */
                        for (int e = 0; e < config->interface[k].altsetting[l].bNumEndpoints; e++) {
                            const struct libusb_endpoint_descriptor *ep = &config->interface[k].altsetting[l].endpoint[i];

                            /* Determine the type and direction of this
                               endpoint. */
                            int is_interrupt =
                                    (ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK)
                                    == LIBUSB_TRANSFER_TYPE_INTERRUPT;
                            int is_output =
                                    (ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
                                    == LIBUSB_ENDPOINT_OUT;
                            int is_input =
                                    (ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK)
                                    == LIBUSB_ENDPOINT_IN;

                            /* Decide whether to use it for input or output. */
                            if (hiddev->input_endpoint == 0 &&
                                is_interrupt && is_input) {
                                /* Use this endpoint for INPUT */
                                hiddev->input_endpoint = ep->bEndpointAddress;
                                hiddev->input_ep_max_packet_size = ep->wMaxPacketSize;
                            }
                            if (hiddev->output_endpoint == 0 &&
                                is_interrupt && is_output) {
                                /* Use this endpoint for OUTPUT */
                                hiddev->output_endpoint = ep->bEndpointAddress;
                            }
                        }

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

        char serial[256];
        char man[256];
        char product[256];

        if (hiddevs[i]->serial_string) {
            wchar2char(hiddevs[i]->serial_string, serial, sizeof(serial));
            SETSYMBOL(orgv + 4, gensym(serial));
        } else {
            SETSYMBOL(orgv + 4, gensym("-"));
        }

        if (hiddevs[i]->manufacturer_string) {
            wchar2char(hiddevs[i]->manufacturer_string, man, sizeof(man));
            SETSYMBOL(orgv + 5, gensym(man));
        } else {
            SETSYMBOL(orgv + 5, gensym("-"));
        }

        if (hiddevs[i]->product_string) {
            wchar2char(hiddevs[i]->product_string, product, sizeof(product));
            SETSYMBOL(orgv + 6, gensym(product));
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
        error("already open");
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




    if (usbhid_map_parse_desc(&hid->hid_map, hiddevs[0]->report_desc, hiddevs[0]->report_desc_len)){
        error("failed to parse HID descriptor");
        hid_free_device_list(hiddevs);
        return;
    }

    // TODO get default / validate report id

    if (usbhid_map_get_report_ids(hid->hid_map, Input(0), &hid->report_id, 1) == 0){
        error("no input reports");
    }

    hid->report_size = usbhid_map_get_report_size(hid->hid_map, Input(0), hid->report_id);
    hid->last_report = calloc(sizeof(uint8_t), hid->report_size);

    hid->value_count = usbhid_map_get_report_item_count(hid->hid_map, Input(0), hid->report_id);
    if (hid->value_count == 0){
        post("no items?");
        hid->report_items = NULL;
        hid->last_values = NULL;
        hid->tmp_values = NULL;
        hid->report_items = NULL;
    } else {
        hid->report_items = calloc(hid->value_count, sizeof(struct usbhid_map_item_st *));
        hid->last_values = calloc(hid->value_count, sizeof(int32_t));
        hid->tmp_values = calloc(hid->value_count, sizeof(int32_t));

        struct usbhid_map_item_st * first_item = usbhid_map_get_item(hid->hid_map, Input(0), hid->report_id, 0,0, NULL);
        for(size_t i = 0; i < hid->value_count; i++){
            hid->report_items[i] = first_item + i;

            post("page %d usage %d offset %d size %d", hid->report_items[i]->usage_page,hid->report_items[i]->usage,hid->report_items[i]->report_offset,hid->report_items[i]->report_size);
        }
    }


    post("report_id = %d (size %d, value count %d)", hid->report_id, hid->report_size, hid->value_count);


    hid->handle = hid_open(hiddevs[0]->desc.idVendor, hiddevs[0]->desc.idProduct, hiddevs[0]->serial_string);
    if (hid->handle < 0){
        error("failed to open");
        hid_free_device_list(hiddevs);
        return;
    }

    hid->poll_ms = 0;

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
        error("already closed");
    } else {
        hid_shutdown(hid);
    }

    outlet_anything(hid->out, gensym("closed"), 0, NULL);
}

static int hid_read_report(hid_t * hid)
{
    uint8_t data[128];


    size_t r = hid_read_timeout(hid->handle, data, sizeof(data), hid->poll_ms ? hid->poll_ms : 0);

    if (r > 0){

//
//        for(int i = 0; i < r; i++){
//            post("%02x", data[i]);
//        }
//
//        return r;
        if (r != hid->report_size){
            error("report size mismatch! expecting errors");
            hid->poll_ms = 0;
        }
        // only act when data is different
        else if (memcmp(data, hid->last_report, hid->report_size) != 0) {
//            post("report changed");

            // update last report
            memcpy(hid->last_report, data, hid->report_size);

            if (usbhid_map_extract_values(hid->tmp_values, hid->report_items, hid->value_count, data, hid->report_size)){
                error("usbhid_map_extract_values()");
            } else {

//                int changed = 0;
                for( size_t i = 0; i < hid->value_count; i++){

//                    post("%d := %d", hid->report_items[i]->usage, hid->tmp_values[i]);

                    if (hid->last_values[i] != hid->tmp_values[i])
                    {
//                        changed = 1;
//                        post("changed %d to %d", hid->report_items[i]->usage, hid->tmp_values[i]);

                        int32_t v = hid->tmp_values[i];

                        t_atom atoms[3];
                        SETFLOAT(atoms, hid->report_items[i]->usage_page);
                        SETFLOAT(atoms + 1, hid->report_items[i]->usage);
                        SETFLOAT(atoms + 2, v);

                        outlet_anything(hid->out, gensym("value"), 3, atoms);
                    }
                }

//                if (changed)
                {

//                    post("changed 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, 0x%02x, ",
//                         data[0],
//                         data[1],
//                         data[2],
//                         data[3],
//                         data[4],
//                         data[5],
//                         data[6]
//                    );
                }

//                int32_t * swap = hid->last_values;
//                hid->last_values = hid->tmp_values;
//                hid->tmp_values = swap;
                memcpy(hid->last_values, hid->tmp_values, hid->value_count * sizeof(int32_t));
            }
        }

    }

    return r > 0;
}

static void hid_cmd_bang(hid_t *hid)
{
    if (!hid->handle){
        error("device not open");
        return;
    }

    if (hid->poll_ms){
        error("polling already in process - it's pointless to bang");
        return;
    }

    hid_set_nonblocking(hid->handle, 1);

    while( hid_read_report(hid) );
}


static void * polling_thread_handler(void * ptr)
{
    hid_t * hid = ptr;

    hid_set_nonblocking(hid->handle, 0);

    while(hid->poll_ms){

        hid_read_report(hid);

        usleep(1000*hid->poll_ms);
    }

    return NULL;
}

static void hid_cmd_poll(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);

    if (!hid->handle){
        error("device not open");
        return;
    }

    if (argc != 1){
        error("requires an argument");
        return;
    }

    t_float p = atom_getfloat(argv);

    if (p < 0.0 || 1000.0 < p || fmod(p, 1.0) != 0.0){
        error("must be integer >= 0 <= 1000");
        return;
    }

    uint32_t poll_ms = p;

    if (hid->poll_ms && poll_ms == 0){
        hid->poll_ms = 0;
//        pthread_kill(hid->polling_thread, SIGCONT);
        pthread_join(hid->polling_thread, NULL);
    }

    // do nothing else if polling should stop
    if (poll_ms == 0){
        return;
    }

    hid->poll_ms = poll_ms;

    int r = pthread_create(&hid->polling_thread, NULL, polling_thread_handler, hid);

    if (r){
        error("pthread_create(): %d", r);
        hid->poll_ms = 0;
        return;
    }
}

static void hid_cmd_report_id(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(s);

    if (argc == 0){
        // do nothing
        t_atom atom_report_id;
        SETFLOAT(&atom_report_id, hid->report_id);
        outlet_anything(hid->out, gensym("report_id"), 1, &atom_report_id);
    } else if (argc == 1){
        hid->report_id = atom_getfloat(argv);
        //TODO set report
        return;
    } else {
        error("invalid argument count");
        return;
    }

}