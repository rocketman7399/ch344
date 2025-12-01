//Copyright (c) 1999-2023  WCH Corporation
//Module Name:DbgFunc.h
/*
Abstract:
    Function for device enum
Environment:
    user mode only
Notes:
  Copyright (c) 1999-2023 WCH Corporation.  All Rights Reserved.
Revision History:
  6/5/2012: TECH30 create
--*/

#ifndef		_DEVICEENUM_H
#define		_DEVICEENUM_H

#ifdef __cplusplus
extern "C" {
#endif

#define EnumComBusType_ALL      0
#define EnumComBusType_USB_WCH  1
#define EnumComBusType_PCIE_WCH 2
#define EnumComBusType_BLE_WCH  3

#define PDO_MAX_CNT 64
#define FDO_MAX_CNT 64

typedef struct _SerObjS{
	CHAR    PortName[64];
	ULONG   PortNum;
	UCHAR   PortI;     //多串序号
	DEVINST DevInst;  
	DEVINST PdoInst;   //单串此值为0
	CHAR    HwId[256];
	ULONG   HwIdLen;
	CHAR    FriendlyName[128];
	CHAR    LocaInfor[128];
	CHAR    DevDesc[256];
	BOOL    IsUsbCdcMode;
	BOOL    IsSinglePort;
	CHAR    ObjName[64];
	SP_DEVINFO_DATA DeviceInfoData;
	HDEVINFO DeviceInfoSet;
	CHAR   Mfg[64];
}SerObjS,*pSerObjS;

typedef struct _MultiSerObjS{
	UCHAR   PortCnt;    //=1,为单串口
	DEVINST DevInst;	
	SerObjS FdoSer[FDO_MAX_CNT];	//单串口信息在[0]内
	CHAR    HwId[256];
	CHAR    DevDesc[256];
	BOOL    IsIAPMode;
	CHAR    LocaInfor[128];
	BOOL    IsSinglePort;
	SP_DEVINFO_DATA DeviceInfoData;
	HDEVINFO DeviceInfoSet;
	ULONG   HubPortIndex;//高16位为HUB位置，低16位为HUB PORT号
	CHAR    ServiceN[64]; //服务值
	USHORT  PathChain[10]; //设备节点路径
	CHAR    FullPathChainStr[64];
	CHAR    PathChainStr[32];
	UCHAR   PathChainLen;  //节点个数
}MultiSerObjS,*pMultiSerObjS;

//初始化设备管理库
BOOL WINAPI InitDevManagerLib();
//释放设备管理库资源
BOOL WINAPI DeinitDevManagerLib();
//枚举串口
/*ComPortBusType定义
  #define EnumComBusType_ALL      0
  #define EnumComBusType_USB_WCH  1
  #define EnumComBusType_PCIE_WCH 2
  #define EnumComBusType_BLE_WCH  3
ShowOrder =0:按HUB位置从小到大排列,=1:按转串数量从小到大排列
ObjMaxCnt = 512,MultiSerObjS *MultiSerObj[512]
*/
ULONG WINAPI EnumComPortDev(ULONG ComPortBusType,UCHAR ShowOrder,MultiSerObjS *MultiSerObj,ULONG ObjMaxCnt);
//改变设备状态：使能或禁用
BOOL WINAPI ChangeDeviceStatus(HDEVINFO DeviceInfoSet,SP_DEVINFO_DATA *DeviceInfoData,BOOL IsEnable,ULONG Timeout);
BOOL WINAPI ModifyCOMName(SerObjS *COMPortData,ULONG NewComNum); //更改指定串口的串口号

#ifdef __cplusplus
}
#endif

#endif