/*++

Copyright (c) Microsoft Corporation

Module Name:

    Filter.c

Abstract:

    Sample NDIS Lightweight filter driver

--*/

#include "precomp.h"

#define __FILENUMBER    'PNPF'

// This directive puts the DriverEntry function into the INIT segment of the
// driver.  To conserve memory, the code will be discarded when the driver's
// DriverEntry function returns.  You can declare other functions used only
// during initialization here.
#pragma NDIS_INIT_FUNCTION(DriverEntry)

static UCHAR MAC0[ETH_LENGTH_OF_ADDRESS];
static UCHAR MAC1[ETH_LENGTH_OF_ADDRESS];
static UCHAR MAC2[ETH_LENGTH_OF_ADDRESS];
static UCHAR MAC3[ETH_LENGTH_OF_ADDRESS];
static ULONG timerPeriod = 3;
static USHORT pauseValue = 0x8000;
static ULONG LengthToDrop = 0;
static ULONG NumberOfProcessors;

//
// Global variables
//
NDIS_HANDLE         FilterDriverHandle; // NDIS handle for filter driver
NDIS_HANDLE         FilterDriverObject;
NDIS_HANDLE         NdisFilterDeviceHandle = NULL;
NDIS_HANDLE         NblPool = NULL;
PDEVICE_OBJECT      NdisDeviceObject = NULL;

FILTER_LOCK         FilterListLock;
LIST_ENTRY          FilterModuleList;


static PNET_BUFFER_LIST AllocatePauseNbl()
{
    if (!NblPool)
        return NULL;
    PNET_BUFFER_LIST nbl = NdisAllocateNetBufferList(NblPool, 0, 0);
    return nbl;
}

static void FreePauseNbl(PNET_BUFFER_LIST Nbl)
{
    NdisFreeNetBufferList(Nbl);
}

static void AllocatePool()
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;

    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.ContextSize = 0;
    PoolParams.PoolTag = 'ppph';
    PoolParams.DataSize = 64;
    NblPool = NdisAllocateNetBufferListPool(NULL, &PoolParams);
}

void ConfigureTimerResolution(BOOLEAN Set)
{
    ExSetTimerResolution(Set ? 10000 : 0, Set);
}

static void QueryConfiguration()
{
    NDIS_CONFIGURATION_OBJECT co;
    NDIS_HANDLE cfg;
    NDIS_STATUS status;
    co.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    co.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    co.Header.Size = NDIS_SIZEOF_CONFIGURATION_OBJECT_REVISION_1;
    co.Flags = 0;
    co.NdisHandle = FilterDriverHandle;
    status = NdisOpenConfigurationEx(&co, &cfg);
    if (status != NDIS_STATUS_SUCCESS)
    {
        DEBUGP(DL_ERROR, "===>Getting configuration...\n");
        return;
    }
    NDIS_CONFIGURATION_PARAMETER *param;
    UNICODE_STRING nameMac0, nameMac1, nameMac2, nameMac3;
    UNICODE_STRING namePeriod, nameValue, nameDebug, nameLengthToDrop;
    
    RtlInitUnicodeString(&nameMac0, L"MAC0");
    RtlInitUnicodeString(&nameMac1, L"MAC1");
    RtlInitUnicodeString(&nameMac2, L"MAC2");
    RtlInitUnicodeString(&nameMac3, L"MAC3");
    RtlInitUnicodeString(&namePeriod, L"TimerPeriod");
    RtlInitUnicodeString(&nameValue, L"Value");
    RtlInitUnicodeString(&nameDebug, L"DebugLevel");
    RtlInitUnicodeString(&nameLengthToDrop, L"DropLength");

    NdisReadConfiguration(&status, &param, cfg, &nameMac0, NdisParameterBinary);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterBinary && param->ParameterData.BinaryData.Length == ETH_LENGTH_OF_ADDRESS)
    {
        UCHAR* a = MAC0;
        ETH_COPY_NETWORK_ADDRESS(a, param->ParameterData.BinaryData.Buffer);
        DEBUGP(DL_INFO, "MAC0 %02x-%02x-%02x-%02x-%02x-%02x\n",
            a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    NdisReadConfiguration(&status, &param, cfg, &nameMac1, NdisParameterBinary);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterBinary && param->ParameterData.BinaryData.Length == ETH_LENGTH_OF_ADDRESS)
    {
        UCHAR* a = MAC1;
        ETH_COPY_NETWORK_ADDRESS(a, param->ParameterData.BinaryData.Buffer);
        DEBUGP(DL_INFO, "MAC1 %02x-%02x-%02x-%02x-%02x-%02x\n",
            a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    NdisReadConfiguration(&status, &param, cfg, &nameMac2, NdisParameterBinary);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterBinary && param->ParameterData.BinaryData.Length == ETH_LENGTH_OF_ADDRESS)
    {
        UCHAR* a = MAC2;
        ETH_COPY_NETWORK_ADDRESS(a, param->ParameterData.BinaryData.Buffer);
        DEBUGP(DL_INFO, "MAC2 %02x-%02x-%02x-%02x-%02x-%02x\n",
            a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    NdisReadConfiguration(&status, &param, cfg, &nameMac3, NdisParameterBinary);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterBinary && param->ParameterData.BinaryData.Length == ETH_LENGTH_OF_ADDRESS)
    {
        UCHAR* a = MAC3;
        ETH_COPY_NETWORK_ADDRESS(a, param->ParameterData.BinaryData.Buffer);
        DEBUGP(DL_INFO, "MAC3 %02x-%02x-%02x-%02x-%02x-%02x\n",
            a[0], a[1], a[2], a[3], a[4], a[5]);
    }
    NdisReadConfiguration(&status, &param, cfg, &namePeriod, NdisParameterInteger);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterInteger)
    {
        timerPeriod = param->ParameterData.IntegerData;
        DEBUGP(DL_INFO, "Using timerPeriod = %d\n", timerPeriod);
    }
    NdisReadConfiguration(&status, &param, cfg, &nameValue, NdisParameterInteger);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterInteger)
    {
        param->ParameterData.IntegerData = min(param->ParameterData.IntegerData, 0xffff);
        pauseValue = (USHORT)param->ParameterData.IntegerData;
        DEBUGP(DL_INFO, "Using pause Value = %d\n", pauseValue);
    }
    NdisReadConfiguration(&status, &param, cfg, &nameDebug, NdisParameterInteger);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterInteger)
    {
        filterDebugLevel = param->ParameterData.IntegerData;
        DEBUGP(DL_ERROR, "Using debug level = %d\n", filterDebugLevel);
    }
    NdisReadConfiguration(&status, &param, cfg, &nameLengthToDrop, NdisParameterInteger);
    if (NT_SUCCESS(status) && param->ParameterType == NdisParameterInteger)
    {
        LengthToDrop = param->ParameterData.IntegerData;
        DEBUGP(DL_ERROR, "Using LengthToDrop = %d\n", LengthToDrop);
    }

    NdisCloseConfiguration(cfg);
}

static BOOLEAN CheckMac(const UCHAR* a)
{
    if (RtlCompareMemory(a, MAC0, ETH_LENGTH_OF_ADDRESS) == ETH_LENGTH_OF_ADDRESS)
        return TRUE;
    if (RtlCompareMemory(a, MAC1, ETH_LENGTH_OF_ADDRESS) == ETH_LENGTH_OF_ADDRESS)
        return TRUE;
    if (RtlCompareMemory(a, MAC2, ETH_LENGTH_OF_ADDRESS) == ETH_LENGTH_OF_ADDRESS)
        return TRUE;
    if (RtlCompareMemory(a, MAC3, ETH_LENGTH_OF_ADDRESS) == ETH_LENGTH_OF_ADDRESS)
        return TRUE;
    return FALSE;
}


NDIS_FILTER_PARTIAL_CHARACTERISTICS DefaultChars = {
{ 0, 0, 0},
      0,
      FilterSendNetBufferLists,
      FilterSendNetBufferListsComplete,
      NULL,
      FilterReceiveNetBufferLists,
      FilterReturnNetBufferLists
};

_Use_decl_annotations_
NTSTATUS
DriverEntry(
    PDRIVER_OBJECT      DriverObject,
    PUNICODE_STRING     RegistryPath
    )
/*++

Routine Description:

    First entry point to be called, when this driver is loaded.
    Register with NDIS as a filter driver and create a device
    for communication with user-mode.

Arguments:

    DriverObject - pointer to the system's driver object structure
                   for this driver

    RegistryPath - system's registry path for this driver

Return Value:

    STATUS_SUCCESS if all initialization is successful, STATUS_XXX
    error code if not.

--*/
{
    NDIS_STATUS Status;
    NDIS_FILTER_DRIVER_CHARACTERISTICS      FChars;
NDIS_STRING ServiceName = RTL_CONSTANT_STRING(FILTER_SERVICE_NAME);
NDIS_STRING UniqueName = RTL_CONSTANT_STRING(FILTER_UNIQUE_NAME);
NDIS_STRING FriendlyName = RTL_CONSTANT_STRING(FILTER_FRIENDLY_NAME);
BOOLEAN bFalse = FALSE;

UNREFERENCED_PARAMETER(RegistryPath);

DEBUGP(DL_TRACE, "===>DriverEntry...\n");

NumberOfProcessors = KeQueryMaximumProcessorCount();
FilterDriverObject = DriverObject;

do
{
    NdisZeroMemory(&FChars, sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS));
    FChars.Header.Type = NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS;
    FChars.Header.Size = sizeof(NDIS_FILTER_DRIVER_CHARACTERISTICS);
#if NDIS_SUPPORT_NDIS61
    FChars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_2;
#else
    FChars.Header.Revision = NDIS_FILTER_CHARACTERISTICS_REVISION_1;
#endif

    FChars.MajorNdisVersion = FILTER_MAJOR_NDIS_VERSION;
    FChars.MinorNdisVersion = FILTER_MINOR_NDIS_VERSION;
    FChars.MajorDriverVersion = 1;
    FChars.MinorDriverVersion = 0;
    FChars.Flags = 0;

    FChars.FriendlyName = FriendlyName;
    FChars.UniqueName = UniqueName;
    FChars.ServiceName = ServiceName;

    //
    // TODO: Most handlers are optional, however, this sample includes them
    // all for illustrative purposes.  If you do not need a particular 
    // handler, set it to NULL and NDIS will more efficiently pass the
    // operation through on your behalf.
    //
    FChars.SetOptionsHandler = FilterRegisterOptions;
    FChars.AttachHandler = FilterAttach;
    FChars.DetachHandler = FilterDetach;
    FChars.RestartHandler = FilterRestart;
    FChars.PauseHandler = FilterPause;
    FChars.SetFilterModuleOptionsHandler = FilterSetModuleOptions;
    //FChars.OidRequestHandler = FilterOidRequest;
    //FChars.OidRequestCompleteHandler = FilterOidRequestComplete;
    //FChars.CancelOidRequestHandler = FilterCancelOidRequest;

    //FChars.SendNetBufferListsHandler = FilterSendNetBufferLists;
    FChars.ReturnNetBufferListsHandler = FilterReturnNetBufferLists;
    FChars.SendNetBufferListsCompleteHandler = FilterSendNetBufferListsComplete;
    FChars.ReceiveNetBufferListsHandler = FilterReceiveNetBufferLists;
    FChars.DevicePnPEventNotifyHandler = FilterDevicePnPEventNotify;
    //FChars.NetPnPEventHandler = FilterNetPnPEvent;
    FChars.StatusHandler = FilterStatus;
    //FChars.CancelSendNetBufferListsHandler = FilterCancelSendNetBufferLists;

    DriverObject->DriverUnload = FilterUnload;

    FilterDriverHandle = NULL;

    //
    // Initialize spin locks
    //
    FILTER_INIT_LOCK(&FilterListLock);

    InitializeListHead(&FilterModuleList);

    Status = NdisFRegisterFilterDriver(DriverObject,
        (NDIS_HANDLE)FilterDriverObject,
        &FChars,
        &FilterDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DEBUGP(DL_WARN, "Register filter driver failed.\n");
        break;
    }

    Status = FilterRegisterDevice();

    if (Status != NDIS_STATUS_SUCCESS)
    {
        NdisFDeregisterFilterDriver(FilterDriverHandle);
        FILTER_FREE_LOCK(&FilterListLock);
        DEBUGP(DL_WARN, "Register device for the filter driver failed.\n");
        break;
    }
    else
    {
        AllocatePool();
        ConfigureTimerResolution(TRUE);
    }
} while (bFalse);


DEBUGP(DL_TRACE, "%d processors\n", NumberOfProcessors);
DEBUGP(DL_TRACE, "<===DriverEntry, Status = %8x\n", Status);

return Status;

}

void FilterSendPauseFrame(PMS_FILTER pFilter)
{
    PNET_BUFFER_LIST nbl = AllocatePauseNbl();
    if (!nbl)
        return;
    NDIS_STATUS status = NdisRetreatNetBufferListDataStart(nbl, 64, 0, NULL, NULL);
    if (!NT_SUCCESS(status))
    {
        FreePauseNbl(nbl);
        return;
    }
    PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(nbl);
    while (nb)
    {
        DEBUGP(DL_LOUD, "NB of %d:\n", NET_BUFFER_DATA_LENGTH(nb));
        PMDL mdl = NET_BUFFER_CURRENT_MDL(nb);
        if (mdl)
        {
            PVOID ps = MmGetSystemAddressForMdlSafe(mdl, NormalPagePriority);
            PVOID pv = MmGetMdlVirtualAddress(mdl);
            ULONG count = MmGetMdlByteCount(mdl);
            DEBUGP(DL_LOUD, "MDL at %p:%p, count %d\n", ps, pv, count);
            if (count == 64)
            {
                RtlZeroMemory(pv, 64);
                RtlCopyMemory(pv, pFilter->PauseData, sizeof(pFilter->PauseData));
                NdisFSendNetBufferLists(pFilter->FilterHandle, nbl, 0, 0);
                break;
            }
        }
        nb = NET_BUFFER_NEXT_NB(nb);
    }
}

_Use_decl_annotations_
NDIS_STATUS
FilterRegisterOptions(
    NDIS_HANDLE  NdisFilterDriverHandle,
    NDIS_HANDLE  FilterDriverContext
    )
/*++

Routine Description:

    Register optional handlers with NDIS.  This sample does not happen to
    have any optional handlers to register, so this routine does nothing
    and could simply have been omitted.  However, for illustrative purposes,
    it is presented here.

Arguments:

    NdisFilterDriverHandle - pointer the driver handle received from
                             NdisFRegisterFilterDriver

    FilterDriverContext    - pointer to our context passed into
                             NdisFRegisterFilterDriver

Return Value:

    NDIS_STATUS_SUCCESS

--*/
{
    DEBUGP(DL_TRACE, "===>FilterRegisterOptions\n");

    ASSERT(NdisFilterDriverHandle == FilterDriverHandle);
    ASSERT(FilterDriverContext == (NDIS_HANDLE)FilterDriverObject);

    if ((NdisFilterDriverHandle != (NDIS_HANDLE)FilterDriverHandle) ||
        (FilterDriverContext != (NDIS_HANDLE)FilterDriverObject))
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    DEBUGP(DL_TRACE, "<===FilterRegisterOptions\n");

    return NDIS_STATUS_SUCCESS;
}

void FilterTimerProc(
    PVOID SystemSpecific1,
    PVOID FunctionContext,
    PVOID SystemSpecific2,
    PVOID SystemSpecific3
)
{
    PMS_FILTER pFilter = (PMS_FILTER)FunctionContext;
    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);
    UNREFERENCED_PARAMETER(pFilter);
    DEBUGP(DL_LOUD, "===>%s\n", __FUNCTION__);
    FilterSendPauseFrame(pFilter);
    pFilter->TickCounter++;
    if ((pFilter->TickCounter % 1024) == 0)
    {
        DEBUGP(DL_INFO, "%s: Another 1024 tick\n", __FUNCTION__);
    }
    else if (timerPeriod >= 5000)
    {
        DEBUGP(DL_INFO, "%s: Tick, dropped %d\n", __FUNCTION__, pFilter->DroppedPackets);
        if (pFilter->DroppedPackets != pFilter->LastDroppedPackets)
        {
            pFilter->LastDroppedPackets = pFilter->DroppedPackets;
            pFilter->NoTransferCounter = 0;
        }
        else if (pFilter->DroppedPackets)
        {
            pFilter->NoTransferCounter++;
            if (pFilter->NoTransferCounter > 2)
            {
                pFilter->DroppedPackets = pFilter->LastDroppedPackets = 0;
                pFilter->NoTransferCounter = 0;
                DEBUGP(DL_INFO, "%s: was %d indications\n", __FUNCTION__, pFilter->NumIndication);
            }
        }
    }
}

void FilterUpdateTimer(PMS_FILTER pFilter)
{
    BOOLEAN bStart = timerPeriod && pFilter->LinkUp && pFilter->State == FilterRunning;
    if (bStart)
    {
        LARGE_INTEGER due;
        due.QuadPart = (LONGLONG)(-10000);
        due.QuadPart *= timerPeriod;
        NdisSetTimerObject(pFilter->TimerObject, due, timerPeriod, pFilter);
    }
    else
    {
        NdisCancelTimerObject(pFilter->TimerObject);
    }
}

static void FilterDpcProc(
    KDPC* Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2
)
{
    PMS_FILTER pFilter = (PMS_FILTER)DeferredContext;
    PNET_BUFFER_LIST Nbl = (PNET_BUFFER_LIST)SystemArgument1;
    ULONG nof = (ULONG)(ULONG_PTR)SystemArgument2;
    FILTER_DPC *Fdpc = (FILTER_DPC*)Dpc;
    NdisFIndicateReceiveNetBufferLists(
        pFilter->FilterHandle,
        Nbl,
        0,
        nof,
        NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
    InterlockedDecrement(&Fdpc->Allocated);
}

_Use_decl_annotations_
NDIS_STATUS
FilterAttach(
    NDIS_HANDLE                     NdisFilterHandle,
    NDIS_HANDLE                     FilterDriverContext,
    PNDIS_FILTER_ATTACH_PARAMETERS  AttachParameters
    )
/*++

Routine Description:

    Filter attach routine.
    Create filter's context, allocate NetBufferLists and NetBuffer pools and any
    other resources, and read configuration if needed.

Arguments:

    NdisFilterHandle - Specify a handle identifying this instance of the filter. FilterAttach
                       should save this handle. It is a required  parameter in subsequent calls
                       to NdisFxxx functions.
    FilterDriverContext - Filter driver context passed to NdisFRegisterFilterDriver.

    AttachParameters - attach parameters

Return Value:

    NDIS_STATUS_SUCCESS: FilterAttach successfully allocated and initialize data structures
                         for this filter instance.
    NDIS_STATUS_RESOURCES: FilterAttach failed due to insufficient resources.
    NDIS_STATUS_FAILURE: FilterAttach could not set up this instance of this filter and it has called
                         NdisWriteErrorLogEntry with parameters specifying the reason for failure.

N.B.:  FILTER can use NdisRegisterDeviceEx to create a device, so the upper 
    layer can send Irps to the filter.

--*/
{
    PMS_FILTER              pFilter = NULL;
    NDIS_STATUS             Status = NDIS_STATUS_SUCCESS;
    NDIS_FILTER_ATTRIBUTES  FilterAttributes;
    ULONG                   Size;
    BOOLEAN               bFalse = FALSE;
    UINT i;

    QueryConfiguration();

    DEBUGP(DL_TRACE, "===>FilterAttach: NdisFilterHandle %p\n", NdisFilterHandle);

    do
    {
        ASSERT(FilterDriverContext == (NDIS_HANDLE)FilterDriverObject);
        if (FilterDriverContext != (NDIS_HANDLE)FilterDriverObject)
        {
            Status = NDIS_STATUS_INVALID_PARAMETER;
            break;
        }

        // Verify the media type is supported.  This is a last resort; the
        // the filter should never have been bound to an unsupported miniport
        // to begin with.  If this driver is marked as a Mandatory filter (which
        // is the default for this sample; see the INF file), failing to attach 
        // here will leave the network adapter in an unusable state.
        //
        // Your setup/install code should not bind the filter to unsupported
        // media types.
        if (AttachParameters->MiniportMediaType != NdisMedium802_3)
//                && (AttachParameters->MiniportMediaType != NdisMediumWan)
//                && (AttachParameters->MiniportMediaType != NdisMediumWirelessWan))
        {
           DEBUGP(DL_ERROR, "Unsupported media type.\n");

           Status = NDIS_STATUS_INVALID_PARAMETER;
           break;
        }
        if (!CheckMac(AttachParameters->CurrentMacAddress))
        {
            const UCHAR* a = AttachParameters->CurrentMacAddress;
            DEBUGP(DL_ERROR, "MAC %02x-%02x-%02x-%02x-%02x-%02x is not configured for filtering\n",
                a[0], a[1], a[2], a[3], a[4], a[5]);

            Status = NDIS_STATUS_INVALID_PARAMETER;
            break;
        }

        Size = sizeof(MS_FILTER) +
               AttachParameters->FilterModuleGuidName->Length +
               AttachParameters->BaseMiniportInstanceName->Length +
               AttachParameters->BaseMiniportName->Length;

        pFilter = (PMS_FILTER)FILTER_ALLOC_MEM(NdisFilterHandle, Size);
        if (pFilter == NULL)
        {
            DEBUGP(DL_WARN, "Failed to allocate context structure.\n");
            Status = NDIS_STATUS_RESOURCES;
            break;
        }

        NdisZeroMemory(pFilter, sizeof(MS_FILTER));

        pFilter->FilterModuleName.Length = pFilter->FilterModuleName.MaximumLength = AttachParameters->FilterModuleGuidName->Length;
        pFilter->FilterModuleName.Buffer = (PWSTR)((PUCHAR)pFilter + sizeof(MS_FILTER));
        NdisMoveMemory(pFilter->FilterModuleName.Buffer,
                        AttachParameters->FilterModuleGuidName->Buffer,
                        pFilter->FilterModuleName.Length);



        pFilter->MiniportFriendlyName.Length = pFilter->MiniportFriendlyName.MaximumLength = AttachParameters->BaseMiniportInstanceName->Length;
        pFilter->MiniportFriendlyName.Buffer = (PWSTR)((PUCHAR)pFilter->FilterModuleName.Buffer + pFilter->FilterModuleName.Length);
        NdisMoveMemory(pFilter->MiniportFriendlyName.Buffer,
                        AttachParameters->BaseMiniportInstanceName->Buffer,
                        pFilter->MiniportFriendlyName.Length);


        pFilter->MiniportName.Length = pFilter->MiniportName.MaximumLength = AttachParameters->BaseMiniportName->Length;
        pFilter->MiniportName.Buffer = (PWSTR)((PUCHAR)pFilter->MiniportFriendlyName.Buffer +
                                                   pFilter->MiniportFriendlyName.Length);
        NdisMoveMemory(pFilter->MiniportName.Buffer,
                        AttachParameters->BaseMiniportName->Buffer,
                        pFilter->MiniportName.Length);

        pFilter->MiniportIfIndex = AttachParameters->BaseMiniportIfIndex;
        //
        // The filter should initialize TrackReceives and TrackSends properly. For this
        // driver, since its default characteristic has both a send and a receive handler,
        // these fields are initialized to TRUE.
        //
        pFilter->TrackReceives = FALSE;
        pFilter->TrackSends = FALSE;
        pFilter->FilterHandle = NdisFilterHandle;


        NdisZeroMemory(&FilterAttributes, sizeof(NDIS_FILTER_ATTRIBUTES));
        FilterAttributes.Header.Revision = NDIS_FILTER_ATTRIBUTES_REVISION_1;
        FilterAttributes.Header.Size = sizeof(NDIS_FILTER_ATTRIBUTES);
        FilterAttributes.Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES;
        FilterAttributes.Flags = 0;

        NDIS_DECLARE_FILTER_MODULE_CONTEXT(MS_FILTER);
        Status = NdisFSetAttributes(NdisFilterHandle,
                                    pFilter,
                                    &FilterAttributes);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP(DL_WARN, "Failed to set attributes.\n");
            break;
        }

        NDIS_TIMER_CHARACTERISTICS TimerChars;
        TimerChars.Header.Type = NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS;
        TimerChars.Header.Revision = NDIS_TIMER_CHARACTERISTICS_REVISION_1;
        TimerChars.Header.Size = NDIS_SIZEOF_TIMER_CHARACTERISTICS_REVISION_1;
        TimerChars.AllocationTag = 'ppph';
        TimerChars.TimerFunction = FilterTimerProc;
        TimerChars.FunctionContext = pFilter;

        Status = NdisAllocateTimerObject(NdisFilterHandle, &TimerChars, &pFilter->TimerObject);

        if (Status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP(DL_WARN, "Failed to set up the timer.\n");
            break;
        }

        const UCHAR MilticastAddress[ETH_LENGTH_OF_ADDRESS] = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x01 };
        ETH_COPY_NETWORK_ADDRESS(pFilter->PauseData, MilticastAddress);
        ETH_COPY_NETWORK_ADDRESS(pFilter->PauseData + ETH_LENGTH_OF_ADDRESS, AttachParameters->CurrentMacAddress);
        pFilter->PauseData[12] = 0x88;
        pFilter->PauseData[13] = 0x08;
        pFilter->PauseData[14] = 0x00;
        pFilter->PauseData[15] = 0x01;
        pFilter->PauseData[16] = pauseValue >> 8; 
        pFilter->PauseData[17] = pauseValue & 0xff;

        for (i = 0; i < RTL_NUMBER_OF(pFilter->DpcArray); ++i)
        {
            KeInitializeDpc(&pFilter->DpcArray[i].Dpc, FilterDpcProc, pFilter);
        }

        pFilter->State = FilterPaused;

        FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
        InsertHeadList(&FilterModuleList, &pFilter->FilterModuleLink);
        FILTER_RELEASE_LOCK(&FilterListLock, bFalse);

    }
    while (bFalse);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        if (pFilter != NULL)
        {
            FILTER_FREE_MEM(pFilter);
        }
    }

    DEBUGP(DL_TRACE, "<===FilterAttach:    Status %x\n", Status);
    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
FilterPause(
    NDIS_HANDLE                     FilterModuleContext,
    PNDIS_FILTER_PAUSE_PARAMETERS   PauseParameters
    )
/*++

Routine Description:

    Filter pause routine.
    Complete all the outstanding sends and queued sends,
    wait for all the outstanding recvs to be returned
    and return all the queued receives.

Arguments:

    FilterModuleContext - pointer to the filter context stucture
    PauseParameters     - additional information about the pause

Return Value:

    NDIS_STATUS_SUCCESS if filter pauses successfully, NDIS_STATUS_PENDING
    if not.  No other return value is allowed (pause must succeed, eventually).

N.B.: When the filter is in Pausing state, it can still process OID requests, 
    complete sending, and returning packets to NDIS, and also indicate status.
    After this function completes, the filter must not attempt to send or 
    receive packets, but it may still process OID requests and status 
    indications.

--*/
{
    PMS_FILTER          pFilter = (PMS_FILTER)(FilterModuleContext);
    NDIS_STATUS         Status;
    BOOLEAN               bFalse = FALSE;

    UNREFERENCED_PARAMETER(PauseParameters);

    DEBUGP(DL_TRACE, "===>NDISLWF FilterPause: FilterInstance %p\n", FilterModuleContext);

    //
    // Set the flag that the filter is going to pause
    //
    FILTER_ASSERT(pFilter->State == FilterRunning);

    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);
    pFilter->State = FilterPausing;
    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);

    //
    // Do whatever work is required to bring the filter into the Paused state.
    //
    // If you have diverted and queued any send or receive NBLs, return them 
    // now.
    //
    // If you send or receive original NBLs, stop doing that and wait for your
    // NBLs to return to you now.
    //


    Status = NDIS_STATUS_SUCCESS;

    pFilter->State = FilterPaused;

    FilterUpdateTimer(pFilter);

    DEBUGP(DL_TRACE, "<===FilterPause:  Status %x\n", Status);
    return Status;
}

_Use_decl_annotations_
NDIS_STATUS
FilterRestart(
    NDIS_HANDLE                     FilterModuleContext,
    PNDIS_FILTER_RESTART_PARAMETERS RestartParameters
    )
/*++

Routine Description:

    Filter restart routine.
    Start the datapath - begin sending and receiving NBLs.

Arguments:

    FilterModuleContext - pointer to the filter context stucture.
    RestartParameters   - additional information about the restart operation.

Return Value:

    NDIS_STATUS_SUCCESS: if filter restarts successfully
    NDIS_STATUS_XXX: Otherwise.

--*/
{
    NDIS_STATUS     Status;
    PMS_FILTER      pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_HANDLE     ConfigurationHandle = NULL;


    PNDIS_RESTART_GENERAL_ATTRIBUTES NdisGeneralAttributes;
    PNDIS_RESTART_ATTRIBUTES         NdisRestartAttributes;
    NDIS_CONFIGURATION_OBJECT        ConfigObject;

    DEBUGP(DL_TRACE, "===>FilterRestart:   FilterModuleContext %p\n", FilterModuleContext);

    FILTER_ASSERT(pFilter->State == FilterPaused);

    ConfigObject.Header.Type = NDIS_OBJECT_TYPE_CONFIGURATION_OBJECT;
    ConfigObject.Header.Revision = NDIS_CONFIGURATION_OBJECT_REVISION_1;
    ConfigObject.Header.Size = sizeof(NDIS_CONFIGURATION_OBJECT);
    ConfigObject.NdisHandle = FilterDriverHandle;
    ConfigObject.Flags = 0;

    Status = NdisOpenConfigurationEx(&ConfigObject, &ConfigurationHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        //
        // Filter driver can choose to fail the restart if it cannot open the configuration
        //

#if 0
        //
        // The code is here just to demonstrate how to call NDIS to write an 
        // event to the eventlog.
        //
        PWCHAR              ErrorString = L"Ndislwf";

        DEBUGP(DL_WARN, "FilterRestart: Cannot open configuration.\n");
        NdisWriteEventLogEntry(FilterDriverObject,
                                EVENT_NDIS_DRIVER_FAILURE,
                                0,
                                1,
                                &ErrorString,
                                sizeof(Status),
                                &Status);
#endif

    }

    //
    // This sample doesn't actually do anything with the configuration handle;
    // it is opened here for illustrative purposes.  If you do not need to
    // read configuration, you may omit the code manipulating the 
    // ConfigurationHandle.
    //

    if (Status == NDIS_STATUS_SUCCESS)
    {
        NdisCloseConfiguration(ConfigurationHandle);
    }

    NdisRestartAttributes = RestartParameters->RestartAttributes;

    //
    // If NdisRestartAttributes is not NULL, then the filter can modify generic 
    // attributes and add new media specific info attributes at the end. 
    // Otherwise, if NdisRestartAttributes is NULL, the filter should not try to 
    // modify/add attributes.
    //
    if (NdisRestartAttributes != NULL)
    {
        PNDIS_RESTART_ATTRIBUTES   NextAttributes;

        ASSERT(NdisRestartAttributes->Oid == OID_GEN_MINIPORT_RESTART_ATTRIBUTES);

        NdisGeneralAttributes = (PNDIS_RESTART_GENERAL_ATTRIBUTES)NdisRestartAttributes->Data;

        //
        // Check to see if we need to change any attributes. For example, the
        // driver can change the current MAC address here. Or the driver can add
        // media specific info attributes.
        //
        NdisGeneralAttributes->LookaheadSize = 128;

        //
        // Check each attribute to see whether the filter needs to modify it.
        //
        NextAttributes = NdisRestartAttributes->Next;

        while (NextAttributes != NULL)
        {
            //
            // If somehow the filter needs to change a attributes which requires more space then
            // the current attributes:
            // 1. Remove the attribute from the Attributes list:
            //    TempAttributes = NextAttributes;
            //    NextAttributes = NextAttributes->Next;
            // 2. Free the memory for the current attributes: NdisFreeMemory(TempAttributes, 0 , 0);
            // 3. Dynamically allocate the memory for the new attributes by calling
            //    NdisAllocateMemoryWithTagPriority:
            //    NewAttributes = NdisAllocateMemoryWithTagPriority(Handle, size, Priority);
            // 4. Fill in the new attribute
            // 5. NewAttributes->Next = NextAttributes;
            // 6. NextAttributes = NewAttributes; // Just to make the next statement work.
            //
            NextAttributes = NextAttributes->Next;
        }

        //
        // Add a new attributes at the end
        // 1. Dynamically allocate the memory for the new attributes by calling
        //    NdisAllocateMemoryWithTagPriority.
        // 2. Fill in the new attribute
        // 3. NextAttributes->Next = NewAttributes;
        // 4. NewAttributes->Next = NULL;



    }

    //
    // If everything is OK, set the filter in running state.
    //
    pFilter->State = FilterRunning; // when successful


    Status = NDIS_STATUS_SUCCESS;


    //
    // Ensure the state is Paused if restart failed.
    //

    if (Status != NDIS_STATUS_SUCCESS)
    {
        pFilter->State = FilterPaused;
    }
    else
    {
        FilterUpdateTimer(pFilter);
    }


    DEBUGP(DL_TRACE, "<===FilterRestart:  FilterModuleContext %p, Status %x\n", FilterModuleContext, Status);
    return Status;
}


_Use_decl_annotations_
VOID
FilterDetach(
    NDIS_HANDLE     FilterModuleContext
    )
/*++

Routine Description:

    Filter detach routine.
    This is a required function that will deallocate all the resources allocated during
    FilterAttach. NDIS calls FilterAttach to remove a filter instance from a filter stack.

Arguments:

    FilterModuleContext - pointer to the filter context area.

Return Value:
    None.

NOTE: Called at PASSIVE_LEVEL and the filter is in paused state

--*/
{
    PMS_FILTER                  pFilter = (PMS_FILTER)FilterModuleContext;
    BOOLEAN                      bFalse = FALSE;


    DEBUGP(DL_TRACE, "===>FilterDetach:    FilterInstance %p\n", FilterModuleContext);


    //
    // Filter must be in paused state
    //
    FILTER_ASSERT(pFilter->State == FilterPaused);


    //
    // Detach must not fail, so do not put any code here that can possibly fail.
    //

    //
    // Free filter instance name if allocated.
    //
    if (pFilter->FilterName.Buffer != NULL)
    {
        FILTER_FREE_MEM(pFilter->FilterName.Buffer);
    }


    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    RemoveEntryList(&pFilter->FilterModuleLink);
    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);

    FilterUpdateTimer(pFilter);
    NdisMSleep(100000);
    FilterUpdateTimer(pFilter);
    NdisFreeTimerObject(pFilter->TimerObject);
    
    //
    // Free the memory allocated
    FILTER_FREE_MEM(pFilter);

    DEBUGP(DL_TRACE, "<===FilterDetach Successfully\n");
    return;
}

_Use_decl_annotations_
VOID
FilterUnload(
    PDRIVER_OBJECT      DriverObject
    )
/*++

Routine Description:

    Filter driver's unload routine.
    Deregister the driver from NDIS.

Arguments:

    DriverObject - pointer to the system's driver object structure
                   for this driver

Return Value:

    NONE

--*/
{
#if DBG
    BOOLEAN               bFalse = FALSE;
#endif

    UNREFERENCED_PARAMETER(DriverObject);

    DEBUGP(DL_TRACE, "===>FilterUnload\n");

    //
    // Should free the filter context list
    //
    FilterDeregisterDevice();

    NdisFDeregisterFilterDriver(FilterDriverHandle);
    DEBUGP(DL_TRACE, "Deleting the NBL pool...\n");
    if (NblPool)
    {
        NdisFreeNetBufferListPool(NblPool);
    }
    ConfigureTimerResolution(FALSE);

#if DBG
    FILTER_ACQUIRE_LOCK(&FilterListLock, bFalse);
    ASSERT(IsListEmpty(&FilterModuleList));

    FILTER_RELEASE_LOCK(&FilterListLock, bFalse);

#endif

    FILTER_FREE_LOCK(&FilterListLock);

    DEBUGP(DL_TRACE, "<===FilterUnload\n");

    return;

}

_Use_decl_annotations_
NDIS_STATUS
FilterOidRequest(
    NDIS_HANDLE         FilterModuleContext,
    PNDIS_OID_REQUEST   Request
    )
/*++

Routine Description:

    Request handler
    Handle requests from upper layers

Arguments:

    FilterModuleContext   - our filter
    Request               - the request passed down


Return Value:

     NDIS_STATUS_SUCCESS
     NDIS_STATUS_PENDING
     NDIS_STATUS_XXX

NOTE: Called at <= DISPATCH_LEVEL  (unlike a miniport's MiniportOidRequest)

--*/
{
    PMS_FILTER              pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_STATUS             Status;
    PNDIS_OID_REQUEST       ClonedRequest=NULL;
    BOOLEAN                 bSubmitted = FALSE;
    PFILTER_REQUEST_CONTEXT Context;
    BOOLEAN                 bFalse = FALSE;


    DEBUGP(DL_TRACE, "===>FilterOidRequest: Request %p.\n", Request);

    //
    // Most of the time, a filter will clone the OID request and pass down
    // the clone.  When the clone completes, the filter completes the original
    // OID request.
    //
    // If your filter needs to modify a specific request, it can modify the
    // request before or after sending down the cloned request.  Or, it can
    // complete the original request on its own without sending down any
    // clone at all.
    //
    // If your filter driver does not need to modify any OID requests, then
    // you may simply omit this routine entirely; NDIS will pass OID requests
    // down on your behalf.  This is more efficient than implementing a 
    // routine that does nothing but clone all requests, as in the sample here.
    //

    do
    {
        Status = NdisAllocateCloneOidRequest(pFilter->FilterHandle,
                                            Request,
                                            FILTER_TAG,
                                            &ClonedRequest);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DEBUGP(DL_WARN, "FilerOidRequest: Cannot Clone Request\n");
            break;
        }

        Context = (PFILTER_REQUEST_CONTEXT)(&ClonedRequest->SourceReserved[0]);
        *Context = Request;

        bSubmitted = TRUE;

        //
        // Use same request ID
        //
        ClonedRequest->RequestId = Request->RequestId;

        pFilter->PendingOidRequest = ClonedRequest;


        Status = NdisFOidRequest(pFilter->FilterHandle, ClonedRequest);

        if (Status != NDIS_STATUS_PENDING)
        {


            FilterOidRequestComplete(pFilter, ClonedRequest, Status);
            Status = NDIS_STATUS_PENDING;
        }


    }while (bFalse);

    if (bSubmitted == FALSE)
    {
        switch(Request->RequestType)
        {
            case NdisRequestMethod:
                Request->DATA.METHOD_INFORMATION.BytesRead = 0;
                Request->DATA.METHOD_INFORMATION.BytesNeeded = 0;
                Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
                break;

            case NdisRequestSetInformation:
                Request->DATA.SET_INFORMATION.BytesRead = 0;
                Request->DATA.SET_INFORMATION.BytesNeeded = 0;
                break;

            case NdisRequestQueryInformation:
            case NdisRequestQueryStatistics:
            default:
                Request->DATA.QUERY_INFORMATION.BytesWritten = 0;
                Request->DATA.QUERY_INFORMATION.BytesNeeded = 0;
                break;
        }

    }
    DEBUGP(DL_TRACE, "<===FilterOidRequest: Status %8x.\n", Status);

    return Status;

}

_Use_decl_annotations_
VOID
FilterCancelOidRequest(
    NDIS_HANDLE             FilterModuleContext,
    PVOID                   RequestId
    )
/*++

Routine Description:

    Cancels an OID request

    If your filter driver does not intercept and hold onto any OID requests,
    then you do not need to implement this routine.  You may simply omit it.
    Furthermore, if the filter only holds onto OID requests so it can pass
    down a clone (the most common case) the filter does not need to implement 
    this routine; NDIS will then automatically request that the lower-level 
    filter/miniport cancel your cloned OID.

    Most filters do not need to implement this routine.

Arguments:

    FilterModuleContext   - our filter
    RequestId             - identifies the request(s) to cancel

--*/
{
    PMS_FILTER                          pFilter = (PMS_FILTER)FilterModuleContext;
    PNDIS_OID_REQUEST                   Request = NULL;
    PFILTER_REQUEST_CONTEXT             Context;
    PNDIS_OID_REQUEST                   OriginalRequest = NULL;
    BOOLEAN                             bFalse = FALSE;

    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);

    Request = pFilter->PendingOidRequest;

    if (Request != NULL)
    {
        Context = (PFILTER_REQUEST_CONTEXT)(&Request->SourceReserved[0]);

        OriginalRequest = (*Context);
    }

    if ((OriginalRequest != NULL) && (OriginalRequest->RequestId == RequestId))
    {
        FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);

        NdisFCancelOidRequest(pFilter->FilterHandle, RequestId);
    }
    else
    {
        FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);
    }


}

_Use_decl_annotations_
VOID
FilterOidRequestComplete(
    NDIS_HANDLE         FilterModuleContext,
    PNDIS_OID_REQUEST   Request,
    NDIS_STATUS         Status
    )
/*++

Routine Description:

    Notification that an OID request has been completed

    If this filter sends a request down to a lower layer, and the request is
    pended, the FilterOidRequestComplete routine is invoked when the request
    is complete.  Most requests we've sent are simply clones of requests
    received from a higher layer; all we need to do is complete the original
    higher request.

    However, if this filter driver sends original requests down, it must not
    attempt to complete a pending request to the higher layer.

Arguments:

    FilterModuleContext   - our filter context area
    NdisRequest           - the completed request
    Status                - completion status

--*/
{
    PMS_FILTER                          pFilter = (PMS_FILTER)FilterModuleContext;
    PNDIS_OID_REQUEST                   OriginalRequest;
    PFILTER_REQUEST_CONTEXT             Context;
    BOOLEAN                             bFalse = FALSE;

    DEBUGP(DL_TRACE, "===>FilterOidRequestComplete, Request %p.\n", Request);

    Context = (PFILTER_REQUEST_CONTEXT)(&Request->SourceReserved[0]);
    OriginalRequest = (*Context);

    //
    // This is an internal request
    //
    if (OriginalRequest == NULL)
    {
        filterInternalRequestComplete(pFilter, Request, Status);
        return;
    }


    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);

    ASSERT(pFilter->PendingOidRequest == Request);
    pFilter->PendingOidRequest = NULL;

    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);


    //
    // Copy the information from the returned request to the original request
    //
    switch(Request->RequestType)
    {
        case NdisRequestMethod:
            OriginalRequest->DATA.METHOD_INFORMATION.OutputBufferLength =  Request->DATA.METHOD_INFORMATION.OutputBufferLength;
            OriginalRequest->DATA.METHOD_INFORMATION.BytesRead = Request->DATA.METHOD_INFORMATION.BytesRead;
            OriginalRequest->DATA.METHOD_INFORMATION.BytesNeeded = Request->DATA.METHOD_INFORMATION.BytesNeeded;
            OriginalRequest->DATA.METHOD_INFORMATION.BytesWritten = Request->DATA.METHOD_INFORMATION.BytesWritten;
            break;

        case NdisRequestSetInformation:
            OriginalRequest->DATA.SET_INFORMATION.BytesRead = Request->DATA.SET_INFORMATION.BytesRead;
            OriginalRequest->DATA.SET_INFORMATION.BytesNeeded = Request->DATA.SET_INFORMATION.BytesNeeded;
            break;

        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
        default:
            OriginalRequest->DATA.QUERY_INFORMATION.BytesWritten = Request->DATA.QUERY_INFORMATION.BytesWritten;
            OriginalRequest->DATA.QUERY_INFORMATION.BytesNeeded = Request->DATA.QUERY_INFORMATION.BytesNeeded;
            break;
    }


    (*Context) = NULL;

    NdisFreeCloneOidRequest(pFilter->FilterHandle, Request);

    NdisFOidRequestComplete(pFilter->FilterHandle, OriginalRequest, Status);

    DEBUGP(DL_TRACE, "<===FilterOidRequestComplete.\n");
}

static void FilterLinkChanged(
    PMS_FILTER              pFilter,
    NDIS_MEDIA_CONNECT_STATE Connect
)
{
    pFilter->LinkUp = Connect == MediaConnectStateConnected;
    DEBUGP(DL_TRACE, "===>Filter Link %s\n", pFilter->LinkUp ? "Up" : "Down");
    FilterUpdateTimer(pFilter);
}

_Use_decl_annotations_
VOID
FilterStatus(
    NDIS_HANDLE             FilterModuleContext,
    PNDIS_STATUS_INDICATION StatusIndication
    )
/*++

Routine Description:

    Status indication handler

Arguments:

    FilterModuleContext     - our filter context
    StatusIndication        - the status being indicated

NOTE: called at <= DISPATCH_LEVEL

  FILTER driver may call NdisFIndicateStatus to generate a status indication to 
  all higher layer modules.

--*/
{
    PMS_FILTER              pFilter = (PMS_FILTER)FilterModuleContext;
#if DBG
    BOOLEAN                  bFalse = FALSE;
#endif

    DEBUGP(DL_TRACE, "===>FilterStatus, IndicateStatus = %8x.\n", StatusIndication->StatusCode);


    //
    // The filter may do processing on the status indication here, including
    // intercepting and dropping it entirely.  However, the sample does nothing
    // with status indications except pass them up to the higher layer.  It is 
    // more efficient to omit the FilterStatus handler entirely if it does 
    // nothing, but it is included in this sample for illustrative purposes.
    //

#if DBG
    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);
    ASSERT(pFilter->bIndicating == FALSE);
    pFilter->bIndicating = TRUE;
    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);
#endif // DBG
    if (StatusIndication->StatusCode == NDIS_STATUS_LINK_STATE)
    {
        NDIS_LINK_STATE *linkState = (NDIS_LINK_STATE*)StatusIndication->StatusBuffer;
        FilterLinkChanged(pFilter, linkState->MediaConnectState);
    }

    NdisFIndicateStatus(pFilter->FilterHandle, StatusIndication);

#if DBG
    FILTER_ACQUIRE_LOCK(&pFilter->Lock, bFalse);
    ASSERT(pFilter->bIndicating == TRUE);
    pFilter->bIndicating = FALSE;
    FILTER_RELEASE_LOCK(&pFilter->Lock, bFalse);
#endif // DBG

    DEBUGP(DL_TRACE, "<===FilterStatus.\n");

}

_Use_decl_annotations_
VOID
FilterDevicePnPEventNotify(
    NDIS_HANDLE             FilterModuleContext,
    PNET_DEVICE_PNP_EVENT   NetDevicePnPEvent
    )
/*++

Routine Description:

    Device PNP event handler

Arguments:

    FilterModuleContext         - our filter context
    NetDevicePnPEvent           - a Device PnP event

NOTE: called at PASSIVE_LEVEL

--*/
{
    PMS_FILTER             pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_DEVICE_PNP_EVENT  DevicePnPEvent = NetDevicePnPEvent->DevicePnPEvent;
#if DBG
    BOOLEAN                bFalse = FALSE;
#endif

    DEBUGP(DL_TRACE, "===>FilterDevicePnPEventNotify: NetPnPEvent = %p.\n", NetDevicePnPEvent);

    //
    // The filter may do processing on the event here, including intercepting
    // and dropping it entirely.  However, the sample does nothing with Device
    // PNP events, except pass them down to the next lower* layer.  It is more
    // efficient to omit the FilterDevicePnPEventNotify handler entirely if it
    // does nothing, but it is included in this sample for illustrative purposes.
    //
    // * Trivia: Device PNP events percolate DOWN the stack, instead of upwards
    // like status indications and Net PNP events.  So the next layer is the
    // LOWER layer.
    //

    switch (DevicePnPEvent)
    {

        case NdisDevicePnPEventQueryRemoved:
        case NdisDevicePnPEventRemoved:
        case NdisDevicePnPEventSurpriseRemoved:
        case NdisDevicePnPEventQueryStopped:
        case NdisDevicePnPEventStopped:
        case NdisDevicePnPEventPowerProfileChanged:
        case NdisDevicePnPEventFilterListChanged:

            break;

        default:
            DEBUGP(DL_ERROR, "FilterDevicePnPEventNotify: Invalid event.\n");
            FILTER_ASSERT(bFalse);

            break;
    }

    NdisFDevicePnPEventNotify(pFilter->FilterHandle, NetDevicePnPEvent);

    DEBUGP(DL_TRACE, "<===FilterDevicePnPEventNotify\n");

}

_Use_decl_annotations_
NDIS_STATUS
FilterNetPnPEvent(
    NDIS_HANDLE              FilterModuleContext,
    PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification
    )
/*++

Routine Description:

    Net PNP event handler

Arguments:

    FilterModuleContext         - our filter context
    NetPnPEventNotification     - a Net PnP event

NOTE: called at PASSIVE_LEVEL

--*/
{
    PMS_FILTER                pFilter = (PMS_FILTER)FilterModuleContext;
    NDIS_STATUS               Status = NDIS_STATUS_SUCCESS;

    //
    // The filter may do processing on the event here, including intercepting 
    // and dropping it entirely.  However, the sample does nothing with Net PNP
    // events, except pass them up to the next higher layer.  It is more
    // efficient to omit the FilterNetPnPEvent handler entirely if it does
    // nothing, but it is included in this sample for illustrative purposes.
    //

    Status = NdisFNetPnPEvent(pFilter->FilterHandle, NetPnPEventNotification);

    return Status;
}

_Use_decl_annotations_
VOID
FilterSendNetBufferListsComplete(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    ULONG               SendCompleteFlags
    )
/*++

Routine Description:

    Send complete handler

    This routine is invoked whenever the lower layer is finished processing 
    sent NET_BUFFER_LISTs.  If the filter does not need to be involved in the
    send path, you should remove this routine and the FilterSendNetBufferLists
    routine.  NDIS will pass along send packets on behalf of your filter more 
    efficiently than the filter can.

Arguments:

    FilterModuleContext     - our filter context
    NetBufferLists          - a chain of NBLs that are being returned to you
    SendCompleteFlags       - flags (see documentation)

Return Value:

     NONE

--*/
{
    PMS_FILTER         pFilter = (PMS_FILTER)FilterModuleContext;
    BOOLEAN            DispatchLevel;
    PNET_BUFFER_LIST   *pCurrent = &NetBufferLists;

    //
    // If your filter injected any send packets into the datapath to be sent,
    // you must identify their NBLs here and remove them from the chain.  Do not
    // attempt to send-complete your NBLs up to the higher layer.
    //

    while (*pCurrent)
    {
        PNET_BUFFER_LIST CurrentNbl = *pCurrent;
        if (CurrentNbl->NdisPoolHandle == NblPool)
        {
            NdisAdvanceNetBufferListDataStart(CurrentNbl, 64, FALSE, NULL);
            *pCurrent = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;
            FreePauseNbl(CurrentNbl);
            DEBUGP(DL_LOUD, "Removed completed Pause NBL, new current %p\n", *pCurrent);
        }
        else
        {
            pCurrent = &(NET_BUFFER_LIST_NEXT_NBL(CurrentNbl));
        }
    }
    if (!NetBufferLists)
    {
        return;
    }
    // Send complete the NBLs.  If you removed any NBLs from the chain, make
    // sure the chain isn't empty (i.e., NetBufferLists!=NULL).
    DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendCompleteFlags);
    NdisFSendNetBufferListsComplete(pFilter->FilterHandle, NetBufferLists, SendCompleteFlags);
}


_Use_decl_annotations_
VOID
FilterSendNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    NDIS_PORT_NUMBER    PortNumber,
    ULONG               SendFlags
    )
/*++

Routine Description:

    Send Net Buffer List handler
    This function is an optional function for filter drivers. If provided, NDIS
    will call this function to transmit a linked list of NetBuffers, described by a
    NetBufferList, over the network. If this handler is NULL, NDIS will skip calling
    this filter when sending a NetBufferList and will call the next lower 
    driver in the stack.  A filter that doesn't provide a FilerSendNetBufferList
    handler can not originate a send on its own.

Arguments:

    FilterModuleContext     - our filter context area
    NetBufferLists          - a List of NetBufferLists to send
    PortNumber              - Port Number to which this send is targeted
    SendFlags               - specifies if the call is at DISPATCH_LEVEL

--*/
{
    PMS_FILTER          pFilter = (PMS_FILTER)FilterModuleContext;
    PNET_BUFFER_LIST    CurrNbl;
    BOOLEAN             DispatchLevel;
    BOOLEAN             bFalse = FALSE;

    DEBUGP(DL_TRACE, "===>SendNetBufferList: NBL = %p.\n", NetBufferLists);

    do
    {

       DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);
#if DBG
        //
        // we should never get packets to send if we are not in running state
        //

        FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
        //
        // If the filter is not in running state, fail the send
        //
        if (pFilter->State != FilterRunning)
        {
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);

            CurrNbl = NetBufferLists;
            while (CurrNbl)
            {
                NET_BUFFER_LIST_STATUS(CurrNbl) = NDIS_STATUS_PAUSED;
                CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
            }
            NdisFSendNetBufferListsComplete(pFilter->FilterHandle,
                        NetBufferLists,
                        DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
            break;

        }
        FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
#endif
        if (pFilter->TrackSends)
        {
            FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
            CurrNbl = NetBufferLists;
            while (CurrNbl)
            {
                pFilter->OutstandingSends++;
                FILTER_LOG_SEND_REF(1, pFilter, CurrNbl, pFilter->OutstandingSends);

                CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
            }
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
        }
        
        //
        // If necessary, queue the NetBufferLists in a local structure for later
        // processing.  However, do not queue them for "too long", or else the
        // system's performance may be degraded.  If you need to hold onto an
        // NBL for an unbounded amount of time, then allocate memory, perform a
        // deep copy, and complete the original NBL.
        //
        
        NdisFSendNetBufferLists(pFilter->FilterHandle, NetBufferLists, PortNumber, SendFlags);


    }
    while (bFalse);

    DEBUGP(DL_TRACE, "<===SendNetBufferList. \n");
}

_Use_decl_annotations_
VOID
FilterReturnNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    ULONG               ReturnFlags
    )
/*++

Routine Description:

    FilterReturnNetBufferLists handler.
    FilterReturnNetBufferLists is an optional function. If provided, NDIS calls
    FilterReturnNetBufferLists to return the ownership of one or more NetBufferLists
    and their embedded NetBuffers to the filter driver. If this handler is NULL, NDIS
    will skip calling this filter when returning NetBufferLists to the underlying
    miniport and will call the next lower driver in the stack. A filter that doesn't
    provide a FilterReturnNetBufferLists handler cannot originate a receive indication
    on its own.

Arguments:

    FilterInstanceContext       - our filter context area
    NetBufferLists              - a linked list of NetBufferLists that this 
                                  filter driver indicated in a previous call to 
                                  NdisFIndicateReceiveNetBufferLists
    ReturnFlags                 - flags specifying if the caller is at DISPATCH_LEVEL

--*/
{
    PMS_FILTER          pFilter = (PMS_FILTER)FilterModuleContext;
#if 0
    PNET_BUFFER_LIST    CurrNbl = NetBufferLists;
    UINT                NumOfNetBufferLists = 0;
    BOOLEAN             DispatchLevel;
    ULONG               Ref;

    DEBUGP(DL_TRACE, "===>ReturnNetBufferLists, NetBufferLists is %p.\n", NetBufferLists);


    //
    // If your filter injected any receive packets into the datapath to be
    // received, you must identify their NBLs here and remove them from the 
    // chain.  Do not attempt to receive-return your NBLs down to the lower
    // layer.
    //

    //
    // If your filter has modified any NBLs (or NBs, MDLs, etc) in your
    // FilterReceiveNetBufferLists handler, you must undo the modifications here.
    // In general, NBLs must be returned in the same condition in which you had
    // you received them.  (Exceptions: the NBLs can be re-ordered on the linked
    // list, and the scratch fields are don't-care).
    //

    if (pFilter->TrackReceives)
    {
        while (CurrNbl)
        {
            NumOfNetBufferLists ++;
            CurrNbl = NET_BUFFER_LIST_NEXT_NBL(CurrNbl);
        }
    }

    
    // Return the received NBLs.  If you removed any NBLs from the chain, make
    // sure the chain isn't empty (i.e., NetBufferLists!=NULL).
#endif
    NdisFReturnNetBufferLists(pFilter->FilterHandle, NetBufferLists, ReturnFlags);
#if 0
    if (pFilter->TrackReceives)
    {
        DispatchLevel = NDIS_TEST_RETURN_AT_DISPATCH_LEVEL(ReturnFlags);
        FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);

        pFilter->OutstandingRcvs -= NumOfNetBufferLists;
        Ref = pFilter->OutstandingRcvs;
        FILTER_LOG_RCV_REF(3, pFilter, NetBufferLists, Ref);
        FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
    }


    DEBUGP(DL_TRACE, "<===ReturnNetBufferLists.\n");
#endif
}


#if 0
_Use_decl_annotations_
VOID
FilterReceiveNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    NetBufferLists,
    NDIS_PORT_NUMBER    PortNumber,
    ULONG               NumberOfNetBufferLists,
    ULONG               ReceiveFlags
    )
/*++

Routine Description:

    FilerReceiveNetBufferLists is an optional function for filter drivers.
    If provided, this function processes receive indications made by underlying
    NIC or lower level filter drivers. This function  can also be called as a
    result of loopback. If this handler is NULL, NDIS will skip calling this
    filter when processing a receive indication and will call the next higher
    driver in the stack. A filter that doesn't provide a
    FilterReceiveNetBufferLists handler cannot provide a
    FilterReturnNetBufferLists handler and cannot a initiate an original receive 
    indication on its own.

Arguments:

    FilterModuleContext      - our filter context area.
    NetBufferLists           - a linked list of NetBufferLists
    PortNumber               - Port on which the receive is indicated
    ReceiveFlags             -

N.B.: It is important to check the ReceiveFlags in NDIS_TEST_RECEIVE_CANNOT_PEND.
    This controls whether the receive indication is an synchronous or 
    asynchronous function call.

--*/
{

    PMS_FILTER          pFilter = (PMS_FILTER)FilterModuleContext;
    BOOLEAN             DispatchLevel;
    ULONG               Ref;
    BOOLEAN             bFalse = FALSE;
#if DBG
    ULONG               ReturnFlags;
#endif

    DEBUGP(DL_TRACE, "===>ReceiveNetBufferList: NetBufferLists = %p.\n", NetBufferLists);
    do
    {

        DispatchLevel = NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags);
#if DBG
        FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);

        if (pFilter->State != FilterRunning)
        {
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);

            if (NDIS_TEST_RECEIVE_CAN_PEND(ReceiveFlags))
            {
                ReturnFlags = 0;
                if (NDIS_TEST_RECEIVE_AT_DISPATCH_LEVEL(ReceiveFlags))
                {
                    NDIS_SET_RETURN_FLAG(ReturnFlags, NDIS_RETURN_FLAGS_DISPATCH_LEVEL);
                }

                NdisFReturnNetBufferLists(pFilter->FilterHandle, NetBufferLists, ReturnFlags);
            }
            break;
        }
        FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
#endif

        ASSERT(NumberOfNetBufferLists >= 1);

        //
        // If you would like to drop a received packet, then you must carefully
        // modify the NBL chain as follows:
        //
        //     if NDIS_TEST_RECEIVE_CANNOT_PEND(ReceiveFlags):
        //         For each NBL that is NOT dropped, temporarily unlink it from
        //         the linked list, and indicate it up alone with 
        //         NdisFIndicateReceiveNetBufferLists and the
        //         NDIS_RECEIVE_FLAGS_RESOURCES flag set.  Then immediately
        //         relink the NBL back into the chain.  When all NBLs have been
        //         indicated up, you may return from this function.
        //     otherwise (NDIS_TEST_RECEIVE_CANNOT_PEND is FALSE):
        //         Divide the linked list of NBLs into two chains: one chain
        //         of packets to drop, and everything else in another chain.
        //         Return the first chain with NdisFReturnNetBufferLists, and
        //         indicate up the rest with NdisFIndicateReceiveNetBufferLists.
        //
        // Note: on the receive path for Ethernet packets, one NBL will have 
        // exactly one NB.  So (assuming you are receiving on Ethernet, or are 
        // attached above Native WiFi) you do not need to worry about dropping
        // one NB, but trying to indicate up the remaining NBs on the same NBL.
        // In other words, if the first NB should be dropped, drop the whole NBL.
        //

        //
        // If you would like to modify a packet, and can do so quickly, you may
        // do it here.  However, make sure you save enough information to undo
        // your modification in the FilterReturnNetBufferLists handler.
        //

        //
        // If necessary, queue the NetBufferLists in a local structure for later
        // processing.  However, do not queue them for "too long", or else the
        // system's performance may be degraded.  If you need to hold onto an
        // NBL for an unbounded amount of time, then allocate memory, perform a
        // deep copy, and return the original NBL.
        //

        if (pFilter->TrackReceives)
        {
            FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
            pFilter->OutstandingRcvs += NumberOfNetBufferLists;
            Ref = pFilter->OutstandingRcvs;

            FILTER_LOG_RCV_REF(1, pFilter, NetBufferLists, Ref);
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
        }

        NdisFIndicateReceiveNetBufferLists(
                   pFilter->FilterHandle,
                   NetBufferLists,
                   PortNumber,
                   NumberOfNetBufferLists,
                   ReceiveFlags);


        if (NDIS_TEST_RECEIVE_CANNOT_PEND(ReceiveFlags) &&
            pFilter->TrackReceives)
        {
            FILTER_ACQUIRE_LOCK(&pFilter->Lock, DispatchLevel);
            pFilter->OutstandingRcvs -= NumberOfNetBufferLists;
            Ref = pFilter->OutstandingRcvs;
            FILTER_LOG_RCV_REF(2, pFilter, NetBufferLists, Ref);
            FILTER_RELEASE_LOCK(&pFilter->Lock, DispatchLevel);
        }

    } while (bFalse);

    DEBUGP(DL_TRACE, "<===ReceiveNetBufferList: Flags = %8x.\n", ReceiveFlags);

}
#endif

#if DROP_PACKETS
static void CopyPacket(PNET_BUFFER NB, UCHAR* Buf, ULONG Length)
{
    ULONG CurrOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NB);
    ULONG Copied = 0;
    PMDL CurrMDL;
    for (CurrMDL = NET_BUFFER_CURRENT_MDL(NB); CurrMDL && Copied < Length; CurrMDL = CurrMDL->Next)
    {
        ULONG CurrLen;
        PVOID CurrAddr;
        NdisQueryMdl(CurrMDL, &CurrAddr, &CurrLen, NormalPagePriority | MdlMappingNoExecute);
        if (!CurrAddr)
        {
            break;
        }
        CurrLen = min(CurrLen - CurrOffset, Length - Copied);
        NdisMoveMemory(Buf + Copied, (PUCHAR)CurrAddr + CurrOffset, CurrLen);

        Copied += CurrLen;
        CurrOffset = 0;
    }
}
#endif

static BOOLEAN ShouldDropRx(PMS_FILTER pFilter, PNET_BUFFER_LIST Nbl)
{
    BOOLEAN bDrop = TRUE;
    PNET_BUFFER nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
    while (nb)
    {
        ULONG len = NET_BUFFER_DATA_LENGTH(nb);
        if (len > LengthToDrop && LengthToDrop)
        {
            InterlockedIncrement(&pFilter->DroppedPackets);
            len = min(len, sizeof(pFilter->PacketBuffer));
#if DROP_PACKETS
            CopyPacket(nb, pFilter->PacketBuffer, len);
#endif
        }
        else
        {
            bDrop = FALSE;
        }
        nb = NET_BUFFER_NEXT_NB(nb);
    }
    return bDrop;
}

static CCHAR GetTargetProcessor(PMS_FILTER pFilter)
{
    ULONG last = pFilter->LastCPUNumber + 1;
    if (last >= NumberOfProcessors)
    {
        last = 0;
    }
    if (last == KeGetCurrentProcessorIndex())
    {
        last++;
    }
    if (last >= NumberOfProcessors)
    {
        last = 0;
    }
    pFilter->LastCPUNumber = last;
    return (CCHAR)last;
}

static FILTER_DPC *FindDpc(PMS_FILTER pFilter)
{
    ULONG last = pFilter->LastDpcIndex;
    ULONG index = last + 1;
    if (index >= RTL_NUMBER_OF(pFilter->DpcArray))
    {
        index = 0;
    }
    while (index != last)
    {
        LONG* pl = &pFilter->DpcArray[index].Allocated;
        if (InterlockedIncrement(pl) == 1)
        {
            return &pFilter->DpcArray[index];
        }
        InterlockedDecrement(pl);
        index++;
        if (index >= RTL_NUMBER_OF(pFilter->DpcArray))
        {
            index = 0;
        }
    }
    return NULL;
}

VOID
FilterReceiveNetBufferLists(
    NDIS_HANDLE         FilterModuleContext,
    PNET_BUFFER_LIST    Nbls,
    NDIS_PORT_NUMBER    PortNumber,
    ULONG               NumberOfNetBufferLists,
    ULONG               ReceiveFlags
)
{
    PMS_FILTER pFilter = (PMS_FILTER)FilterModuleContext;
    BOOLEAN    bDriverIsOwner = NDIS_TEST_RECEIVE_CANNOT_PEND(ReceiveFlags);
    InterlockedIncrement(&pFilter->NumIndication);
    if (bDriverIsOwner || !LengthToDrop)
    {
        // for simplicity - do not drop anything
        NdisFIndicateReceiveNetBufferLists(
            pFilter->FilterHandle,
            Nbls,
            PortNumber,
            NumberOfNetBufferLists,
            ReceiveFlags);
        return;
    }
    PNET_BUFFER_LIST DropHead = NULL, IndicateHead = NULL;
    PNET_BUFFER_LIST* DropTail = &DropHead, *IndicateTail = &IndicateHead;
    ULONG nofDrop = 0, nofIndicate = 0;
    while (Nbls)
    {
        PNET_BUFFER_LIST next = NET_BUFFER_LIST_NEXT_NBL(Nbls);
        if (ShouldDropRx(pFilter, Nbls))
        {
            *DropTail = Nbls;
            DropTail = &NET_BUFFER_LIST_NEXT_NBL(Nbls);
            nofDrop++;
        }
        else
        {
            *IndicateTail = Nbls;
            IndicateTail = &NET_BUFFER_LIST_NEXT_NBL(Nbls);
            nofIndicate++;
        }
        NET_BUFFER_LIST_NEXT_NBL(Nbls) = NULL;
        Nbls = next;
    }
    if (nofIndicate)
    {
        NdisFIndicateReceiveNetBufferLists(
            pFilter->FilterHandle,
            IndicateHead,
            PortNumber,
            nofIndicate,
            ReceiveFlags);
    }
    if (nofDrop)
    {
#if DROP_PACKETS
        NdisFReturnNetBufferLists(pFilter->FilterHandle, DropHead, ReceiveFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL);
#else
        FILTER_DPC* dpc = FindDpc(pFilter);
        BOOLEAN bIndicateNow = FALSE;
        if (!dpc)
        {
            pFilter->DpcNotFoundCounter++;
            bIndicateNow = TRUE;
        }
        else
        {
            KeSetImportanceDpc(&dpc->Dpc, HighImportance);
            KeSetTargetProcessorDpc(&dpc->Dpc, GetTargetProcessor(pFilter));
            if (!KeInsertQueueDpc(&dpc->Dpc, DropHead, (PVOID)nofDrop))
            {
                InterlockedDecrement(&dpc->Allocated);
                bIndicateNow = TRUE;
            }
        }
        if (bIndicateNow)
        {
            NdisFIndicateReceiveNetBufferLists(
                pFilter->FilterHandle,
                DropHead,
                PortNumber,
                nofDrop,
                ReceiveFlags);
        }
#endif
    }
}

_Use_decl_annotations_
VOID
FilterCancelSendNetBufferLists(
    NDIS_HANDLE             FilterModuleContext,
    PVOID                   CancelId
    )
/*++

Routine Description:

    This function cancels any NET_BUFFER_LISTs pended in the filter and then
    calls the NdisFCancelSendNetBufferLists to propagate the cancel operation.

    If your driver does not queue any send NBLs, you may omit this routine.  
    NDIS will propagate the cancelation on your behalf more efficiently.

Arguments:

    FilterModuleContext      - our filter context area.
    CancelId                 - an identifier for all NBLs that should be dequeued

Return Value:

    None

*/
{
    PMS_FILTER  pFilter = (PMS_FILTER)FilterModuleContext;

    NdisFCancelSendNetBufferLists(pFilter->FilterHandle, CancelId);
}


_Use_decl_annotations_
NDIS_STATUS
FilterSetModuleOptions(
    NDIS_HANDLE             FilterModuleContext
    )
/*++

Routine Description:

    This function set the optional handlers for the filter

Arguments:

    FilterModuleContext: The FilterModuleContext given to NdisFSetAttributes

Return Value:

    NDIS_STATUS_SUCCESS
    NDIS_STATUS_RESOURCES
    NDIS_STATUS_FAILURE

--*/
{
   NDIS_STATUS                              Status = NDIS_STATUS_SUCCESS;
   UNREFERENCED_PARAMETER(FilterModuleContext);
   return Status;
}



_IRQL_requires_max_(DISPATCH_LEVEL)
NDIS_STATUS
filterDoInternalRequest(
    _In_ PMS_FILTER                   FilterModuleContext,
    _In_ NDIS_REQUEST_TYPE            RequestType,
    _In_ NDIS_OID                     Oid,
    _Inout_updates_bytes_to_(InformationBufferLength, *pBytesProcessed)
         PVOID                        InformationBuffer,
    _In_ ULONG                        InformationBufferLength,
    _In_opt_ ULONG                    OutputBufferLength,
    _In_ ULONG                        MethodId,
    _Out_ PULONG                      pBytesProcessed
    )
/*++

Routine Description:

    Utility routine that forms and sends an NDIS_OID_REQUEST to the
    miniport, waits for it to complete, and returns status
    to the caller.

    NOTE: this assumes that the calling routine ensures validity
    of the filter handle until this returns.

Arguments:

    FilterModuleContext - pointer to our filter module context
    RequestType - NdisRequest[Set|Query|method]Information
    Oid - the object being set/queried
    InformationBuffer - data for the request
    InformationBufferLength - length of the above
    OutputBufferLength  - valid only for method request
    MethodId - valid only for method request
    pBytesProcessed - place to return bytes read/written

Return Value:

    Status of the set/query request

--*/
{
    FILTER_REQUEST              FilterRequest;
    PNDIS_OID_REQUEST           NdisRequest = &FilterRequest.Request;
    NDIS_STATUS                 Status;
    BOOLEAN                     bFalse;


    bFalse = FALSE;
    *pBytesProcessed = 0;
    NdisZeroMemory(NdisRequest, sizeof(NDIS_OID_REQUEST));

    NdisInitializeEvent(&FilterRequest.ReqEvent);

    NdisRequest->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    NdisRequest->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    NdisRequest->Header.Size = sizeof(NDIS_OID_REQUEST);
    NdisRequest->RequestType = RequestType;

    switch (RequestType)
    {
        case NdisRequestQueryInformation:
             NdisRequest->DATA.QUERY_INFORMATION.Oid = Oid;
             NdisRequest->DATA.QUERY_INFORMATION.InformationBuffer =
                                    InformationBuffer;
             NdisRequest->DATA.QUERY_INFORMATION.InformationBufferLength =
                                    InformationBufferLength;
            break;

        case NdisRequestSetInformation:
             NdisRequest->DATA.SET_INFORMATION.Oid = Oid;
             NdisRequest->DATA.SET_INFORMATION.InformationBuffer =
                                    InformationBuffer;
             NdisRequest->DATA.SET_INFORMATION.InformationBufferLength =
                                    InformationBufferLength;
            break;

        case NdisRequestMethod:
             NdisRequest->DATA.METHOD_INFORMATION.Oid = Oid;
             NdisRequest->DATA.METHOD_INFORMATION.MethodId = MethodId;
             NdisRequest->DATA.METHOD_INFORMATION.InformationBuffer =
                                    InformationBuffer;
             NdisRequest->DATA.METHOD_INFORMATION.InputBufferLength =
                                    InformationBufferLength;
             NdisRequest->DATA.METHOD_INFORMATION.OutputBufferLength = OutputBufferLength;
             break;



        default:
            FILTER_ASSERT(bFalse);
            break;
    }

    NdisRequest->RequestId = (PVOID)FILTER_REQUEST_ID;

    Status = NdisFOidRequest(FilterModuleContext->FilterHandle,
                            NdisRequest);


    if (Status == NDIS_STATUS_PENDING)
    {

        NdisWaitEvent(&FilterRequest.ReqEvent, 0);
        Status = FilterRequest.Status;
    }


    if (Status == NDIS_STATUS_SUCCESS)
    {
        if (RequestType == NdisRequestSetInformation)
        {
            *pBytesProcessed = NdisRequest->DATA.SET_INFORMATION.BytesRead;
        }

        if (RequestType == NdisRequestQueryInformation)
        {
            *pBytesProcessed = NdisRequest->DATA.QUERY_INFORMATION.BytesWritten;
        }

        if (RequestType == NdisRequestMethod)
        {
            *pBytesProcessed = NdisRequest->DATA.METHOD_INFORMATION.BytesWritten;
        }

        //
        // The driver below should set the correct value to BytesWritten
        // or BytesRead. But now, we just truncate the value to InformationBufferLength
        //
        if (RequestType == NdisRequestMethod)
        {
            if (*pBytesProcessed > OutputBufferLength)
            {
                *pBytesProcessed = OutputBufferLength;
            }
        }
        else
        {

            if (*pBytesProcessed > InformationBufferLength)
            {
                *pBytesProcessed = InformationBufferLength;
            }
        }
    }


    return Status;
}

VOID
filterInternalRequestComplete(
    _In_ NDIS_HANDLE                  FilterModuleContext,
    _In_ PNDIS_OID_REQUEST            NdisRequest,
    _In_ NDIS_STATUS                  Status
    )
/*++

Routine Description:

    NDIS entry point indicating completion of a pended NDIS_OID_REQUEST.

Arguments:

    FilterModuleContext - pointer to filter module context
    NdisRequest - pointer to NDIS request
    Status - status of request completion

Return Value:

    None

--*/
{
    PFILTER_REQUEST              FilterRequest;


    UNREFERENCED_PARAMETER(FilterModuleContext);

    //
    //  Get at the request context.
    //
    FilterRequest = CONTAINING_RECORD(NdisRequest, FILTER_REQUEST, Request);

    //
    //  Save away the completion status.
    //
    FilterRequest->Status = Status;

    //
    //  Wake up the thread blocked for this request to complete.
    //
    NdisSetEvent(&FilterRequest->ReqEvent);
}

