/*********************************************************************
 * INCLUDES
 */
#include "OSAL.h"
#include "AF.h"
#include "ZDApp.h"
#include "ZDObject.h"
#include "ZDProfile.h"
#include "sapi.h"

#include "SensorSys_Coor.h"
#include "device.h"
#include "DebugTrace.h"

#if !defined( WIN32 )
  #include "OnBoard.h"
#endif

/* HAL */
#include "hal_led.h"
#include "hal_uart.h"

/*********************************************************************
 * MACROS
 */

/*********************************************************************
 * CONSTANTS
 */

/*********************************************************************
 * TYPEDEFS
 */

/*********************************************************************
 * GLOBAL VARIABLES
 */
byte Led_TaskID;
uint8 ledCnt;


// Led 端点的簇ID
// This list should be filled with Application specific Cluster IDs.
cId_t Led_ClusterList[LED_MAX_CLUSTERS] =
{
    LED_LIGHT,
    LED_DIM,
    LED_FLASH
};

// Led 端点简单描述符
SimpleDescriptionFormat_t Led_SimpleDesc[LED_NUM_MAX] =
{
	LED_ENDPOINT,           //  int Endpoint;
	SYS_PROFID,                //  uint16 AppProfId[2];
	SYS_DEVICEID,              //  uint16 AppDeviceId[2];
	SYS_DEVICE_VERSION,        //  int   AppDevVer:4;
	SYS_FLAGS,                 //  int   AppFlags:4;
	0,                          //  byte  AppNumInClusters;
	NULL,                       //  byte *pAppInClusterList;
	LED_MAX_CLUSTERS,          //  byte  AppNumOutClusters;
	(cId_t *)Led_ClusterList   //  byte *pAppOutClusterList;
};

endPointDesc_t Led_epDesc[LED_NUM_MAX];

/*********************************************************************
 * EXTERNAL VARIABLES
 */

/*********************************************************************
 * EXTERNAL FUNCTIONS
 */

/*********************************************************************
 * LOCAL VARIABLES
 */
afAddrType_t Led_DstAddr;
uint16 led_bindInProgress;

/*********************************************************************
 * LOCAL FUNCTIONS
 */
void Led_Init( byte task_id );
void Led_ProcessZDOMsgs( zdoIncomingMsg_t *inMsg );

void Led_BindDevice ( uint8 create, uint8 endpoint, uint16 *commandId, uint8 *pDestination );

static void SAPI_BindConfirm( uint16 commandId, uint8 status );
/*********************************************************************
 * @fn      Led_Init
 *
 * @brief   Initialization function for the Generic App Task.
 *          This is called during initialization and should contain
 *          any application specific initialization (ie. hardware
 *          initialization/setup, table initialization, power up
 *          notificaiton ... ).
 *
 * @param   task_id - the ID assigned by OSAL.  This ID should be
 *                    used to send messages and set timers.
 *
 * @return  none
 */
void Led_Init( byte task_id )
{
  char i;
  Led_TaskID = task_id;
  ledCnt = 0;
  
  // Device hardware initialization can be added here or in main() (Zmain.c).
  // If the hardware is application specific - add it here.
  // If the hardware is other parts of the device add it in main().

  Led_DstAddr.addrMode = (afAddrMode_t)AddrNotPresent;
  Led_DstAddr.endPoint = 0;
  Led_DstAddr.addr.shortAddr = 0;

    for( i=0; i<LED_NUM_MAX; i++)
    {
        // Fill out the endpoint description.
        Led_epDesc[i].endPoint = LED_ENDPOINT+i;
        Led_epDesc[i].task_id = &Led_TaskID;
        Led_SimpleDesc[i] = Led_SimpleDesc[0];
        Led_epDesc[i].simpleDesc
                                            = (SimpleDescriptionFormat_t *)&(Led_SimpleDesc[i]);
        Led_SimpleDesc[i].EndPoint += i;
        Led_epDesc[i].latencyReq = noLatencyReqs;
        
        // Register the endpoint description with the AF
        afRegister( &(Led_epDesc[i]) );
    }

  led_bindInProgress = 0xffff;

  ZDO_RegisterForZDOMsg( Led_TaskID, Match_Desc_rsp );
}

/*********************************************************************
 * @fn      Led_ProcessEvent
 *
 * @brief   Generic Application Task event processor.  This function
 *          is called to process all events for the task.  Events
 *          include timers, messages and any other user defined events.
 *
 * @param   task_id  - The OSAL assigned task ID.
 * @param   events - events to process.  This is a bit map and can
 *                   contain more than one event.
 *
 * @return  none
 */
UINT16 Led_ProcessEvent( byte task_id, UINT16 events )
{
  afIncomingMSGPacket_t *MSGpkt = NULL;

  (void)task_id;  // Intentionally unreferenced parameter

  if ( events & SYS_EVENT_MSG )
  {
    MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( task_id );
    while ( MSGpkt )
    {
      switch ( MSGpkt->hdr.event )
      {
        case ZDO_CB_MSG:  // 收到被绑定节点的rsp
          Led_ProcessZDOMsgs( (zdoIncomingMsg_t *)MSGpkt);
          break;
      }
      // Release the memory
      osal_msg_deallocate( (uint8 *)MSGpkt );

      // Next
      MSGpkt = (afIncomingMSGPacket_t *)osal_msg_receive( Led_TaskID );
    }
   return (events ^ SYS_EVENT_MSG);
  }

  if( events & MATCH_BIND_EVT ) // 广播 match 绑定
  {
    if(ledCnt == LED_NUM_MAX)
    {
        // do something
    }
    else
    {
        Led_BindDevice(TRUE, LED_ENDPOINT+ledCnt, Led_ClusterList, NULL); 
    }
    return (events ^ MATCH_BIND_EVT);
  }
  
  if ( events & LED_BIND_TIMER )
  {
    // Send bind confirm callback to application
    SAPI_BindConfirm( led_bindInProgress, ZB_TIMEOUT );
    led_bindInProgress = 0xffff;

    return (events ^ LED_BIND_TIMER);
  }
  
  return 0;
}

/*********************************************************************
 * @fn      Led_ProcessZDOMsgs()
 *
 * @brief   Process response messages
 *
 * @param   none
 *
 * @return  none
 */
void Led_ProcessZDOMsgs( zdoIncomingMsg_t *inMsg )
{
  switch ( inMsg->clusterID )
  {
    case Match_Desc_rsp:
      {
        zAddrType_t dstAddr;
        ZDO_ActiveEndpointRsp_t *pRsp = ZDO_ParseEPListRsp( inMsg );

        if ( led_bindInProgress != 0xffff )
        {
          uint8 ret = 0;
          // Create a binding table entry
          dstAddr.addrMode = Addr16Bit;
          dstAddr.addr.shortAddr = pRsp->nwkAddr;

          for(char i=0; i<LED_MAX_CLUSTERS; i++)
          {
                if ( APSME_BindRequest( Led_epDesc[ledCnt].simpleDesc->EndPoint,
                     led_bindInProgress+i, &dstAddr, pRsp->epList[0] ) != ZSuccess )
                {
                    ret = 1;
                    break;
                }
          }
          if(ret == ZSuccess)
          {
            osal_stop_timerEx(Led_TaskID,  LED_BIND_TIMER);
            osal_start_timerEx( ZDAppTaskID, ZDO_NWK_UPDATE_NV, 250 );
            // Find IEEE addr
            ZDP_IEEEAddrReq( pRsp->nwkAddr, ZDP_ADDR_REQTYPE_SINGLE, 0, 0 );
            // Send bind confirm callback to application
            Sys_BindConfirm( led_bindInProgress, ZB_SUCCESS );
            led_bindInProgress = 0xffff;
            ledCnt++;
          }
          else
          {
                // ClusterID wrong
                HalLedSet(HAL_LED_2, HAL_LED_MODE_ON);
                HalLedSet(HAL_LED_3, HAL_LED_MODE_OFF);
          }
        }
      }
    break;
  }
}

/******************************************************************************
 * @fn          Led_BindDevice
 *
 * @brief       The zb_BindDevice function establishes or removes a ��binding? *              between two devices.  Once bound, an application can send
 *              messages to a device by referencing the commandId for the
 *              binding.
 *
 * @param       create - TRUE to create a binding, FALSE to remove a binding
 *              commandId - The identifier of the binding
 *              pDestination - The 64-bit IEEE address of the device to bind to
 *
 * @return      The status of the bind operation is returned in the
 *              Sys_BindConfirm callback.
 */
void Led_BindDevice ( uint8 create, uint8 endpoint, uint16 *commandId, uint8 *pDestination )
{
  zAddrType_t destination;
  uint8 ret = ZB_ALREADY_IN_PROGRESS;

  if ( create )
  {
    if (led_bindInProgress == 0xffff)
    {
      if ( pDestination )
      {
        destination.addrMode = Addr64Bit;
        osal_cpyExtAddr( destination.addr.extAddr, pDestination );
          // srcEndpoint, dstEndpoint
        ret = APSME_BindRequest( endpoint, commandId[0],
                                            &destination, endpoint );

        if ( ret == ZSuccess )
        {
          // Find nwk addr
          ZDP_NwkAddrReq(pDestination, ZDP_ADDR_REQTYPE_SINGLE, 0, 0 );
          osal_start_timerEx( ZDAppTaskID, ZDO_NWK_UPDATE_NV, 250 );
        }
      }
      else
      {
        ret = ZB_INVALID_PARAMETER;
        destination.addrMode = Addr16Bit;
        destination.addr.shortAddr = NWK_BROADCAST_SHORTADDR;
        if ( ZDO_AnyClusterMatches( LED_MAX_CLUSTERS, commandId, Led_epDesc[ledCnt].simpleDesc->AppNumOutClusters,
                                                Led_epDesc[ledCnt].simpleDesc->pAppOutClusterList ) )
        {
          // Try to match with a device in the allow bind mode
          ret = ZDP_MatchDescReq( &destination, NWK_BROADCAST_SHORTADDR,
              Led_epDesc[ledCnt].simpleDesc->AppProfId, LED_MAX_CLUSTERS, commandId, 0, (cId_t *)NULL, 0 );
        }
        else if ( ZDO_AnyClusterMatches( LED_MAX_CLUSTERS, commandId, Led_epDesc[ledCnt].simpleDesc->AppNumInClusters,
                                                Led_epDesc[ledCnt].simpleDesc->pAppInClusterList ) )
        {
          ret = ZDP_MatchDescReq( &destination, NWK_BROADCAST_SHORTADDR,
              Led_epDesc[ledCnt].simpleDesc->AppProfId, 0, (cId_t *)NULL, LED_MAX_CLUSTERS, commandId, 0 );
        }

        if ( ret == ZB_SUCCESS )
        {
          // Set a timer to make sure bind completes
#if ( ZG_BUILD_RTR_TYPE )
          osal_start_timerEx(Led_TaskID, LED_BIND_TIMER, AIB_MaxBindingTime);
#else
          // AIB_MaxBindingTime is not defined for an End Device
          osal_start_timerEx(Led_TaskID, LED_BIND_TIMER, zgApsDefaultMaxBindingTime);
#endif
          led_bindInProgress = commandId[0];
          return; // dont send cback event
        }
      }
    }

  //  SAPI_SendCback( SAPICB_BIND_CNF, ret, commandId );
  }
  else
  {
    // Remove local bindings for the commandId
    BindingEntry_t *pBind;

    // Loop through bindings an remove any that match the cluster
    while ( pBind = bindFind( endpoint, commandId[0], 0 ) )
    {
      bindRemoveEntry(pBind);
    }
    osal_start_timerEx( ZDAppTaskID, ZDO_NWK_UPDATE_NV, 250 );
  }
  return;
}


/*********************************
 * CALLBACK FUNCTIONS   */
/******************************************************************************
 * @fn          SAPI_BindConfirm
 *
 * @brief       The SAPI_BindConfirm callback is called by the ZigBee stack
 *              after a bind operation completes.
 *
 * @param       commandId - The command ID of the binding being confirmed.
 *              status - The status of the bind operation.
 *              allowBind - TRUE if the bind operation was initiated by a call
 *                          to zb_AllowBindRespones.  FALSE if the operation
 *                          was initiated by a call to ZB_BindDevice
 *
 * @return      none
 */
void SAPI_BindConfirm( uint16 commandId, uint8 status )
{
    Sys_BindConfirm( commandId, status );
}