#ifndef BTE_H
#define BTE_H

#include "revolution/bte/data_types.h"
#include "revolution/types.h"

#ifdef __cplusplus
extern "C" {
#endif

// from bluedroid source

typedef unsigned char BOOLEAN;

/* muff1n: It seems like BTE_TRACE_LEVEL_VERBOSE did not exist at the time. Or
 * maybe it was a different logging level. At the very least, one logging level
 * is missing.
 */

#define BD_ADDR_LEN 6               /* Device address length */
typedef UINT8 BD_ADDR[BD_ADDR_LEN]; /* Device address */
typedef UINT8* BD_ADDR_PTR;         /* Pointer to Device Address */

#define LINK_KEY_LEN 16
typedef UINT8 LINK_KEY[LINK_KEY_LEN]; /* Link Key */

#define DEV_CLASS_LEN 3
typedef UINT8 DEV_CLASS[DEV_CLASS_LEN]; /* Device class */
typedef UINT8* DEV_CLASS_PTR;           /* Pointer to Device class */

/* muff1n: TODO: bta_dm_pin_cback looks mostly the same except that BD_NAME_LEN
 * is 33 (or it was 32 and BD_NAME_LEN was not decreased by 1 in BCM_STRNCPY_S
 * calls)?
 * confirm this
 *
 * UPDATE: there is a case where it is 248
 */
#define BD_NAME_LEN 248
typedef UINT8 BD_NAME[BD_NAME_LEN]; /* Device name */
typedef UINT8* BD_NAME_PTR;         /* Pointer to Device name */

#define BD_FEATURES_LEN 8
typedef UINT8 BD_FEATURES[BD_FEATURES_LEN]; /* LMP features supported by device */

typedef UINT8 tBT_DEVICE_TYPE;

/* Maximum UUID size - 16 bytes, and structure to hold any type of UUID. */
#define MAX_UUID_SIZE 16
typedef struct {

    UINT16 len;

    union {
        UINT16 uuid16;
        UINT32 uuid32;
        UINT8 uuid128[MAX_UUID_SIZE];
    } uu;

} tBT_UUID;

/* Status Return Value */

typedef UINT8 tBTA_STATUS;

/*
 * Service ID
 *
 * NOTES: When you add a new Service ID for BTA AND require to change the value of BTA_MAX_SERVICE_ID,
 *        make sure that the correct security ID of the new service from Security service definitions (btm_api.h)
 *        should be added to bta_service_id_to_btm_srv_id_lkup_tbl table in bta_dm_act.c.
 */

#define BTA_MAX_SERVICE_ID 30

/* service IDs (BTM_SEC_SERVICE_FIRST_EMPTY + 1) to (BTM_SEC_MAX_SERVICES - 1)
 * are used by BTA JV */

typedef UINT8 tBTA_SERVICE_ID;

/* Service ID Mask */

// btla-specific ++
// btla-specific --

typedef UINT32 tBTA_SERVICE_MASK;

/* SW sub-systems */
/* BLUETOOTH PART - from 0 to BTA_ID_BLUETOOTH_MAX */
#define BTA_ID_BLUETOOTH_MAX 33 /* last BT profile */

/* FM */

/* SENSOR */

/* GPS */

/* GENERIC */

/* JV */

typedef UINT8 tBTA_SYS_ID;

enum {
    BTM_SUCCESS = 0,         /* 0  Command succeeded                 */
    BTM_CMD_STARTED,         /* 1  Command started OK.               */
    BTM_BUSY,                /* 2  Device busy with another command  */
    BTM_NO_RESOURCES,        /* 3  No resources to issue command     */
    BTM_MODE_UNSUPPORTED,    /* 4  Request for 1 or more unsupported modes */
    BTM_ILLEGAL_VALUE,       /* 5  Illegal parameter value           */
    BTM_WRONG_MODE,          /* 6  Device in wrong mode for request  */
    BTM_UNKNOWN_ADDR,        /* 7  Unknown remote BD address         */
    BTM_DEVICE_TIMEOUT,      /* 8  Device timeout                    */
    BTM_BAD_VALUE_RET,       /* 9  A bad value was received from HCI */
    BTM_ERR_PROCESSING,      /* 10 Generic error                     */
    BTM_NOT_AUTHORIZED,      /* 11 Authorization failed              */
    BTM_DEV_RESET,           /* 12 Device has been reset             */
    BTM_CMD_STORED,          /* 13 request is stored in control block */
    BTM_ILLEGAL_ACTION,      /* 14 state machine gets illegal command */
    BTM_DELAY_CHECK,         /* 15 delay the check on encryption */
    BTM_SCO_BAD_LENGTH,      /* 16 Bad SCO over HCI data length */
    BTM_SUCCESS_NO_SECURITY, /* 17 security passed, no security set  */
    BTM_FAILED_ON_SECURITY,  /* 18 security failed                   */
    BTM_REPEATED_ATTEMPTS    /* 19 repeated attempts for LE security requests */
};
typedef UINT8 tBTM_STATUS;

typedef struct {
    UINT16 event;
    UINT16 len;
    UINT16 offset;
    UINT16 layer_specific;
} BT_HDR;

/* Security Service Levels [bit mask] (BTM_SetSecurityLevel)
** Encryption should not be used without authentication
*/

/* Security Setting Mask */

typedef UINT8 tBTA_SEC;

/* Pairable Modes */

/* Connectable Paired Only Mode */

/* Inquiry modes
 * Note: These modes are associated with the inquiry active values (BTM_*ACTIVE) */
/* high byte of inquiry mode for BLE inquiry mode */

typedef UINT8 tBTA_DM_INQ_MODE;

/* Inquiry Filter Type */

typedef UINT8 tBTA_DM_INQ_FILT;

/* Inquiry filter device class condition */
typedef struct {
    DEV_CLASS dev_class;      /* device class of interest */
    DEV_CLASS dev_class_mask; /* mask to determine the bits of device class of interest */
} tBTA_DM_COD_COND;

/* Inquiry Filter Condition */
typedef union {
    BD_ADDR bd_addr;                 /* BD address of  device to filter. */
    tBTA_DM_COD_COND dev_class_cond; /* Device class filter condition */
} tBTA_DM_INQ_COND;

/* Inquiry Parameters */
typedef struct {
    tBTA_DM_INQ_MODE mode;        /* Inquiry mode, limited or general. */
    UINT8 duration;               /* Inquiry duration in 1.28 sec units. */
    UINT8 max_resps;              /* Maximum inquiry responses.  Set to zero for unlimited responses. */
    BOOLEAN report_dup;           /* report duplicated inquiry response with higher RSSI value */
    tBTA_DM_INQ_FILT filter_type; /* Filter condition type. */
    tBTA_DM_INQ_COND filter_cond; /* Filter condition data. */
} tBTA_DM_INQ;

/* Security Callback Events */
#define BTA_DM_ENABLE_EVT 0             /* Enable Event */
#define BTA_DM_PIN_REQ_EVT 2            /* PIN request. */
#define BTA_DM_AUTH_CMPL_EVT 3          /* Authentication complete indication. */
#define BTA_DM_AUTHORIZE_EVT 4          /* Authorization request. */
#define BTA_DM_LINK_UP_EVT 5            /* Connection UP event */
#define BTA_DM_LINK_DOWN_EVT 6          /* Connection DOWN event */
#define BTA_DM_SIG_STRENGTH_EVT 7       /* Signal strength for bluetooth connection */
#define BTA_DM_BUSY_LEVEL_EVT 8         /* System busy level */
// btla-specific ++
// btla-specific --
typedef UINT8 tBTA_DM_SEC_EVT;

/* Structure associated with BTA_DM_ENABLE_EVT */
typedef struct {
    BD_ADDR bd_addr; /* BD address of local device. */
    tBTA_STATUS status;
} tBTA_DM_ENABLE;

/* Structure associated with BTA_DM_PIN_REQ_EVT */
typedef struct {
    BD_ADDR bd_addr;     /* BD address peer device. */
    BD_NAME bd_name;     /* Name of peer device. */
    DEV_CLASS dev_class; /* Class of Device */
} tBTA_DM_PIN_REQ;

/* Link Key Notification Event (Key Type) definitions */

/* Structure associated with BTA_DM_AUTH_CMPL_EVT */
typedef struct {
    BD_ADDR bd_addr;     /* BD address peer device. */
    BD_NAME bd_name;     /* Name of peer device. */
    BOOLEAN key_present; /* Valid link key value in key element */
    LINK_KEY key;        /* Link key associated with peer device. */
    UINT8 key_type;      /* The type of Link Key */
    BOOLEAN success;     /* TRUE of authentication succeeded, FALSE if failed. */
    UINT8 fail_reason;   /* The HCI reason/error code for when success=FALSE */
} tBTA_DM_AUTH_CMPL;

/* Structure associated with BTA_DM_AUTHORIZE_EVT */
typedef struct {
    BD_ADDR bd_addr;         /* BD address peer device. */
    BD_NAME bd_name;         /* Name of peer device. */
    tBTA_SERVICE_ID service; /* Service ID to authorize. */
                             // btla-specific ++
    DEV_CLASS dev_class;
    // btla-specific --
} tBTA_DM_AUTHORIZE;

/* Structure associated with BTA_DM_LINK_UP_EVT */
typedef struct {
    BD_ADDR bd_addr; /* BD address peer device. */
} tBTA_DM_LINK_UP;

/*
**  Defentions for HCI Error Codes that are past in the events
*/

/* Structure associated with BTA_DM_LINK_DOWN_EVT */
typedef struct {
    BD_ADDR bd_addr; /* BD address peer device. */
    UINT8 status;    /* connection open/closed */
} tBTA_DM_LINK_DOWN;

typedef INT8 tBTA_DM_RSSI_VALUE;
typedef UINT8 tBTA_DM_LINK_QUALITY_VALUE;

/* signal strength mask */

typedef UINT8 tBTA_SIG_STRENGTH_MASK;

/* Structure associated with BTA_DM_SIG_STRENGTH_EVT */
typedef struct {
    BD_ADDR bd_addr;             /* BD address peer device. */
    tBTA_SIG_STRENGTH_MASK mask; /* mask for the values that are valid */
    tBTA_DM_RSSI_VALUE rssi_value;
    tBTA_DM_LINK_QUALITY_VALUE link_quality_value;

} tBTA_DM_SIG_STRENGTH;

/* Structure associated with BTA_DM_BUSY_LEVEL_EVT */
typedef struct {
    UINT8 level; /* when paging or inquiring, level is 10.
                      Otherwise, the number of ACL links */
} tBTA_DM_BUSY_LEVEL;

// muff1n: only filled with used members
typedef union {
    tBTA_DM_ENABLE enable;             /* BTA enabled */
    tBTA_DM_PIN_REQ pin_req;           /* PIN request */
    tBTA_DM_AUTH_CMPL auth_cmpl;       /* Authentication complete indication */
    tBTA_DM_AUTHORIZE authorize;       /* Authorization request */
    tBTA_DM_LINK_UP link_up;           /* ACL connection up event */
    tBTA_DM_LINK_DOWN link_down;       /* ACL connection down event */
    tBTA_DM_SIG_STRENGTH sig_strength; /* rssi and link quality value */
    tBTA_DM_BUSY_LEVEL busy_level;     /* System busy level */
} tBTA_DM_SEC;

typedef void(tBTA_DM_SEC_CBACK)(tBTA_DM_SEC_EVT event, tBTA_DM_SEC* p_data);

/* Search callback events */
#define BTA_DM_INQ_RES_EVT 0            /* Inquiry result for a peer device. */
#define BTA_DM_INQ_CMPL_EVT 1           /* Inquiry complete. */
#define BTA_DM_DISC_RES_EVT 2           /* Discovery result for a peer device. */
#define BTA_DM_DI_DISC_CMPL_EVT 5       /* Discovery complete. */

typedef UINT8 tBTA_DM_SEARCH_EVT;

// muff1n: commented some fields out
/* Structure associated with BTA_DM_INQ_RES_EVT */
typedef struct {
    BD_ADDR bd_addr;     /* BD address peer device. */
    DEV_CLASS dev_class; /* Device class of peer device. */
    // BOOLEAN         remt_name_not_required; /* Application sets this flag if it already knows the name of the device */
    /* If the device name is known to application BTA skips the remote name request */
    // BOOLEAN         is_limited;             /* TRUE, if the limited inquiry bit is set in the CoD */
    INT8 rssi;    /* The rssi value */
    UINT8* p_eir; /* received EIR */
} tBTA_DM_INQ_RES;

/* Structure associated with BTA_DM_INQ_CMPL_EVT */
typedef struct {
    UINT8 num_resps; /* Number of inquiry responses. */
} tBTA_DM_INQ_CMPL;

/* Structure associated with BTA_DM_DI_DISC_CMPL_EVT */
typedef struct {
    BD_ADDR bd_addr;  /* BD address peer device. */
    UINT8 num_record; /* Number of DI record */
    tBTA_STATUS result;
} tBTA_DM_DI_DISC_CMPL;

/* Structure associated with BTA_DM_DISC_RES_EVT */
typedef struct {
    BD_ADDR bd_addr;             /* BD address peer device. */
    BD_NAME bd_name;             /* Name of peer device. */
    tBTA_SERVICE_MASK services;  /* Services found on peer device. */
                                 // btla-specific ++
    UINT8* p_raw_data;           /* Raw data for discovery DB */
    UINT32 raw_data_size;        /* size of raw data */
    tBT_DEVICE_TYPE device_type; /* device type in case it is BLE device */
    UINT32 num_uuids;
    UINT8* p_uuid_list;
    // btla-specific --
    tBTA_STATUS result;
} tBTA_DM_DISC_RES;

/* Structure associated with tBTA_DM_DISC_BLE_RES */
typedef struct {
    BD_ADDR bd_addr;  /* BD address peer device. */
    BD_NAME bd_name;  /* Name of peer device. */
    tBT_UUID service; /* GATT based Services UUID found on peer device. */
} tBTA_DM_DISC_BLE_RES;

/* Union of all search callback structures */
typedef union {
    tBTA_DM_INQ_RES inq_res;           /* Inquiry result for a peer device. */
    tBTA_DM_INQ_CMPL inq_cmpl;         /* Inquiry complete. */
    tBTA_DM_DISC_RES disc_res;         /* Discovery result for a peer device. */
    tBTA_DM_DISC_BLE_RES disc_ble_res; /* discovery result for GATT based service */
    tBTA_DM_DI_DISC_CMPL di_disc;      /* DI discovery result for a peer device */
} tBTA_DM_SEARCH;

/* Search callback */
typedef void(tBTA_DM_SEARCH_CBACK)(tBTA_DM_SEARCH_EVT event, tBTA_DM_SEARCH* p_data);

/* type of protocol mode */
typedef UINT8 tBTA_HH_PROTO_MODE;

/* BTA HID Host callback events */
#define BTA_HH_OPEN_EVT 2       /* connection opened */
#define BTA_HH_CLOSE_EVT 3      /* connection closed */
#define BTA_HH_SET_RPT_EVT 5    /* BTA_HhSetReport callback */
#define BTA_HH_SET_PROTO_EVT 7  /* BTA_HhSetProtoMode callback */
#define BTA_HH_SET_IDLE_EVT 9   /* BTA_HhSetIdle finish callback */
#define BTA_HH_ADD_DEV_EVT 11   /* Add Device callback */
#define BTA_HH_RMV_DEV_EVT 12   /* remove device finished */

typedef UINT16 tBTA_HH_EVT;

enum {
    BTA_HH_OK,
    BTA_HH_HS_HID_NOT_READY,  /* handshake error : device not ready */
    BTA_HH_HS_INVALID_RPT_ID, /* handshake error : invalid report ID */
    BTA_HH_HS_TRANS_NOT_SPT,  /* handshake error : transaction not spt */
    BTA_HH_HS_INVALID_PARAM,  /* handshake error : invalid paremter */
    BTA_HH_HS_ERROR,          /* handshake error : unspecified HS error */
    BTA_HH_ERR,               /* general BTA HH error */
    BTA_HH_ERR_SDP,           /* SDP error */
    BTA_HH_ERR_PROTO,         /* SET_Protocol error,
                                  only used in BTA_HH_OPEN_EVT callback */
    BTA_HH_ERR_DB_FULL,       /* device database full error, used in
                                 BTA_HH_OPEN_EVT/BTA_HH_ADD_DEV_EVT */
    BTA_HH_ERR_TOD_UNSPT,     /* type of device not supported */
    BTA_HH_ERR_NO_RES,        /* out of system resources */
    BTA_HH_ERR_AUTH_FAILED,   /* authentication fail */
    BTA_HH_ERR_HDL
};
typedef UINT8 tBTA_HH_STATUS;

/* callback event data for BTA_HH_OPEN_EVT */
typedef struct {
    BD_ADDR bda;           /* HID device bd address    */
    tBTA_HH_STATUS status; /* operation status         */
    UINT8 handle;          /* device handle            */
} tBTA_HH_CONN;

typedef tBTA_HH_CONN tBTA_HH_DEV_INFO;

/* callback event data */
typedef struct {
    tBTA_HH_STATUS status; /* operation status         */
    UINT8 handle;          /* device handle            */
} tBTA_HH_CBDATA;

// muff1n: only filled with used members
typedef union {
    tBTA_HH_DEV_INFO dev_info; /* BTA_HH_ADD_DEV_EVT, BTA_HH_RMV_DEV_EVT   */
    tBTA_HH_CONN conn;         /* BTA_HH_OPEN_EVT      */
    tBTA_HH_CBDATA dev_status; /* BTA_HH_CLOSE_EVT,
                                  BTA_HH_SET_PROTO_EVT
                                  BTA_HH_SET_RPT_EVT
                                  BTA_HH_SET_IDLE_EVT  */
} tBTA_HH;

/* BTA HH callback function */
typedef void(tBTA_HH_CBACK)(tBTA_HH_EVT event, tBTA_HH* p_data);

/* General callback function for notifying an application that a synchronous
** BTM function is complete. The pointer contains the address of any returned data.
*/
typedef void(tBTM_CMPL_CB)(void* p1);
/* Structure returned with local version information */

typedef struct {
    UINT8 hci_version;
    UINT16 hci_revision;
    UINT8 lmp_version;
    UINT16 manufacturer;
    UINT16 lmp_subversion;
} tBTM_VERSION_INFO;

/* Structure returned with Vendor Specific Command complete callback */
typedef struct {
    UINT16 opcode;
    UINT16 param_len;
    UINT8* p_param_buf;
} tBTM_VSC_CMPL;

/* Callback function for when device status changes. Appl must poll for
** what the new state is (BTM_IsDeviceUp). The event occurs whenever the stack
** has detected that the controller status has changed. This asynchronous event
** is enabled/disabled by calling BTM_RegisterForDeviceStatusNotif().
*/
enum { BTM_DEV_STATUS_UP, BTM_DEV_STATUS_DOWN, BTM_DEV_STATUS_CMD_TOUT };
typedef UINT8 tBTM_DEV_STATUS;

typedef void(tBTM_DEV_STATUS_CB)(tBTM_DEV_STATUS status);

/* Callback function for when a vendor specific event occurs. The length and
** array of returned parameter bytes are included. This asynchronous event
** is enabled/disabled by calling BTM_RegisterForVSEvents().
*/
typedef void(tBTM_VS_EVT_CB)(UINT8 len, UINT8* p);

/* VSC callback function for notifying an application that a synchronous
** BTM function is complete. The pointer contains the address of any returned data.
*/
typedef void(tBTM_VSC_CMPL_CB)(tBTM_VSC_CMPL* p1);

/* Attributes mask values to be used in HID_HostAddDev API */

typedef UINT16 tBTA_HH_ATTR_MASK;

typedef struct desc_info {
    UINT16 dl_len;
    UINT8* dsc_list;
} tHID_DEV_DSCP_INFO;

typedef tHID_DEV_DSCP_INFO tBTA_HH_DEV_DESCR;

/* Policy settings status */

/* HCI mode defenitions */
#define HCI_MODE_ACTIVE 0x00
#define HCI_MODE_HOLD 0x01
#define HCI_MODE_SNIFF 0x02
#define HCI_MODE_PARK 0x03

/* BTM Power manager status codes */
enum {
    BTM_PM_STS_ACTIVE = HCI_MODE_ACTIVE,
    BTM_PM_STS_HOLD = HCI_MODE_HOLD,
    BTM_PM_STS_SNIFF = HCI_MODE_SNIFF,
    BTM_PM_STS_PARK = HCI_MODE_PARK,
    BTM_PM_STS_SSR,     /* report the SSR parameters in HCI_SNIFF_SUB_RATE_EVT */
    BTM_PM_STS_PENDING, /* when waiting for status from controller */
    BTM_PM_STS_ERROR    /* when HCI command status returns error */
};
typedef UINT8 tBTM_PM_STATUS;

/* BTM Power manager modes */

enum {
    BTM_PM_MD_ACTIVE = BTM_PM_STS_ACTIVE,
    BTM_PM_MD_HOLD = BTM_PM_STS_HOLD,
    BTM_PM_MD_SNIFF = BTM_PM_STS_SNIFF,
    BTM_PM_MD_PARK = BTM_PM_STS_PARK,
    BTM_PM_MD_FORCE = 0x10 /* OR this to force ACL link to a certain mode */
};
typedef UINT8 tBTM_PM_MODE;

/* Operation codes */

typedef struct {
    UINT16 max;
    UINT16 min;
    UINT16 attempt;
    UINT16 timeout;
    tBTM_PM_MODE mode;
} tBTM_PM_PWR_MD;

/*************************************
**  Power Manager Callback Functions
**************************************/
typedef void(tBTM_PM_STATUS_CBACK)(BD_ADDR p_bda, tBTM_PM_STATUS status, UINT16 value, UINT8 hci_status);

/************************
**  Stored Linkkey Types
*************************/

typedef struct {
    UINT8 event;

} tBTM_STORED_LINK_KEYS_EVT;

typedef struct {
    UINT8 event;
    UINT8 num_keys;

} tBTM_RETURN_LINK_KEYS_EVT;

typedef struct {
    BD_ADDR bd_addr;
    LINK_KEY link_key;

} tBTM_BD_ADDR_LINK_KEY_PAIR;

typedef struct {
    UINT8 event;
    UINT8 status;
    UINT16 max_keys;
    UINT16 read_keys;

} tBTM_READ_STORED_LINK_KEY_COMPLETE;

typedef struct {
    UINT8 event;
    UINT8 status;
    UINT8 num_keys;

} tBTM_WRITE_STORED_LINK_KEY_COMPLETE;

typedef struct {
    UINT8 event;
    UINT8 status;
    UINT16 num_keys;

} tBTM_DELETE_STORED_LINK_KEY_COMPLETE;

// ---

tBTA_STATUS BTA_EnableBluetooth(tBTA_DM_SEC_CBACK* p_cback);
tBTA_STATUS BTA_DisableBluetooth(void);

// muff1n: most changed prototype so far
tBTA_STATUS BTA_DmAddDevice(BD_ADDR bd_addr, LINK_KEY link_key, tBTA_SERVICE_MASK trusted_mask, BOOLEAN is_trusted);
tBTA_STATUS BTA_DmRemoveDevice(BD_ADDR bd_addr);
void BTA_DmSearch(tBTA_DM_INQ* p_dm_inq, tBTA_SERVICE_MASK services, tBTA_DM_SEARCH_CBACK* p_cback);
void BTA_DmSearchCancel(void);
void BTA_DmPinReply(BD_ADDR bd_addr, BOOLEAN accept, UINT8 pin_len, UINT8* p_pin);
BOOLEAN BTA_DmIsDeviceUp(void);
void BTA_DmSetDeviceName(char* p_name);

// muff1n: disc_mode and conn_mode might be the , based on usage
void BTA_DmSetVisibility(UINT8, UINT8);

// muff1n: ucd_enabled might be the missing parameter
void BTA_HhEnable(tBTA_SEC sec_mask, tBTA_HH_CBACK* p_cback);
// muff1n: possibly takes just a tBTA_HH_DEV_DESCR in this version? i wrote it like that
void BTA_HhAddDev(BD_ADDR bda, tBTA_HH_ATTR_MASK attr_mask, UINT8 sub_class, UINT8 app_id, tBTA_HH_DEV_DESCR descr);
void BTA_HhRemoveDev(UINT8 dev_handle);
void BTA_HhOpen(BD_ADDR dev_bda, tBTA_HH_PROTO_MODE mode, tBTA_SEC sec_mask);
void BTA_HhClose(UINT8 dev_handle);

// muff1n: dev_bda is likely the missing parameter, as it is not used
void BTA_HhSendData(UINT8 dev_handle, BT_HDR* p_data);

void bta_sys_set_trace_level(UINT8 level);

void BTM_DeviceReset(tBTM_CMPL_CB* p_cb);

tBTM_STATUS BTM_EnableTestMode(void);

tBTM_STATUS BTM_ReadStoredLinkKey(BD_ADDR bd_addr, tBTM_CMPL_CB* p_cb);
tBTM_STATUS BTM_WriteStoredLinkKey(UINT8 num_keys, BD_ADDR* bd_addr, LINK_KEY* link_key, tBTM_CMPL_CB* p_cb);
tBTM_STATUS BTM_DeleteStoredLinkKey(BD_ADDR bd_addr, tBTM_CMPL_CB* p_cb);

tBTM_STATUS BTM_SetPowerMode(UINT8 pm_id, BD_ADDR remote_bda, tBTM_PM_PWR_MD* p_mode);
tBTM_STATUS BTM_SetAfhChannels(UINT8 first, UINT8 last);

tBTM_STATUS BTM_VendorSpecificCommand(UINT16 opcode, UINT8 param_len, UINT8* p_param_buf, tBTM_VSC_CMPL_CB* p_cb);

tBTM_STATUS BTM_ReadLocalVersion(tBTM_VERSION_INFO* p_vers);
tBTM_STATUS BTM_SetDeviceClass(DEV_CLASS dev_class);

tBTM_STATUS btm_remove_acl(BD_ADDR bd_addr);

// muff1n: is_register is probably the missing parameter here
tBTM_STATUS BTM_RegisterForVSEvents(tBTM_VS_EVT_CB* p_cb);
tBTM_DEV_STATUS_CB* BTM_RegisterForDeviceStatusNotif(tBTM_DEV_STATUS_CB* p_cb);

tBTM_STATUS BTM_PmRegister(UINT8 mask, UINT8* p_pm_id, tBTM_PM_STATUS_CBACK* p_cb);
tBTM_STATUS BTM_WritePageTimeout(UINT16 timeout);
void BTM_SetDefaultLinkPolicy(UINT16 settings);
void BTM_SetDefaultLinkSuperTout(UINT16 timeout);

BOOLEAN BTM_SecAddDevice(BD_ADDR bd_addr, DEV_CLASS dev_class, BD_NAME bd_name, BD_FEATURES features, UINT32 trusted_mask[], LINK_KEY link_key);

void* GKI_getbuf(UINT16 size);

UINT8 L2CA_SetTraceLevel(UINT8 trace_level);

UINT8 SDP_SetTraceLevel(UINT8 new_level);

/* muff1n: not in bluedroid source; might be older removed API functions */
void BTA_DmSendHciReset(void);
void BTA_HhGetAclQueueInfo(void);
void BTA_Init(void);
void BTA_CleanUp(void (*p_cb)(tBTA_STATUS status));  // probably

// ---
/* muff1n: I wrote this definition myself
 * TODO: would this be part of BLE or WUD?
 */

#define SC_MAX_DEV_ENTRY_FOR_STD 10
#define SC_MAX_DEV_ENTRY_FOR_SMP 6
#define SC_MAX_DEV_ENTRY (SC_MAX_DEV_ENTRY_FOR_STD + SC_MAX_DEV_ENTRY_FOR_SMP)

struct small_dev_info {
    char devName[20];  // size 0x14? offset 0x00 // might be 0x13?
    char at_0x14[1];   // size 0x??, offset 0x14?
    char __pad0[0x20 - (0x14 + 0x01)];
    LINK_KEY linkKey;  // size 0x10, offset 0x20
    char __pad1[0x10];
};  // size 0x40

typedef struct {
    UINT8 handle;         /* device handle */
    UINT16 num_queued;    /* Number of buffers queued by l2cap */
    UINT16 num_not_acked; /* Number of buffers queued in baseband (sent to baseband, but have not received 'number of packets complete event') */
} tBTA_HH_ACL_QUEUE;

/* Data for BTA_HH_ACL_QUEUE_INFO_EVT */
typedef struct {
    UINT16 num_avail_buffs;          /* Number of available ACL buffers */
    UINT16 num_buffs;                /* Number of ACL buffers that the controller can handle */
    UINT16 num_links;                /* Number of active links */
    tBTA_HH_ACL_QUEUE queue_info[4]; /* Array of queue info */
} tBTA_HH_ACL_QUEUE_INFO;

typedef void (*tUSER_CBACK)(UINT8 err);

#ifdef __cplusplus
}
#endif

#endif  // BTE_H
