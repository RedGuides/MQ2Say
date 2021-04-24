// MQ2Say.cpp : Say Detection and Alerting
//

#include "../MQ2Plugin.h"
#include <time.h>
#include <ctime>
#include <iostream>
#include <chrono>

PreSetup("MQ2Say");
PLUGIN_VERSION(1.0);

constexpr int CMD_HIST_MAX = 50;
constexpr int MAX_CHAT_SIZE = 700;
constexpr int LINES_PER_FRAME = 3;

std::list<std::string> sPendingSay;
std::map<std::string, std::chrono::steady_clock::time_point> mAlertTimers;

int iOldVScrollPos = 0;
int bmStripFirstStmlLines = 0;
int intIgnoreDelay = 300;
char szSayINISection[MAX_STRING] = { 0 };
char szWinTitle[MAX_STRING] = { 0 };
char strSayAlertCommand[MAX_STRING] = { 0 };
bool bSayStatus = true;
bool bSayDebug = false;
bool bAutoScroll = true;
bool bSaveByChar = true;
bool bSayAlerts = true;
bool bSayTimestamps = true;
class CMQSayWnd;
CMQSayWnd* MQSayWnd = nullptr;

class CMQSayWnd : public CCustomWnd
{
public:
	CMQSayWnd(char* Template) :CCustomWnd(Template)
	{
		DebugSpew("CMQSayWnd()");
		SetWndNotification(CMQSayWnd);
		SetWindowStyle(CWS_CLIENTMOVABLE | CWS_USEMYALPHA | CWS_RESIZEALL | CWS_BORDER | CWS_MINIMIZE | CWS_TITLE);
		RemoveStyle(CWS_TRANSPARENT | CWS_CLOSE);
		SetBGColor(0xFF000000);//black background
		InputBox = (CEditWnd*)GetChildItem("CW_ChatInput");
		InputBox->AddStyle(CWS_AUTOVSCROLL | CWS_RELATIVERECT | CWS_BORDER);// 0x800C0;
		this->SetFaded(false);
		this->SetEscapable(false);
		this->SetClickable(true);
		this->SetAlpha(0xFF);
		this->SetBGType(1);
		this->ContextMenuID = 3;
		InputBox->SetCRNormal(0xFFFFFFFF);//we want a white cursor
		InputBox->SetMaxChars(512);
		OutputBox = (CStmlWnd*)GetChildItem("CW_ChatOutput");
		OutputBox->SetParentWindow((_CSIDLWND*)this);
		InputBox->SetParentWindow((_CSIDLWND*)this);
		OutBoxLines = 0;
		OutputBox->MaxLines = 0x190;
		OutputBox->SetClickable(true);
		OutputBox->AddStyle(CWS_CLIENTMOVABLE);
		iCurrentCmd = -1;
		SetZLayer(1); // Make this the topmost window (we will leave it as such for charselect, and allow it to move to background ingame)
	}
	~CMQSayWnd()
	{
	}

	int WndNotification(CXWnd* pWnd, unsigned int Message, void* data)
	{
		if (pWnd == nullptr)
		{
			if (Message == XWM_CLOSE)
			{
				SetVisible(1);
				return 1;
			}
		}
		else if (pWnd == (CXWnd*)InputBox)
		{
			if (Message == XWM_HITENTER)
			{
				char szBuffer[MAX_STRING] = { 0 };
				GetCXStr((PCXSTR)InputBox->InputText, szBuffer, MAX_STRING);
				if (szBuffer[0] != 0)
				{
					if (!sCmdHistory.size() || sCmdHistory.front().compare(szBuffer))
					{
						if (sCmdHistory.size() > CMD_HIST_MAX)
						{
							sCmdHistory.pop_back();
						}
						sCmdHistory.insert(sCmdHistory.begin(), std::string(szBuffer));
					}
					iCurrentCmd = -1;
					SetCXStr(&InputBox->InputText, "");
					if (szBuffer[0] == '/')
					{
						DoCommand((PSPAWNINFO)pLocalPlayer, szBuffer);
					}
					else
					{
						Echo((PSPAWNINFO)pLocalPlayer, szBuffer);
					}
				}
				((CXWnd*)InputBox)->ClrFocus();
			}
			else if (Message == XWM_HISTORY)
			{
				if (data)
				{
					int* pInt = (int*)data;
					int iKeyPress = pInt[1];
					if (iKeyPress == 0xc8) // KeyUp
					{
						if (sCmdHistory.size() > 0)
						{
							iCurrentCmd++;
							if (iCurrentCmd < (static_cast<int>(sCmdHistory.size())) && iCurrentCmd >= 0)
							{
								const std::string& s = sCmdHistory.at(iCurrentCmd);
								((CXWnd*)InputBox)->SetWindowTextA(CXStr(s.c_str()));
							}
							else
							{
								iCurrentCmd = (static_cast<int>(sCmdHistory.size())) - 1;
							}
						}
					}
					else if (iKeyPress == 0xd0) // KeyDown
					{
						if (sCmdHistory.size() > 0)
						{
							iCurrentCmd--;
							if (iCurrentCmd >= 0 && sCmdHistory.size() > 0)
							{
								const std::string& s = sCmdHistory.at(iCurrentCmd);
								((CXWnd*)InputBox)->SetWindowTextA(CXStr(s.c_str()));
							}
							else if (iCurrentCmd < 0)
							{
								iCurrentCmd = -1;
								SetCXStr(&InputBox->InputText, "");
							}
						}
					}
				}
			}
			else
			{
				//DebugSpew("InputBox message %Xh, value: %Xh", Message, data);
			}
		}
		else if (Message == XWM_LINK)
		{
			class CChatWindow* p = (class CChatWindow*)this;
			if (OutputBox != (CStmlWnd*)pWnd)
			{
				CStmlWnd* tmp;
				int ret;
				//DebugSpew("MQ2Say: 0x%X, Msg: 0x%X, value: %Xh",pWnd,Message,data);
				//DebugSpew("MQ2Say: pWnd 0x%x != OutputBox 0x%x\n",pWnd,OutputBox);
				tmp = OutputBox;
				OutputBox = (CStmlWnd*)pWnd;
				ret = p->WndNotification(pWnd, Message, data);
				OutputBox = tmp;
				return ret;
			}
			return p->WndNotification(pWnd, Message, data);
		}
		else
		{
			//DebugSpew("MQ2Say: 0x%X, Msg: 0x%X, value: %Xh",pWnd,Message,data);
		}

		return CSidlScreenWnd::WndNotification(pWnd, Message, data);
	};

	void SetSayFont(int size) // brainiac 12-12-2007
	{
		struct FONTDATA
		{
			DWORD NumFonts;
			PCHAR* Fonts;
		};
		FONTDATA* Fonts;            // font array structure
		DWORD* SelFont;             // selected font
		// get fonts structure -- this offset can be found by looking at
		// SetSayFont which is called from the /chatfontsize function
		Fonts = (FONTDATA*)&(((char*)pWndMgr)[EQ_CHAT_FONT_OFFSET]);
		// check font array bounds and pointers
		if (size < 0 || size >= (static_cast<int>(Fonts->NumFonts)))
		{
			return;
		}
		if (!Fonts->Fonts || !MQSayWnd)
		{
			return;
		}
		//DebugSpew("Setting Size: %i", size);
		SelFont = (DWORD*)Fonts->Fonts[size];
		// Save the text, change the font, then restore the text
		CXStr str(((CStmlWnd*)MQSayWnd->OutputBox)->GetSTMLText());
		((CXWnd*)MQSayWnd->OutputBox)->SetFont(SelFont);
		((CStmlWnd*)MQSayWnd->OutputBox)->SetSTMLText(str, 1, 0);
		((CStmlWnd*)MQSayWnd->OutputBox)->ForceParseNow();
		// scroll to bottom of chat window
		((CXWnd*)MQSayWnd->OutputBox)->SetVScrollPos(MQSayWnd->OutputBox->GetVScrollMax());
		MQSayWnd->FontSize = size;
	};
	CEditWnd* InputBox;
	CStmlWnd* OutputBox;
	//CXWnd* OutWnd;
	//struct _CSIDLWND* OutStruct;
	DWORD OutBoxLines;
	DWORD FontSize = 0;
private:
	std::vector<std::string> sCmdHistory;
	int iCurrentCmd;
};

std::string SayCheckGM(std::string strCheckName)
{
	PSPAWNINFO pSpawn = (PSPAWNINFO)GetSpawnByName(&strCheckName[0]);
	if (pSpawn)
	{
		if (pSpawn->GM)
		{
			return " \ar(GM)\ax ";
		}
		if (pSpawn->Type == PC)
		{
			return "";
		}
	}
	else
	{
		return " \ar(GM)\ax ";
	}
	return "";
}

std::vector<std::string> SplitSay(const std::string& strLine)
{
	std::vector<std::string> vReturn;
	// Create a string and assign it the value of strLine, if we end up not formatting, then the original string is returned.
	std::string strOut = strLine;
	// Find the first space
	size_t Pos = strLine.find(' ');
	// If the first space is found (ie, it's not npos)
	if (Pos != std::string::npos)
	{
		// Set strName to the substring starting from 2 and going to Pos - 3 (check the math on this, I'm trying to exclude the space)
		vReturn.push_back(strLine.substr(2, Pos - 3));
		// Find the first quote
		Pos = strLine.find('\'');
		// Find the last quote
		size_t Pos2 = strLine.rfind('\'');
		// Make sure we found the first quote and it's not the same quote as the last quote
		if (Pos != std::string::npos && Pos != Pos2)
		{
			//  Set the say text to the substring starting at 1 after the position (excluding the quote itself, and ending at one before the other quote -- again check the math)
			vReturn.push_back('"' + strLine.substr(Pos + 1, Pos2 - Pos - 1) + '"');
		}
	}
	// End your split code
	return vReturn;
}

std::string FormatSay(const std::string& strSenderIn, const std::string& strTextIn)
{
	std::string strTimestamp = " ";
	// Concatenate and color here...example:
	std::string strOut = "\at" + strSenderIn + SayCheckGM(strSenderIn) + "\ax \a-wsays, '\ax\aw" + strTextIn + "\ax\a-w'\ax";
	if (bSayTimestamps)
	{
		char* tmpbuf = new char[128];
		struct tm today = { 0 };
		time_t tm = { 0 };
		tm = time(nullptr);
		localtime_s(&today, &tm);
		strftime(tmpbuf, 128, "%m/%d %H:%M:%S", &today);
		strTimestamp += " \ar[\ax\ag";
		strTimestamp += tmpbuf;
		strTimestamp += "\ax\ar]\ax ";
		return strTimestamp + strOut;
	}

	return strOut;
}

void WriteSay(const std::string& SaySender, const std::string& SayText)
{
	if (MQSayWnd == nullptr || !bSayStatus)
	{
		return;
	}
	std::string SayGMFlag = SayCheckGM(SaySender);
	std::string Line = FormatSay(SaySender, SayText);
	MQSayWnd->SetVisible(true);
	char* szProcessed = new char[MAX_STRING];
	int pos = MQToSTML(&Line[0], szProcessed, MAX_STRING - 4);
	strcat_s(szProcessed, MAX_STRING, "<br>");//we left room for this above...
	CXStr NewText(szProcessed);
	ConvertItemTags(NewText, FALSE);
	sPendingSay.push_back(NewText.Ptr->Text);
	delete[] szProcessed;
}

void DoAlerts(const std::string& Line)
{
	if (bSayStatus)
	{
		std::vector<std::string> SaySplit = SplitSay(Line);
		if (SaySplit.size() == 2)
		{
			WriteSay(SaySplit[0], SaySplit[1]);
			if (bSayAlerts)
			{
				if (strSayAlertCommand[0] == '\0')
				{
					WriteChatf("[\arMQ2Say\ax] \arError\ax: \a-wSay Detected, Alerts are turned on, but AlertCommand is not set in the INI\ax");
				}
				else if (strSayAlertCommand[0] == '/')
				{
					if (mAlertTimers.find(SaySplit[0]) == mAlertTimers.end())
					{
						EzCommand(strSayAlertCommand);
						mAlertTimers[SaySplit[0]] = std::chrono::steady_clock::now();
					}
				}
				else
				{
					WriteChatf("\arMQ2Say Error\ax: AlertCommand in INI must begin with a / and be a valid EQ or MQ2 Command.");
				}
			}
		}
	}
}

void LoadSaySettings()
{
	char szTemp[MAX_STRING] = { 0 };
	GetPrivateProfileString("Settings", "SayStatus", bSayStatus ? "on" : "off", szTemp, MAX_STRING, INIFileName);
	bSayStatus = (!_strnicmp(szTemp, "on", 3));
	GetPrivateProfileString("Settings", "SayDebug", bSayDebug ? "on" : "off", szTemp, MAX_STRING, INIFileName);
	bSayDebug = (!_strnicmp(szTemp, "on", 3));
	GetPrivateProfileString("Settings", "AutoScroll", bAutoScroll ? "on" : "off", szTemp, MAX_STRING, INIFileName);
	bAutoScroll = (!_strnicmp(szTemp, "on", 3));
	GetPrivateProfileString("Settings", "SaveByChar", bSaveByChar ? "on" : "off", szTemp, MAX_STRING, INIFileName);
	bSaveByChar = (!_strnicmp(szTemp, "on", 3));
	intIgnoreDelay = GetPrivateProfileInt("Settings", "IgnoreDelay", intIgnoreDelay, INIFileName);
}

void LoadSayFromINI(PCSIDLWND pWindow)
{
	char szTemp[MAX_STRING] = { 0 };

	LoadSaySettings();

	if (bSaveByChar && pLocalPlayer)
	{
		sprintf_s(szSayINISection, "%s.%s", EQADDR_SERVERNAME, ((PSPAWNINFO)pLocalPlayer)->Name);
	}
	else
	{
		strcpy_s(szSayINISection, "Default");
	}

	GetPrivateProfileString(szSayINISection, "Alerts", bSayAlerts ? "on" : "off", szTemp, MAX_STRING, INIFileName);
	bSayAlerts = !_strnicmp(szTemp, "on", 3);
	const int i = GetPrivateProfileString(szSayINISection, "AlertCommand", "/multiline ; /beep ; /timed 1 /beep ; /timed 2 /beep", strSayAlertCommand, MAX_STRING, INIFileName);
	if (i == 0)
	{
		strcpy_s(strSayAlertCommand, "/multiline ; /beep ; /timed 1 /beep ; /timed 2 /beep");
	}
	GetPrivateProfileString(szSayINISection, "Timestamps", bSayTimestamps ? "on" : "off", szTemp, MAX_STRING, INIFileName);
	bSayTimestamps = !_strnicmp(szTemp, "on", 3);

	//The settings for default location are also in the reset command.
	pWindow->SetLocation({ (LONG)GetPrivateProfileInt(szSayINISection,"SayLeft", 300,INIFileName),
		(LONG)GetPrivateProfileInt(szSayINISection,"SayTop",       10,INIFileName),
		(LONG)GetPrivateProfileInt(szSayINISection,"SayRight",    600,INIFileName),
		(LONG)GetPrivateProfileInt(szSayINISection,"SayBottom",   210,INIFileName) });

	pWindow->SetLocked((GetPrivateProfileInt(szSayINISection, "Locked", 0, INIFileName) ? true : false));
	pWindow->SetFades((GetPrivateProfileInt(szSayINISection, "Fades", 0, INIFileName) ? true : false));
	pWindow->SetFadeDelay(GetPrivateProfileInt(szSayINISection, "Delay", 2000, INIFileName));
	pWindow->SetFadeDuration(GetPrivateProfileInt(szSayINISection, "Duration", 500, INIFileName));
	pWindow->SetAlpha((BYTE)GetPrivateProfileInt(szSayINISection, "Alpha", 255, INIFileName));
	pWindow->SetFadeToAlpha((BYTE)GetPrivateProfileInt(szSayINISection, "FadeToAlpha", 255, INIFileName));
	pWindow->SetBGType(GetPrivateProfileInt(szSayINISection, "BGType", 1, INIFileName));
	ARGBCOLOR col = { 0 };
	col.ARGB = pWindow->GetBGColor();
	col.A = GetPrivateProfileInt(szSayINISection, "BGTint.alpha", 255, INIFileName);
	col.R = GetPrivateProfileInt(szSayINISection, "BGTint.red", 0, INIFileName);
	col.G = GetPrivateProfileInt(szSayINISection, "BGTint.green", 0, INIFileName);
	col.B = GetPrivateProfileInt(szSayINISection, "BGTint.blue", 0, INIFileName);
	pWindow->SetBGColor(col.ARGB);
	MQSayWnd->SetSayFont(GetPrivateProfileInt(szSayINISection, "FontSize", 4, INIFileName));
	GetPrivateProfileString(szSayINISection, "WindowTitle", "Say Detection", szTemp, MAX_STRING, INIFileName);
	pWindow->CSetWindowText(szTemp);
}

template <unsigned int _Size>LPSTR SafeItoa(int _Value, char(&_Buffer)[_Size], int _Radix)
{
	errno_t err = _itoa_s(_Value, _Buffer, _Radix);
	if (!err)
	{
		return _Buffer;
	}
	return "";
}

void SaveSayToINI(PCSIDLWND pWindow)
{
	char szTemp[MAX_STRING] = { 0 };
	WritePrivateProfileString("Settings", "SayStatus", bSayStatus ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "SayDebug", bSayDebug ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "AutoScroll", bAutoScroll ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "SaveByChar", bSaveByChar ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "IgnoreDelay", SafeItoa(intIgnoreDelay, szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "Alerts", bSayAlerts ? "on" : "off", INIFileName);
	WritePrivateProfileString(szSayINISection, "AlertCommand", strSayAlertCommand, INIFileName);
	WritePrivateProfileString(szSayINISection, "Timestamps", bSayTimestamps ? "on" : "off", INIFileName);
	WritePrivateProfileString(szSayINISection, "WindowTitle", szTemp, INIFileName);
	if (pWindow->IsMinimized())
	{
		WritePrivateProfileString(szSayINISection, "SayTop", SafeItoa(pWindow->GetOldLocation().top, szTemp, 10), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayBottom", SafeItoa(pWindow->GetOldLocation().bottom, szTemp, 10), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayLeft", SafeItoa(pWindow->GetOldLocation().left, szTemp, 10), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayRight", SafeItoa(pWindow->GetOldLocation().right, szTemp, 10), INIFileName);
	}
	else
	{
		WritePrivateProfileString(szSayINISection, "SayTop", SafeItoa(pWindow->GetLocation().top, szTemp, 10), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayBottom", SafeItoa(pWindow->GetLocation().bottom, szTemp, 10), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayLeft", SafeItoa(pWindow->GetLocation().left, szTemp, 10), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayRight", SafeItoa(pWindow->GetLocation().right, szTemp, 10), INIFileName);
	}
	WritePrivateProfileString(szSayINISection, "Locked", SafeItoa(pWindow->IsLocked(), szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "Fades", SafeItoa(pWindow->GetFades(), szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "Delay", SafeItoa(pWindow->GetFadeDelay(), szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "Duration", SafeItoa(pWindow->GetFadeDuration(), szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "Alpha", SafeItoa(pWindow->GetAlpha(), szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "FadeToAlpha", SafeItoa(pWindow->GetFadeToAlpha(), szTemp, 10), INIFileName);
	ARGBCOLOR col = { 0 };
	col.ARGB = pWindow->GetBGColor();
	WritePrivateProfileString(szSayINISection, "BGType", SafeItoa(pWindow->GetBGType(), szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.alpha", SafeItoa(col.A, szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.red", SafeItoa(col.R, szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.green", SafeItoa(col.G, szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.blue", SafeItoa(col.B, szTemp, 10), INIFileName);
	WritePrivateProfileString(szSayINISection, "FontSize", SafeItoa(MQSayWnd->FontSize, szTemp, 10), INIFileName);
	GetCXStr(pWindow->CGetWindowText(), szTemp, MAX_STRING);
}

void CreateSayWnd()
{
	if (MQSayWnd)
	{
		return;
	}

	MQSayWnd = new CMQSayWnd("ChatWindow");

	LoadSayFromINI((PCSIDLWND)MQSayWnd);
	SaveSayToINI((PCSIDLWND)MQSayWnd);
}

void DestroySayWnd()
{
	if (MQSayWnd)
	{
		sPendingSay.clear();

		SaveSayToINI((PCSIDLWND)MQSayWnd);

		delete MQSayWnd;
		MQSayWnd = nullptr;

		iOldVScrollPos = 0;
	}
}

void MQSayFont(PSPAWNINFO pChar, PCHAR Line)
{
	if (MQSayWnd && Line[0] != '\0')
	{
		const int size = atoi(Line);
		if (size < 0 || size > 10)
		{
			WriteChatf("Usage: /sayfont 0-10");
			return;
		}
		MQSayWnd->SetSayFont(size);

		SaveSayToINI((PCSIDLWND)MQSayWnd);
	}
}

void MQSayClear()
{
	if (MQSayWnd)
	{
		CChatWindow* saywnd = (CChatWindow*)MQSayWnd;
		saywnd->Clear();
		iOldVScrollPos = 0;
		MQSayWnd->OutBoxLines = 0;
	}
}

void SetSayTitle(PSPAWNINFO pChar, PCHAR Line)
{
	if (MQSayWnd)
	{
		MQSayWnd->CSetWindowText(Line);
	}
}

bool AdjustBoolSetting(const char* SettingCmd, const char* INIsection, const char* INIkey, const char* INIvalue, bool currentValue)
{
	if (INIvalue[0] != '\0')
	{
		if (!_stricmp(INIvalue, "on"))
		{
			currentValue = true;
		}
		else if (!_stricmp(INIvalue, "off"))
		{
			currentValue = false;
		}
		else
		{
			WriteChatf("Usage: /mqsay %s [on | off]\n IE: /mqsay %s on", SettingCmd, SettingCmd);
			return currentValue;
		}

		WritePrivateProfileString(INIsection, INIkey, currentValue ? "on" : "off", INIFileName);
	}

	WriteChatf("[\arMQ2Say\ax] \aw%s is: %s", INIkey, (currentValue ? "\agOn" : "\arOff"));
	return currentValue;
}

void MQSay(PSPAWNINFO pChar, PCHAR Line)
{
	PCSIDLWND pWindow = (PCSIDLWND)MQSayWnd;
	char szTemp[MAX_STRING] = { 0 };

	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, Line, 1);
	//Display Command Syntax
	if (Arg[0] == '\0' || !_stricmp(Arg, "help"))
	{
		WriteChatf("[\arMQ2Say\ax] \awPlugin is:\ax %s", bSayStatus ? "\agOn\ax" : "\arOff\ax");
		WriteChatf("Usage: /mqsay <on/off>");
		WriteChatf("/mqsay [Option Name] <value/on/off>");
		WriteChatf("Valid options are Reset, Clear, Alerts, Autoscroll, IgnoreDelay, Reload, Timestamps, Title");
	}
	else if (!_stricmp(Arg, "on"))
	{
		CreateSayWnd();
		bSayStatus = true;
		WriteChatf("[\arMQ2Say\ax] \awSay Status is:\ax \agOn.");
		WritePrivateProfileString("Settings", "SayStatus", bSayStatus ? "on" : "off", INIFileName);
	}
	else if (!_stricmp(Arg, "off"))
	{
		DestroySayWnd();
		bSayStatus = false;
		WriteChatf("[\arMQ2Say\ax] \awSay Status is:\ax \arOff.");
		WritePrivateProfileString("Settings", "SayStatus", bSayStatus ? "on" : "off", INIFileName);
	}
	else if (!_stricmp(Arg, "reload"))
	{
		LoadSayFromINI((PCSIDLWND)MQSayWnd);
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Settings Reloaded.\ax");
	}
	else if (!_stricmp(Line, "reset"))
	{
		if (MQSayWnd)
		{
			MQSayWnd->SetLocked(false);
			// The settings below are also in the LoadSayFromINI function
			CXRect rc = { 300, 10, 600, 210 };
			((CXWnd*)MQSayWnd)->Move(rc, false);

			SaveSayToINI((PCSIDLWND)MQSayWnd);
		}
		else
		{
			WriteChatf("[\arMQ2Say\ax] \awReset is only valid when Say Window is visible (\ag/mqsay on\ax).\ax");
		}
	}
	else if (!_stricmp(Line, "clear"))
	{
		if (MQSayWnd)
		{
			MQSayClear();
		}
		else
		{
			WriteChatf("[\arMQ2Say\ax] \awClear is only valid when Say Window is visible (\ag/mqsay on\ax).\ax");
		}
	}
	else if (!_stricmp(Arg, "debug"))
	{
		GetArg(Arg, Line, 2);
		bSayDebug = AdjustBoolSetting("debug", "Settings", "SayDebug", Arg, bSayDebug);
	}
	else if (!_stricmp(Arg, "title"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			if (pWindow != nullptr)
			{
				GetCXStr(pWindow->CGetWindowText(), szTemp, MAX_STRING);
				WriteChatf("The Window title is currently: \ar%s\ax", szTemp);
			}
			else
			{
				WriteChatf("[\arMQ2Say\ax] \arSay window not found, cannot retrieve Title.");
			}
		}
		else
		{
			const char* szPos = GetNextArg(Line, 1);
			if (szPos[0] != '\"')
			{
				strcpy_s(Arg, szPos);
			}
			MQSayWnd->CSetWindowText(Arg);
			WritePrivateProfileString(szSayINISection, "WindowTitle", Arg, INIFileName);
		}
	}
	else if (!_stricmp(Arg, "font"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("[\arMQ2Say\ax] \awThe font size is currently:\ax %s", MQSayWnd->FontSize);
		}
		else if (atoi(Arg) > 0)
		{
			MQSayWnd->SetSayFont(atoi(Arg));
			WritePrivateProfileString(szSayINISection, "FontSize", SafeItoa(MQSayWnd->FontSize, szTemp, 10), INIFileName);
		}
		else
		{
			WriteChatf("Usage: /mqsay font <font size>\n IE: /mqsay font 5");
		}
	}
	else if (!_stricmp(Arg, "IgnoreDelay"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("[\arMQ2Say\ax] \awThe Ignore Delay is currently:\ax %i", intIgnoreDelay);
		}
		else
		{
			const int ignoreDelay = atoi(Arg) >= 0;
			if (ignoreDelay >= 0)
			{
				intIgnoreDelay = ignoreDelay;
				WriteChatf("[\arMQ2Say\ax] \awThe Ignore Delay is now set to:\ax %i", intIgnoreDelay);
				WritePrivateProfileString("Settings", "IgnoreDelay", SafeItoa(intIgnoreDelay, szTemp, 10), INIFileName);
			}
			else
			{
				WriteChatf("Usage: /mqsay ignoredelay <Time In Seconds>\n IE: /mqsay ignoredelay 300");
			}
		}
	}
	else if (!_stricmp(Arg, "alerts"))
	{
		GetArg(Arg, Line, 2);
		bSayAlerts = AdjustBoolSetting("alerts", szSayINISection, "Alerts", Arg, bSayAlerts);
	}
	else if (!_stricmp(Arg, "timestamps"))
	{
		GetArg(Arg, Line, 2);
		bSayTimestamps = AdjustBoolSetting("timestamps", szSayINISection, "Timestamps", Arg, bSayTimestamps);
	}
	else if (!_stricmp(Arg, "autoscroll"))
	{
		GetArg(Arg, Line, 2);
		bAutoScroll = AdjustBoolSetting("autoscroll", szSayINISection, "AutoScroll", Arg, bAutoScroll);
	}
	else if (!_stricmp(Arg, "SaveByChar"))
	{
		GetArg(Arg, Line, 2);
		bSaveByChar = AdjustBoolSetting("SaveByChar", "Settings", "SaveByChar", Arg, bSaveByChar);
	}
	else
	{
		WriteChatf("[\arMQ2Say\ax] %s was not a valid option. Use \ag/mqsay help\ax to show options.", Arg);
	}
}

PLUGIN_API VOID OnReloadUI()
{
	// redraw window when you load/reload UI
	CreateSayWnd();
}

PLUGIN_API VOID OnCleanUI()
{
	// destroy SayWnd before server select & while reloading UI
	DestroySayWnd();
}

PLUGIN_API VOID SetGameState(DWORD GameState)
{
	if (GameState == GAMESTATE_INGAME && !MQSayWnd)
	{
		// we entered the game, set up shop
		if (bSayStatus)
		{
			CreateSayWnd();
		}
	}
}

PLUGIN_API DWORD OnIncomingChat(PCHAR Line, DWORD Color)
{
	if (GetGameState() == GAMESTATE_INGAME
		&& bSayStatus
		&& Color == USERCOLOR_SAY)
	{
		DoAlerts(Line);
	}

	return 0;
}

PLUGIN_API VOID OnPulse()
{
	if (MQSayWnd)
	{
		if (GetGameState() == GAMESTATE_INGAME && MQSayWnd->GetZLayer() != 0)
		{
			MQSayWnd->SetZLayer(0);
		}
		if (!sPendingSay.empty())
		{
			// set 'old' to current
			iOldVScrollPos = MQSayWnd->OutputBox->GetVScrollPos();

			// scroll down if autoscroll enabled, or current position is the bottom of SayWnd
			bool bScrollDown = bAutoScroll || (MQSayWnd->OutputBox->GetVScrollPos() == MQSayWnd->OutputBox->GetVScrollMax());

			size_t ThisPulse = sPendingSay.size();
			if (ThisPulse > LINES_PER_FRAME)
			{
				ThisPulse = LINES_PER_FRAME;
			}
			MQSayWnd->OutBoxLines += ThisPulse;
			if (MQSayWnd->OutBoxLines > MAX_CHAT_SIZE)
			{
				DWORD Diff = (MQSayWnd->OutBoxLines - MAX_CHAT_SIZE) + LINES_PER_FRAME;
				MQSayWnd->OutBoxLines -= Diff;
				Benchmark(bmStripFirstStmlLines, MQSayWnd->OutputBox->StripFirstSTMLLines(Diff));
			}
			for (DWORD N = 0; N < ThisPulse; N++)
			{
				if (!sPendingSay.empty())
				{
					MQSayWnd->OutputBox->AppendSTML(sPendingSay.begin()->c_str());
					sPendingSay.pop_front();
				}
			}
			if (bScrollDown)
			{
				// set current vscroll position to bottom
				((CXWnd*)MQSayWnd->OutputBox)->SetVScrollPos(MQSayWnd->OutputBox->GetVScrollMax());
			}
			else
			{
				// autoscroll is disabled and current vscroll position was not at the bottom, retain position
				// note: if the window is full (VScrollMax value between 9793 and 9835), this will not adjust with
				// the flushing of buffer that keeps window a certain max size
				((CXWnd*)MQSayWnd->OutputBox)->SetVScrollPos(iOldVScrollPos);
			}
		}
		if (InHoverState())
		{
			((CXWnd*)MQSayWnd)->DoAllDrawing();
		}

		static std::chrono::steady_clock::time_point PulseTimer = std::chrono::steady_clock::now();
		if (!mAlertTimers.empty() && std::chrono::steady_clock::now() >= PulseTimer + std::chrono::seconds(1))
		{
			for (auto it = mAlertTimers.cbegin(); it != mAlertTimers.cend(); /* Increment in the function */)
			{
				if (it->second + std::chrono::seconds(intIgnoreDelay) <= std::chrono::steady_clock::now())
				{
					it = mAlertTimers.erase(it);
				}
				else
				{
					++it;
				}
			}
		}
	}
}

class MQ2SayType* pSayWndType = nullptr;

class MQ2SayType : public MQ2Type
{
public:
	enum SayWndMembers
	{
		Title = 1,
	};
	MQ2SayType() :MQ2Type("saywnd")
	{
		TypeMember(Title);
	}
	~MQ2SayType() {}
	bool GetMember(MQ2VARPTR VarPtr, PCHAR Member, PCHAR Index, MQ2TYPEVAR& Dest)
	{
		PMQ2TYPEMEMBER pMember = MQ2SayType::FindMember(Member);
		if (!pMember)
			return false;
		switch ((SayWndMembers)pMember->ID)
		{
		case Title:
		{
			if (MQSayWnd)
			{
				GetCXStr(MQSayWnd->CGetWindowText(), DataTypeTemp);
				//GetCXStr(MQSayWnd->WindowText, DataTypeTemp);
				Dest.Ptr = &DataTypeTemp[0];
				Dest.Type = pStringType;
				return true;
			}
			break;
		}
		default:
			break;
		}
		return false;
	}

	bool ToString(MQ2VARPTR VarPtr, PCHAR Destination)
	{
		if (MQSayWnd)
		{
			GetCXStr(MQSayWnd->CGetWindowText(), Destination);
		}
		else
		{
			strcpy_s(Destination, MAX_STRING, "");
		}
		return true;
	}
	bool FromData(MQ2VARPTR& VarPtr, MQ2TYPEVAR& Source)
	{
		return false;
	}
	bool FromString(MQ2VARPTR& VarPtr, PCHAR Source)
	{
		return false;
	}
};

BOOL dataSayWnd(PCHAR szName, MQ2TYPEVAR & Dest)
{
	Dest.DWord = 1;
	Dest.Type = pSayWndType;
	return true;
}

PLUGIN_API VOID InitializePlugin()
{
	// Add commands, macro parameters, hooks, etc.
	AddMQ2Data("SayWnd", dataSayWnd);
	pSayWndType = new MQ2SayType;
	AddCommand("/mqsay", MQSay);
	bmStripFirstStmlLines = AddMQ2Benchmark("StripFirstStmlLines");
	LoadSaySettings();
	if (!bSayStatus)
	{
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Loaded successfully. Window not displayed due to the plugin being turned off in the ini. Type /mqsay on to turn it back on.\ax");
	}
	else
	{
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Loaded successfully.\ax");
	}
}

PLUGIN_API VOID ShutdownPlugin()
{
	sPendingSay.clear();
	// Remove commands, macro parameters, hooks, etc.
	RemoveCommand("/mqsay");
	RemoveMQ2Data("SayWnd");
	delete pSayWndType;
	RemoveMQ2Benchmark(bmStripFirstStmlLines);
	bmStripFirstStmlLines = 0;
	DestroySayWnd();
}
