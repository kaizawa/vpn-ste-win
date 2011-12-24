/*
 * Copyright (C) 2004-2010 Kazuyoshi Aizawa. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/****************************************************************************
 *  ste_local.c
 *
 *    NDIS のエントリポイント以外の Ste.sys ドライバ固有の関数が記述されている
 *
 *  変更履歴:
 *    2006/05/05
 *      o MAC アドレスをドライバーのインストール時に動的に生成するようにした。
 *
 *****************************************************************************/
#include "ste.h"
#include <ntstrsafe.h>

/****************************************************************************
 * SteCreateAdapter()
 * 
 *   STE_ADAPTER 構造体を確保し、その他、必要なリソースを確保、初期化する。
 *
 * 引数:
 *
 *    Adapter : アダプタ構造体のポインタのアドレス
 *
 * 戻り値:
 *
 *   成功時 : NDIS_STATUS_SUCCESS
 *
***************************************************************************/
NDIS_STATUS
SteCreateAdapter(
    IN OUT STE_ADAPTER    **pAdapter     
    )
{
    STE_ADAPTER      *Adapter = NULL;
    NDIS_STATUS       Status;
    INT                i;
    LARGE_INTEGER     SystemTime;    

    DEBUG_PRINT0(3, "SteCreateAdapter called\n");    

    do {
        //
        // STE_ADAPTER を確保
        //
        Status = NdisAllocateMemoryWithTag(
            (PVOID)&Adapter,     //OUT PVOID  *
            sizeof(STE_ADAPTER), //IN UINT    
            (ULONG)'CrtA'        //IN ULONG   
            );
        if (Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT1(1, "SteCreateAdapter: NdisAllocateMemoryWithTag Failed (0x%x)\n",
                            Status);
            break;
        }

        DEBUG_PRINT1(3, "SteCreateAdapter: Adapter = 0x%p\n", Adapter);
        
        NdisZeroMemory(Adapter, sizeof(STE_ADAPTER));

        //
        // 受信用バッファープールを確保
        // 
        NdisAllocateBufferPool(
            &Status,                      //OUT PNDIS_STATUS  
            &Adapter->RxBufferPoolHandle, //OUT PNDIS_HANDLE  
            STE_MAX_RXDS                  //IN UINT           
            );
        if (Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT1(1, "SteCreateAdapter: NdisAllocateBufferPool Failed (0x%x)\n",Status);
            break;
        }
        
        //
        // 受信用パケットプールを確保
        //
        NdisAllocatePacketPool(
            &Status,                         //OUT PNDIS_STATUS  
            &Adapter->RxPacketPoolHandle,    //OUT PNDIS_HANDLE  
            STE_MAX_RXDS,                    //IN UINT           
            PROTOCOL_RESERVED_SIZE_IN_PACKET //IN UINT           
            );
        if (Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT1(1, "SteCreateAdapter: NdisAllocatePacketPool Failed (0x%x)\n", Status);
            break;
        }            

        // 受信用の Queue を初期化
        NdisInitializeListHead(&Adapter->RecvQueue.List);
        NdisAllocateSpinLock(&Adapter->RecvQueue.SpinLock);
        Adapter->RecvQueue.QueueMax = STE_QUEUE_MAX;
        Adapter->RecvQueue.QueueCount = 0;
        
        //送信用の Queue を初期化
        NdisInitializeListHead(&Adapter->SendQueue.List);        
        NdisAllocateSpinLock(&Adapter->SendQueue.SpinLock);
        Adapter->SendQueue.QueueMax = STE_QUEUE_MAX;        
        Adapter->SendQueue.QueueCount = 0;

        // アダプター内のロックを初期化
        NdisAllocateSpinLock(&Adapter->AdapterSpinLock);
        NdisAllocateSpinLock(&Adapter->SendSpinLock);
        NdisAllocateSpinLock(&Adapter->RecvSpinLock);                        

        // パケットフィルターの初期値をセット
        // 不要なような気もする。
        Adapter->PacketFilter |=
            (NDIS_PACKET_TYPE_ALL_MULTICAST| NDIS_PACKET_TYPE_BROADCAST |NDIS_PACKET_TYPE_DIRECTED);
        
        // グローバルのアダプタリストに追加
        NdisAcquireSpinLock(&SteGlobalSpinLock);
        Status = SteInsertAdapterToList(Adapter);
        NdisReleaseSpinLock(&SteGlobalSpinLock);
        if(Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT1(1, "SteCreateAdapter: SteInsertAdapterToList Failed (0x%x)\n", Status);
            break;
        }

        //
        // MAC アドレスを登録
        // 第一オクテットの U/L ビットを 1（=ローカルアドレス）にセットする。
        // 後半の 32 bit はドライバがインストールされた時間から決める。
        //
        KeQuerySystemTime(&SystemTime);
        
        Adapter->EthernetAddress[0] = 0x0A; 
        Adapter->EthernetAddress[1] = 0x00;
        Adapter->EthernetAddress[2] = (UCHAR)((SystemTime.QuadPart >> 24) & 0xff);
        Adapter->EthernetAddress[3] = (UCHAR)((SystemTime.QuadPart >> 16) & 0xff);
        Adapter->EthernetAddress[4] = (UCHAR)((SystemTime.QuadPart >>  8) & 0xff);
        Adapter->EthernetAddress[5] = (UCHAR)((SystemTime.QuadPart + Adapter->Instance) & 0xff);

    } while(FALSE);


    if(Status != NDIS_STATUS_SUCCESS && Adapter != NULL){
        //
        // エラー時には確保したリソースを開放する
        // 
        // バッファープールを開放
        if(Adapter->RxBufferPoolHandle != NULL)
            NdisFreeBufferPool(Adapter->RxBufferPoolHandle);
        // パケットプールを開放
        if(Adapter->RxPacketPoolHandle != NULL)
            NdisFreePacketPool(Adapter->RxPacketPoolHandle);
        // STE_ADAPTER 構造体を開放
        NdisFreeMemory(
            (PVOID)Adapter,      //OUT PVOID  
            sizeof(STE_ADAPTER), //IN UINT    
            0                    //IN UINT
            );
    } else {
        //
        // 成功時は確保したアダプターのアドレスをセット
        //
        *pAdapter = Adapter;
    }
    
    return(Status);
}

/****************************************************************************
 * SteDeleteAdapter()
 * 
 *   STE_ADAPTER 構造体を削除し、リソースを解除する。
 *
 * 引数:
 *
 *    Adapter : 開放する STE_ADAPTER 構造体
 *
 * 戻り値:
 *
 *  正常時 :  NDIS_STATUS_SUCCESS
 *
***************************************************************************/
NDIS_STATUS
SteDeleteAdapter(
    IN STE_ADAPTER    *Adapter
    )
{
    NDIS_STATUS        Status = NDIS_STATUS_SUCCESS;
    NDIS_PACKET       *Packet = NULL;

    DEBUG_PRINT0(3, "SteDeleteAdapter called\n");        

    // 送信キューに入っているパケットを処理する
    while (SteGetQueue(&Adapter->SendQueue, &Packet) == NDIS_STATUS_SUCCESS) {
        NdisMSendComplete(
            Adapter->MiniportAdapterHandle,  //IN NDIS_HANDLE   
            Packet,                          //IN PNDIS_PACKET  
            NDIS_STATUS_REQUEST_ABORTED      //IN NDIS_STATUS   
            );
    }

    //  受信キューに入っているパケットを処理する
    while (SteGetQueue(&Adapter->RecvQueue, &Packet) == NDIS_STATUS_SUCCESS) {
        SteFreeRecvPacket(Adapter, Packet);
    }
    //  これが終われば、全ての受信パケットがフリーされているはず。
    
    // バッファープールを開放
    if(Adapter->RxBufferPoolHandle != NULL)
        NdisFreeBufferPool(Adapter->RxBufferPoolHandle);

    // パケットプールを開放
    if(Adapter->RxPacketPoolHandle != NULL)
        NdisFreePacketPool(Adapter->RxPacketPoolHandle);

    // グローバルのアダプターのリストから削除
    NdisAcquireSpinLock(&SteGlobalSpinLock);
    SteRemoveAdapterFromList(Adapter);
    NdisReleaseSpinLock(&SteGlobalSpinLock);    

    //
    // STE_ADAPTER 構造体を開放
    //
    NdisFreeMemory(
        (PVOID)Adapter,      //OUT PVOID  
        sizeof(STE_ADAPTER), //IN UINT    
        0                    //IN UINT
    );
    
    return(Status);
}

/****************************************************************************
 * SteResetTimerFunc()
 *
 *  リセット処理の完了を確認するためのタイマー関数。
 *  SteMiniportInitialize() にて初期化され　NdisSetTimer()　にて時間をきめ
 *  られた時に呼び出される。
 *
 * 引数:
 *
 *    Adapter : STE_ADAPTER 構造体
 *
 * 戻り値:
 *
 *   無し
 *
***************************************************************************/
VOID 
SteResetTimerFunc(
    IN PVOID  SystemSpecific1,
    IN PVOID  FunctionContext,
    IN PVOID  SystemSpecific2,
    IN PVOID  SystemSpecific3
    )
{
    STE_ADAPTER    *Adapter;
    NDIS_STATUS     Status = NDIS_STATUS_SUCCESS;

    DEBUG_PRINT0(3, "SteResetTimverFunc called\n");            
    
    Adapter = (STE_ADAPTER *)FunctionContext;

    Adapter->ResetTrys++;

    // 上位プロトコルに通知済みでまだ戻ってきていないパケットがあるか
    // どうかを確認する。

    if (Adapter->RecvIndicatedPackets > 0) {
        if(Adapter->ResetTrys > STE_MAX_WAIT_FOR_RESET){
            // 最大試行回数を超えた
            Status = NDIS_STATUS_FAILURE;
        } else {
            // 後ほど再トライ
            NdisSetTimer(&Adapter->ResetTimer, 300);
            return;
        }
    } else {
        Status = NDIS_STATUS_SUCCESS;
    }

    NdisMResetComplete(
        Adapter->MiniportAdapterHandle,  //IN NDIS_HANDLE  
        Status,                          //IN NDIS_STATUS  
        FALSE                            //IN BOOLEAN      
        );    

    return;
}

/****************************************************************************
 * SteRecvTimerFunc()
 *
 *  受信パケットキューに溜まったパケットを上位プロトコルに通知するための
 *  タイマー関数。SteMiniportInitialize() にて初期化され NdisSetTimer()
 *  にて時間をきめられた時に呼び出される。
 *
 * 引数:
 *
 *    Adapter : 開放する STE_ADAPTER 構造体
 *
 * 戻り値:
 *
 *   無し
 *
***************************************************************************/
VOID 
SteRecvTimerFunc(
    IN PVOID  SystemSpecific1,
    IN PVOID  FunctionContext,
    IN PVOID  SystemSpecific2,
    IN PVOID  SystemSpecific3
    )
{
    STE_ADAPTER *Adapter;
    NDIS_PACKET *Packet = NULL;
    
    Adapter = (STE_ADAPTER *)FunctionContext;

    DEBUG_PRINT0(3, "SteRecvtTimverFunc called\n");                

    while (SteGetQueue(&Adapter->RecvQueue, &Packet) == NDIS_STATUS_SUCCESS){
        /*
         * NdisMIndicateReceivePacket() を呼んで上位プロトコルに対して
         * パケットを転送する。
         */
        NdisMIndicateReceivePacket(
            Adapter->MiniportAdapterHandle,  // IN NDIS_HANDLE    
            &Packet,                         //IN PPNDIS_PACKET  
            1                                //IN UINT           
            );
    }
    
    return;
}


/****************************************************************************
 * SteRegisterDevice()
 * 
 *   NdisMRegisterDevice を呼ぶ事により、デバイスオブジェクトを作成し、各種 
 *   Dispatch　ルーチンを登録する。                                               
 *   作成されたデバイスオブジェクトは仮想 NIC デーモンによってオープンされ        
 *   IOCTL コマンドの送信、ReadFile()、WriteFile() によるデータの送受に使われる。 
 *                                                                                
 *  引数:
 *                                                                               
 *      Adapter : STE_ADAPTER 構造体
 *                                                                               
 *  返り値:                                                                
 *                                                                               
 *      正常時 : NDIS_STATUS_SUCCESS 
 *
 ****************************************************************************/
NDIS_STATUS
SteRegisterDevice(
    IN STE_ADAPTER    *Adapter
    )
{
    NDIS_STATUS        Status = NDIS_STATUS_SUCCESS;
    NDIS_STRING        DeviceName;
    NDIS_STRING        SymbolicName;
    NDIS_STRING        Instance; // 使ってない
    PDRIVER_DISPATCH   MajorFunctions[IRP_MJ_MAXIMUM_FUNCTION+1]; // IOCTL + READ + WRITE
    NDIS_HANDLE        NdisDeviceHandle;
    UCHAR              SteDeviceName[STE_MAX_DEVICE_NAME];
    UCHAR              SteSymbolicName[STE_MAX_DEVICE_NAME];

    DEBUG_PRINT0(3, "SteRegisterDevice called\n");

    //
    // インスタンス毎に異なるデバイスを作成するようにする。
    // （複数の仮想 NIC をサポートするため）
    // TODO: インスタンス番号を1桁と仮定している。なので、
    //       2桁以上だと問題が発生する。なにか対策を行わなければ・・・
    //
    Status = RtlStringCbPrintfA(
        SteDeviceName,  //OUT LPSTR  
        15,             //IN size_t  
        "%s%d",STE_DEVICE_NAME, Adapter->Instance   //IN LPCSTR  
        );

    Status = RtlStringCbPrintfA(
        SteSymbolicName, //OUT LPSTR  
        19,              //IN size_t  
        "%s%d",STE_SYMBOLIC_NAME, Adapter->Instance  //IN LPCSTR  
        );        

    NdisInitializeString(
        &DeviceName, //IN OUT PNDIS_STRING
        SteDeviceName //IN PUCHAR
        );
    
    NdisInitializeString(
        &SymbolicName,    //IN OUT PNDIS_STRING  
        SteSymbolicName   //IN PCWSTR            
        );

    NdisZeroMemory(MajorFunctions, sizeof(MajorFunctions));    

    //
    // ディスパッチルーチン
    //
    MajorFunctions[IRP_MJ_READ]           = SteDispatchRead;
    MajorFunctions[IRP_MJ_WRITE]          = SteDispatchWrite;
    MajorFunctions[IRP_MJ_DEVICE_CONTROL] = SteDispatchIoctl;
    MajorFunctions[IRP_MJ_CREATE]         = SteDispatchOther;
    MajorFunctions[IRP_MJ_CLEANUP]        = SteDispatchOther;
    MajorFunctions[IRP_MJ_CLOSE]          = SteDispatchOther;
        
    Status = NdisMRegisterDevice(
        NdisWrapperHandle,      //IN NDIS_HANDLE   
        &DeviceName,            //IN PNDIS_STRING     
        &SymbolicName,          //IN PNDIS_STRING     
        MajorFunctions,         //IN PDRIVER_DISPATCH 
        &Adapter->DeviceObject, //OUT PDEVICE_OBJECT *
        &Adapter->DeviceHandle  //OUT NDIS_HANDLE *
        );

    /*
     * フラグを確認・・・ 0x40(DO_DEVICE_HAS_NAME)だった。
     */
    DEBUG_PRINT0(3, "SteRegisterDevice ");         
    if(Adapter->DeviceObject->Flags & DO_BUFFERED_IO){
        DEBUG_PRINT0(3, "DeviceObject has DO_BUFFERED_IO flag\n");
    } else if (Adapter->DeviceObject->Flags & DO_DIRECT_IO){
        DEBUG_PRINT0(3, "DeviceObject has DO_DIRECT_IO flag\n");
    } else {
        DEBUG_PRINT1(3, "DeviceObject has flag 0x%x\n",Adapter->DeviceObject->Flags );
    }
    /*
     * 新しく作成された DeviceObject のフラグに Buffered I/O のフラグをセットする。
     * これにより Irp->AssociatedIrp.SystemBuffer にデータが入ってくるようになるようだ。
     * メモリのコピーが必要な Buffered I/O より Direct I/O のほうがいいという考えもある
     * が、今のとこは Buffered I/O にしておこう。（分かりやすいし・・)
     */    
    Adapter->DeviceObject->Flags |= DO_BUFFERED_IO;
    /*
     * 再度フラグを確認
     */
    DEBUG_PRINT0(3, "SteRegisterDevice ");    
    if(Adapter->DeviceObject->Flags & DO_BUFFERED_IO){
        DEBUG_PRINT0(3, "After set flag: DeviceObject has DO_BUFFERED_IO flag\n");
    } else if (Adapter->DeviceObject->Flags & DO_DIRECT_IO){
        DEBUG_PRINT0(3, "After set flag: DeviceObject has DO_DIRECT_IO flag\n");
    } else {
        DEBUG_PRINT1(3, "After set flag: DeviceObject has flag 0x%x\n",Adapter->DeviceObject->Flags );
    }

    DEBUG_PRINT1(3, "SteRegisterDevice returned (0x%d)\n", Status);                    
    return(Status);
}


/****************************************************************************
 * SteDeregisterDevice()
 * 
 *   IOCTL/ReadFile/WiteFile 用のデバイスオブジェクトを削除する。
 *
 *  引数:
 *                                                                               
 *      Adapter : STE_ADAPTER 構造体
 *                                                                               
 *  返り値:                                                                
 *                                                                               
 *      正常時 : NDIS_STATUS_SUCCESS 
 *
 ****************************************************************************/
NDIS_STATUS
SteDeregisterDevice(
    IN STE_ADAPTER    *Adapter
    )
{
    NDIS_STATUS        Status  = NDIS_STATUS_SUCCESS;

    DEBUG_PRINT0(3, "SteDeregisterDevice called\n");                

    Status = NdisMDeregisterDevice(Adapter->DeviceHandle);
    Adapter->DeviceHandle = NULL;
    
    return(Status);
}

/*********************************************************************
 * SteDispatchRead() 
 *
 * この関数は Dispatch Read ルーチン。IRP_MJ_READ の処理のために呼ばれる。
 * 
 * 引数:
 *
 *    DeviceObject - デバイスオブジェクトへのポインタ
 *    Irp      - I/O リクエストパケットへのポインタ(I/O Request Packt)
 *
 * 返り値:
 *
 *    正常時   : STATUS_SUCCESS
 *    エラー時 : STATUS_UNSUCCESSFUL
 *                 
 ******************************************************************************/
NTSTATUS
SteDispatchRead(
    IN PDEVICE_OBJECT           DeviceObject,
    IN PIRP                     Irp
    )
{
    NTSTATUS            NtStatus = STATUS_SUCCESS;
    NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;
    STE_ADAPTER        *Adapter = NULL; // Packet の送受信を行う Adapter
    NDIS_PACKET        *Packet = NULL;

    DEBUG_PRINT0(3, "SteDispatchRead called\n");

    //
    // Debug 用。 Buffer のフラグをチェック
    //
    if( DebugLevel >= 3) {
        DEBUG_PRINT0(3, "SteDispatchRead: ");
        if ( Irp->Flags & DO_BUFFERED_IO){
            DEBUG_PRINT1(3, "Irp->Flags(0x%x)==DO_BUFFERED_IO\n",Irp->Flags);
        } else if ( Irp->Flags & DO_DIRECT_IO){
            DEBUG_PRINT1(3, "Irp->Flags(0x%x)==DO_DIRECT_IO\n",Irp->Flags);
        } else {
            DEBUG_PRINT1(3, "Irp->Flags(0x%x)\n",Irp->Flags);
        }
    }    


    //
    // 途中で break するためだけの do-while 文
    // 
    do {    
        //
        // グローバルロックを取得し、アダプターのリストから 引数で
        // 渡された DeviceObject を含むアダプターを見つける
        //
        NdisAcquireSpinLock(&SteGlobalSpinLock);
        Status =  SteFindAdapterByDeviceObject(DeviceObject, &Adapter);
        NdisReleaseSpinLock(&SteGlobalSpinLock);
        
        if ( Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT0(1, "SteDispatchRead: Can't find Adapter\n");
            break;
        }

        // 送信キューからパケットを取得
        Status = SteGetQueue(&Adapter->SendQueue, &Packet);
        if ( Status != NDIS_STATUS_SUCCESS){        
            DEBUG_PRINT0(2, "SteDispatchRead: No packet found\n");
            break;
        }

        // 取得したパケットの中身を表示
        if(DebugLevel >= 3)
            StePrintPacket(Packet);                
        
        // データを Irp にコピーする
        Status = SteCopyPacketToIrp(Packet, Irp);
        if(Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT0(1, "SteDispatchRead: SteCopyPacketToIrp failed\n");  
            break;
        }             

        // NDIS に SendComplete を返す
        Status = NDIS_STATUS_SUCCESS;
        NdisMSendComplete(
            Adapter->MiniportAdapterHandle,  //IN NDIS_HANDLE   
            Packet,                          //IN PNDIS_PACKET  
            Status                           //IN NDIS_STATUS   
            );
        
    } while (FALSE);

    if(Status != NDIS_STATUS_SUCCESS){
        // エラーの場合 IoStatus.Information に 0 をセット
        NtStatus = STATUS_UNSUCCESSFUL;
        Irp->IoStatus.Information = 0;
    }

    Irp->IoStatus.Status = NtStatus;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return(NtStatus);
}  

/*********************************************************************
 * SteDispatchWrite()
 *  
 *  Dispatch Write ルーチン。IRP_MJ_WRITE の処理のために呼ばれる。
 *  このルーチンは sted から受けとたったイーサネットフレームの中身を
 *  コピーし、受信パケットを得、バッファとこの受信パケットに関連付けを行い、
 *  パケットをアダプターインスタンスの受信キューに入れる。
 *  また、後ほどパケットが上位プロトコルに通知されるように、タイマーをセット。
 *
 * 引数:
 *
 *    DeviceObject :  デバイスオブジェクトへのポインタ
 *    Irp          :  I/O リクエストパケットへのポインタ(I/O Request Packt)
 *
 * 返り値:
 *
 *    正常時   : STATUS_SUCCESS
 *    エラー時 : STATUS_UNSUCCESSFUL
 *                 
 ******************************************************************************/
NTSTATUS
SteDispatchWrite(
    IN PDEVICE_OBJECT           DeviceObject,
    IN PIRP                     Irp
    )
{
    NDIS_STATUS            Status   = NDIS_STATUS_SUCCESS;
    NTSTATUS               NtStatus = STATUS_SUCCESS;
    IO_STACK_LOCATION     *irpStack;
    STE_ADAPTER           *Adapter  = NULL;// Packet の送受信を行う Adapter
    NDIS_PACKET           *RecvPacket   = NULL;
    NDIS_BUFFER           *NdisBuffer;
    UINT                   NdisBufferLen;
    VOID                  *IrpRecvBuffer;    // IRP の buffer 
    ULONG                  IrpRecvBufferLen; // IRP の buffer の長さ
    VOID                  *VirtualAddress = NULL; // IRP の Buffer のデータのアドレス
    
    DEBUG_PRINT0(3, "SteDispatchWrite called\n");        

    // IRP から IO_STACK_LOCATION を得る
    irpStack = IoGetCurrentIrpStackLocation(Irp);

    // IRP から受け取ったデータと、データ長を得る
    IrpRecvBuffer = Irp->AssociatedIrp.SystemBuffer;
    IrpRecvBufferLen = irpStack->Parameters.Write.Length;

    //
    // 途中で break できるようにするためだけの do-while 文
    //
    do {
        //
        // IRP のバッファーをチェック
        //
        if(IrpRecvBufferLen == 0 ){
            DEBUG_PRINT0(2, "SteDispatchWrite: IrpRecvBufferLen == 0\n");
            Status = NDIS_STATUS_FAILURE;
            break;
        }
        if( IrpRecvBuffer == NULL){
            DEBUG_PRINT0(2, "SteDispatchWrite: IrpRecvBuffer is NULL\n");
            Status = NDIS_STATUS_FAILURE;
            break;
        }
        
        //
        // グローバルロックを取得し、Adapter のリストから 引数で
        // 渡された DeviceObject を含む Adapter を見つける
        //
        NdisAcquireSpinLock(&SteGlobalSpinLock);
        Status =  SteFindAdapterByDeviceObject(DeviceObject, &Adapter);
        NdisReleaseSpinLock(&SteGlobalSpinLock);
        
        if ( Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT0(1, "SteDispatchWrite: Can't find Adapter\n");
            break;
        }

        //
        // Debug 用。 Buffer のフラグをチェック
        //
        if( DebugLevel >= 3) {
            DEBUG_PRINT0(3, "SteDispatchWrite: ");
            if ( Irp->Flags & DO_BUFFERED_IO){
                DEBUG_PRINT1(3, "Irp->Flags(0x%x)== DO_BUFFERED_IO\n",Irp->Flags);
            } else if ( Irp->Flags & DO_DIRECT_IO){
                DEBUG_PRINT1(3, "Irp->Flags(0x%x)== DO_DIRECT_IO\n",Irp->Flags);
            } else {
                DEBUG_PRINT1(3, "Irp->Flags(0x%x)n",Irp->Flags);
            }
        }
        
        DEBUG_PRINT1(3, "SteDispatchWrite: IrpRecvBufferLen = %d\n",IrpRecvBufferLen);
        
        //
        //  アダプター毎のロックを取得し、
        //  パケットプールから受信用のパケットを確保する
        //
        NdisAcquireSpinLock(&Adapter->AdapterSpinLock);                        
        Status = SteAllocateRecvPacket(Adapter, &RecvPacket);
        NdisReleaseSpinLock(&Adapter->AdapterSpinLock);
        
        if(Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT0(2, "SteDispatchWrite: SteAllocateRecvPacket failed\n");
            Adapter->NoResources++;
            break;
        }
        
        DEBUG_PRINT1(3, "SteDispatchWrite: RecvPacket = %p\n", RecvPacket);        


        // Packet の Status を NDIS_STATUS_SUCCESS にセット
        NDIS_SET_PACKET_STATUS(RecvPacket, NDIS_STATUS_SUCCESS);

        //
        // 確保した Packet 内の NDIS_BUFFER 構造体のポインタとイーサネット
        // フレームを格納するためのメモリのアドレスを得る
        //
        NdisQueryPacket(
            RecvPacket,           //IN  PNDIS_PACKET   
            NULL,                 //OUT PUINT OPTIONAL
            NULL,                 //OUT PUINT         
            &NdisBuffer,          //OUT PNDIS_BUFFER *
            NULL                  //OUT PUINT         
            );

        // Debug 用
        if(NdisBuffer == NULL){
            DEBUG_PRINT0(2, "SteDispatchWrite: NdisBuffer is NULL.\n");
            Status = NDIS_STATUS_FAILURE;
            break;
        } else {
            DEBUG_PRINT1(3, "SteDispatchWrite: NdisBuffer is %p\n", NdisBuffer);
        }
            
        NdisQueryBufferSafe(
            NdisBuffer,           //IN PNDIS_BUFFER    
            &VirtualAddress,      //OUT PVOID *
            &NdisBufferLen,       //OUT PUINT          
            NormalPagePriority    //IN MM_PAGE_PRIORITY
            );

        // NdisBuffer には ETHERMAX(1514bytes)以上はコピーできない
        if ( IrpRecvBufferLen > ETHERMAX){
            IrpRecvBufferLen = ETHERMAX;
        }

        //
        // IRP の内のデータを Packet にコピーする。
        //
        NdisMoveMemory(VirtualAddress, IrpRecvBuffer, IrpRecvBufferLen);

        // 実際に書き込んだサイズを Irp にセット 
        Irp->IoStatus.Information = IrpRecvBufferLen;
            
        // Buffer の Next ポインターを NULL へ。
        NdisBuffer->Next = NULL;
        // BufferLength をセット
        NdisAdjustBufferLength(NdisBuffer, IrpRecvBufferLen);
        
        DEBUG_PRINT0(3, "SteDispatchWrite: Copying buffer data Completed\n");            

        //
        // TODO:
        // パケットフィルターのチェックできるようにする。
        // 今はなんでも（たとえ自分宛じゃなくても）上に上げる。
        //

        // コピーしたパケットの中身を表示
        if(DebugLevel >= 3)
            StePrintPacket(RecvPacket);        

        //   
        // パケットを受信キューにキューイング
        //
        Status = StePutQueue(&Adapter->RecvQueue, RecvPacket);
        if(Status != NDIS_STATUS_SUCCESS){            
            // 受信キューが一杯のようだ。
            DEBUG_PRINT0(2, "SteDispatchWrite: Receive Queue is Full.\n");
            break;
        }        
        
        NdisInterlockedIncrement(&Adapter->RecvIndicatedPackets);
        Adapter->Ipackets++;
        DEBUG_PRINT0(3, "SteDispatchWrite: StePutQueue Completed\n");
        //
        // SteRecvTimerFunc() を呼び出すためのタイマーをセット
        //
        NdisSetTimer(&Adapter->RecvTimer, 0);

        DEBUG_PRINT0(3, "SteDispatchWrite: NdisSetTimer Completed\n");  
    } while (FALSE);

    if(Status != NDIS_STATUS_SUCCESS){
        NtStatus = STATUS_UNSUCCESSFUL;
        Adapter->Ierrors++;
        // 書き込みサイズを 0 にセット
        Irp->IoStatus.Information = 0;
        // Packet を取得済みの場合は開放する。
        if (RecvPacket != NULL){
            SteFreeRecvPacket(Adapter, RecvPacket);
        }
    }

    Irp->IoStatus.Status = NtStatus; 
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return(NtStatus);
 }  

/***********************************************************************
 * SteDispatchIoctl()
 *
 *    このデバイスに送られた IOCTL コマンドを処理する
 *
 * 引数:
 *
 *    DeviceObject :  デバイスオブジェクトへのポインタ
 *    Irp          :  I/O リクエストパケットへのポインタ(I/O Request Packt)
 *
 * 返り値:
 *
 *    成功時 : STATUS_SUCCESS　
 *    失敗時 : STATUS_UNSUCCESSFUL
 *
 *********************************************************************/
NTSTATUS
SteDispatchIoctl(
    IN PDEVICE_OBJECT           DeviceObject,
    IN PIRP                     Irp
    )
{

    NTSTATUS             NtStatus = STATUS_SUCCESS;
    NDIS_STATUS          Status   = NDIS_STATUS_SUCCESS;
    STE_ADAPTER         *Adapter  = NULL; // Packet の送受信を行う Adapter     
    IO_STACK_LOCATION   *irpStack = NULL;
    HANDLE               hEvent;          // sted.exe から渡されるイベントハンドル

    DEBUG_PRINT0(3, "SteDispatchIoctl called\n");
        
    // IRP から IO_STACK_LOCATION を得る
    irpStack = IoGetCurrentIrpStackLocation(Irp);

    // break するためだけの do-while 文
    do {
    
        //
        // グローバルロックを取得し、Adapter のリストから 引数で
        // 渡された DeviceObject を含む Adapter を見つける
        //
        NdisAcquireSpinLock(&SteGlobalSpinLock);
        Status = SteFindAdapterByDeviceObject(DeviceObject, &Adapter);
        NdisReleaseSpinLock(&SteGlobalSpinLock);    

        if ( Status != NDIS_STATUS_SUCCESS){
            DEBUG_PRINT0(1, "SteDispatchIoctl: Can't find Adapter\n");
            break;
        }    

        switch (irpStack->Parameters.DeviceIoControl.IoControlCode) {
            //
            // 仮想 NIC デーモンからのイベントオブジェクトを受け取り
            // アダプター構造体に保存する。
            //
            case REGSVC:
                DEBUG_PRINT0(3, "SteDispatchIoctl: Received REGSVC IOCTL\n");                  
                hEvent = (HANDLE) irpStack->Parameters.DeviceIoControl.Type3InputBuffer;

                if(hEvent == NULL){
                    DEBUG_PRINT0(1, "SteDispatchIoctl: hEvent is NULL\n");
                    Status = NDIS_STATUS_FAILURE;
                    break;
                }
                DEBUG_PRINT1(3,"SteDispatchIoctl: hEvent = 0x%p\n", hEvent);
                DEBUG_PRINT1(3,"SteDispatchIoctl: Adapter = 0x%p\n", Adapter);            
                DEBUG_PRINT1(3,"SteDispatchIoctl: &Adapter->EventObject = 0x%p\n",
                               &Adapter->EventObject);

                //
                // STE_ADAPTER 構造体の EventObject を変更するので、
                // アダプター毎のロックを取得する
                //
                NdisAcquireSpinLock(&Adapter->AdapterSpinLock);                
                NtStatus = ObReferenceObjectByHandle(
                    hEvent,                 //IN HANDLE                       
                    GENERIC_ALL,            //IN ACCESS_MASK                  
                    NULL,                   //IN POBJECT_TYPE                 
                    KernelMode,             //IN KPROCESSOR_MODE              
                    &Adapter->EventObject,  //OUT PVOID  *                    
                    NULL                    //OUT POBJECT_HANDLE_INFORMATION  
                    );

                NdisReleaseSpinLock(&Adapter->AdapterSpinLock);
            
                if(NtStatus != STATUS_SUCCESS){
                    DEBUG_PRINT1(1, "SteDispatchIoctl: ObReferenceObjectByHandle failed(%d) ",
                                    NtStatus);
                    switch(NtStatus){
                        case STATUS_OBJECT_TYPE_MISMATCH:
                            DEBUG_PRINT0(1," Failed STATUS_OBJECT_TYPE_MISMATCH\n");
                            break;
                        case STATUS_ACCESS_DENIED:
                            DEBUG_PRINT0(1," Failed STATUS_ACCESS_DENIED\n");
                            break;
                        case STATUS_INVALID_HANDLE:
                            DEBUG_PRINT0(1," Failed STATUS_INVALID_HANDLE\n");
                            break;
                        default:
                            DEBUG_PRINT1(1," Failed (0x%x)\n", NtStatus);
                            break;                        
                    }
                    Status = NDIS_STATUS_FAILURE;
                    break;
                }
                break;
            //
            // イベントオブジェクトの登録解除する
            //
            case UNREGSVC:
                DEBUG_PRINT0(3, "SteDispatchIoctl: Received UNREGSVC IOCTL\n");
                NdisAcquireSpinLock(&Adapter->AdapterSpinLock);                                
                if(Adapter->EventObject){
                    ObDereferenceObject(Adapter->EventObject);
                    Adapter->EventObject = NULL;
                }
                NdisReleaseSpinLock(&Adapter->AdapterSpinLock);                
                break;
            default:
                DEBUG_PRINT1(3, "SteDispatchIoctl: Received Unknown(0x%x)IOCTL\n",
                                irpStack->Parameters.DeviceIoControl.IoControlCode);             
                NtStatus = STATUS_UNSUCCESSFUL;
                break;
        }
    } while(FALSE);

    if(Status != NDIS_STATUS_SUCCESS){
        // エラーの場合
        NtStatus = STATUS_UNSUCCESSFUL;
    }    
    Irp->IoStatus.Information = 0;    
    Irp->IoStatus.Status = NtStatus; 
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    
    return(NtStatus);
}

/***********************************************************************
 * SteDispatchOther()
 *
 *    ReadFile, WriteFile, IOCTL 以外の I/O リクエストを処理する。
 *
 * 引数:
 *
 *    DeviceObject :  デバイスオブジェクトへのポインタ
 *    Irp          :  I/O リクエストパケットへのポインタ(I/O Request Packt)
 *
 * 返り値:
 *
 *    常に　STATUS_SUCCESS
 *
 *********************************************************************/
NTSTATUS
SteDispatchOther(
    IN PDEVICE_OBJECT           DeviceObject,
    IN PIRP                     Irp
    )
{
    NTSTATUS             NtStatus = STATUS_SUCCESS;
    IO_STACK_LOCATION   *irpStack = NULL;

    DEBUG_PRINT0(3, "SteDispatchOther called\n");

    /* IRP から IO_STACK_LOCATION を得る*/
    irpStack = IoGetCurrentIrpStackLocation(Irp);

    DEBUG_PRINT0(3, "SteDispatchOther: MajorFunction = ");                    
    switch (irpStack->MajorFunction){
        case IRP_MJ_CREATE:
            DEBUG_PRINT0(3, "IRP_MJ_CREATE\n");                
            break;
        case IRP_MJ_CLEANUP:
            DEBUG_PRINT0(3, "IRP_MJ_CLEANUP\n");            
            break;
        case IRP_MJ_CLOSE:
            DEBUG_PRINT0(3, "IRP_MJ_CLOSE\n");            
            break;
        default:
            DEBUG_PRINT1(3, "0x%x (See ndis.h)\n", irpStack->MajorFunction);
            break;
    }

    Irp->IoStatus.Status = NtStatus;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return(NtStatus);
}


/***********************************************************************
 * SteAllocateRecvPacket()
 *
 *    受信パケットプールからパケットを確保する。
 *
 * 引数:
 *
 *        Adapter : STE_ADAPTER 構造体のポインタ
 *        Packet  : NIDS_PACKET 構造体のポインタのアドレス
 *
 * 返り値:
 *
 *    正常時   : NDIS_STATUS_SUCCESS
 *    エラー時 : NDIS_STATUS_RESOURCES
 *
 *********************************************************************/
NDIS_STATUS
SteAllocateRecvPacket(
    IN STE_ADAPTER      *Adapter,
    IN OUT NDIS_PACKET  **pPacket
    )
{
    NDIS_STATUS       Status;    
    NDIS_BUFFER      *Buffer   = NULL;
    PVOID             VirtualAddress = NULL;

    DEBUG_PRINT0(3, "SteAllocateRecvPacket called\n");                

    *pPacket = NULL;
    
    //
    // 各パケットにバッファーを関連付け、また、確保した
    // 各受信ディスクリプタにパケットを関連付ける。
    //
    do {
        NdisAllocatePacket(
            &Status,                     //OUT PNDIS_STATUS  
            pPacket,                     //OUT PNDIS_PACKET  
            Adapter->RxPacketPoolHandle  //IN NDIS_HANDLE    
            );
        if (Status != NDIS_STATUS_SUCCESS)
            break;

        NDIS_SET_PACKET_HEADER_SIZE(*pPacket, ETHERMAX-ETHERMTU);        
        
        //
        // Ethernet フレームの格納用メモリを確保
        //
        Status = NdisAllocateMemoryWithTag(
            (PVOID)&VirtualAddress,   //OUT PVOID  *
            ETHERMAX,                 //IN UINT    
            (ULONG)'AlcE'             //IN ULONG   
            );
        if (Status != NDIS_STATUS_SUCCESS){
            NdisFreePacket(*pPacket);
            break;
        }
        NdisZeroMemory(VirtualAddress, ETHERMAX);

        // バッファープールから、バッファーを取り出す
        NdisAllocateBuffer(
            &Status,                      //OUT PNDIS_STATUS  
            &Buffer,                      //OUT PNDIS_BUFFER
            Adapter->RxBufferPoolHandle,  //IN NDIS_HANDLE
            VirtualAddress,               //IN PVOID
            ETHERMAX                      //IN UINT
            );
        if (Status != NDIS_STATUS_SUCCESS){
            NdisFreePacket(*pPacket);
            NdisFreeMemory(VirtualAddress, ETHERMAX, 0);
            break;
        }
        //
        // バッファーと、パケットを関連付ける
        // 
        NdisChainBufferAtBack(*pPacket, Buffer);

    } while(FALSE);

    return(Status);
}

/***********************************************************************
 * SteFreeRecvPacket()
 *
 *    受信パケットプールにパケットをもどす
 *
 * 引数:
 *
 *        Adapter : STE_ADAPTER 構造体のポインタ
 *        Packet  : 開放する NDIS_PACKET 構造体
 *
 * 返り値:
 *
 *     NDIS_STATUS_SUCCESS
 *
 *********************************************************************/
NDIS_STATUS
SteFreeRecvPacket(
    IN STE_ADAPTER   *Adapter,
    IN NDIS_PACKET   *Packet
    )
{
    NDIS_STATUS       Status = NDIS_STATUS_SUCCESS;    
    NDIS_BUFFER      *Buffer = NULL;
    VOID             *VirtualAddress = NULL;
    UINT              Length;

    DEBUG_PRINT0(3, "SteFreeRecvPacket called\n");     

    NdisQueryPacket(
        Packet,               //IN PNDIS_PACKET   
        NULL,                 //OUT PUINT OPTIONAL
        NULL,                 //OUT PUINT         
        &Buffer,              //OUT PNDIS_BUFFER  
        NULL                  //OUT PUINT         
        );

    NdisQueryBufferSafe(
        Buffer,               //IN PNDIS_BUFFER    
        &VirtualAddress,      //OUT PVOID *
        &Length,              //OUT PUINT          
        NormalPagePriority    //IN MM_PAGE_PRIORITY
        );
        
    // Ethernet フレームの格納用メモリを開放
    NdisFreeMemory(VirtualAddress, ETHERMAX, 0);
        
    // バッファープールにバッファーを戻す
    NdisFreeBuffer(Buffer);

    // パケットプールにパケットを戻す
    NdisFreePacket(Packet);

    return(Status);
}

/***********************************************************************
 * StePutQueue()
 *
 *    パケットをキューに入れる。
 *
 * 引数:
 *
 *        Queue      : Queue 構造体のポインタ
 *        Packet     : キューに入れるパケット
 *
 * 返り値:
 *
 *     成功時 : NDIS_STATUS_SUCCESS
 *     失敗時 : NDIS_STATUS_RESOURCES
 *
 *********************************************************************/
NDIS_STATUS
StePutQueue(
    IN STE_QUEUE     *Queue,    
    IN NDIS_PACKET   *Packet
    )
{
    NDIS_STATUS      Status = NDIS_STATUS_SUCCESS;

    DEBUG_PRINT0(3, "StePutQueue called\n");         

    //
    // キューに設定している保持できるパケットの最大数を比較する。
    // Queue->QueueMax 参照時にロックはとらないのであくまで目安。
    //
    if( Queue->QueueCount >= Queue->QueueMax){
        // もう最大数分以上キューイングされてしまっているようだ。
        DEBUG_PRINT2(2, "StePutQueue: QFULL (%d >= %d)\n", Queue->QueueCount, Queue->QueueMax);
        Status = NDIS_STATUS_RESOURCES;
        DEBUG_PRINT0(3, "StePutQueue returned\n");
        return(Status);
    }

    // リストの最後に挿入
    NdisInterlockedInsertTailList(
        &Queue->List,                           //IN PLIST_ENTRY      
        (PLIST_ENTRY)&Packet->MiniportReserved, //IN PLIST_ENTRY
        &Queue->SpinLock                        //IN PNDIS_SPIN_LOCK  
        );

   // キューのカウント値を増やす
    NdisInterlockedIncrement(&Queue->QueueCount);
    DEBUG_PRINT1(3, "StePutQueue: QueueCount = %d\n", Queue->QueueCount);    

    DEBUG_PRINT0(3, "StePutQueue returned\n");
    return(Status);
}

/***********************************************************************
 * SteGetQueue()
 *
 *    キューからパケットを取り出す
 *
 * 引数:
 *
 *        Queue      : Queue 構造体のポインタ
 *        Packet     : キューから取り出すパケットのポインタのアドレス
 *
 * 返り値:
 *
 *     成功時 : NDIS_STATUS_SUCCESS
 *     失敗時 : NDIS_STATUS_RESOURCES
 *
 *********************************************************************/
NDIS_STATUS
SteGetQueue(
    IN STE_QUEUE         *Queue,    
    IN OUT NDIS_PACKET   **pPacket
    )
{
    NDIS_STATUS       Status = NDIS_STATUS_SUCCESS;
    LIST_ENTRY       *ListEntry;
    NDIS_SPIN_LOCK   *SpinLock;

    DEBUG_PRINT0(3, "SteGetQueue called\n");             

    ListEntry = NdisInterlockedRemoveHeadList(
        &Queue->List,          //IN PLIST_ENTRY      
        &Queue->SpinLock       //IN PNDIS_SPIN_LOCK  
        );

    if (ListEntry == NULL){
        // Packet はキューイングされていない
        DEBUG_PRINT0(2, "SteGetQueue: No queued packet\n");
        Status = NDIS_STATUS_RESOURCES;
        *pPacket = NULL;
        return(Status);
    }
    
    *pPacket = CONTAINING_RECORD(ListEntry, NDIS_PACKET, MiniportReserved);

    // キューのカウント値を減らす
    NdisInterlockedDecrement(&Queue->QueueCount);

    return(Status);
}


/*****************************************************************************
 * SteInsertAdapterToList()
 *
 * STE_ADAPTER 構造体をアダプターのリンクリストに追加する。
 *
 *  引数：
 *          Adapter : STE_ADAPTER 構造体
 * 
 * 戻り値：
 *          成功: NDIS_STATUS_SUCCESS
 *          失敗: 
 *****************************************************************************/
NDIS_STATUS
SteInsertAdapterToList(
    IN STE_ADAPTER  *Adapter
    )
{
    NDIS_STATUS    Status = NDIS_STATUS_SUCCESS;
    STE_ADAPTER   *pAdapter;
    ULONG          Instance = 0;

    DEBUG_PRINT0(3, "SteInsertAdapterToList called\n");

    // SteCreateAdapter にて NULL に初期化されているはずだが念のため
    Adapter->NextAdapter = NULL;

    // break するためだけの do-while 文
    do {
        if (SteAdapterHead == NULL){
            // 最初のアダプターなので、インスタンス番号は 0。
            Adapter->Instance = Instance; 
            SteAdapterHead = Adapter;
            break;
        }
        //
        // 登録されている最後のアダプターを探し、新しいアダプターの
        // インスタンス番号を決定する。
        //
        pAdapter = SteAdapterHead;
        while(pAdapter->NextAdapter != NULL){
            pAdapter = pAdapter->NextAdapter;
        }
        // インスタンス番号は最後のアダプターの次の数字
        Adapter->Instance = pAdapter->Instance + 1;
        // 最大インスタンス数は 10。なので、インスタンス番号は 9 以上は取れない・・
        if(Adapter->Instance > STE_MAX_ADAPTERS - 1){
            Status = NDIS_STATUS_RESOURCES;
            break;
        }
        // アダプターリストの最後に追加 
        pAdapter->NextAdapter = Adapter;
    } while(FALSE);
    
    DEBUG_PRINT1(3, "SteInsertAdapterToList: New Adapter's instance is %d\n", Adapter->Instance);

    return(Status);
}

/*****************************************************************************
 * SteRemoveAdapterFromList()
 *
 * STE_ADAPTER 構造体をアダプターのリンクリストから取り除く
 *
 *  引数：
 *          Adapter : STE_ADAPTER 構造体
 * 
 * 戻り値：
 *          成功: NDIS_STATUS_SUCCESS
 *          失敗:  
 *****************************************************************************/
NDIS_STATUS
SteRemoveAdapterFromList(
    IN STE_ADAPTER    *Adapter
    )
{  
    NDIS_STATUS    Status = NDIS_STATUS_SUCCESS;
    STE_ADAPTER   *pAdapter, *PreviousAdapter;

    DEBUG_PRINT0(3, "SteRemoveAdapterFromList called\n");

    if (SteAdapterHead == NULL){
        // まだひとつもアダプタが登録されていない
        DEBUG_PRINT0(1, "SteRemoveAdapterFromList: No Adapter exist\n");
        return(NDIS_STATUS_FAILURE);
    }

    pAdapter = SteAdapterHead;
    PreviousAdapter = (STE_ADAPTER *)NULL;
    do{
        if (pAdapter == Adapter){
            if (PreviousAdapter == NULL)
                SteAdapterHead = (STE_ADAPTER *)NULL;
            else
                PreviousAdapter->NextAdapter = pAdapter->NextAdapter;

            return(Status);
        }
        PreviousAdapter = pAdapter;
        pAdapter = pAdapter->NextAdapter;
    } while(pAdapter);

    // 該当するアダプタが存在しなかった
    DEBUG_PRINT0(1, "SteRemoveAdapterFromList: Adapter not found\n");            
    return(NDIS_STATUS_FAILURE);
}

/*****************************************************************************
 * SteFindAdapterByDeviceObject()
 *
 *  与えられたデバイスオブジェクトからアダプターを見つける
 *
 *  引数：
 *          DeviceObject: デバイスオブジェクトのポインタ
 *          Adapter     : 見つかった STE_ADAPTER 構造体のポインタ
 * 
 * 戻り値：
 *          成功: NDIS_STATUS_SUCCESS
 *          失敗: NDIS_STATUS_FAILURE
 * 
 *****************************************************************************/
NDIS_STATUS
SteFindAdapterByDeviceObject(
    IN      DEVICE_OBJECT    *DeviceObject,
    IN OUT  STE_ADAPTER      **pAdapter
    )
{ 
    NDIS_STATUS    Status = NDIS_STATUS_SUCCESS;
    STE_ADAPTER   *TempAdapter;

    DEBUG_PRINT0(3, "SteFindAdapterByDeviceObject called\n");
    
    if (SteAdapterHead == NULL){
        // まだひとつもアダプタが登録されていない
        DEBUG_PRINT0(1, "SteFindAdapterByDeviceObject: No Adapter exist\n");
        return(NDIS_STATUS_FAILURE);
    }

    TempAdapter = SteAdapterHead;    

    while (TempAdapter){
        if (TempAdapter->DeviceObject  == DeviceObject){
            *pAdapter = TempAdapter;
            return(Status);
        }
        TempAdapter = TempAdapter->NextAdapter;
    }

    DEBUG_PRINT0(1, "SteFindAdapterByDeviceObject: No Adapter exist\n");            
    // 該当するアダプタが存在しなかった
    return(NDIS_STATUS_FAILURE);
}

/********************************************************************
 * SteCopyPacketToIrp()
 * 
 *    引数として渡された Packet のデータを、同じく引数としてわたされた
 *    IRP にコピーする。
 *        
 * 引数:
 *
 *     Packet    : パケット
 *     Irp       : 仮想 NIC デーモンから受け取った IRP
 *
 * 返り値:
 *
 *    NDIS_STATUS
 *
 ********************************************************************/
NDIS_STATUS
SteCopyPacketToIrp(    
    IN     NDIS_PACKET  *Packet,
    IN OUT PIRP          Irp
    )
{
    NDIS_STATUS         Status = NDIS_STATUS_SUCCESS;
    PIO_STACK_LOCATION  irpStack;        
    UINT                IrpBufferSize; // 元々の IRP のバッファーのサイズ
    UINT                IrpBufferLeft; // コピー可能な残りの IRP バッファーのサイズ
    UCHAR              *IrpBuffer;
    NDIS_BUFFER        *NdisBuffer;
    UINT                TotalPacketLength;
    UINT                CopiedLength = 0;
    VOID               *VirtualAddress;    
    UINT                Length;
    
    DEBUG_PRINT0(3, "SteCopyPacketToIrp called\n");                

    /* IRP から IO_STACK_LOCATION を得る*/
    irpStack = IoGetCurrentIrpStackLocation(Irp);

    /* IRP から受信したバッファーと、バッファーサイズを得る */
    IrpBuffer = Irp->AssociatedIrp.SystemBuffer;
    IrpBufferLeft = IrpBufferSize = irpStack->Parameters.Read.Length;

    DEBUG_PRINT1(3, "SteCopyPacketToIrp: IrpBufferSize = %d\n", IrpBufferSize);
    // IRP のバッファーサイズをチェック
    if(IrpBufferSize < ETHERMIN){
        DEBUG_PRINT0(1, "SteCopyPacketToIrp: IRP buffer size too SMALL < ETHERMIN\n");
        Status = NDIS_STATUS_FAILURE;
        Irp->IoStatus.Information  = 0;        
        return(Status);
    }     
    
    if(IrpBuffer == NULL){
        DEBUG_PRINT0(1, "SteCopyPacketToIrp: Irp->AssociatedIrp.SystemBuffer is NULL\n");
        Status = NDIS_STATUS_FAILURE;
        Irp->IoStatus.Information  = 0;        
        return(Status);
    }

    NdisQueryPacket(
        Packet,              //IN PNDIS_PACKET           
        NULL,                //OUT PUINT OPTIONAL        
        NULL,                //OUT PUINT OPTIONAL        
        &NdisBuffer,         //OUT PNDIS_BUFFER OPTIONAL 
        &TotalPacketLength   //OUT PUINT OPTIONAL        
        );

    if( TotalPacketLength > ETHERMAX){
        // 大きすぎる。ジャンボフレーム？
        DEBUG_PRINT0(1, "SteCopyPacketToIrp: Packet size too BIG > ETHERMAX\n");
        Status = NDIS_STATUS_FAILURE;
        Irp->IoStatus.Information  = 0;        
        return(Status);
    }

    while(NdisBuffer != NULL) {
        NdisQueryBufferSafe(
            NdisBuffer,        //IN PNDIS_BUFFER      
            &VirtualAddress,   //OUT PVOID * OPTIONAL  
            &Length,           //OUT PUINT            
            NormalPagePriority //IN MM_PAGE_PRIORITY
            );

        //
        // 残りの IRP バッファー量をチェック。
        // もし仮想 NIC デーモンが ReadFile() にて ETHERMAX 未満のバッファーサイズ
        // を指定してきた場合は、IRP のバッファーが足りなくなる可能性がある。
        //
        if (Length > IrpBufferLeft){
            //
            // 中途半端な Ethernet フレームをコピーしても仕方が無いので、エラー
            // としてリターンする。
            //
            Status = NDIS_STATUS_FAILURE;
            DEBUG_PRINT0(1, "SteCopyPacketToIrp: Insufficient IRP buffer size\n");            
            break;            
        }
  
        if(VirtualAddress == NULL){
            // NDIS バッファーにデータが含まれていない？
            Status = NDIS_STATUS_FAILURE;
            DEBUG_PRINT0(1, "SteCopyPacketToIrp: NdisQueryBuffer: VirtualAddress is NULL\n");
            break;
        }

        // データをコピーする
        NdisMoveMemory(IrpBuffer, VirtualAddress, Length);
        CopiedLength  += Length;
        IrpBufferLeft -= Length;
        IrpBuffer     += Length;

        // 残りのバッファー量をチェック。もし空ならここでブレイク。
        if(IrpBufferLeft == 0){
            break;
        }

        // 次の NDIS バッファーを得る。
        NdisGetNextBuffer(
            NdisBuffer,  //IN PNDIS_BUFFER   
            &NdisBuffer  //OUT PNDIS_BUFFER  
            );
    }

    if(Status != NDIS_STATUS_SUCCESS) {
        // エラーが発生したようなので Irp->IoStatus.Information に 0 をセット        
        Irp->IoStatus.Information  = 0;
    } else {
        // コピーしたサイズをチェック
        if(CopiedLength < ETHERMIN){
            // 小さい。パディングする。
            // IRP バッファーが ETHERMIN 以上であることは確認済みなので、ここで
            // コピー済みサイズを ETHERMIN に拡大しても問題は無い。
            DEBUG_PRINT0(2, "SteCopyPacketToIrp: Packet size is < ETHERMIN. Padded.\n"); 
            CopiedLength = ETHERMIN;   
        } 
        Irp->IoStatus.Information  = CopiedLength;        
    }

    return(Status);
}


/********************************************************************
 * StePrintPacket()
 * 
 *    引数として渡された Packet のデータをdebug 出力する
 *        
 * 引数:
 *
 *     Packet    - 表示する Packet
 *
 * 返り値:
 *
 *    成功時: TRUE
 *    失敗時: FALSE
 *
 ********************************************************************/
VOID
StePrintPacket(
    NDIS_PACKET *Packet)
{
    
    NDIS_BUFFER       *Ndis_Buffer;  
    PVOID              VirtualAddress; 
    UINT               NdisBufferLen;  
    UINT               TotalPacketLength;   
    UINT               PrintOffset = 0;
    UCHAR             *Data;           
    
    DEBUG_PRINT0(3, "StePrintPacket called\n");

    NdisQueryPacket(
        Packet,             //IN  PNDIS_PACKET  
        NULL,               //OUT PUINT OPTIONAL
        NULL,               //OUT PUINT         
        &Ndis_Buffer,       //OUT PNDIS_BUFFER *
        &TotalPacketLength  //OUT PUINT         
        );

    while(Ndis_Buffer){
        UINT i = 0;
            
        NdisQueryBufferSafe(
            Ndis_Buffer,
            &VirtualAddress,
            &NdisBufferLen,
            NormalPagePriority);
        
        if(VirtualAddress == NULL){
            DEBUG_PRINT0(1, "StePrintPacket: VirtuallAddress is NULL\n");            
            break;
        }

        Data = (unsigned char *)VirtualAddress;

        /*
         * debug レベルが 4 未満の場合は 14byte
         * （Ethernet Header 分）だけ出力する。
         */
        if(DebugLevel < 4){
            NdisBufferLen = 14;
        }
        
        while(NdisBufferLen){
            if( PrintOffset%16 == 0){
                if(PrintOffset != 0)
                    DbgPrint ("\n");
                DbgPrint("0x%.4x: ", PrintOffset);
            }
            DbgPrint("%.2x ", Data[i]);
            PrintOffset++;
            
            NdisBufferLen--;
            i++;
        }
        NdisGetNextBuffer( Ndis_Buffer, &Ndis_Buffer);
    }
    DbgPrint("\n");
    DEBUG_PRINT0(3, "StePrintPacket end\n");

    return;
}

/*****************************************************************************
 * bebug_print()
 *
 * デバッグ出力用関数
 *
 *  引数：
 *           Level  :  エラーの深刻度。
 *           Format :  メッセージの出力フォーマット
 *           ...
 * 戻り値：
 *           なし。
 *****************************************************************************/
VOID
SteDebugPrint(
    IN ULONG   Level,
    IN PCCHAR  Format,
    ...
    )
{
    va_list        list;
    UCHAR          Buffer[STE_MAX_DEBUG_MSG];
    NTSTATUS       NtStatus;
    
    va_start(list, Format);
    NtStatus = RtlStringCbVPrintfA(Buffer, sizeof(Buffer), Format, list);
    if(NtStatus != STATUS_SUCCESS) {
        DbgPrint("Ste: SteDebugPrint Failed.\n");
        return;
    }
    va_end(list);

    if (Level <= DebugLevel) {
         DbgPrint ("Ste: %s", Buffer);
    }        
    return;
}
