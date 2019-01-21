/**
 * \file libmtp.h
 * Interface to the Media Transfer Protocol library.
 *
 * Copyright (C) 2005-2008 Linus Walleij <triad@df.lth.se>
 * Copyright (C) 2005-2008 Richard A. Low <richard@wentnet.com>
 * Copyright (C) 2007 Ted Bullock <tbullock@canada.com>
 * Copyright (C) 2008 Florent Mertens <flomertens@gmail.com>
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
 * <code>
 * #include <libmtp.h>
 * </code>
 */
#ifndef LIBMTP_H_INCLUSION_GUARD
#define LIBMTP_H_INCLUSION_GUARD

#define LIBMTP_VERSION 0.3.7
#define LIBMTP_VERSION_STRING "0.3.7"

/* This handles MSVC pecularities */
#ifdef _MSC_VER
#include <windows.h>
#define __WIN32__
#define snprintf _snprintf
#define ssize_t SSIZE_T
/*
 * Types that do not exist in Windows
 * sys/types.h, but they exist in mingw32
 * sys/types.h.
 */
typedef char int8_t;
typedef unsigned char uint8_t;
typedef __int16 int16_t;
typedef unsigned __int16 uint16_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef unsigned __int64 uint64_t;
#endif

#include <stdio.h>
#include <usb.h>
#include <stdint.h>

/**
 * @defgroup types libmtp global type definitions
 * @{
 * The filetypes defined here are the external types used
 * by the libmtp library interface. The types used internally
 * as PTP-defined enumerator types is something different.
 */
typedef enum {
  LIBMTP_FILETYPE_WAV,
  LIBMTP_FILETYPE_MP3,
  LIBMTP_FILETYPE_WMA,
  LIBMTP_FILETYPE_OGG,
  LIBMTP_FILETYPE_AUDIBLE,
  LIBMTP_FILETYPE_MP4,
  LIBMTP_FILETYPE_UNDEF_AUDIO,
  LIBMTP_FILETYPE_WMV,
  LIBMTP_FILETYPE_AVI,
  LIBMTP_FILETYPE_MPEG,
  LIBMTP_FILETYPE_ASF,
  LIBMTP_FILETYPE_QT,
  LIBMTP_FILETYPE_UNDEF_VIDEO,
  LIBMTP_FILETYPE_JPEG,
  LIBMTP_FILETYPE_JFIF,
  LIBMTP_FILETYPE_TIFF,
  LIBMTP_FILETYPE_BMP,
  LIBMTP_FILETYPE_GIF,
  LIBMTP_FILETYPE_PICT,
  LIBMTP_FILETYPE_PNG,
  LIBMTP_FILETYPE_VCALENDAR1,
  LIBMTP_FILETYPE_VCALENDAR2,
  LIBMTP_FILETYPE_VCARD2,
  LIBMTP_FILETYPE_VCARD3,
  LIBMTP_FILETYPE_WINDOWSIMAGEFORMAT,
  LIBMTP_FILETYPE_WINEXEC,
  LIBMTP_FILETYPE_TEXT,
  LIBMTP_FILETYPE_HTML,
  LIBMTP_FILETYPE_FIRMWARE,
  LIBMTP_FILETYPE_AAC,
  LIBMTP_FILETYPE_MEDIACARD,
  LIBMTP_FILETYPE_FLAC,
  LIBMTP_FILETYPE_MP2,
  LIBMTP_FILETYPE_M4A,
  LIBMTP_FILETYPE_DOC,
  LIBMTP_FILETYPE_XML,
  LIBMTP_FILETYPE_XLS,
  LIBMTP_FILETYPE_PPT,
  LIBMTP_FILETYPE_MHT,
  LIBMTP_FILETYPE_JP2,
  LIBMTP_FILETYPE_JPX,
  LIBMTP_FILETYPE_UNKNOWN
} LIBMTP_filetype_t;

/**
 * \def LIBMTP_FILETYPE_IS_AUDIO
 * Audio filetype test.
 *
 * For filetypes that can be either audio
 * or video, use LIBMTP_FILETYPE_IS_AUDIOVIDEO
 */
#define LIBMTP_FILETYPE_IS_AUDIO(a)\
(a == LIBMTP_FILETYPE_WAV ||\
 a == LIBMTP_FILETYPE_MP3 ||\
 a == LIBMTP_FILETYPE_MP2 ||\
 a == LIBMTP_FILETYPE_WMA ||\
 a == LIBMTP_FILETYPE_OGG ||\
 a == LIBMTP_FILETYPE_FLAC ||\
 a == LIBMTP_FILETYPE_AAC ||\
 a == LIBMTP_FILETYPE_M4A ||\
 a == LIBMTP_FILETYPE_UNDEF_AUDIO)

/**
 *  \def LIBMTP_FILETYPE_IS_VIDEO
 *  Video filetype test.
 *
 * For filetypes that can be either audio
 * or video, use LIBMTP_FILETYPE_IS_AUDIOVIDEO
 */
#define LIBMTP_FILETYPE_IS_VIDEO(a)\
(a == LIBMTP_FILETYPE_WMV ||\
 a == LIBMTP_FILETYPE_AVI ||\
 a == LIBMTP_FILETYPE_MPEG ||\
 a == LIBMTP_FILETYPE_UNDEF_VIDEO)

/**
 *  \def LIBMTP_FILETYPE_IS_AUDIOVIDEO
 *  Audio and&slash;or video filetype test.
 */
#define LIBMTP_FILETYPE_IS_AUDIOVIDEO(a)\
(a == LIBMTP_FILETYPE_MP4 ||\
 a == LIBMTP_FILETYPE_ASF ||\
 a == LIBMTP_FILETYPE_QT)

/**
 *  \def LIBMTP_FILETYPE_IS_TRACK
 *  Test if filetype is a track.
 *  Use this to determine if the File API or Track API
 *  should be used to upload or download an object.
 */
#define LIBMTP_FILETYPE_IS_TRACK(a)\
(LIBMTP_FILETYPE_IS_AUDIO(a) ||\
 LIBMTP_FILETYPE_IS_VIDEO(a) ||\
 LIBMTP_FILETYPE_IS_AUDIOVIDEO(a))

/**
 *  \def LIBMTP_FILETYPE_IS_IMAGE
 *  Image filetype test
 */
#define LIBMTP_FILETYPE_IS_IMAGE(a)\
(a == LIBMTP_FILETYPE_JPEG ||\
a == LIBMTP_FILETYPE_JFIF ||\
a == LIBMTP_FILETYPE_TIFF ||\
a == LIBMTP_FILETYPE_BMP ||\
a == LIBMTP_FILETYPE_GIF ||\
a == LIBMTP_FILETYPE_PICT ||\
a == LIBMTP_FILETYPE_PNG ||\
a == LIBMTP_FILETYPE_JP2 ||\
a == LIBMTP_FILETYPE_JPX ||\
a == LIBMTP_FILETYPE_WINDOWSIMAGEFORMAT)

/**
 *  \def LIBMTP_FILETYPE_IS_ADDRESSBOOK
 *  Addressbook and Business card filetype test
 */
#define LIBMTP_FILETYPE_IS_ADDRESSBOOK(a)\
(a == LIBMTP_FILETYPE_VCARD2 ||\
a == LIBMTP_FILETYPE_VCARD2)

/**
 *  \def LIBMTP_FILETYPE_IS_CALENDAR
 *  Calendar and Appointment filetype test
 */
#define LIBMTP_FILETYPE_IS_CALENDAR(a)\
(a == LIBMTP_FILETYPE_VCALENDAR1 ||\
a == LIBMTP_FILETYPE_VCALENDAR2)

/**
 * These are the numbered error codes. You can also
 * get string representations for errors.
 */
typedef enum {
  LIBMTP_ERROR_NONE,
  LIBMTP_ERROR_GENERAL,
  LIBMTP_ERROR_PTP_LAYER,
  LIBMTP_ERROR_USB_LAYER,
  LIBMTP_ERROR_MEMORY_ALLOCATION,
  LIBMTP_ERROR_NO_DEVICE_ATTACHED,
  LIBMTP_ERROR_STORAGE_FULL,
  LIBMTP_ERROR_CONNECTING,
  LIBMTP_ERROR_CANCELLED
} LIBMTP_error_number_t;
typedef struct LIBMTP_device_entry_struct LIBMTP_device_entry_t; /**< @see LIBMTP_device_entry_struct */
typedef struct LIBMTP_raw_device_struct LIBMTP_raw_device_t; /**< @see LIBMTP_raw_device_struct */
typedef struct LIBMTP_error_struct LIBMTP_error_t; /**< @see LIBMTP_error_struct */
typedef struct LIBMTP_mtpdevice_struct LIBMTP_mtpdevice_t; /**< @see LIBMTP_mtpdevice_struct */
typedef struct LIBMTP_file_struct LIBMTP_file_t; /**< @see LIBMTP_file_struct */
typedef struct LIBMTP_track_struct LIBMTP_track_t; /**< @see LIBMTP_track_struct */
typedef struct LIBMTP_playlist_struct LIBMTP_playlist_t; /**< @see LIBMTP_playlist_struct */
typedef struct LIBMTP_album_struct LIBMTP_album_t; /**< @see LIBMTP_album_struct */
typedef struct LIBMTP_folder_struct LIBMTP_folder_t; /**< @see LIBMTP_folder_t */
typedef struct LIBMTP_object_struct LIBMTP_object_t; /**< @see LIBMTP_object_t */
typedef struct LIBMTP_filesampledata_struct LIBMTP_filesampledata_t; /**< @see LIBMTP_filesample_t */
typedef struct LIBMTP_devicestorage_struct LIBMTP_devicestorage_t; /**< @see LIBMTP_devicestorage_t */

/**
 * The callback type definition. Notice that a progress percentage ratio
 * is easy to calculate by dividing <code>sent</code> by
 * <code>total</code>.
 * @param sent the number of bytes sent so far
 * @param total the total number of bytes to send
 * @param data a user-defined dereferencable pointer
 * @return if anything else than 0 is returned, the current transfer will be
 *         interrupted / cancelled.
 */
typedef int (* LIBMTP_progressfunc_t) (uint64_t const sent, uint64_t const total,
                		void const * const data);

/**
 * @}
 * @defgroup structar libmtp data structures
 * @{
 */

/**
 * A data structure to hold MTP device entries.
 */
struct LIBMTP_device_entry_struct {
  char *vendor; /**< The vendor of this device */
  uint16_t vendor_id; /**< Vendor ID for this device */
  char *product; /**< The product name of this device */
  uint16_t product_id; /**< Product ID for this device */
  uint32_t device_flags; /**< Bugs, device specifics etc */
};

/**
 * A data structure to hold a raw MTP device connected
 * to the bus.
 */
struct LIBMTP_raw_device_struct {
  LIBMTP_device_entry_t device_entry; /**< The device entry for this raw device */
  uint32_t bus_location; /**< Location of the bus, if device available */
  uint8_t devnum; /**< Device number on the bus, if device available */
};

/**
 * A data structure to hold errors from the library.
 */
struct LIBMTP_error_struct {
  LIBMTP_error_number_t errornumber;
  char *error_text;
  LIBMTP_error_t *next;
};

/**
 * Main MTP device object struct
 */
struct LIBMTP_mtpdevice_struct {
  /**
   * Object bitsize, typically 32 or 64.
   */
  uint8_t object_bitsize;
  /**
   * Parameters for this device, must be cast into
   * \c (PTPParams*) before internal use.
   */
  void *params;
  /**
   * USB device for this device, must be cast into
   * \c (PTP_USB*) before internal use.
   */
  void *usbinfo;
  /** 
   * The storage for this device, do not use strings in here without 
   * copying them first, and beware that this list may be rebuilt at
   * any time.
   * @see LIBMTP_Get_Storage()
   */
  LIBMTP_devicestorage_t *storage;
  /**
   * The error stack. This shall be handled using the error getting
   * and clearing functions, not by dereferencing this list.
   */
  LIBMTP_error_t *errorstack;
  /** The maximum battery level for this device */
  uint8_t maximum_battery_level;
  /** Default music folder */
  uint32_t default_music_folder;
  /** Default playlist folder */
  uint32_t default_playlist_folder;
  /** Default picture folder */
  uint32_t default_picture_folder;
  /** Default video folder */
  uint32_t default_video_folder;
  /** Default organizer folder */
  uint32_t default_organizer_folder;
  /** Default ZENcast folder (only Creative devices...) */
  uint32_t default_zencast_folder;
  /** Default Album folder */
  uint32_t default_album_folder;
  /** Default Text folder */
  uint32_t default_text_folder;
  /** Per device iconv() converters, only used internally */
  void *cd;
  
  /** Pointer to next device in linked list; NULL if this is the last device */
  LIBMTP_mtpdevice_t *next;
};

/**
 * MTP file struct
 */
struct LIBMTP_file_struct {
  uint32_t item_id; /**< Unique item ID */
  uint32_t parent_id; /**< ID of parent folder */
  uint32_t storage_id; /**< ID of storage holding this file */
  char *filename; /**< Filename of this file */
  uint64_t filesize; /**< Size of file in bytes */
  LIBMTP_filetype_t filetype; /**< Filetype used for the current file */
  LIBMTP_file_t *next; /**< Next file in list or NULL if last file */
};

/**
 * MTP track struct
 */
struct LIBMTP_track_struct {
  uint32_t item_id; /**< Unique item ID */
  uint32_t parent_id; /**< ID of parent folder */
  uint32_t storage_id; /**< ID of storage holding this track */
  char *title; /**< Track title */
  char *artist; /**< Name of recording artist */
  char *composer; /**< Name of recording composer */
  char *genre; /**< Genre name for track */
  char *album; /**< Album name for track */
  char *date; /**< Date of original recording as a string */
  char *filename; /**< Original filename of this track */
  uint16_t tracknumber; /**< Track number (in sequence on recording) */
  uint32_t duration; /**< Duration in milliseconds */
  uint32_t samplerate; /**< Sample rate of original file, min 0x1f80 max 0xbb80 */
  uint16_t nochannels; /**< Number of channels in this recording 0 = unknown, 1 or 2 */
  uint32_t wavecodec; /**< FourCC wave codec name */
  uint32_t bitrate; /**< (Average) bitrate for this file min=1 max=0x16e360 */
  uint16_t bitratetype; /**< 0 = unused, 1 = constant, 2 = VBR, 3 = free */
  uint16_t rating; /**< User rating 0-100 (0x00-0x64) */
  uint32_t usecount; /**< Number of times used/played */
  uint64_t filesize; /**< Size of track file in bytes */
  LIBMTP_filetype_t filetype; /**< Filetype used for the current track */
  LIBMTP_track_t *next; /**< Next track in list or NULL if last track */
};

/**
 * MTP Playlist structure
 */
struct LIBMTP_playlist_struct {
  uint32_t playlist_id; /**< Unique playlist ID */
  uint32_t parent_id; /**< ID of parent folder */
  uint32_t storage_id; /**< ID of storage holding this playlist */
  char *name; /**< Name of playlist */
  uint32_t *tracks; /**< The tracks in this playlist */
  uint32_t no_tracks; /**< The number of tracks in this playlist */
  LIBMTP_playlist_t *next; /**< Next playlist or NULL if last playlist */
};

/**
 * MTP Album structure
 */
struct LIBMTP_album_struct {
  uint32_t album_id; /**< Unique playlist ID */
  uint32_t parent_id; /**< ID of parent folder */
  uint32_t storage_id; /**< ID of storage holding this album */
  char *name; /**< Name of album */
  char *artist; /**< Name of album artist */
  char *composer; /**< Name of recording composer */
  char *genre; /**< Genre of album */
  uint32_t *tracks; /**< The tracks in this album */
  uint32_t no_tracks; /**< The number of tracks in this album */
  LIBMTP_album_t *next; /**< Next album or NULL if last album */
};

/**
 * MTP Folder structure
 */
struct LIBMTP_folder_struct {
  uint32_t folder_id; /**< Unique folder ID */
  uint32_t parent_id; /**< ID of parent folder */
  uint32_t storage_id; /**< ID of storage holding this folder */
  char *name; /**< Name of folder */
  LIBMTP_folder_t *sibling; /**< Next folder at same level or NULL if no more */
  LIBMTP_folder_t *child; /**< Child folder or NULL if no children */
};

/**
 * LIBMTP Object RepresentativeSampleData Structure
 */
struct LIBMTP_filesampledata_struct {
  uint32_t width; /**< Width of sample if it is an image */
  uint32_t height; /**< Height of sample if it is an image */
  uint32_t duration; /**< Duration in milliseconds if it is audio */
  LIBMTP_filetype_t filetype; /**< Filetype used for the sample */
  uint64_t size; /**< Size of sample data in bytes */
  char *data; /**< Sample data */
};

/**
 * LIBMTP Device Storage structure
 */
struct LIBMTP_devicestorage_struct {
  uint32_t id; /**< Unique ID for this storage */
  uint16_t StorageType; /**< Storage type */
  uint16_t FilesystemType; /**< Filesystem type */
  uint16_t AccessCapability; /**< Access capability */
  uint64_t MaxCapacity; /**< Maximum capability */
  uint64_t FreeSpaceInBytes; /**< Free space in bytes */
  uint64_t FreeSpaceInObjects; /**< Free space in objects */
  char *StorageDescription; /**< A brief description of this storage */
  char *VolumeIdentifier; /**< A volume identifier */
  LIBMTP_devicestorage_t *next; /**< Next storage, follow this link until NULL */
  LIBMTP_devicestorage_t *prev; /**< Previous storage */
};
  

/** @} */

/* Make functions available for C++ */
#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup internals The libmtp internals API.
 * @{
 */
void LIBMTP_Init(void);
int LIBMTP_Get_Supported_Devices_List(LIBMTP_device_entry_t ** const, int * const);
/**
 * @}
 * @defgroup basic The basic device management API.
 * @{
 */
LIBMTP_error_number_t LIBMTP_Detect_Raw_Devices(LIBMTP_raw_device_t **, int *);
LIBMTP_mtpdevice_t *LIBMTP_Open_Raw_Device(LIBMTP_raw_device_t *);
/* Begin old, legacy interface */
LIBMTP_mtpdevice_t *LIBMTP_Get_First_Device(void);
LIBMTP_error_number_t LIBMTP_Get_Connected_Devices(LIBMTP_mtpdevice_t **);
uint32_t LIBMTP_Number_Devices_In_List(LIBMTP_mtpdevice_t *);
void LIBMTP_Release_Device_List(LIBMTP_mtpdevice_t*);
/* End old, legacy interface */
void LIBMTP_Release_Device(LIBMTP_mtpdevice_t*);
void LIBMTP_Dump_Device_Info(LIBMTP_mtpdevice_t*);
int LIBMTP_Reset_Device(LIBMTP_mtpdevice_t*);
char *LIBMTP_Get_Manufacturername(LIBMTP_mtpdevice_t*);
char *LIBMTP_Get_Modelname(LIBMTP_mtpdevice_t*);
char *LIBMTP_Get_Serialnumber(LIBMTP_mtpdevice_t*);
char *LIBMTP_Get_Deviceversion(LIBMTP_mtpdevice_t*);
char *LIBMTP_Get_Friendlyname(LIBMTP_mtpdevice_t*);
int LIBMTP_Set_Friendlyname(LIBMTP_mtpdevice_t*, char const * const);
char *LIBMTP_Get_Syncpartner(LIBMTP_mtpdevice_t*);
int LIBMTP_Set_Syncpartner(LIBMTP_mtpdevice_t*, char const * const);
int LIBMTP_Get_Batterylevel(LIBMTP_mtpdevice_t *,
			    uint8_t * const,
			    uint8_t * const);
int LIBMTP_Get_Secure_Time(LIBMTP_mtpdevice_t *, char ** const);
int LIBMTP_Get_Device_Certificate(LIBMTP_mtpdevice_t *, char ** const);
int LIBMTP_Get_Supported_Filetypes(LIBMTP_mtpdevice_t *, uint16_t ** const, uint16_t * const);
LIBMTP_error_t *LIBMTP_Get_Errorstack(LIBMTP_mtpdevice_t*);
void LIBMTP_Clear_Errorstack(LIBMTP_mtpdevice_t*);
void LIBMTP_Dump_Errorstack(LIBMTP_mtpdevice_t*);

#define LIBMTP_STORAGE_SORTBY_NOTSORTED 0
#define LIBMTP_STORAGE_SORTBY_FREESPACE 1
#define LIBMTP_STORAGE_SORTBY_MAXSPACE  2

int LIBMTP_Get_Storage(LIBMTP_mtpdevice_t *, int const);
int LIBMTP_Format_Storage(LIBMTP_mtpdevice_t *, LIBMTP_devicestorage_t *);


/**
 * @}
 * @defgroup files The file management API.
 * @{
 */
LIBMTP_file_t *LIBMTP_new_file_t(void);
void LIBMTP_destroy_file_t(LIBMTP_file_t*);
char const * LIBMTP_Get_Filetype_Description(LIBMTP_filetype_t);
LIBMTP_file_t *LIBMTP_Get_Filelisting(LIBMTP_mtpdevice_t *);
LIBMTP_file_t *LIBMTP_Get_Filelisting_With_Callback(LIBMTP_mtpdevice_t *,
      LIBMTP_progressfunc_t const, void const * const);
LIBMTP_file_t *LIBMTP_Get_Filemetadata(LIBMTP_mtpdevice_t *, uint32_t const);
int LIBMTP_Get_File_To_File(LIBMTP_mtpdevice_t*, uint32_t, char const * const,
			LIBMTP_progressfunc_t const, void const * const);
int LIBMTP_Get_File_To_File_Descriptor(LIBMTP_mtpdevice_t*, uint32_t const, int const,
			LIBMTP_progressfunc_t const, void const * const);
int LIBMTP_Send_File_From_File(LIBMTP_mtpdevice_t *, char const * const,
	                 LIBMTP_file_t * const, LIBMTP_progressfunc_t const,
			 void const * const);
int LIBMTP_Send_File_From_File_Descriptor(LIBMTP_mtpdevice_t *, int const,
	                LIBMTP_file_t * const, LIBMTP_progressfunc_t const,
			void const * const);
int LIBMTP_Set_File_Name(LIBMTP_mtpdevice_t *, LIBMTP_file_t *, const char *);
LIBMTP_filesampledata_t *LIBMTP_new_filesampledata_t(void);
void LIBMTP_destroy_filesampledata_t(LIBMTP_filesampledata_t *);
int LIBMTP_Get_Representative_Sample_Format(LIBMTP_mtpdevice_t *,
                        LIBMTP_filetype_t const,
                        LIBMTP_filesampledata_t **);
int LIBMTP_Send_Representative_Sample(LIBMTP_mtpdevice_t *, uint32_t const,
                          LIBMTP_filesampledata_t *);
int LIBMTP_Get_Representative_Sample(LIBMTP_mtpdevice_t *, uint32_t const,
                          LIBMTP_filesampledata_t *);

/**
 * @}
 * @defgroup tracks The track management API.
 * @{
 */
LIBMTP_track_t *LIBMTP_new_track_t(void);
void LIBMTP_destroy_track_t(LIBMTP_track_t*);
LIBMTP_track_t *LIBMTP_Get_Tracklisting(LIBMTP_mtpdevice_t*);
LIBMTP_track_t *LIBMTP_Get_Tracklisting_With_Callback(LIBMTP_mtpdevice_t*,
      LIBMTP_progressfunc_t const, void const * const);
LIBMTP_track_t *LIBMTP_Get_Trackmetadata(LIBMTP_mtpdevice_t*, uint32_t const);
int LIBMTP_Get_Track_To_File(LIBMTP_mtpdevice_t*, uint32_t, char const * const,
			LIBMTP_progressfunc_t const, void const * const);
int LIBMTP_Get_Track_To_File_Descriptor(LIBMTP_mtpdevice_t*, uint32_t const, int const,
			LIBMTP_progressfunc_t const, void const * const);
int LIBMTP_Send_Track_From_File(LIBMTP_mtpdevice_t *,
			 char const * const, LIBMTP_track_t * const,
                         LIBMTP_progressfunc_t const,
			 void const * const);
int LIBMTP_Send_Track_From_File_Descriptor(LIBMTP_mtpdevice_t *,
			 int const, LIBMTP_track_t * const,
                         LIBMTP_progressfunc_t const,
			 void const * const);
int LIBMTP_Update_Track_Metadata(LIBMTP_mtpdevice_t *,
			LIBMTP_track_t const * const);
int LIBMTP_Track_Exists(LIBMTP_mtpdevice_t *, uint32_t);
int LIBMTP_Set_Track_Name(LIBMTP_mtpdevice_t *, LIBMTP_track_t *, const char *);
/** @} */

/**
 * @}
 * @defgroup folders The folder management API.
 * @{
 */
LIBMTP_folder_t *LIBMTP_new_folder_t(void);
void LIBMTP_destroy_folder_t(LIBMTP_folder_t*);
LIBMTP_folder_t *LIBMTP_Get_Folder_List(LIBMTP_mtpdevice_t*);
LIBMTP_folder_t *LIBMTP_Find_Folder(LIBMTP_folder_t*, uint32_t const);
uint32_t LIBMTP_Create_Folder(LIBMTP_mtpdevice_t*, char *, uint32_t, uint32_t);
int LIBMTP_Set_Folder_Name(LIBMTP_mtpdevice_t *, LIBMTP_folder_t *, const char *);
/** @} */


/**
 * @}
 * @defgroup playlists The audio/video playlist management API.
 * @{
 */
LIBMTP_playlist_t *LIBMTP_new_playlist_t(void);
void LIBMTP_destroy_playlist_t(LIBMTP_playlist_t *);
LIBMTP_playlist_t *LIBMTP_Get_Playlist_List(LIBMTP_mtpdevice_t *);
LIBMTP_playlist_t *LIBMTP_Get_Playlist(LIBMTP_mtpdevice_t *, uint32_t const);
int LIBMTP_Create_New_Playlist(LIBMTP_mtpdevice_t *, LIBMTP_playlist_t * const);
int LIBMTP_Update_Playlist(LIBMTP_mtpdevice_t *, LIBMTP_playlist_t * const);
int LIBMTP_Set_Playlist_Name(LIBMTP_mtpdevice_t *, LIBMTP_playlist_t *, const char *);

/**
 * @}
 * @defgroup albums The audio/video album management API.
 * @{
 */
LIBMTP_album_t *LIBMTP_new_album_t(void);
void LIBMTP_destroy_album_t(LIBMTP_album_t *);
LIBMTP_album_t *LIBMTP_Get_Album_List(LIBMTP_mtpdevice_t *);
LIBMTP_album_t *LIBMTP_Get_Album(LIBMTP_mtpdevice_t *, uint32_t const);
int LIBMTP_Create_New_Album(LIBMTP_mtpdevice_t *, LIBMTP_album_t * const);
int LIBMTP_Update_Album(LIBMTP_mtpdevice_t *, LIBMTP_album_t const * const);
int LIBMTP_Set_Album_Name(LIBMTP_mtpdevice_t *, LIBMTP_album_t *, const char *);

/**
 * @}
 * @defgroup objects The object management API.
 * @{
 */
int LIBMTP_Delete_Object(LIBMTP_mtpdevice_t *, uint32_t);
int LIBMTP_Set_Object_Filename(LIBMTP_mtpdevice_t *, uint32_t , char *);

/** @} */

/* End of C++ exports */
#ifdef __cplusplus
}
#endif

#endif /* LIBMTP_H_INCLUSION_GUARD */

