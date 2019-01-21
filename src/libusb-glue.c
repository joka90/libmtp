/*
 * \file libusb-glue.c
 * Low-level USB interface glue towards libusb.
 *
 * Copyright (C) 2005-2007 Richard A. Low <richard@wentnet.com>
 * Copyright (C) 2005-2008 Linus Walleij <triad@df.lth.se>
 * Copyright (C) 2006-2007 Marcus Meissner
 * Copyright (C) 2007 Ted Bullock
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Created by Richard Low on 24/12/2005. (as mtp-utils.c)
 * Modified by Linus Walleij 2006-03-06
 *  (Notice that Anglo-Saxons use little-endian dates and Swedes 
 *   use big-endian dates.)
 *
 */
#include "libmtp.h"
#include "libusb-glue.h"
#include "device-flags.h"
#include "util.h"
#include "ptp.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <usb.h>

#include "ptp-pack.c"

/* To enable debug prints, switch on this */
//#define ENABLE_USB_BULK_DEBUG

/* this must not be too short - the original 4000 was not long
   enough for big file transfers. I imagine the player spends a 
   bit of time gearing up to receiving lots of data. This also makes
   connecting/disconnecting more reliable */
#define USB_TIMEOUT		10000

/* USB control message data phase direction */
#ifndef USB_DP_HTD
#define USB_DP_HTD		(0x00 << 7)	/* host to device */
#endif
#ifndef USB_DP_DTH
#define USB_DP_DTH		(0x01 << 7)	/* device to host */
#endif

/* USB Feature selector HALT */
#ifndef USB_FEATURE_HALT
#define USB_FEATURE_HALT	0x00
#endif

/* Internal data types */
struct mtpdevice_list_struct {
  struct usb_device *libusb_device;
  PTPParams *params;
  PTP_USB *ptp_usb;
  uint32_t bus_location;
  struct mtpdevice_list_struct *next;
};
typedef struct mtpdevice_list_struct mtpdevice_list_t;

static const LIBMTP_device_entry_t mtp_device_table[] = {
/* We include an .h file which is shared between us and libgphoto2 */
#include "music-players.h"
};
static const int mtp_device_table_size = sizeof(mtp_device_table) / sizeof(LIBMTP_device_entry_t);

// Local functions
static struct usb_bus* init_usb();
static void close_usb(PTP_USB* ptp_usb);
static void find_interface_and_endpoints(struct usb_device *dev,
					 uint8_t *interface,
					 int* inep, 
					 int* inep_maxpacket, 
					 int* outep, 
					 int* outep_maxpacket, 
					 int* intep);
static void clear_stall(PTP_USB* ptp_usb);
static int init_ptp_usb (PTPParams* params, PTP_USB* ptp_usb, struct usb_device* dev);
static short ptp_write_func (unsigned long,PTPDataHandler*,void *data,unsigned long*);
static short ptp_read_func (unsigned long,PTPDataHandler*,void *data,unsigned long*,int);
static int usb_clear_stall_feature(PTP_USB* ptp_usb, int ep);
static int usb_get_endpoint_status(PTP_USB* ptp_usb, int ep, uint16_t* status);

/**
 * Get a list of the supported USB devices.
 *
 * The developers depend on users of this library to constantly
 * add in to the list of supported devices. What we need is the
 * device name, USB Vendor ID (VID) and USB Product ID (PID).
 * put this into a bug ticket at the project homepage, please.
 * The VID/PID is used to let e.g. udev lift the device to
 * console userspace access when it's plugged in.
 *
 * @param devices a pointer to a pointer that will hold a device
 *        list after the call to this function, if it was
 *        successful.
 * @param numdevs a pointer to an integer that will hold the number
 *        of devices in the device list if the call was successful.
 * @return 0 if the list was successfull retrieved, any other
 *        value means failure.
 */
int LIBMTP_Get_Supported_Devices_List(LIBMTP_device_entry_t ** const devices, int * const numdevs)
{
  *devices = (LIBMTP_device_entry_t *) &mtp_device_table;
  *numdevs = mtp_device_table_size;
  return 0;
}


static struct usb_bus* init_usb()
{
  usb_init();
  usb_find_busses();
  usb_find_devices();
  return (usb_get_busses());
}

/**
 * Small recursive function to append a new usb_device to the linked list of
 * USB MTP devices
 * @param devlist dynamic linked list of pointers to usb devices with MTP 
 *        properties, to be extended with new device.
 * @param newdevice the new device to add.
 * @param bus_location bus for this device.
 * @return an extended array or NULL on failure.
 */
static mtpdevice_list_t *append_to_mtpdevice_list(mtpdevice_list_t *devlist,
						  struct usb_device *newdevice,
						  uint32_t bus_location)
{
  mtpdevice_list_t *new_list_entry;
  
  new_list_entry = (mtpdevice_list_t *) malloc(sizeof(mtpdevice_list_t));
  if (new_list_entry == NULL) {
    return NULL;
  }
  // Fill in USB device, if we *HAVE* to make a copy of the device do it here.
  new_list_entry->libusb_device = newdevice;
  new_list_entry->bus_location = bus_location;
  new_list_entry->next = NULL;
  
  if (devlist == NULL) {
    return new_list_entry;
  } else {
    mtpdevice_list_t *tmp = devlist;
    while (tmp->next != NULL) {
      tmp = tmp->next;
    }
    tmp->next = new_list_entry;
  }
  return devlist;
}

/**
 * Small recursive function to free dynamic memory allocated to the linked list
 * of USB MTP devices
 * @param devlist dynamic linked list of pointers to usb devices with MTP 
 * properties.
 * @return nothing
 */
static void free_mtpdevice_list(mtpdevice_list_t *devlist)
{
  mtpdevice_list_t *tmplist = devlist;

  if (devlist == NULL)
    return;
  while (tmplist != NULL) {
    mtpdevice_list_t *tmp = tmplist;
    tmplist = tmplist->next;
    // Do not free() the fields (ptp_usb, params)! These are used elsewhere.
    free(tmp);
  }
  return;
}

/**
 * This checks if a device has an MTP descriptor. The descriptor was
 * elaborated about in gPhoto bug 1482084, and some official documentation
 * with no strings attached was published by Microsoft at
 * http://www.microsoft.com/whdc/system/bus/USB/USBFAQ_intermed.mspx#E3HAC
 *
 * @param dev a device struct from libusb.
 * @param dumpfile set to non-NULL to make the descriptors dump out
 *        to this file in human-readable hex so we can scruitinze them.
 * @return 1 if the device is MTP compliant, 0 if not.
 */
static int probe_device_descriptor(struct usb_device *dev, FILE *dumpfile)
{
  usb_dev_handle *devh;
  unsigned char buf[1024], cmd;
  int i;
  int ret;
  
  /* Don't examine hubs (no point in that) */
  if (dev->descriptor.bDeviceClass == USB_CLASS_HUB) {
    return 0;
  }
  
  /* Attempt to open Device on this port */
  devh = usb_open(dev);
  if (devh == NULL) {
    /* Could not open this device */
    return 0;
  }

  /*
   * Loop over the device configurations and interfaces. Nokia MTP-capable 
   * handsets (possibly others) typically have the string "MTP" in their 
   * MTP interface descriptions, that's how they can be detected, before
   * we try the more esoteric "OS descriptors" (below).
   */
  for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
    uint8_t j;
    
    for (j = 0; j < dev->config[i].bNumInterfaces; j++) {
      int k;
      for (k = 0; k < dev->config[i].interface[j].num_altsetting; k++) {
        buf[0] = '\0';
        ret = usb_get_string_simple(devh, 
				    dev->config[i].interface[j].altsetting[k].iInterface, 
				    (char *) buf, 
				    1024);
	if (ret < 3)
	  continue;
        if (strcmp((char *) buf, "MTP") == 0) {
	  if (dumpfile != NULL) {
            fprintf(dumpfile, "Configuration %d, interface %d, altsetting %d:\n", i, j, k);
	    fprintf(dumpfile, "   Interface description contains the string \"MTP\"\n");
	    fprintf(dumpfile, "   Device recognized as MTP, no further probing.\n");
	  }
          usb_close(devh);
          return 1;
        }
      }
    }
  }
  
  /* Read the special descriptor */
  ret = usb_get_descriptor(devh, 0x03, 0xee, buf, sizeof(buf));

  // Dump it, if requested
  if (dumpfile != NULL && ret > 0) {
    fprintf(dumpfile, "Microsoft device descriptor 0xee:\n");
    data_dump_ascii(dumpfile, buf, ret, 16);
  }
  
  /* Check if descriptor length is at least 10 bytes */
  if (ret < 10) {
    usb_close(devh);
    return 0;
  }
      
  /* Check if this device has a Microsoft Descriptor */
  if (!((buf[2] == 'M') && (buf[4] == 'S') &&
	(buf[6] == 'F') && (buf[8] == 'T'))) {
    usb_close(devh);
    return 0;
  }
      
  /* Check if device responds to control message 1 or if there is an error */
  cmd = buf[16];
  ret = usb_control_msg (devh,
			 USB_ENDPOINT_IN|USB_RECIP_DEVICE|USB_TYPE_VENDOR,
			 cmd,
			 0,
			 4,
			 (char *) buf,
			 sizeof(buf),
			 USB_TIMEOUT);

  // Dump it, if requested
  if (dumpfile != NULL && ret > 0) {
    fprintf(dumpfile, "Microsoft device response to control message 1, CMD 0x%02x:\n", cmd);
    data_dump_ascii(dumpfile, buf, ret, 16);
  }
  
  /* If this is true, the device either isn't MTP or there was an error */
  if (ret <= 0x15) {
    /* TODO: If there was an error, flag it and let the user know somehow */
    /* if(ret == -1) {} */
    usb_close(devh);
    return 0;
  }
  
  /* Check if device is MTP or if it is something like a USB Mass Storage 
     device with Janus DRM support */
  if ((buf[0x12] != 'M') || (buf[0x13] != 'T') || (buf[0x14] != 'P')) {
    usb_close(devh);
    return 0;
  }
      
  /* After this point we are probably dealing with an MTP device */

  /* Check if device responds to control message 2 or if there is an error*/
  ret = usb_control_msg (devh,
			 USB_ENDPOINT_IN|USB_RECIP_DEVICE|USB_TYPE_VENDOR,
			 cmd,
			 0,
			 5,
			 (char *) buf,
			 sizeof(buf),
			 USB_TIMEOUT);

  // Dump it, if requested
  if (dumpfile != NULL && ret > 0) {
    fprintf(dumpfile, "Microsoft device response to control message 2, CMD 0x%02x:\n", cmd);
    data_dump_ascii(dumpfile, buf, ret, 16);
  }
  
  /* If this is true, the device errored against control message 2 */
  if (ret == -1) {
    /* TODO: Implement callback function to let managing program know there
       was a problem, along with description of the problem */
    fprintf(stderr, "Potential MTP Device with VendorID:%04x and "
	    "ProductID:%04x encountered an error responding to "
	    "control message 2.\n"
	    "Problems may arrise but continuing\n",
	    dev->descriptor.idVendor, dev->descriptor.idProduct);
  } else if (ret <= 0x15) {
    /* TODO: Implement callback function to let managing program know there
       was a problem, along with description of the problem */
    fprintf(stderr, "Potential MTP Device with VendorID:%04x and "
	    "ProductID:%04x responded to control message 2 with a "
	    "response that was too short. Problems may arrise but "
	    "continuing\n",
	    dev->descriptor.idVendor, dev->descriptor.idProduct);
  } else if ((buf[0x12] != 'M') || (buf[0x13] != 'T') || (buf[0x14] != 'P')) {
    /* TODO: Implement callback function to let managing program know there
       was a problem, along with description of the problem */
    fprintf(stderr, "Potential MTP Device with VendorID:%04x and "
	    "ProductID:%04x encountered an error responding to "
	    "control message 2\n"
	    "Problems may arrise but continuing\n",
	    dev->descriptor.idVendor, dev->descriptor.idProduct);
  }
  
  /* Close the USB device handle */
  usb_close(devh);
  return 1;
}

/**
 * This function scans through the connected usb devices on a machine and
 * if they match known Vendor and Product identifiers appends them to the
 * dynamic array mtp_device_list. Be sure to call 
 * <code>free_mtpdevice_list(mtp_device_list)</code> when you are done 
 * with it, assuming it is not NULL.
 * @param mtp_device_list dynamic array of pointers to usb devices with MTP 
 *        properties (if this list is not empty, new entries will be appended
 *        to the list).
 * @return LIBMTP_ERROR_NONE implies that devices have been found, scan the list
 *        appropriately. LIBMTP_ERROR_NO_DEVICE_ATTACHED implies that no 
 *        devices have been found.
 */
static LIBMTP_error_number_t get_mtp_usb_device_list(mtpdevice_list_t ** mtp_device_list)
{
  struct usb_bus *bus = init_usb();
  for (; bus != NULL; bus = bus->next) {
    struct usb_device *dev = bus->devices;
    for (; dev != NULL; dev = dev->next) {
      if (dev->descriptor.bDeviceClass != USB_CLASS_HUB) {
	int i;
        int found = 0;

	// First check if we know about the device already.
	// Devices well known to us will not have their descriptors
	// probed, it caused problems with some devices.
        for(i = 0; i < mtp_device_table_size; i++) {
          if(dev->descriptor.idVendor == mtp_device_table[i].vendor_id &&
            dev->descriptor.idProduct == mtp_device_table[i].product_id) {
            /* Append this usb device to the MTP device list */
            *mtp_device_list = append_to_mtpdevice_list(*mtp_device_list, 
							dev, 
							bus->location);
            found = 1;
            break;
          }
        }
	// If we didn't know it, try probing the "OS Descriptor".
        if (!found) {
          if (probe_device_descriptor(dev, NULL)) {
            /* Append this usb device to the MTP USB Device List */
            *mtp_device_list = append_to_mtpdevice_list(*mtp_device_list, 
							dev,
							bus->location);
          }
        }
      }
    }
  }
  
  /* If nothing was found we end up here. */
  if(*mtp_device_list == NULL) {
    return LIBMTP_ERROR_NO_DEVICE_ATTACHED;
  }
  return LIBMTP_ERROR_NONE;
}

/**
 * Detect the raw MTP device descriptors and return a list of
 * of the devices found.
 * 
 * @param devices a pointer to a variable that will hold
 *        the list of raw devices found. This may be NULL
 *        on return if the number of detected devices is zero.
 *        The user shall simply <code>free()</code> this
 *        variable when finished with the raw devices,
 *        in order to release memory.
 * @param numdevs a pointer to an integer that will hold 
 *        the number of devices in the list. This may
 *        be 0.
 * @return 0 if successful, any other value means failure.
 */
LIBMTP_error_number_t LIBMTP_Detect_Raw_Devices(LIBMTP_raw_device_t ** devices, 
			      int * numdevs)
{
  mtpdevice_list_t *devlist = NULL;
  mtpdevice_list_t *dev;
  LIBMTP_error_number_t ret;
  LIBMTP_raw_device_t *retdevs;
  int devs = 0;
  int i, j;

  ret = get_mtp_usb_device_list(&devlist);
  if (ret == LIBMTP_ERROR_NO_DEVICE_ATTACHED) {
    *devices = NULL;
    *numdevs = 0;
    return ret;
  } else if (ret != LIBMTP_ERROR_NONE) {
    fprintf(stderr, "LIBMTP PANIC: get_mtp_usb_device_list() "
	    "error code: %d on line %d\n", ret, __LINE__);
    return ret;
  }

  // Get list size
  dev = devlist;
  while (dev != NULL) {
    devs++;
    dev = dev->next;
  }
  if (devs == 0) {
    *devices = NULL;
    *numdevs = 0;
    return LIBMTP_ERROR_NONE;
  }
  // Conjure a device list
  retdevs = (LIBMTP_raw_device_t *) malloc(sizeof(LIBMTP_raw_device_t) * devs);
  if (retdevs == NULL) {
    // Out of memory
    *devices = NULL;
    *numdevs = 0;
    return LIBMTP_ERROR_MEMORY_ALLOCATION;
  }
  dev = devlist;
  i = 0;
  while (dev != NULL) {
    int device_known = 0;

    // Assign default device info
    retdevs[i].device_entry.vendor = NULL;
    retdevs[i].device_entry.vendor_id = dev->libusb_device->descriptor.idVendor;
    retdevs[i].device_entry.product = NULL;
    retdevs[i].device_entry.product_id = dev->libusb_device->descriptor.idProduct;
    retdevs[i].device_entry.device_flags = 0x00000000U;
    // See if we can locate some additional vendor info and device flags
    for(j = 0; j < mtp_device_table_size; j++) {
      if(dev->libusb_device->descriptor.idVendor == mtp_device_table[j].vendor_id &&
	 dev->libusb_device->descriptor.idProduct == mtp_device_table[j].product_id) {
	device_known = 1;
	retdevs[i].device_entry.vendor = mtp_device_table[j].vendor;
	retdevs[i].device_entry.product = mtp_device_table[j].product;
	retdevs[i].device_entry.device_flags = mtp_device_table[j].device_flags;
#ifdef ENABLE_USB_BULK_DEBUG
	// This device is known to the developers
	fprintf(stderr, "Device %d (VID=%04x and PID=%04x) is a %s %s.\n", 
		i,
		dev->libusb_device->descriptor.idVendor,
		dev->libusb_device->descriptor.idProduct,
		mtp_device_table[j].vendor,
		mtp_device_table[j].product);
#endif
	break;
      }
    }
    if (!device_known) {
      // This device is unknown to the developers
      fprintf(stderr, "Device %d (VID=%04x and PID=%04x) is UNKNOWN.\n", 
	      i,
	      dev->libusb_device->descriptor.idVendor,
	      dev->libusb_device->descriptor.idProduct);
      fprintf(stderr, "Please report this VID/PID and the device model to the "
	      "libmtp development team\n");
      /*
       * Trying to get iManufacturer or iProduct from the device at this
       * point would require opening a device handle, that we don't want
       * to do right now. (Takes time for no good enough reason.)
       */
    }
    // Save the location on the bus
    retdevs[i].bus_location = dev->bus_location;
    retdevs[i].devnum = dev->libusb_device->devnum;
    i++;
    dev = dev->next;
  }  
  *devices = retdevs;
  *numdevs = i;
  free_mtpdevice_list(devlist);
  return LIBMTP_ERROR_NONE;
}

/**
 * This routine just dumps out low-level
 * USB information about the current device.
 * @param ptp_usb the USB device to get information from.
 */
void dump_usbinfo(PTP_USB *ptp_usb)
{
  int res;
  struct usb_device *dev;

#ifdef LIBUSB_HAS_GET_DRIVER_NP
  char devname[0x10];
  
  devname[0] = '\0';
  res = usb_get_driver_np(ptp_usb->handle, (int) ptp_usb->interface, devname, sizeof(devname));
  if (devname[0] != '\0') {
    printf("   Using kernel interface \"%s\"\n", devname);
  }
#endif
  dev = usb_device(ptp_usb->handle);
  printf("   bcdUSB: %d\n", dev->descriptor.bcdUSB);
  printf("   bDeviceClass: %d\n", dev->descriptor.bDeviceClass);
  printf("   bDeviceSubClass: %d\n", dev->descriptor.bDeviceSubClass);
  printf("   bDeviceProtocol: %d\n", dev->descriptor.bDeviceProtocol);
  printf("   idVendor: %04x\n", dev->descriptor.idVendor);
  printf("   idProduct: %04x\n", dev->descriptor.idProduct);
  printf("   IN endpoint maxpacket: %d bytes\n", ptp_usb->inep_maxpacket);
  printf("   OUT endpoint maxpacket: %d bytes\n", ptp_usb->outep_maxpacket);
  printf("   Raw device info:\n");
  printf("      Bus location: %d\n", ptp_usb->rawdevice.bus_location);
  printf("      Device number: %d\n", ptp_usb->rawdevice.devnum);
  printf("      Device entry info:\n");
  printf("         Vendor: %s\n", ptp_usb->rawdevice.device_entry.vendor);
  printf("         Vendor id: 0x%04x\n", ptp_usb->rawdevice.device_entry.vendor_id);
  printf("         Product: %s\n", ptp_usb->rawdevice.device_entry.product);
  printf("         Vendor id: 0x%04x\n", ptp_usb->rawdevice.device_entry.product_id);
  printf("         Device flags: 0x%08x\n", ptp_usb->rawdevice.device_entry.device_flags);
  (void) probe_device_descriptor(dev, stdout);
}

/**
 * Retrieve the apropriate playlist extension for this
 * device. Rather hacky at the moment. This is probably
 * desired by the managing software, but when creating
 * lists on the device itself you notice certain preferences.
 * @param ptp_usb the USB device to get suggestion for.
 * @return the suggested playlist extension.
 */
char const * const get_playlist_extension(PTP_USB *ptp_usb)
{
  struct usb_device *dev;
  static char creative_pl_extension[] = ".zpl";
  static char default_pl_extension[] = ".pla";

  dev = usb_device(ptp_usb->handle);
  if (dev->descriptor.idVendor == 0x041e) {
    return creative_pl_extension;
  }
  return default_pl_extension;
}

static void
ptp_debug (PTPParams *params, const char *format, ...)
{  
        va_list args;

        va_start (args, format);
        if (params->debug_func!=NULL)
                params->debug_func (params->data, format, args);
        else
	{
                vfprintf (stderr, format, args);
		fprintf (stderr,"\n");
		fflush (stderr);
	}
        va_end (args);
}  

static void
ptp_error (PTPParams *params, const char *format, ...)
{  
        va_list args;

        va_start (args, format);
        if (params->error_func!=NULL)
                params->error_func (params->data, format, args);
        else
	{
                vfprintf (stderr, format, args);
		fprintf (stderr,"\n");
		fflush (stderr);
	}
        va_end (args);
}


/*
 * ptp_read_func() and ptp_write_func() are
 * based on same functions usb.c in libgphoto2.
 * Much reading packet logs and having fun with trials and errors
 * reveals that WMP / Windows is probably using an algorithm like this
 * for large transfers:
 *
 * 1. Send the command (0x0c bytes) if headers are split, else, send 
 *    command plus sizeof(endpoint) - 0x0c bytes.
 * 2. Send first packet, max size to be sizeof(endpoint) but only when using
 *    split headers. Else goto 3.
 * 3. REPEAT send 0x10000 byte chunks UNTIL remaining bytes < 0x10000
 *    We call 0x10000 CONTEXT_BLOCK_SIZE.
 * 4. Send remaining bytes MOD sizeof(endpoint)
 * 5. Send remaining bytes. If this happens to be exactly sizeof(endpoint)
 *    then also send a zero-length package.
 *
 * Further there is some special quirks to handle zero reads from the
 * device, since some devices can't do them at all due to shortcomings
 * of the USB slave controller in the device.
 */
#define CONTEXT_BLOCK_SIZE_1	0x3e00
#define CONTEXT_BLOCK_SIZE_2  0x200
#define CONTEXT_BLOCK_SIZE    CONTEXT_BLOCK_SIZE_1+CONTEXT_BLOCK_SIZE_2
static short
ptp_read_func (
	unsigned long size, PTPDataHandler *handler,void *data,
	unsigned long *readbytes,
	int readzero
) {
  PTP_USB *ptp_usb = (PTP_USB *)data;
  unsigned long toread = 0;
  int result = 0;
  unsigned long curread = 0;
  unsigned long written;
  unsigned char *bytes;
  int expect_terminator_byte = 0;

  // This is the largest block we'll need to read in.
  bytes = malloc(CONTEXT_BLOCK_SIZE);
  while (curread < size) {
    
#ifdef ENABLE_USB_BULK_DEBUG
    printf("Remaining size to read: 0x%04lx bytes\n", size - curread);
#endif
    // check equal to condition here
    if (size - curread < CONTEXT_BLOCK_SIZE)
    {
      // this is the last packet
      toread = size - curread;
      // this is equivalent to zero read for these devices
      if (readzero && FLAG_NO_ZERO_READS(ptp_usb) && toread % 64 == 0) {
        toread += 1;
        expect_terminator_byte = 1;
      }
    }
    else if (curread == 0)
      // we are first packet, but not last packet
      toread = CONTEXT_BLOCK_SIZE_1;
    else if (toread == CONTEXT_BLOCK_SIZE_1)
      toread = CONTEXT_BLOCK_SIZE_2;
    else if (toread == CONTEXT_BLOCK_SIZE_2)
      toread = CONTEXT_BLOCK_SIZE_1;
    else
      printf("unexpected toread size 0x%04x, 0x%04x remaining bytes\n", 
	     (unsigned int) toread, (unsigned int) (size-curread));

#ifdef ENABLE_USB_BULK_DEBUG
    printf("Reading in 0x%04lx bytes\n", toread);
#endif
    result = USB_BULK_READ(ptp_usb->handle, ptp_usb->inep, (char*)bytes, toread, USB_TIMEOUT);
#ifdef ENABLE_USB_BULK_DEBUG
    printf("Result of read: 0x%04x\n", result);
#endif
        
    if (result < 0) {
      return PTP_ERROR_IO;
    }
#ifdef ENABLE_USB_BULK_DEBUG
    printf("<==USB IN\n");
    if (result == 0)
      printf("Zero Read\n");
    else
      data_dump_ascii (stdout,bytes,result,16);
#endif
    
    // want to discard extra byte
    if (expect_terminator_byte && result == toread)
    {
#ifdef ENABLE_USB_BULK_DEBUG
      printf("<==USB IN\nDiscarding extra byte\n");
#endif
      result--;
    }
    
    handler->putfunc(NULL, handler->private, result, bytes, &written);
    
    ptp_usb->current_transfer_complete += result;
    curread += result;

    // Increase counters, call callback
    if (ptp_usb->callback_active) {
      if (ptp_usb->current_transfer_complete >= ptp_usb->current_transfer_total) {
	// send last update and disable callback.
	ptp_usb->current_transfer_complete = ptp_usb->current_transfer_total;
	ptp_usb->callback_active = 0;
      }
      if (ptp_usb->current_transfer_callback != NULL) {
	int ret;
	ret = ptp_usb->current_transfer_callback(ptp_usb->current_transfer_complete,
						 ptp_usb->current_transfer_total,
						 ptp_usb->current_transfer_callback_data);
	if (ret != 0) {
	  return PTP_ERROR_CANCEL;
	}
      }
    }  

    if (result < toread) /* short reads are common */
      break;
  }
  if (readbytes) *readbytes = curread;
  free (bytes);
  
  // there might be a zero packet waiting for us...
  if (readzero && 
      !FLAG_NO_ZERO_READS(ptp_usb) && 
      curread % ptp_usb->outep_maxpacket == 0) {
    char temp;
    int zeroresult = 0;

#ifdef ENABLE_USB_BULK_DEBUG
    printf("<==USB IN\n");
    printf("Zero Read\n");
#endif
    zeroresult = USB_BULK_READ(ptp_usb->handle, ptp_usb->inep, &temp, 0, USB_TIMEOUT);
    if (zeroresult != 0)
      printf("LIBMTP panic: unable to read in zero packet, response 0x%04x", zeroresult);
  }
  
  if (result > 0) {
    return (PTP_RC_OK);
  } else {
    return PTP_ERROR_IO;
  }
}

static short
ptp_write_func (
        unsigned long   size,
        PTPDataHandler  *handler,
        void            *data,
        unsigned long   *written
) {
  PTP_USB *ptp_usb = (PTP_USB *)data;
  unsigned long towrite = 0;
  int result = 0;
  unsigned long curwrite = 0;
  unsigned char *bytes;

  // This is the largest block we'll need to read in.  
  bytes = malloc(CONTEXT_BLOCK_SIZE);
  if (!bytes) {
    return PTP_ERROR_IO;
  }
  while (curwrite < size) {
    towrite = size-curwrite;
    if (towrite > CONTEXT_BLOCK_SIZE) {
      towrite = CONTEXT_BLOCK_SIZE;
    } else {
      // This magic makes packets the same size that WMP send them.
      if (towrite > ptp_usb->outep_maxpacket && towrite % ptp_usb->outep_maxpacket != 0) {
        towrite -= towrite % ptp_usb->outep_maxpacket;
      }
    }
    handler->getfunc(NULL, handler->private,towrite,bytes,&towrite);
    result = USB_BULK_WRITE(ptp_usb->handle,ptp_usb->outep,(char*)bytes,towrite,USB_TIMEOUT);
#ifdef ENABLE_USB_BULK_DEBUG
    printf("USB OUT==>\n");
    data_dump_ascii (stdout,bytes,towrite,16);
#endif
    if (result < 0) {
      return PTP_ERROR_IO;
    }
    // Increase counters
    ptp_usb->current_transfer_complete += result;
    curwrite += result;

    // call callback
    if (ptp_usb->callback_active) {
      if (ptp_usb->current_transfer_complete >= ptp_usb->current_transfer_total) {
	// send last update and disable callback.
	ptp_usb->current_transfer_complete = ptp_usb->current_transfer_total;
	ptp_usb->callback_active = 0;
      }
      if (ptp_usb->current_transfer_callback != NULL) {
	int ret;
	ret = ptp_usb->current_transfer_callback(ptp_usb->current_transfer_complete,
						 ptp_usb->current_transfer_total,
						 ptp_usb->current_transfer_callback_data);
	if (ret != 0) {
	  return PTP_ERROR_CANCEL;
	}
      }
    }
    if (result < towrite) /* short writes happen */
      break;
  }
  free (bytes);
  if (written) {
    *written = curwrite;
  }
  

  // If this is the last transfer send a zero write if required
  if (ptp_usb->current_transfer_complete >= ptp_usb->current_transfer_total) {
    if ((towrite % ptp_usb->outep_maxpacket) == 0) {
#ifdef ENABLE_USB_BULK_DEBUG
      printf("USB OUT==>\n");
      printf("Zero Write\n");
#endif
      result=USB_BULK_WRITE(ptp_usb->handle,ptp_usb->outep,(char *)"x",0,USB_TIMEOUT);
    }
  }
    
  if (result < 0)
    return PTP_ERROR_IO;
  return PTP_RC_OK;
}

/* memory data get/put handler */
typedef struct {
	unsigned char	*data;
	unsigned long	size, curoff;
} PTPMemHandlerPrivate;

static uint16_t
memory_getfunc(PTPParams* params, void* private,
	       unsigned long wantlen, unsigned char *data,
	       unsigned long *gotlen
) {
	PTPMemHandlerPrivate* priv = (PTPMemHandlerPrivate*)private;
	unsigned long tocopy = wantlen;

	if (priv->curoff + tocopy > priv->size)
		tocopy = priv->size - priv->curoff;
	memcpy (data, priv->data + priv->curoff, tocopy);
	priv->curoff += tocopy;
	*gotlen = tocopy;
	return PTP_RC_OK;
}

static uint16_t
memory_putfunc(PTPParams* params, void* private,
	       unsigned long sendlen, unsigned char *data,
	       unsigned long *putlen
) {
	PTPMemHandlerPrivate* priv = (PTPMemHandlerPrivate*)private;

	if (priv->curoff + sendlen > priv->size) {
		priv->data = realloc (priv->data, priv->curoff+sendlen);
		priv->size = priv->curoff + sendlen;
	}
	memcpy (priv->data + priv->curoff, data, sendlen);
	priv->curoff += sendlen;
	*putlen = sendlen;
	return PTP_RC_OK;
}

/* init private struct for receiving data. */
static uint16_t
ptp_init_recv_memory_handler(PTPDataHandler *handler) {
	PTPMemHandlerPrivate* priv;
	priv = malloc (sizeof(PTPMemHandlerPrivate));
	handler->private = priv;
	handler->getfunc = memory_getfunc;
	handler->putfunc = memory_putfunc;
	priv->data = NULL;
	priv->size = 0;
	priv->curoff = 0;
	return PTP_RC_OK;
}

/* init private struct and put data in for sending data.
 * data is still owned by caller.
 */
static uint16_t
ptp_init_send_memory_handler(PTPDataHandler *handler,
	unsigned char *data, unsigned long len
) {
	PTPMemHandlerPrivate* priv;
	priv = malloc (sizeof(PTPMemHandlerPrivate));
	if (!priv)
		return PTP_RC_GeneralError;
	handler->private = priv;
	handler->getfunc = memory_getfunc;
	handler->putfunc = memory_putfunc;
	priv->data = data;
	priv->size = len;
	priv->curoff = 0;
	return PTP_RC_OK;
}

/* free private struct + data */
static uint16_t
ptp_exit_send_memory_handler (PTPDataHandler *handler) {
	PTPMemHandlerPrivate* priv = (PTPMemHandlerPrivate*)handler->private;
	/* data is owned by caller */
	free (priv);
	return PTP_RC_OK;
}

/* hand over our internal data to caller */
static uint16_t
ptp_exit_recv_memory_handler (PTPDataHandler *handler,
	unsigned char **data, unsigned long *size
) {
	PTPMemHandlerPrivate* priv = (PTPMemHandlerPrivate*)handler->private;
	*data = priv->data;
	*size = priv->size;
	free (priv);
	return PTP_RC_OK;
}

/* send / receive functions */

uint16_t
ptp_usb_sendreq (PTPParams* params, PTPContainer* req)
{
	uint16_t ret;
	PTPUSBBulkContainer usbreq;
	PTPDataHandler	memhandler;
	unsigned long written = 0;
	unsigned long towrite;
#ifdef ENABLE_USB_BULK_DEBUG
	char txt[256];

	(void) ptp_render_opcode (params, req->Code, sizeof(txt), txt);
	printf("REQUEST: 0x%04x, %s\n", req->Code, txt);
#endif
	/* build appropriate USB container */
	usbreq.length=htod32(PTP_USB_BULK_REQ_LEN-
		(sizeof(uint32_t)*(5-req->Nparam)));
	usbreq.type=htod16(PTP_USB_CONTAINER_COMMAND);
	usbreq.code=htod16(req->Code);
	usbreq.trans_id=htod32(req->Transaction_ID);
	usbreq.payload.params.param1=htod32(req->Param1);
	usbreq.payload.params.param2=htod32(req->Param2);
	usbreq.payload.params.param3=htod32(req->Param3);
	usbreq.payload.params.param4=htod32(req->Param4);
	usbreq.payload.params.param5=htod32(req->Param5);
	/* send it to responder */
	towrite = PTP_USB_BULK_REQ_LEN-(sizeof(uint32_t)*(5-req->Nparam));
	ptp_init_send_memory_handler (&memhandler, (unsigned char*)&usbreq, towrite);
	ret=ptp_write_func(
		towrite,
		&memhandler,
		params->data,
		&written
	);
	ptp_exit_send_memory_handler (&memhandler);
	if (ret!=PTP_RC_OK && ret!=PTP_ERROR_CANCEL) {
		ret = PTP_ERROR_IO;
	}
	if (written != towrite && ret != PTP_ERROR_CANCEL && ret != PTP_ERROR_IO) {
		ptp_error (params, 
			"PTP: request code 0x%04x sending req wrote only %ld bytes instead of %d",
			req->Code, written, towrite
		);
		ret = PTP_ERROR_IO;
	}
	return ret;
}

uint16_t
ptp_usb_senddata (PTPParams* params, PTPContainer* ptp,
		  unsigned long size, PTPDataHandler *handler
) {
	uint16_t ret;
	int wlen, datawlen;
	unsigned long written;
	PTPUSBBulkContainer usbdata;
	uint32_t bytes_left_to_transfer;
	PTPDataHandler memhandler;

#ifdef ENABLE_USB_BULK_DEBUG
	printf("SEND DATA PHASE\n");
#endif
	/* build appropriate USB container */
	usbdata.length	= htod32(PTP_USB_BULK_HDR_LEN+size);
	usbdata.type	= htod16(PTP_USB_CONTAINER_DATA);
	usbdata.code	= htod16(ptp->Code);
	usbdata.trans_id= htod32(ptp->Transaction_ID);
  
	((PTP_USB*)params->data)->current_transfer_complete = 0;
	((PTP_USB*)params->data)->current_transfer_total = size+PTP_USB_BULK_HDR_LEN;

	if (params->split_header_data) {
		datawlen = 0;
		wlen = PTP_USB_BULK_HDR_LEN;
	} else {
		unsigned long gotlen;
		/* For all camera devices. */
		datawlen = (size<PTP_USB_BULK_PAYLOAD_LEN_WRITE)?size:PTP_USB_BULK_PAYLOAD_LEN_WRITE;
		wlen = PTP_USB_BULK_HDR_LEN + datawlen;
		ret = handler->getfunc(params, handler->private, datawlen, usbdata.payload.data, &gotlen);
		if (ret != PTP_RC_OK)
			return ret;
		if (gotlen != datawlen)
			return PTP_RC_GeneralError;
	}
	ptp_init_send_memory_handler (&memhandler, (unsigned char *)&usbdata, wlen);
	/* send first part of data */
	ret = ptp_write_func(wlen, &memhandler, params->data, &written);
	ptp_exit_send_memory_handler (&memhandler);
	if (ret!=PTP_RC_OK) {
		return ret;
	}
	if (size <= datawlen) return ret;
	/* if everything OK send the rest */
	bytes_left_to_transfer = size-datawlen;
	ret = PTP_RC_OK;
	while(bytes_left_to_transfer > 0) {
		ret = ptp_write_func (bytes_left_to_transfer, handler, params->data, &written);
		if (ret != PTP_RC_OK)
			break;
		if (written == 0) {
			ret = PTP_ERROR_IO;
			break;
		}
		bytes_left_to_transfer -= written;
	}
	if (ret!=PTP_RC_OK && ret!=PTP_ERROR_CANCEL)
		ret = PTP_ERROR_IO;
	return ret;
}

static uint16_t ptp_usb_getpacket(PTPParams *params,
		PTPUSBBulkContainer *packet, unsigned long *rlen)
{
	PTPDataHandler	memhandler;
	uint16_t	ret;
	unsigned char	*x = NULL;

	/* read the header and potentially the first data */
	if (params->response_packet_size > 0) {
		/* If there is a buffered packet, just use it. */
		memcpy(packet, params->response_packet, params->response_packet_size);
		*rlen = params->response_packet_size;
		free(params->response_packet);
		params->response_packet = NULL;
		params->response_packet_size = 0;
		/* Here this signifies a "virtual read" */
		return PTP_RC_OK;
	}
	ptp_init_recv_memory_handler (&memhandler);
	ret = ptp_read_func(PTP_USB_BULK_HS_MAX_PACKET_LEN_READ, &memhandler, params->data, rlen, 0);
	ptp_exit_recv_memory_handler (&memhandler, &x, rlen);
	if (x) {
		memcpy (packet, x, *rlen);
		free (x);
	}
	return ret;
}

uint16_t
ptp_usb_getdata (PTPParams* params, PTPContainer* ptp, PTPDataHandler *handler)
{
	uint16_t ret;
	PTPUSBBulkContainer usbdata;
	unsigned long	written;
	PTP_USB *ptp_usb = (PTP_USB *) params->data;

#ifdef ENABLE_USB_BULK_DEBUG
	printf("GET DATA PHASE\n");
#endif
	memset(&usbdata,0,sizeof(usbdata));
	do {
		unsigned long len, rlen;

		ret = ptp_usb_getpacket(params, &usbdata, &rlen);
		if (ret!=PTP_RC_OK) {
			ret = PTP_ERROR_IO;
			break;
		}
		if (dtoh16(usbdata.type)!=PTP_USB_CONTAINER_DATA) {
			ret = PTP_ERROR_DATA_EXPECTED;
			break;
		}
		if (dtoh16(usbdata.code)!=ptp->Code) {
			if (FLAG_IGNORE_HEADER_ERRORS(ptp_usb)) {
				ptp_debug (params, "ptp2/ptp_usb_getdata: detected a broken "
					   "PTP header, code field insane, expect problems! (But continuing)");
				// Repair the header, so it won't wreak more havoc, don't just ignore it.
				// Typically these two fields will be broken.
				usbdata.code	 = htod16(ptp->Code);
				usbdata.trans_id = htod32(ptp->Transaction_ID);
				ret = PTP_RC_OK;
			} else {
				ret = dtoh16(usbdata.code);
				// This filters entirely insane garbage return codes, but still
				// makes it possible to return error codes in the code field when
				// getting data. It appears Windows ignores the contents of this 
				// field entirely.
				if (ret < PTP_RC_Undefined || ret > PTP_RC_SpecificationOfDestinationUnsupported) {
					ptp_debug (params, "ptp2/ptp_usb_getdata: detected a broken "
						   "PTP header, code field insane.");
					ret = PTP_ERROR_IO;
				}
				break;
			}
		}
		if (usbdata.length == 0xffffffffU) {
			/* stuff data directly to passed data handler */
			while (1) {
				unsigned long readdata;
				int xret;

				xret = ptp_read_func(
					PTP_USB_BULK_HS_MAX_PACKET_LEN_READ,
					handler,
					params->data,
					&readdata,
					0
				);
				if (xret != PTP_RC_OK)
					return ret;
				if (readdata < PTP_USB_BULK_HS_MAX_PACKET_LEN_READ)
					break;
			}
			return PTP_RC_OK;
		}
		if (rlen > dtoh32(usbdata.length)) {
			/*
			 * Buffer the surplus response packet if it is >=
			 * PTP_USB_BULK_HDR_LEN
			 * (i.e. it is probably an entire package)
			 * else discard it as erroneous surplus data.
			 * This will even work if more than 2 packets appear
			 * in the same transaction, they will just be handled
			 * iteratively.
			 *
			 * Marcus observed stray bytes on iRiver devices;
			 * these are still discarded.
			 */
			unsigned int packlen = dtoh32(usbdata.length);
			unsigned int surplen = rlen - packlen;

			if (surplen >= PTP_USB_BULK_HDR_LEN) {
				params->response_packet = malloc(surplen);
				memcpy(params->response_packet,
				       (uint8_t *) &usbdata + packlen, surplen);
				params->response_packet_size = surplen;
			/* Ignore reading one extra byte if device flags have been set */
			} else if(!FLAG_NO_ZERO_READS(ptp_usb) &&
				  (rlen - dtoh32(usbdata.length) == 1)) {
			  ptp_debug (params, "ptp2/ptp_usb_getdata: read %d bytes "
				     "too much, expect problems!", 
				     rlen - dtoh32(usbdata.length));
			}
			rlen = packlen;
		}

		/* For most PTP devices rlen is 512 == sizeof(usbdata)
		 * here. For MTP devices splitting header and data it might
		 * be 12.
		 */
		/* Evaluate full data length. */
		len=dtoh32(usbdata.length)-PTP_USB_BULK_HDR_LEN;

		/* autodetect split header/data MTP devices */
		if (dtoh32(usbdata.length) > 12 && (rlen==12))
			params->split_header_data = 1;

		/* Copy first part of data to 'data' */
		handler->putfunc(
			params, handler->private, rlen - PTP_USB_BULK_HDR_LEN, usbdata.payload.data,
			&written
		);
    
		if (FLAG_NO_ZERO_READS(ptp_usb) && 
		    len+PTP_USB_BULK_HDR_LEN == PTP_USB_BULK_HS_MAX_PACKET_LEN_READ) {
#ifdef ENABLE_USB_BULK_DEBUG
		  printf("Reading in extra terminating byte\n");
#endif
		  // need to read in extra byte and discard it
		  int result = 0;
		  char byte = 0;
		  result = USB_BULK_READ(ptp_usb->handle, ptp_usb->inep, &byte, 1, USB_TIMEOUT);
		  
		  if (result != 1)
		    printf("Could not read in extra byte for PTP_USB_BULK_HS_MAX_PACKET_LEN_READ long file, return value 0x%04x\n", result);
		} else if (len+PTP_USB_BULK_HDR_LEN == PTP_USB_BULK_HS_MAX_PACKET_LEN_READ && params->split_header_data == 0) {
		  int zeroresult = 0;
		  char zerobyte = 0;

#ifdef ENABLE_USB_BULK_DEBUG
		  printf("Reading in zero packet after header\n");
#endif
		  zeroresult = USB_BULK_READ(ptp_usb->handle, ptp_usb->inep, &zerobyte, 0, USB_TIMEOUT);
		  
		  if (zeroresult != 0)
		    printf("LIBMTP panic: unable to read in zero packet, response 0x%04x", zeroresult);
		}
		
		/* Is that all of data? */
		if (len+PTP_USB_BULK_HDR_LEN<=rlen) {
		  break;
		}
		
		ret = ptp_read_func(len - (rlen - PTP_USB_BULK_HDR_LEN),
				    handler,
				    params->data, &rlen, 1);
		
		if (ret!=PTP_RC_OK) {
		  break;
		}
	} while (0);
	return ret;
}

uint16_t
ptp_usb_getresp (PTPParams* params, PTPContainer* resp)
{
	uint16_t ret;
	unsigned long rlen;
	PTPUSBBulkContainer usbresp;
	PTP_USB *ptp_usb = (PTP_USB *)(params->data);

#ifdef ENABLE_USB_BULK_DEBUG
	printf("RESPONSE: ");
#endif
	memset(&usbresp,0,sizeof(usbresp));
	/* read response, it should never be longer than sizeof(usbresp) */
	ret = ptp_usb_getpacket(params, &usbresp, &rlen);

	// Fix for bevahiour reported by Scott Snyder on Samsung YP-U3. The player
	// sends a packet containing just zeroes of length 2 (up to 4 has been seen too)
	// after a NULL packet when it should send the response. This code ignores
	// such illegal packets.
	while (ret==PTP_RC_OK && rlen<PTP_USB_BULK_HDR_LEN && usbresp.length==0) {
	  ptp_debug (params, "ptp_usb_getresp: detected short response "
		     "of %d bytes, expect problems! (re-reading "
		     "response), rlen");
	  ret = ptp_usb_getpacket(params, &usbresp, &rlen);
	}

	if (ret!=PTP_RC_OK) {
		ret = PTP_ERROR_IO;
	} else
	if (dtoh16(usbresp.type)!=PTP_USB_CONTAINER_RESPONSE) {
		ret = PTP_ERROR_RESP_EXPECTED;
	} else
	if (dtoh16(usbresp.code)!=resp->Code) {
		ret = dtoh16(usbresp.code);
	}
#ifdef ENABLE_USB_BULK_DEBUG
	printf("%04x\n", ret);
#endif
	if (ret!=PTP_RC_OK) {
/*		ptp_error (params,
		"PTP: request code 0x%04x getting resp error 0x%04x",
			resp->Code, ret);*/
		return ret;
	}
	/* build an appropriate PTPContainer */
	resp->Code=dtoh16(usbresp.code);
	resp->SessionID=params->session_id;
	resp->Transaction_ID=dtoh32(usbresp.trans_id);
	if (FLAG_IGNORE_HEADER_ERRORS(ptp_usb)) {
		if (resp->Transaction_ID != params->transaction_id-1) {
			ptp_debug (params, "ptp_usb_getresp: detected a broken "
				   "PTP header, transaction ID insane, expect "
				   "problems! (But continuing)");
			// Repair the header, so it won't wreak more havoc.
			resp->Transaction_ID = params->transaction_id-1;
		}
	}
	resp->Param1=dtoh32(usbresp.payload.params.param1);
	resp->Param2=dtoh32(usbresp.payload.params.param2);
	resp->Param3=dtoh32(usbresp.payload.params.param3);
	resp->Param4=dtoh32(usbresp.payload.params.param4);
	resp->Param5=dtoh32(usbresp.payload.params.param5);
	return ret;
}

/* Event handling functions */

/* PTP Events wait for or check mode */
#define PTP_EVENT_CHECK			0x0000	/* waits for */
#define PTP_EVENT_CHECK_FAST		0x0001	/* checks */

static inline uint16_t
ptp_usb_event (PTPParams* params, PTPContainer* event, int wait)
{
	uint16_t ret;
	int result;
	unsigned long rlen;
	PTPUSBEventContainer usbevent;
	PTP_USB *ptp_usb = (PTP_USB *)(params->data);

	memset(&usbevent,0,sizeof(usbevent));

	if ((params==NULL) || (event==NULL)) 
		return PTP_ERROR_BADPARAM;
	ret = PTP_RC_OK;
	switch(wait) {
	case PTP_EVENT_CHECK:
		result=USB_BULK_READ(ptp_usb->handle, ptp_usb->intep,(char *)&usbevent,sizeof(usbevent),USB_TIMEOUT);
		if (result==0)
			result = USB_BULK_READ(ptp_usb->handle, ptp_usb->intep,(char *) &usbevent, sizeof(usbevent), USB_TIMEOUT);
		if (result < 0) ret = PTP_ERROR_IO;
		break;
	case PTP_EVENT_CHECK_FAST:
		result=USB_BULK_READ(ptp_usb->handle, ptp_usb->intep,(char *)&usbevent,sizeof(usbevent),USB_TIMEOUT);
		if (result==0)
			result = USB_BULK_READ(ptp_usb->handle, ptp_usb->intep,(char *) &usbevent, sizeof(usbevent), USB_TIMEOUT);
		if (result < 0) ret = PTP_ERROR_IO;
		break;
	default:
		ret=PTP_ERROR_BADPARAM;
		break;
	}
	if (ret!=PTP_RC_OK) {
		ptp_error (params,
			"PTP: reading event an error 0x%04x occurred", ret);
		return PTP_ERROR_IO;
	}
	rlen = result;
	if (rlen < 8) {
		ptp_error (params,
			"PTP: reading event an short read of %ld bytes occurred", rlen);
		return PTP_ERROR_IO;
	}
	/* if we read anything over interrupt endpoint it must be an event */
	/* build an appropriate PTPContainer */
	event->Code=dtoh16(usbevent.code);
	event->SessionID=params->session_id;
	event->Transaction_ID=dtoh32(usbevent.trans_id);
	event->Param1=dtoh32(usbevent.param1);
	event->Param2=dtoh32(usbevent.param2);
	event->Param3=dtoh32(usbevent.param3);
	return ret;
}

uint16_t
ptp_usb_event_check (PTPParams* params, PTPContainer* event) {

	return ptp_usb_event (params, event, PTP_EVENT_CHECK_FAST);
}

uint16_t
ptp_usb_event_wait (PTPParams* params, PTPContainer* event) {

	return ptp_usb_event (params, event, PTP_EVENT_CHECK);
}

uint16_t
ptp_usb_control_cancel_request (PTPParams *params, uint32_t transactionid) {
	PTP_USB *ptp_usb = (PTP_USB *)(params->data);
	int ret;
	unsigned char buffer[6];

	htod16a(&buffer[0],PTP_EC_CancelTransaction);
	htod32a(&buffer[2],transactionid);
	ret = usb_control_msg(ptp_usb->handle, 
			      USB_TYPE_CLASS | USB_RECIP_INTERFACE,
			      0x64, 0x0000, 0x0000, (char *) buffer, sizeof(buffer), USB_TIMEOUT);
	if (ret < sizeof(buffer))
		return PTP_ERROR_IO;
	return PTP_RC_OK;
}

static int init_ptp_usb (PTPParams* params, PTP_USB* ptp_usb, struct usb_device* dev)
{
  usb_dev_handle *device_handle;
  
  params->error_func=NULL;
  params->debug_func=NULL;
  params->sendreq_func=ptp_usb_sendreq;
  params->senddata_func=ptp_usb_senddata;
  params->getresp_func=ptp_usb_getresp;
  params->getdata_func=ptp_usb_getdata;
  params->cancelreq_func=ptp_usb_control_cancel_request;
  params->data=ptp_usb;
  params->transaction_id=0;
  /*
   * This is hardcoded here since we have no devices whatsoever that are BE.
   * Change this the day we run into our first BE device (if ever).
   */
  params->byteorder = PTP_DL_LE;
  
  if ((device_handle = usb_open(dev))){
    if (!device_handle) {
      perror("usb_open()");
      return -1;
    }
    ptp_usb->handle = device_handle;
#ifdef LIBUSB_HAS_DETACH_KERNEL_DRIVER_NP
    /*
     * If this device is known to be wrongfully claimed by other kernel
     * drivers (such as mass storage), then try to unload it to make it
     * accessible from user space.
     */
    if (FLAG_UNLOAD_DRIVER(ptp_usb)) {
      if (usb_detach_kernel_driver_np(device_handle, (int) ptp_usb->interface)) {
	// Totally ignore this error!
	// perror("usb_detach_kernel_driver_np()");
      }
    }
#endif
#ifdef __WIN32__
    // Only needed on Windows, and cause problems on other platforms.
    if (usb_set_configuration(device_handle, dev->config->bConfigurationValue)) {
      perror("usb_set_configuration()");
      return -1;
    }
#endif
    if (usb_claim_interface(device_handle, (int) ptp_usb->interface)) {
      perror("usb_claim_interface()");
      return -1;
    }
  }
  return 0;
}

static void clear_stall(PTP_USB* ptp_usb)
{
  uint16_t status;
  int ret;
  
  /* check the inep status */
  status = 0;
  ret = usb_get_endpoint_status(ptp_usb,ptp_usb->inep,&status);
  if (ret<0) {
    perror ("inep: usb_get_endpoint_status()");
  } else if (status) {
    printf("Clearing stall on IN endpoint\n");
    ret = usb_clear_stall_feature(ptp_usb,ptp_usb->inep);
    if (ret<0) {
      perror ("usb_clear_stall_feature()");
    }
  }
  
  /* check the outep status */
  status=0;
  ret = usb_get_endpoint_status(ptp_usb,ptp_usb->outep,&status);
  if (ret<0) {
    perror("outep: usb_get_endpoint_status()");
  } else if (status) {
    printf("Clearing stall on OUT endpoint\n");
    ret = usb_clear_stall_feature(ptp_usb,ptp_usb->outep);
    if (ret<0) {
      perror("usb_clear_stall_feature()");
    }
  }

  /* TODO: do we need this for INTERRUPT (ptp_usb->intep) too? */
}

static void clear_halt(PTP_USB* ptp_usb)
{
  int ret;

  ret = usb_clear_halt(ptp_usb->handle,ptp_usb->inep);
  if (ret<0) {
    perror("usb_clear_halt() on IN endpoint");
  }
  ret = usb_clear_halt(ptp_usb->handle,ptp_usb->outep);
  if (ret<0) {
    perror("usb_clear_halt() on OUT endpoint");
  }
  ret = usb_clear_halt(ptp_usb->handle,ptp_usb->intep);
  if (ret<0) {
    perror("usb_clear_halt() on INTERRUPT endpoint");
  }
}

static void close_usb(PTP_USB* ptp_usb)
{
  // Commented out since it was confusing some
  // devices to do these things.
  if (!FLAG_NO_RELEASE_INTERFACE(ptp_usb)) {
    /*
     * Clear any stalled endpoints
     * On misbehaving devices designed for Windows/Mac, quote from:
     * http://www2.one-eyed-alien.net/~mdharm/linux-usb/target_offenses.txt
     * Device does Bad Things(tm) when it gets a GET_STATUS after CLEAR_HALT
     * (...) Windows, when clearing a stall, only sends the CLEAR_HALT command, 
     * and presumes that the stall has cleared.  Some devices actually choke 
     * if the CLEAR_HALT is followed by a GET_STATUS (used to determine if the 
     * STALL is persistant or not).
     */
    clear_stall(ptp_usb);
    // Clear halts on any endpoints
    clear_halt(ptp_usb);
    // Added to clear some stuff on the OUT endpoint
    // TODO: is this good on the Mac too?
    // HINT: some devices may need that you comment these two out too.
    usb_resetep(ptp_usb->handle, ptp_usb->outep);
    usb_release_interface(ptp_usb->handle, (int) ptp_usb->interface);
  }
  // Brutally reset device
  // TODO: is this good on the Mac too?
  usb_reset(ptp_usb->handle);
  usb_close(ptp_usb->handle);
}

/**
 * Self-explanatory?
 */
static void find_interface_and_endpoints(struct usb_device *dev, 
					 uint8_t *interface,
					 int* inep, 
					 int* inep_maxpacket, 
					 int* outep, 
					 int *outep_maxpacket, 
					 int* intep)
{
  int i;

  // Loop over the device configurations
  for (i = 0; i < dev->descriptor.bNumConfigurations; i++) {
    uint8_t j;

    for (j = 0; j < dev->config[i].bNumInterfaces; j++) {
      uint8_t k;
      uint8_t no_ep;
      struct usb_endpoint_descriptor *ep;
      
      if (dev->descriptor.bNumConfigurations > 1 || dev->config[i].bNumInterfaces > 1) {
	// OK This device has more than one interface, so we have to find out
	// which one to use! 
	// FIXME: Probe the interface.
	// FIXME: Release modules attached to all other interfaces in Linux...?
      }

      *interface = dev->config[i].interface[j].altsetting->bInterfaceNumber;
      ep = dev->config[i].interface[j].altsetting->endpoint;
      no_ep = dev->config[i].interface[j].altsetting->bNumEndpoints;
      
      for (k = 0; k < no_ep; k++) {
	if (ep[k].bmAttributes==USB_ENDPOINT_TYPE_BULK)	{
	  if ((ep[k].bEndpointAddress&USB_ENDPOINT_DIR_MASK)==
	      USB_ENDPOINT_DIR_MASK)
	    {
	      *inep=ep[k].bEndpointAddress;
	      *inep_maxpacket=ep[k].wMaxPacketSize;
	    }
	  if ((ep[k].bEndpointAddress&USB_ENDPOINT_DIR_MASK)==0)
	    {
	      *outep=ep[k].bEndpointAddress;
	      *outep_maxpacket=ep[k].wMaxPacketSize;
	    }
	} else if (ep[k].bmAttributes==USB_ENDPOINT_TYPE_INTERRUPT){
	  if ((ep[k].bEndpointAddress&USB_ENDPOINT_DIR_MASK)==
	      USB_ENDPOINT_DIR_MASK)
	    {
	      *intep=ep[k].bEndpointAddress;
	    }
	}
      }
      // We assigned the endpoints so return here.
      return;
    }
  }
}

/**
 * This function assigns params and usbinfo given a raw device
 * as input.
 * @param device the device to be assigned.
 * @param usbinfo a pointer to the new usbinfo.
 * @return an error code.
 */
LIBMTP_error_number_t configure_usb_device(LIBMTP_raw_device_t *device, 
					   PTPParams *params,
					   void **usbinfo)
{
  PTP_USB *ptp_usb;
  struct usb_device *libusb_device;
  uint16_t ret = 0;
  struct usb_bus *bus;
  int found = 0;

  /* See if we can find this raw device again... */
  bus = init_usb();
  for (; bus != NULL; bus = bus->next) {
    if (bus->location == device->bus_location) {
      struct usb_device *dev = bus->devices;

      for (; dev != NULL; dev = dev->next) {
	if(dev->devnum == device->devnum &&
	   dev->descriptor.idVendor == device->device_entry.vendor_id &&
	   dev->descriptor.idProduct == device->device_entry.product_id ) {
	  libusb_device = dev;
	  found = 1;
	  break;
	}
      }
      if (found)
	break;
    }
  }
  /* Device has gone since detecting raw devices! */
  if (!found) {
    return LIBMTP_ERROR_NO_DEVICE_ATTACHED;
  }

  /* Allocate structs */
  ptp_usb = (PTP_USB *) malloc(sizeof(PTP_USB));
  if (ptp_usb == NULL) {
    return LIBMTP_ERROR_MEMORY_ALLOCATION;
  }
  /* Start with a blank slate (includes setting device_flags to 0) */
  memset(ptp_usb, 0, sizeof(PTP_USB));

  /* Copy the raw device */
  memcpy(&ptp_usb->rawdevice, device, sizeof(LIBMTP_raw_device_t));

  /*
   * Some devices must have their "OS Descriptor" massaged in order
   * to work.
   */
  if (FLAG_ALWAYS_PROBE_DESCRIPTOR(ptp_usb)) {
    // Massage the device descriptor
    (void) probe_device_descriptor(libusb_device, NULL);
  }
  
  /* Assign endpoints to usbinfo... */
  find_interface_and_endpoints(libusb_device,
		   &ptp_usb->interface,
		   &ptp_usb->inep,
		   &ptp_usb->inep_maxpacket,
		   &ptp_usb->outep,
		   &ptp_usb->outep_maxpacket,
		   &ptp_usb->intep);
    
  /* Attempt to initialize this device */
  if (init_ptp_usb(params, ptp_usb, libusb_device) < 0) {
    fprintf(stderr, "LIBMTP PANIC: Unable to initialize device\n");
    return LIBMTP_ERROR_CONNECTING;
  }
  
  /*
   * This works in situations where previous bad applications
   * have not used LIBMTP_Release_Device on exit 
   */
  if ((ret = ptp_opensession(params, 1)) == PTP_ERROR_IO) {
    fprintf(stderr, "PTP_ERROR_IO: Trying again after re-initializing USB interface\n");
    close_usb(ptp_usb);
      
    if(init_ptp_usb(params, ptp_usb, libusb_device) <0) {
      fprintf(stderr, "LIBMTP PANIC: Could not open session on device\n");
      return LIBMTP_ERROR_CONNECTING;
    }
    
    /* Device has been reset, try again */
    ret = ptp_opensession(params, 1);
  }
  
  /* Was the transaction id invalid? Try again */
  if (ret == PTP_RC_InvalidTransactionID) {
    fprintf(stderr, "LIBMTP WARNING: Transaction ID was invalid, increment and try again\n");
    params->transaction_id += 10;
    ret = ptp_opensession(params, 1);
  }

  if (ret != PTP_RC_SessionAlreadyOpened && ret != PTP_RC_OK) {
    fprintf(stderr, "LIBMTP PANIC: Could not open session! "
	    "(Return code %d)\n  Try to reset the device.\n",
	    ret);
    usb_release_interface(ptp_usb->handle,
			  (int) ptp_usb->interface);
    return LIBMTP_ERROR_CONNECTING;
  }

  /* OK configured properly */
  *usbinfo = (void *) ptp_usb;
  return LIBMTP_ERROR_NONE;
}


void close_device (PTP_USB *ptp_usb, PTPParams *params)
{
  if (ptp_closesession(params)!=PTP_RC_OK)
    fprintf(stderr,"ERROR: Could not close session!\n");
  close_usb(ptp_usb);
}

static int usb_clear_stall_feature(PTP_USB* ptp_usb, int ep)
{
  
  return (usb_control_msg(ptp_usb->handle,
			  USB_RECIP_ENDPOINT, USB_REQ_CLEAR_FEATURE, USB_FEATURE_HALT,
			  ep, NULL, 0, USB_TIMEOUT));
}

static int usb_get_endpoint_status(PTP_USB* ptp_usb, int ep, uint16_t* status)
{
  return (usb_control_msg(ptp_usb->handle,
			  USB_DP_DTH|USB_RECIP_ENDPOINT, USB_REQ_GET_STATUS,
			  USB_FEATURE_HALT, ep, (char *)status, 2, USB_TIMEOUT));
}
