
#include "stdafx.h"
// 开启 Windows 视觉样式 (圆角按钮、现代滚动条等)
#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#include "resource.h"
#include <stdio.h>
#include <string>
#include "CH343PT.H"

#pragma comment (lib,"CH343PT")

// -------------------------------------------------------------------------
// 全局变量
// -------------------------------------------------------------------------
HANDLE AfxPortH = INVALID_HANDLE_VALUE;
HWND AfxWndHwnd = NULL;
ChipPropertyS AfxUsbSer = { 0 };
BOOL IsAutoOpen = TRUE; // 默认开启插拔自动连接

// -------------------------------------------------------------------------
// 核心映射配置 (顺序: J3, J4, J5, J7, J6)
// -------------------------------------------------------------------------
// 状态追踪
bool g_ChState[5] = { false, false, false, false, false };

// 按钮 ID 数组 (界面从上到下的顺序)
const int g_BtnIDs[5] = {
	IDC_BTN_J3,
	IDC_BTN_J4, 
	IDC_BTN_J5,
	IDC_BTN_J7,
	IDC_BTN_J6   
};

// 按钮显示标签
const char* g_BtnLabels[5] = { "J3", "J4", "J5", "J7", "J6" };

// GPIO 起始引脚 (对应上面的顺序)
// J3=0, J4=12, J5=3, J7=9, J6=6
const int g_BasePins[5] = { 0, 12, 3, 9, 6 };

// -------------------------------------------------------------------------
// 函数声明
// -------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
VOID CH341SerNotifyFunc(LONG iDevIndexAndEvent);
VOID CALLBACK CH341PT_NOTIFY_ROUTINE(LONG iDevIndexAndEvent);
BOOL OpenPort();
VOID ClosePort();
VOID UpdateSelection();
void SmartSearchAndConnect(HWND hWnd, BOOL bAutoOpen);

// -------------------------------------------------------------------------
// 辅助功能函数
// -------------------------------------------------------------------------

// 调试输出
VOID DbgPrint(LPCTSTR lpFormat, ...)
{
	CHAR TextBufferTmp[1024] = "";
	va_list arglist;
	va_start(arglist, lpFormat);
	vsprintf(TextBufferTmp, lpFormat, arglist);
	va_end(arglist);
	OutputDebugString(TextBufferTmp);
}

// 核心控制函数 (v6: 5V 保持 High)
void SetGroupMode(HANDLE hPort, pChipPropertyS pChip, int basePin, bool isFast)
{
	if (hPort == INVALID_HANDLE_VALUE) return;

	// 1. 定义引脚掩码
	ULONG Mask_5V = (1 << basePin);
	ULONG Mask_SEL = (1 << (basePin + 1));
	ULONG Mask_PWR = (1 << (basePin + 2));
	ULONG Mask_All = Mask_5V | Mask_SEL | Mask_PWR;

	// 2. 配置为输出
	CH910x_GpioConfig(hPort, pChip, Mask_All, Mask_All, Mask_All);

	// 3. 断电初始化 (PWR=0, 5V=1)
	CH910x_GpioSet(hPort, pChip, Mask_PWR | Mask_5V, Mask_5V);

	// 4. 断电等待 (保持 500ms)
	Sleep(500);

	// 5. 【切换模式】 
	ULONG targetVal = 0;
	if (isFast) {
		// 快充: SEL=0, 5V=1
		targetVal = Mask_5V;
	}
	else {
		// 数据: SEL=1, 5V=1
		targetVal = Mask_SEL | Mask_5V;
	}
	CH910x_GpioSet(hPort, pChip, Mask_SEL | Mask_5V, targetVal);

	// 等待 SEL 信号电平完全稳定（特别是从 0 变 1 时）
	// 防止 SEL 还没升上去，PWR 就通电了
	Sleep(100);

	// 6. 上电 (PWR=1, 5V=1)
	CH910x_GpioSet(hPort, pChip, Mask_PWR | Mask_5V, Mask_PWR | Mask_5V);
}

// 更新界面按钮文字
void UpdateButtonUI(HWND hWnd, int chIndex)
{
	char szText[100];
	if (g_ChState[chIndex] == true) {
		sprintf(szText, "%s: [超级快充] (点击切换)", g_BtnLabels[chIndex]);
	}
	else {
		sprintf(szText, "%s: [数据传输] (点击切换)", g_BtnLabels[chIndex]);
	}
	SetDlgItemText(hWnd, g_BtnIDs[chIndex], szText);
}

// -------------------------------------------------------------------------
// 智能搜索
// -------------------------------------------------------------------------
void SmartSearchAndConnect(HWND hWnd, BOOL bAutoOpen)
{
	SendDlgItemMessage(hWnd, IDC_PortNum, CB_RESETCONTENT, 0, 0);

	CHAR portname[20] = "";
	CHAR fullpath[32] = "";
	int foundCount = 0;
	int firstIndex = -1;

	for (int j = 1; j <= 32; j++) {
		sprintf(portname, "COM%d", j);
		sprintf(fullpath, "\\\\.\\%s", portname);

		if (CH341PtNameIsCH341((UCHAR*)fullpath)) {
			int idx = SendDlgItemMessage(hWnd, IDC_PortNum, CB_ADDSTRING, 0, (LONG)portname);
			if (foundCount == 0) firstIndex = idx;
			foundCount++;
		}
	}

	if (foundCount > 0) {
		SendDlgItemMessage(hWnd, IDC_PortNum, CB_SETCURSEL, firstIndex, 0);
		if (bAutoOpen) {
			SendMessage(hWnd, WM_COMMAND, IDC_OpenPort, 0);
		}
	}
	else {
		if (!bAutoOpen) SetWindowText(hWnd, "未发现 CH34x 设备");
	}
}
// -------------------------------------------------------------------------
// 静默执行逻辑 (供外部调用，不显示界面)
// mode: 1=快充, 0=数据
// -------------------------------------------------------------------------
void SilentRun(bool isFastMode)
{
	CHAR portname[20] = "";
	CHAR fullpath[32] = "";
	HANDLE hSilentPort = INVALID_HANDLE_VALUE;
	ChipPropertyS SilentChipInfo = { 0 };
	DCB dcb;

	// 1. 自动搜索第一个 CH34x 设备
	for (int j = 1; j <= 32; j++) {
		sprintf(portname, "COM%d", j);
		sprintf(fullpath, "\\\\.\\%s", portname);
		if (CH341PtNameIsCH341((UCHAR*)fullpath)) {
			// 找到了！尝试打开
			hSilentPort = CreateFile(fullpath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, NULL, NULL);
			if (hSilentPort != INVALID_HANDLE_VALUE) {
				break; // 打开成功，跳出循环
			}
		}
	}

	if (hSilentPort == INVALID_HANDLE_VALUE) {
		return; // 没找到设备，直接退出
	}

	// 2. 初始化配置
	if (GetCommState(hSilentPort, &dcb)) {
		SetCommState(hSilentPort, &dcb);
		CH343PT_GetChipProperty(hSilentPort, &SilentChipInfo);

		// 3. 执行 5 路控制
		// J3=0, J5=3, J6=6, J7=9, J4=12
		int pins[] = { 0, 3, 6, 9, 12 };

		for (int i = 0; i<5; i++) {
			// 调用你写好的通用控制函数
			SetGroupMode(hSilentPort, &SilentChipInfo, pins[i], isFastMode);
			Sleep(50); // 稍微间隔
		}
	}

	// 4. 关闭清理
	CloseHandle(hSilentPort);
}
// -------------------------------------------------------------------------
// 主程序入口 (支持命令行参数)
// -------------------------------------------------------------------------
int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	// 将命令行参数转换为大写，方便匹配 (如 -FAST, -fast 都可以)
	char szCmd[256];
	strncpy(szCmd, lpCmdLine, 255);
	_strupr(szCmd); // 转大写

	// --- 接口调用模式 (无界面) ---

	if (strstr(szCmd, "-FAST") || strstr(szCmd, "/FAST"))
	{
		// 别人调用：Demo.exe -fast
		SilentRun(true); // 执行快充
		return 0; // 执行完直接退出，不显示界面
	}
	else if (strstr(szCmd, "-DATA") || strstr(szCmd, "/DATA"))
	{
		// 别人调用：Demo.exe -data
		SilentRun(false); // 执行数据
		return 0; // 退出
	}

	// --- 正常模式 (有界面) ---
	// 如果没有参数，或者参数不对，就显示控制面板
	return DialogBox(hInstance, (LPCTSTR)IDD_main, 0, (DLGPROC)WndProc);
}

// -------------------------------------------------------------------------
// 消息循环
// -------------------------------------------------------------------------
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	int wmId;

	switch (message)
	{
	case WM_INITDIALOG:
	{
						  AfxWndHwnd = hWnd;

						  // 初始化按钮文字
						  for (int i = 0; i<5; i++) UpdateButtonUI(hWnd, i);

						  // 勾选自动连接
						  SendDlgItemMessage(hWnd, IDC_EnableDevPnPNotify, BM_CLICK, 0, 0);

						  // 启动自动连接
						  SmartSearchAndConnect(hWnd, TRUE);
	}
		break;

	case WM_COMMAND:
		wmId = LOWORD(wParam);
		switch (wmId)
		{
		case IDC_BTN_SEARCH:
			SmartSearchAndConnect(hWnd, FALSE);
			break;

		case IDC_BTN_J3:
		case IDC_BTN_J4:
		case IDC_BTN_J5:
		case IDC_BTN_J7:
		case IDC_BTN_J6:
		{
						   if (AfxPortH == INVALID_HANDLE_VALUE) break;

						   // 确定是数组里的第几个 (0-4)
						   int chIndex = 0;
						   if (wmId == IDC_BTN_J3) chIndex = 0;
						   else if (wmId == IDC_BTN_J4) chIndex = 1;
						   else if (wmId == IDC_BTN_J5) chIndex = 2;
						   else if (wmId == IDC_BTN_J7) chIndex = 3;
						   else if (wmId == IDC_BTN_J6) chIndex = 4;

						   g_ChState[chIndex] = !g_ChState[chIndex];
						   SetGroupMode(AfxPortH, &AfxUsbSer, g_BasePins[chIndex], g_ChState[chIndex]);
						   UpdateButtonUI(hWnd, chIndex);
		}
			break;

		case IDC_BTN_ALL_DATA:
		{
								 if (AfxPortH == INVALID_HANDLE_VALUE) break;
								 for (int i = 0; i < 5; i++) {
									 g_ChState[i] = false;
									 SetGroupMode(AfxPortH, &AfxUsbSer, g_BasePins[i], false);
									 UpdateButtonUI(hWnd, i);
									 Sleep(50);
								 }
		}
			break;

		case IDC_BTN_ALL_FAST:
		{
								 if (AfxPortH == INVALID_HANDLE_VALUE) break;
								 for (int i = 0; i < 5; i++) {
									 g_ChState[i] = true;
									 SetGroupMode(AfxPortH, &AfxUsbSer, g_BasePins[i], true);
									 UpdateButtonUI(hWnd, i);
									 Sleep(50);
								 }
		}
			break;

		case IDC_OpenPort:
			OpenPort();
			break;

		case IDC_ClosePort:
			ClosePort();
			break;

		case IDC_EnableDevPnPNotify:
			if (IsDlgButtonChecked(hWnd, IDC_EnableDevPnPNotify) == BST_CHECKED) {
				CH341PtSetDevNotify(NULL, CH341PT_NOTIFY_ROUTINE);
				IsAutoOpen = TRUE;
			}
			else {
				CH341PtSetDevNotify(NULL, NULL);
				IsAutoOpen = FALSE;
			}
			break;

		default:
			return DefWindowProc(hWnd, message, wParam, lParam);
		}
		break;

	case WM_KEYUP:
	{
					 LONG iDevIndexAndEvent = (LONG)wParam;
					 CH341SerNotifyFunc(iDevIndexAndEvent);
	}
		break;

	case WM_DESTROY:
		CH341PtSetDevNotify(NULL, NULL);
		ClosePort();
		PostQuitMessage(0);
		break;

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

VOID CALLBACK CH341PT_NOTIFY_ROUTINE(LONG iDevIndexAndEvent)
{
	PostMessage(AfxWndHwnd, WM_KEYUP, iDevIndexAndEvent, 0);
}

VOID CH341SerNotifyFunc(LONG iDevIndexAndEvent)
{
	if (iDevIndexAndEvent > 0 && IsAutoOpen)
	{
		Sleep(100);
		SmartSearchAndConnect(AfxWndHwnd, TRUE);
	}
	else if (iDevIndexAndEvent < 0 && IsAutoOpen)
	{
		ClosePort();
		SetWindowText(AfxWndHwnd, "设备已断开");
	}
}

BOOL OpenPort()
{
	DCB dcb;
	CHAR PortName[50] = "";
	CHAR WinTitle[100] = "";

	if (AfxPortH != INVALID_HANDLE_VALUE)
		ClosePort();

	int SelIndex = SendDlgItemMessage(AfxWndHwnd, IDC_PortNum, CB_GETCURSEL, 0, 0);
	if (SelIndex == CB_ERR) return FALSE;

	SendDlgItemMessage(AfxWndHwnd, IDC_PortNum, CB_GETLBTEXT, SelIndex, (LPARAM)PortName);

	char FullPath[64];
	sprintf(FullPath, "\\\\.\\%s", PortName);

	AfxPortH = CreateFile(FullPath, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, NULL, NULL);

	if (AfxPortH == INVALID_HANDLE_VALUE) {
		sprintf(WinTitle, "打开 %s 失败", PortName);
		SetWindowText(AfxWndHwnd, WinTitle);
		UpdateSelection();
		return FALSE;
	}

	if (!GetCommState(AfxPortH, &dcb)) return FALSE;
	if (!SetCommState(AfxPortH, &dcb)) return FALSE;

	CH343PT_GetChipProperty(AfxPortH, &AfxUsbSer);

	sprintf(WinTitle, "已连接: %s", PortName);
	SetWindowText(AfxWndHwnd, WinTitle);

	UpdateSelection();
	return TRUE;
}

VOID ClosePort()
{
	if (AfxPortH != INVALID_HANDLE_VALUE) {
		CloseHandle(AfxPortH);
		AfxPortH = INVALID_HANDLE_VALUE;
	}
	UpdateSelection();
}

VOID UpdateSelection()
{
	BOOL bIsOpen = (AfxPortH != INVALID_HANDLE_VALUE);
	EnableWindow(GetDlgItem(AfxWndHwnd, IDC_OpenPort), !bIsOpen);
	EnableWindow(GetDlgItem(AfxWndHwnd, IDC_ClosePort), bIsOpen);
	EnableWindow(GetDlgItem(AfxWndHwnd, IDC_PortNum), !bIsOpen);

	for (int i = 0; i<5; i++)
		EnableWindow(GetDlgItem(AfxWndHwnd, g_BtnIDs[i]), bIsOpen);
	EnableWindow(GetDlgItem(AfxWndHwnd, IDC_BTN_ALL_DATA), bIsOpen);
	EnableWindow(GetDlgItem(AfxWndHwnd, IDC_BTN_ALL_FAST), bIsOpen);
}