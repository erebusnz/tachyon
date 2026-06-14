/**
  ******************************************************************************
  * @file    usbd_midi.c
  * @brief   Class-compliant USB-MIDI 1.0 device. One embedded MIDI port:
  *          host->device MIDI arrives on the bulk-OUT endpoint as 32-bit USB-MIDI
  *          Event Packets, which are parsed here and forwarded to the app voice
  *          engine via app_midi_event(). See usb-midi.md §4/§5.
  ******************************************************************************
  */

#include "usbd_midi.h"
#include "usbd_ctlreq.h"
#include "app.h"            /* app_midi_event(), APP_MIDI_NOTE_ON/OFF */

/* Private function prototypes ----------------------------------------------*/
static uint8_t  USBD_MIDI_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t  USBD_MIDI_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx);
static uint8_t  USBD_MIDI_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req);
static uint8_t  USBD_MIDI_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t  USBD_MIDI_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum);
static uint8_t *USBD_MIDI_GetCfgDesc(uint16_t *length);
static uint8_t *USBD_MIDI_GetDeviceQualifierDesc(uint16_t *length);

/* Class handle: a single static instance (no USBD_malloc — the static pool in
 * usbd_conf.c is sized for the CDC handle). */
typedef struct {
  uint8_t  rx[MIDI_DATA_FS_MAX_PACKET_SIZE];
} USBD_MIDI_HandleTypeDef;

static USBD_MIDI_HandleTypeDef s_midi;
static volatile uint8_t        s_connected;

/* Class callbacks structure (non-composite layout, matches USBD_ClassTypeDef). */
USBD_ClassTypeDef USBD_MIDI =
{
  USBD_MIDI_Init,
  USBD_MIDI_DeInit,
  USBD_MIDI_Setup,
  NULL,                              /* EP0_TxSent       */
  NULL,                              /* EP0_RxReady      */
  USBD_MIDI_DataIn,
  USBD_MIDI_DataOut,
  NULL,                              /* SOF              */
  NULL,                              /* IsoINIncomplete  */
  NULL,                              /* IsoOUTIncomplete */
  USBD_MIDI_GetCfgDesc,              /* GetHSConfigDescriptor        */
  USBD_MIDI_GetCfgDesc,              /* GetFSConfigDescriptor        */
  USBD_MIDI_GetCfgDesc,              /* GetOtherSpeedConfigDescriptor */
  USBD_MIDI_GetDeviceQualifierDesc,
};

/* Configuration descriptor: AudioControl interface + MIDIStreaming interface
 * with one embedded port (1 IN jack pair + 1 OUT jack pair) and a bulk OUT/IN
 * endpoint pair. */
__ALIGN_BEGIN static uint8_t USBD_MIDI_CfgDesc[USB_MIDI_CONFIG_DESC_SIZ] __ALIGN_END =
{
  /* ---- Configuration descriptor ---- */
  0x09, USB_DESC_TYPE_CONFIGURATION,
  LOBYTE(USB_MIDI_CONFIG_DESC_SIZ), HIBYTE(USB_MIDI_CONFIG_DESC_SIZ),
  0x02,                              /* bNumInterfaces: AC + MS          */
  0x01,                              /* bConfigurationValue              */
  0x00,                              /* iConfiguration                   */
#if (USBD_SELF_POWERED == 1U)
  0xC0,                              /* bmAttributes: self powered       */
#else
  0x80,                              /* bmAttributes: bus powered        */
#endif
  USBD_MAX_POWER,                    /* bMaxPower                        */

  /* ---- Standard AudioControl interface (interface 0) ---- */
  0x09, USB_DESC_TYPE_INTERFACE,
  0x00,                              /* bInterfaceNumber                 */
  0x00,                              /* bAlternateSetting                */
  0x00,                              /* bNumEndpoints                    */
  0x01,                              /* bInterfaceClass: AUDIO           */
  0x01,                              /* bInterfaceSubClass: AUDIO_CONTROL*/
  0x00,                              /* bInterfaceProtocol               */
  0x00,                              /* iInterface                       */

  /* ---- Class-specific AC header ---- */
  0x09, 0x24,                        /* bLength, CS_INTERFACE            */
  0x01,                              /* HEADER                           */
  0x00, 0x01,                        /* bcdADC 1.00                      */
  0x09, 0x00,                        /* wTotalLength (this header)       */
  0x01,                              /* bInCollection: 1 streaming if    */
  0x01,                              /* baInterfaceNr(1): MS interface   */

  /* ---- Standard MIDIStreaming interface (interface 1) ---- */
  0x09, USB_DESC_TYPE_INTERFACE,
  0x01,                              /* bInterfaceNumber                 */
  0x00,                              /* bAlternateSetting                */
  0x02,                              /* bNumEndpoints: bulk OUT + IN     */
  0x01,                              /* bInterfaceClass: AUDIO           */
  0x03,                              /* bInterfaceSubClass: MIDISTREAMING*/
  0x00,                              /* bInterfaceProtocol               */
  0x00,                              /* iInterface                       */

  /* ---- Class-specific MS header ---- */
  0x07, 0x24,                        /* bLength, CS_INTERFACE            */
  0x01,                              /* MS_HEADER                        */
  0x00, 0x01,                        /* bcdMSC 1.00                      */
  0x25, 0x00,                        /* wTotalLength: 37 (hdr + 4 jacks) */

  /* ---- MIDI IN Jack (Embedded) id 1 ---- */
  0x06, 0x24, 0x02,                  /* MIDI_IN_JACK                     */
  0x01,                              /* EMBEDDED                         */
  0x01,                              /* bJackID = 1                      */
  0x00,                              /* iJack                            */

  /* ---- MIDI IN Jack (External) id 2 ---- */
  0x06, 0x24, 0x02,
  0x02,                              /* EXTERNAL                         */
  0x02,                              /* bJackID = 2                      */
  0x00,

  /* ---- MIDI OUT Jack (Embedded) id 3, fed from External IN (2) ---- */
  0x09, 0x24, 0x03,                  /* MIDI_OUT_JACK                    */
  0x01,                              /* EMBEDDED                         */
  0x03,                              /* bJackID = 3                      */
  0x01,                              /* bNrInputPins = 1                 */
  0x02,                              /* BaSourceID(1) = IN jack 2        */
  0x01,                              /* BaSourcePin(1)                   */
  0x00,                              /* iJack                            */

  /* ---- MIDI OUT Jack (External) id 4, fed from Embedded IN (1) ---- */
  0x09, 0x24, 0x03,
  0x02,                              /* EXTERNAL                         */
  0x04,                              /* bJackID = 4                      */
  0x01,                              /* bNrInputPins = 1                 */
  0x01,                              /* BaSourceID(1) = IN jack 1        */
  0x01,                              /* BaSourcePin(1)                   */
  0x00,

  /* ---- Standard bulk OUT endpoint (host -> device) ---- */
  0x09, USB_DESC_TYPE_ENDPOINT,
  MIDI_OUT_EP,                       /* bEndpointAddress                 */
  0x02,                              /* bmAttributes: Bulk               */
  LOBYTE(MIDI_DATA_FS_MAX_PACKET_SIZE), HIBYTE(MIDI_DATA_FS_MAX_PACKET_SIZE),
  0x00,                              /* bInterval                        */
  0x00,                              /* bRefresh                         */
  0x00,                              /* bSynchAddress                    */

  /* ---- Class-specific MS bulk OUT endpoint ---- */
  0x05, 0x25,                        /* bLength, CS_ENDPOINT             */
  0x01,                              /* MS_GENERAL                       */
  0x01,                              /* bNumEmbMIDIJack = 1              */
  0x01,                              /* BaAssocJackID = Embedded IN (1)  */

  /* ---- Standard bulk IN endpoint (device -> host) ---- */
  0x09, USB_DESC_TYPE_ENDPOINT,
  MIDI_IN_EP,                        /* bEndpointAddress                 */
  0x02,                              /* bmAttributes: Bulk               */
  LOBYTE(MIDI_DATA_FS_MAX_PACKET_SIZE), HIBYTE(MIDI_DATA_FS_MAX_PACKET_SIZE),
  0x00,                              /* bInterval                        */
  0x00,                              /* bRefresh                         */
  0x00,                              /* bSynchAddress                    */

  /* ---- Class-specific MS bulk IN endpoint ---- */
  0x05, 0x25,                        /* bLength, CS_ENDPOINT             */
  0x01,                              /* MS_GENERAL                       */
  0x01,                              /* bNumEmbMIDIJack = 1              */
  0x03,                              /* BaAssocJackID = Embedded OUT (3) */
};

/* Standard device-qualifier descriptor (FS device). */
__ALIGN_BEGIN static uint8_t USBD_MIDI_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END =
{
  USB_LEN_DEV_QUALIFIER_DESC, USB_DESC_TYPE_DEVICE_QUALIFIER,
  0x00, 0x02, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00,
};

/* Private functions --------------------------------------------------------*/

static uint8_t USBD_MIDI_Init(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  (void)USBD_LL_OpenEP(pdev, MIDI_OUT_EP, USBD_EP_TYPE_BULK, MIDI_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_out[MIDI_OUT_EP & 0xFU].is_used = 1U;

  (void)USBD_LL_OpenEP(pdev, MIDI_IN_EP, USBD_EP_TYPE_BULK, MIDI_DATA_FS_MAX_PACKET_SIZE);
  pdev->ep_in[MIDI_IN_EP & 0xFU].is_used = 1U;

  pdev->pClassDataCmsit[pdev->classId] = &s_midi;
  pdev->pClassData = &s_midi;
  s_connected = 1U;

  /* Arm the OUT endpoint for the first packet. */
  (void)USBD_LL_PrepareReceive(pdev, MIDI_OUT_EP, s_midi.rx, MIDI_DATA_FS_MAX_PACKET_SIZE);

  return (uint8_t)USBD_OK;
}

static uint8_t USBD_MIDI_DeInit(USBD_HandleTypeDef *pdev, uint8_t cfgidx)
{
  UNUSED(cfgidx);

  (void)USBD_LL_CloseEP(pdev, MIDI_OUT_EP);
  pdev->ep_out[MIDI_OUT_EP & 0xFU].is_used = 0U;

  (void)USBD_LL_CloseEP(pdev, MIDI_IN_EP);
  pdev->ep_in[MIDI_IN_EP & 0xFU].is_used = 0U;

  pdev->pClassDataCmsit[pdev->classId] = NULL;
  pdev->pClassData = NULL;
  s_connected = 0U;

  return (uint8_t)USBD_OK;
}

/* MIDI defines no mandatory class requests; handle the standard ones the way the
 * stock classes do and stall anything else. */
static uint8_t USBD_MIDI_Setup(USBD_HandleTypeDef *pdev, USBD_SetupReqTypedef *req)
{
  USBD_StatusTypeDef ret = USBD_OK;
  uint16_t status_info = 0U;
  uint8_t  ifalt = 0U;

  switch (req->bmRequest & USB_REQ_TYPE_MASK)
  {
    case USB_REQ_TYPE_STANDARD:
      switch (req->bRequest)
      {
        case USB_REQ_GET_STATUS:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
            (void)USBD_CtlSendData(pdev, (uint8_t *)&status_info, 2U);
          else { USBD_CtlError(pdev, req); ret = USBD_FAIL; }
          break;

        case USB_REQ_GET_INTERFACE:
          if (pdev->dev_state == USBD_STATE_CONFIGURED)
            (void)USBD_CtlSendData(pdev, &ifalt, 1U);
          else { USBD_CtlError(pdev, req); ret = USBD_FAIL; }
          break;

        case USB_REQ_SET_INTERFACE:
          if (pdev->dev_state != USBD_STATE_CONFIGURED) { USBD_CtlError(pdev, req); ret = USBD_FAIL; }
          break;

        case USB_REQ_CLEAR_FEATURE:
          break;

        default:
          USBD_CtlError(pdev, req); ret = USBD_FAIL; break;
      }
      break;

    case USB_REQ_TYPE_CLASS:
    default:
      USBD_CtlError(pdev, req);
      ret = USBD_FAIL;
      break;
  }

  return (uint8_t)ret;
}

static uint8_t USBD_MIDI_DataIn(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  UNUSED(pdev);
  UNUSED(epnum);
  return (uint8_t)USBD_OK;   /* device->host stream unused */
}

/* Bulk-OUT complete: parse the 4-byte USB-MIDI Event Packets and forward note
 * events, then re-arm the endpoint. Runs in the OTG IRQ — app_midi_event() only
 * pushes to a lock-free ring, so this stays short. */
static uint8_t USBD_MIDI_DataOut(USBD_HandleTypeDef *pdev, uint8_t epnum)
{
  uint32_t len = USBD_LL_GetRxDataSize(pdev, epnum);
  const uint8_t *p = s_midi.rx;

  for (uint32_t i = 0U; (i + 4U) <= len; i += 4U, p += 4)
  {
    uint8_t cin  = p[0] & 0x0FU;     /* Code Index Number */
    uint8_t note = p[2];
    uint8_t vel  = p[3];

    if (cin == 0x09U)                /* Note On */
    {
      if (vel == 0U) app_midi_event(APP_MIDI_NOTE_OFF, note, 0U);
      else           app_midi_event(APP_MIDI_NOTE_ON,  note, vel);
    }
    else if (cin == 0x08U)           /* Note Off */
    {
      app_midi_event(APP_MIDI_NOTE_OFF, note, 0U);
    }
    /* CC (0x0B) / pitch-bend (0x0E) etc. ignored for now — see usb-midi.md §8 */
  }

  (void)USBD_LL_PrepareReceive(pdev, MIDI_OUT_EP, s_midi.rx, MIDI_DATA_FS_MAX_PACKET_SIZE);
  return (uint8_t)USBD_OK;
}

static uint8_t *USBD_MIDI_GetCfgDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_MIDI_CfgDesc);
  return USBD_MIDI_CfgDesc;
}

static uint8_t *USBD_MIDI_GetDeviceQualifierDesc(uint16_t *length)
{
  *length = (uint16_t)sizeof(USBD_MIDI_DeviceQualifierDesc);
  return USBD_MIDI_DeviceQualifierDesc;
}

uint8_t USBD_MIDI_IsConnected(void)
{
  return s_connected;
}
