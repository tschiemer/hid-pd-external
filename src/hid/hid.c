

#include "m_pd.h"
#include "libusb/libusb.h"
#include <stdlib.h>
#include <string.h>

#define UNUSED(x) (void)(x)

static t_class *hid_class;

typedef struct {
    t_object  x_obj;
    libusb_context * context;
//    t_inlet *in;
    t_outlet *out;
//    t_outlet *dbg_out;
//    bool runningStatusEnabled;
//    uint8_t runningStatusState;
} hid_t;


typedef struct {
    libusb_device * dev;
    struct libusb_device_descriptor desc;
//    struct libusb_config_descriptor *config;
    uint8_t interface_num;
    uint8_t report_desc[];
    #define hid_usage_page  report_desc[1]
    #define hid_usage       report_desc[3]
} hid_device_t;

void hid_setup(void);
static void * hid_new();
static void hid_free(hid_t *hid);

static void hid_anything(hid_t *hid, t_symbol *s, int argc, t_atom *argv);
//void midimessage_gen_anything(t_midimessage_gen *self, t_symbol *s, int argc, t_atom *argv);
//void midimessage_gen_generatorError(int code, uint8_t argc, uint8_t ** argv);

static void hid_cmd_list(hid_t *hid, t_symbol *s, int argc, t_atom *argv);

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


//    class_addmethod(hid_class,
//                    (t_method)hid_list
//                    , gensym("list"), 0);

//    class_addmethod(midi)
    class_addlist(hid_class, hid_anything);
    class_addanything(hid_class, hid_anything);


}


static void *hid_new()
{
    hid_t *hid = (hid_t *)pd_new(hid_class);

    int r = libusb_init(&hid->context);
    if (r < 0){
        error("failed to init libusb: %d", r);
        return NULL;
    }

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

static void hid_anything(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
{
    if (s == gensym("list")){
        hid_cmd_list(hid, s, argc, argv);
    } else {
        error("not a recognized command");
    }
}


static int hid_filter_device_list(libusb_device **devs, ssize_t count, hid_device_t ***hiddevs, uint16_t vendor, uint16_t product, char * serial, uint8_t usage_page, uint8_t usage)
{
    libusb_device *dev;
    int i = 0, o = 0;

    // let's assume that there will be fewer HID devices (interfaces) than original usb devices...
    *hiddevs = calloc(1+count,sizeof(hid_device_t));
    if (!*hiddevs){
        error("failed to allocate memory for HID devices list");
        return -1;
    }

    while ((dev = devs[i++]) != NULL) {
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

        for(int k = 0; k < config->bNumInterfaces; k++) {
//            printf("\t if %d / num_altsetting = %d\n", k, config->interface[k].num_altsetting);

            for (int l = 0; l < config->interface[k].num_altsetting; l++) {

                if (config->interface[k].altsetting[l].bInterfaceClass != LIBUSB_CLASS_HID){
                    continue;
                }

                libusb_device_handle *handle;
                r = libusb_open(dev, &handle);

                if (r < 0){
                    error("failed to open dev: %d", r);
                    continue;
                }


                uint8_t interface_num = config->interface[k].altsetting[l].bInterfaceNumber;

                uint8_t report_desc[256];
                r = libusb_control_transfer(handle, LIBUSB_ENDPOINT_IN|LIBUSB_RECIPIENT_INTERFACE, LIBUSB_REQUEST_GET_DESCRIPTOR, (LIBUSB_DT_REPORT << 8)|interface_num, 0, report_desc, sizeof(report_desc), 5000);
                if (r < 4){
                    error("control_transfer() fail for report descriptor (%04x:%04x): %d", desc.idVendor, desc.idProduct, r);
                } else if (report_desc[0] != 0x05 || report_desc[2] != 0x09) {
                    error("Notice: report descriptor not starting with usage page and usage bytes: %02x %02x %02x %02x", report_desc[0], report_desc[1], report_desc[2], report_desc[3]);
                } else if ((usage_page != 0 && usage_page != report_desc[1]) || (usage != 0 && usage != report_desc[3])) {
                    // filter out unwanted usages
                    // ie, do nothing
                } else if (o >= count){
                    error("Too many HID interfaces, skipping device %04x:%04x usage %d %d", desc.idVendor, desc.idProduct, report_desc[1], report_desc[3]);
                } else {

                    hid_device_t * hidDevice = malloc(sizeof(hid_device_t) + r);

                    hidDevice->dev = dev;
                    libusb_ref_device(dev); // increase reference count

                    memcpy(&hidDevice->desc, &desc, sizeof(struct libusb_device_descriptor));

//                    dev->config = NULL;

                    hidDevice->interface_num = interface_num;

                    memcpy(hidDevice->report_desc, report_desc, r);

                    (*hiddevs)[o++] = hidDevice;

                    post("usage (page) = %d (%d)", report_desc[3], report_desc[1]);

                }

                libusb_close(handle);
            }
        }

        libusb_free_config_descriptor(config);
    }

    // if not found any devices free list;
    if (o == 0){
        free(*hiddevs);
        *hiddevs = NULL;
    }

    return o;
}

static void hid_free_device_list(hid_device_t ** hiddevs)
{
    if (!hiddevs){
        return;
    }

    hid_device_t *dev;
    int i = 0;

    while( (dev = hiddevs[i++]) != NULL){
        libusb_unref_device(dev->dev);
//        if (dev->config != NULL) {
//            libusb_free_config_descriptor(dev->config);
//        }
        free(dev);
    }
}

static void hid_cmd_list(hid_t *hid, t_symbol *s, int argc, t_atom *argv)
{
    UNUSED(hid);
    UNUSED(s);
    UNUSED(argc);
    UNUSED(argv);

    post("list");

    uint16_t vendor = 0;
    uint16_t product = 0;
    char * serial = NULL;
    uint8_t usage_page = 0;
    uint8_t usage = 0;

    libusb_device **devs;
    ssize_t cnt;

    cnt = libusb_get_device_list(NULL, &devs);
    if (cnt < 0){
        error("Failed to get device list: %zd", cnt);
        return;
    }

    hid_device_t ** hiddevs;
    cnt = hid_filter_device_list(devs, cnt, &hiddevs, vendor, product, serial, usage_page, usage);

    libusb_free_device_list(devs, 1);

    if (cnt < 0){
        error("Error during filter operation: %zd", cnt);
        return;
    }

    post("list all devices now: %d", cnt);

    for(int i = 0; i < cnt; i++){
        int orgc = 4;
        t_atom orgv[5];
        SETFLOAT(orgv, hiddevs[i]->desc.idVendor);
        SETFLOAT(orgv+1, hiddevs[i]->desc.idProduct);
        SETFLOAT(orgv+2, hiddevs[i]->hid_usage_page);
        SETFLOAT(orgv+4, hiddevs[i]->hid_usage);
        outlet_anything(hid->out, gensym("dev"), orgc, orgv);
    }

    hid_free_device_list(hiddevs);
}