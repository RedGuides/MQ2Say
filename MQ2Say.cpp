// MQ2Say.cpp : Say Detection and Alerting
//

#include <mq/Plugin.h>

#include <time.h>
#include <ctime>
#include <iostream>
#include <chrono>

PreSetup("MQ2Say");
PLUGIN_VERSION(1.0);

constexpr int LINES_PER_FRAME = 3;
constexpr int CMD_HIST_MAX = 50;
constexpr int MAX_CHAT_SIZE = 700;

std::list<CXStr> sPendingSay;
std::map<std::string, std::chrono::steady_clock::time_point> mAlertTimers;

int iOldVScrollPos = 0;
int iStripFirstStmlLines = 0;
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
	CMQSayWnd(char* Template) : CCustomWnd(Template)
	{
		DebugSpew("CMQSayWnd()");

		SetWindowStyle(CWS_CLIENTMOVABLE | CWS_USEMYALPHA | CWS_RESIZEALL | CWS_BORDER | CWS_MINIMIZE | CWS_TITLE);
		RemoveStyle(CWS_TRANSPARENT | CWS_CLOSE);
		SetBGColor(0xFF000000); // black background

		InputBox = (CEditWnd*)GetChildItem("CW_ChatInput");
		InputBox->AddStyle(CWS_AUTOVSCROLL | CWS_RELATIVERECT | CWS_BORDER); // 0x800C0;
		SetFaded(false);
		SetEscapable(false);
		SetClickable(true);
		SetAlpha(0xFF);
		SetBGType(1);

		ContextMenuID = 3;
		InputBox->SetCRNormal(0xFFFFFFFF); // we want a white cursor
		InputBox->SetMaxChars(512);
		OutputBox = (CStmlWnd*)GetChildItem("CW_ChatOutput");
		OutputBox->SetParentWindow(this);
		InputBox->SetParentWindow(this);
		OutputBox->MaxLines = MAX_CHAT_SIZE;
		OutputBox->SetClickable(true);
		OutputBox->AddStyle(CWS_CLIENTMOVABLE);
		iCurrentCmd = -1;
		SetZLayer(1); // Make this the topmost window (we will leave it as such for charselect, and allow it to move to background ingame)
	}

	~CMQSayWnd()
	{
	}

	virtual int WndNotification(CXWnd* pWnd, unsigned int Message, void* data) override
	{
		if (pWnd == nullptr)
		{
			if (Message == XWM_CLOSE)
			{
				SetVisible(1);
				return 1;
			}
		}
		else if (pWnd == InputBox)
		{
			if (Message == XWM_HITENTER)
			{
				CXStr text = InputBox->InputText;
				if (!text.empty())
				{
					if (!sCmdHistory.size() || sCmdHistory.front().compare(text.c_str()))
					{
						if (sCmdHistory.size() > CMD_HIST_MAX)
						{
							sCmdHistory.pop_back();
						}

						sCmdHistory.insert(sCmdHistory.begin(), text.c_str());
					}

					iCurrentCmd = -1;
					InputBox->InputText.clear();
					if (text[0] == '/')
					{
						DoCommand((SPAWNINFO*)pLocalPlayer, text.c_str());
					}
					else
					{
						Echo((SPAWNINFO*)pLocalPlayer, (char*)text.c_str());
					}
				}

				InputBox->ClrFocus();
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
								InputBox->SetWindowText(s.c_str());
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
								std::string s = sCmdHistory.at(iCurrentCmd);
								InputBox->SetWindowText(s.c_str());
							}
							else if (iCurrentCmd < 0)
							{
								iCurrentCmd = -1;
								InputBox->InputText.clear();
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
			//DebugSpewAlways("Link clicked: 0x%X, 0x%X, %Xh", pWnd, Message, data);
			for (auto wnd : pChatManager->ChannelMap)
			{
				if (wnd)
				{
					//DebugSpewAlways("Found wnd %Xh : %s", wnd, wnd->GetWindowName()->c_str());
					wnd->WndNotification(wnd->OutputWnd, Message, data);
					break;
				}
			}
		}
		else
		{
			//DebugSpew("MQ2Say: 0x%X, Msg: 0x%X, value: %Xh",pWnd,Message,data);
		}

		return CSidlScreenWnd::WndNotification(pWnd, Message, data);
	};

	void SetSayFont(int size) // brainiac 12-12-2007
	{
		// check font array bounds and pointers
		if (size < 0 || size >= pWndMgr->FontsArray.GetCount())
		{
			return;
		}

		CTextureFont* font = pWndMgr->FontsArray[size];
		if (!font || !MQSayWnd)
		{
			return;
		}
		//DebugSpew("Setting Size: %i", size);

		// Save the text, change the font, then restore the text
		CXStr str = OutputBox->GetSTMLText();

		OutputBox->SetFont(font);
		OutputBox->SetSTMLText(str);
		OutputBox->ForceParseNow();

		// scroll to bottom of chat window
		OutputBox->SetVScrollPos(OutputBox->GetVScrollMax());

		FontSize = size;
	};

	void Clear()
	{
		if (OutputBox)
		{
			OutputBox->SetSTMLText("");
			OutputBox->ForceParseNow();
			OutputBox->SetVScrollPos(OutputBox->GetVScrollMax());
		}
	}

	int FontSize = 4;
	CEditWnd* InputBox;
	CStmlWnd* OutputBox;

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

	int pos = MQToSTML(Line.c_str(), szProcessed, MAX_STRING - 4);

	CXStr text = szProcessed;
	text.append("<br>");

	ConvertItemTags(text);
	sPendingSay.push_back(text);

	delete[] szProcessed;
}

void DoAlerts(std::string Line)
{
	if (bSayStatus)
	{
		std::vector<std::string> strSaySplit = SplitSay(Line);
		if (strSaySplit.size() == 2)
		{
			WriteSay(strSaySplit[0], strSaySplit[1]);
			if (bSayAlerts)
			{
				if (strSayAlertCommand[0] == '\0')
				{
					WriteChatf("[\arMQ2Say\ax] \arError\ax: \a-wSay Detected, Alerts are turned on, but AlertCommand is not set in the INI\ax");
				}
				else if (strSayAlertCommand[0] == '/')
				{
					if (mAlertTimers.find(strSaySplit[0]) == mAlertTimers.end())
					{
						EzCommand(strSayAlertCommand);
						//mAlertTimers.insert(strSaySplit[0], std::chrono::steady_clock::now());
						mAlertTimers[strSaySplit[0]] = std::chrono::steady_clock::now();
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

	bSayStatus = GetPrivateProfileBool("Settings", "SayStatus", bSayStatus, INIFileName);
	bSayDebug = GetPrivateProfileBool("Settings", "SayDebug", bSayDebug, INIFileName);
	bAutoScroll = GetPrivateProfileBool("Settings", "AutoScroll", bAutoScroll, INIFileName);
	bSaveByChar = GetPrivateProfileBool("Settings", "SaveByChar", bSaveByChar, INIFileName);
	intIgnoreDelay = GetPrivateProfileInt("Settings", "IgnoreDelay", intIgnoreDelay, INIFileName);
}

void LoadSayFromINI(CSidlScreenWnd* pWindow)
{
	char szTemp[MAX_STRING] = { 0 };

	LoadSaySettings();

	sprintf_s(szSayINISection, "%s.%s", EQADDR_SERVERNAME, ((PSPAWNINFO)pLocalPlayer)->Name);

	if (!bSaveByChar)
	{
		sprintf_s(szSayINISection, "Default");
	}

	//left top right bottom
	pWindow->SetLocation({ (LONG)GetPrivateProfileInt(szSayINISection,"ChatLeft",      10,INIFileName),
		(LONG)GetPrivateProfileInt(szSayINISection,"SayTop",       10,INIFileName),
		(LONG)GetPrivateProfileInt(szSayINISection,"SayRight",    410,INIFileName),
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
	pWindow->SetWindowText(szTemp);
	bSayAlerts = GetPrivateProfileBool(szSayINISection, "Alerts", bSayAlerts, INIFileName);
	int i = GetPrivateProfileString(szSayINISection, "AlertCommand", "/multiline ; /beep ; /timed 1 /beep ; /timed 2 /beep", strSayAlertCommand, MAX_STRING, INIFileName);
	if (i == 0)
	{
		strcpy_s(strSayAlertCommand, "/multiline ; /beep ; /timed 1 /beep ; /timed 2 /beep");
	}
	bSayTimestamps = GetPrivateProfileBool(szSayINISection, "Timestamps", bSayTimestamps, INIFileName);
}

void SaveSayToINI(CSidlScreenWnd* pWindow)
{
	char szTemp[MAX_STRING] = { 0 };
	WritePrivateProfileString("Settings", "SayStatus", bSayStatus ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "SayDebug", bSayDebug ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "AutoScroll", bAutoScroll ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "SaveByChar", bSaveByChar ? "on" : "off", INIFileName);
	WritePrivateProfileString("Settings", "IgnoreDelay", std::to_string(intIgnoreDelay), INIFileName);
	if (pWindow->IsMinimized())
	{
		WritePrivateProfileString(szSayINISection, "ChatTop", std::to_string(pWindow->GetOldLocation().top), INIFileName);
		WritePrivateProfileString(szSayINISection, "ChatBottom", std::to_string(pWindow->GetOldLocation().bottom), INIFileName);
		WritePrivateProfileString(szSayINISection, "ChatLeft", std::to_string(pWindow->GetOldLocation().left), INIFileName);
		WritePrivateProfileString(szSayINISection, "ChatRight", std::to_string(pWindow->GetOldLocation().right), INIFileName);
	}
	else
	{
		WritePrivateProfileString(szSayINISection, "ChatTop", std::to_string(pWindow->GetLocation().top), INIFileName);
		WritePrivateProfileString(szSayINISection, "ChatBottom", std::to_string(pWindow->GetLocation().bottom), INIFileName);
		WritePrivateProfileString(szSayINISection, "ChatLeft", std::to_string(pWindow->GetLocation().left), INIFileName);
		WritePrivateProfileString(szSayINISection, "ChatRight", std::to_string(pWindow->GetLocation().right), INIFileName);
	}
	WritePrivateProfileString(szSayINISection, "Locked", std::to_string(pWindow->IsLocked()), INIFileName);
	WritePrivateProfileString(szSayINISection, "Fades", std::to_string(pWindow->GetFades()), INIFileName);
	WritePrivateProfileString(szSayINISection, "Delay", std::to_string(pWindow->GetFadeDelay()), INIFileName);
	WritePrivateProfileString(szSayINISection, "Duration", std::to_string(pWindow->GetFadeDuration()), INIFileName);
	WritePrivateProfileString(szSayINISection, "Alpha", std::to_string(pWindow->GetAlpha()), INIFileName);
	WritePrivateProfileString(szSayINISection, "FadeToAlpha", std::to_string(pWindow->GetFadeToAlpha()), INIFileName);
	ARGBCOLOR col = { 0 };
	col.ARGB = pWindow->GetBGColor();
	WritePrivateProfileString(szSayINISection, "BGType", std::to_string(pWindow->GetBGType()), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.alpha", std::to_string(col.A), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.red", std::to_string(col.R), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.green", std::to_string(col.G), INIFileName);
	WritePrivateProfileString(szSayINISection, "BGTint.blue", std::to_string(col.B), INIFileName);
	WritePrivateProfileString(szSayINISection, "FontSize", std::to_string(MQSayWnd->FontSize), INIFileName);
	WritePrivateProfileString(szSayINISection, "WindowTitle", pWindow->GetWindowText().c_str(), INIFileName);
	WritePrivateProfileString(szSayINISection, "Alerts", bSayAlerts ? "on" : "off", INIFileName);
	WritePrivateProfileString(szSayINISection, "AlertCommand", strSayAlertCommand, INIFileName);
	WritePrivateProfileString(szSayINISection, "Timestamps", bSayTimestamps ? "on" : "off", INIFileName);
}

void CreateSayWnd()
{
	if (MQSayWnd)
	{
		return;
	}

	MQSayWnd = new CMQSayWnd("ChatWindow");

	if (!MQSayWnd)
	{
		return;
	}

	LoadSayFromINI(MQSayWnd);
	SaveSayToINI(MQSayWnd); // A) we're masochists, B) this creates the file if its not there..
}

void DestroySayWnd()
{
	if (MQSayWnd)
	{
		sPendingSay.clear();

		SaveSayToINI(MQSayWnd);

		delete MQSayWnd;
		MQSayWnd = nullptr;

		iOldVScrollPos = 0;
	}
}

void MQSayFont(SPAWNINFO* pChar, char* Line)
{
	if (MQSayWnd && Line[0])
	{
		const int size = GetIntFromString(Line, -1);
		if (size < 0 || size > 10)
		{
			WriteChatf("Usage: /sayfont 0-10");
			return;
		}
		MQSayWnd->SetSayFont(size);

		SaveSayToINI(MQSayWnd);
	}
}

void MQSayClear()
{
	if (MQSayWnd)
	{
		MQSayWnd->Clear();
		iOldVScrollPos = 0;
	}
}

void SetSayTitle(SPAWNINFO* pChar, char* Line)
{
	if (MQSayWnd)
	{
		MQSayWnd->SetWindowText(Line);
	}
}

void MQSay(SPAWNINFO* pChar, char* Line)
{
	if (MQSayWnd)
	{
		if (!_stricmp(Line, "reset"))
		{
			MQSayWnd->SetLocked(false);
			CXRect rc = { 300, 10, 600, 210 };
			MQSayWnd->Move(rc, false);

			SaveSayToINI(MQSayWnd);
		}
		else if (!_stricmp(Line, "clear"))
		{
			MQSayClear();
		}
	}

	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, Line, 1);
	//Display Command Syntax
	if (Arg[0] == '\0')
	{
		WriteChatf("[\arMQ2Say\ax] \awPlugin is:\ax %s", bSayStatus ? "\agOn\ax" : "\arOff\ax");
		WriteChatf("Usage: /mqsay <on/off>");
		WriteChatf("/mqsay [Option Name] <value/on/off>");
		WriteChatf("Valid options are Reset, Clear, Alerts, Autoscroll, IgnoreDelay, Reload, Timestamps, Title");
	}
	//user wants to adjust Say Status
	else if (!_stricmp(Arg, "on"))
	{
		CreateSayWnd();
		bSayStatus = true;
		WriteChatf("Say Status is now: \agOn.");
		WritePrivateProfileString("Settings", "SayStatus", bSayStatus ? "on" : "off", INIFileName);
	}
	else if (!_stricmp(Arg, "off"))
	{
		DestroySayWnd();
		bSayStatus = false;
		WriteChatf("Say Status is now: \arOff.");
		WritePrivateProfileString("Settings", "SayStatus", bSayDebug ? "on" : "off", INIFileName);
	}
	else if (!_stricmp(Arg, "reload"))
	{
		LoadSayFromINI(MQSayWnd);
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Settings Reloaded.\ax");
	}
	//user wants to adjust debug
	else if (!_stricmp(Arg, "debug"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("Debug is currently: %s", (bSayDebug ? "\agOn" : "\arOff"));
		}
		//turn it on.
		else if (!_stricmp(Arg, "on"))
		{
			bSayDebug = true;
			WriteChatf("Debug is now: \agOn.");
		}
		//turn it off.
		else if (!_stricmp(Arg, "off"))
		{
			bSayDebug = false;
			WriteChatf("Debug is now: \arOff.");
			WritePrivateProfileString("Settings", "SayDebug", bSayDebug ? "on" : "off", INIFileName);
		}
		else
		{
			WriteChatf("Usage: /mqsay debug [on | off]\n IE: /mqsay debug on");
		}
	}
	//user wants to change the window title
	else if (!_stricmp(Arg, "title"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			if (MQSayWnd != nullptr)
			{
				WriteChatf("The Window title is currently: \ar%s\ax", MQSayWnd->GetWindowText());
			}
			else
			{
				WriteChatf("Say window not found, cannot retrieve Title.");
			}
		}
		else
		{
			const char* szPos = GetNextArg(Line, 1);
			if (szPos[0] != '\"')
			{
				strcpy_s(Arg, szPos);
			}
			MQSayWnd->SetWindowText(Arg);
			WritePrivateProfileString(szSayINISection, "WindowTitle", Arg, INIFileName);
		}
	}
	//user wants to adjust the font size
	else if (!_stricmp(Arg, "font"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("The font size is currently: %s", MQSayWnd->FontSize);
		}
		else if (int fontSize = GetIntFromString(Arg, 0))
		{
			MQSayWnd->SetSayFont(fontSize);
			WritePrivateProfileString(szSayINISection, "FontSize", std::to_string(MQSayWnd->FontSize), INIFileName);
		}
		else
		{
			WriteChatf("Usage: /mqsay font <font size>\n IE: /mqsay font 5");
		}
	}
	//user wants to adjust the Alert Ignore Timer
	else if (!_stricmp(Arg, "IgnoreDelay"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("The Ignore Delay is currently: %i", intIgnoreDelay);
		}
		else
		{
			const int ignoreDelay = GetIntFromString(Arg, -1);
			if (ignoreDelay >= 0)
			{
				intIgnoreDelay = ignoreDelay;
				WriteChatf("The Ignore Delay is now set to: %i", intIgnoreDelay);
				WritePrivateProfileString("Settings", "IgnoreDelay", std::to_string(intIgnoreDelay), INIFileName);
			}
			else
			{
				WriteChatf("Usage: /mqsay ignoredelay <Time In Seconds>\n IE: /mqsay ignoredelay 300");
			}
		}
	}
	//user wants to audible alerts
	else if (!_stricmp(Arg, "alerts"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("Alerts are currently: %s", (bSayAlerts ? "\agOn" : "\arOff"));
		}
		//turn it on.
		else if (!_stricmp(Arg, "on"))
		{
			bSayAlerts = true;
			WriteChatf("Alerts are now: \agOn.");
			WritePrivateProfileString(szSayINISection, "Alerts", "on", INIFileName);
		}
		//turn it off.
		else if (!_stricmp(Arg, "off"))
		{
			bSayAlerts = false;
			WriteChatf("Alerts are now: \arOff.");
			WritePrivateProfileString(szSayINISection, "Alerts", "off", INIFileName);
		}
		else
		{
			WriteChatf("Usage: /mqsay alerts [on | off]\n IE: /mqsay alerts on");
		}
	}
	//user wants to display timestamps
	else if (!_stricmp(Arg, "timestamps"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("Timestamps are currently: %s", (bSayTimestamps ? "\agOn" : "\arOff"));
		}
		//turn it on.
		else if (!_stricmp(Arg, "on"))
		{
			bSayTimestamps = true;
			WriteChatf("Timestamps are now: \agOn.");
			WritePrivateProfileString(szSayINISection, "Timestamps", "on", INIFileName);
		}
		//turn it off.
		else if (!_stricmp(Arg, "off"))
		{
			bSayTimestamps = false;
			WriteChatf("Timestamps are now: \arOff.");
			WritePrivateProfileString(szSayINISection, "Timestamps", "off", INIFileName);
		}
		else
		{
			WriteChatf("Usage: /mqsay timestamps [on | off]\n IE: /mqsay timestamps on");
		}
	}
	//user wants to adjust autoscroll
	else if (!_stricmp(Arg, "autoscroll"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("Autoscroll is currently: %s", (bAutoScroll ? "\agOn" : "\arOff"));
		}
		//turn it on.
		else if (!_stricmp(Arg, "on"))
		{
			bAutoScroll = true;
			WriteChatf("Autoscroll is now: \agOn.");
			WritePrivateProfileString("Settings", "AutoScroll", "on", INIFileName);
		}
		//turn it off.
		else if (!_stricmp(Arg, "off"))
		{
			bAutoScroll = false;
			WriteChatf("Autoscroll is now: \arOff.");
			WritePrivateProfileString("Settings", "AutoScroll", "off", INIFileName);
		}
		else
		{
			WriteChatf("Usage: /mqsay autoscroll [on | off]\n IE: /mqsay autoscroll on");
		}
	}
	//user wants to adjust SaveByChar
	else if (!_stricmp(Arg, "SaveByChar"))
	{
		GetArg(Arg, Line, 2);
		if (Arg[0] == '\0')
		{
			WriteChatf("SaveByChar is currently: %s", (bSaveByChar ? "\agOn" : "\arOff"));
		}
		//turn it on.
		else if (!_stricmp(Arg, "on"))
		{
			bSaveByChar = true;
			WriteChatf("SaveByChar is now: \agOn.");
			WritePrivateProfileString("Settings", "SaveByChar", "on", INIFileName);
		}
		//turn it off.
		else if (!_stricmp(Arg, "off"))
		{
			bSaveByChar = false;
			WriteChatf("SaveByChar is now: \arOff.");
			WritePrivateProfileString("Settings", "SaveByChar", "off", INIFileName);
		}
		else
		{
			WriteChatf("Usage: /mqsay SaveByChar [on | off]\n IE: /mqsay SaveByChar on");
		}
	}
	else
	{
		WriteChatf("%s was not a valid option. Valid options are Reset, Clear, Alerts, Autoscroll, IgnoreDelay, Reload, Timestamps, Title", Arg);
	}
}

PLUGIN_API void OnReloadUI()
{
	// redraw window when you load/reload UI
	CreateSayWnd();
}

PLUGIN_API void OnCleanUI()
{
	// destroy SayWnd before server select & while reloading UI
	DestroySayWnd();
}

PLUGIN_API void SetGameState(int GameState)
{
	DebugSpew("MQ2Say::SetGameState()");
	if (GameState == GAMESTATE_INGAME && !MQSayWnd)
	{
		// we entered the game, set up shop
		if (bSayStatus)
		{
			CreateSayWnd();
		}
	}
}

PLUGIN_API bool OnIncomingChat(const char* Line, DWORD Color)
{
	if (GetGameState() != GAMESTATE_INGAME)
		return false;

	if (!bSayStatus)
		return false;

	switch (Color)
	{
		case 256://Color: 256 - Other player /say messages
			DoAlerts(Line);
			break;
		default:
			//Don't Know these, lets see what it is.
			if (bSayDebug) WriteChatf("[\ar%i\ax]\a-t: \ap%s", Color, Line);
			break;
	}
	return true;
}

PLUGIN_API void OnPulse()
{
	if (MQSayWnd)
	{
		if (GetGameState() == GAMESTATE_INGAME && MQSayWnd->GetZLayer() != 0)
		{
			MQSayWnd->SetZLayer(0);
		}

		// TODO: move all this to OnProcessFrame()
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

			for (size_t N = 0; N < ThisPulse; N++)
			{
				if (!sPendingSay.empty())
				{
					MQSayWnd->OutputBox->AppendSTML(*sPendingSay.begin());
					sPendingSay.pop_front();
				}
			}
			if (bScrollDown)
			{
				// set current vscroll position to bottom
				MQSayWnd->OutputBox->SetVScrollPos(MQSayWnd->OutputBox->GetVScrollMax());
			}
			else
			{
				// autoscroll is disabled and current vscroll position was not at the bottom, retain position
				// note: if the window is full (VScrollMax value between 9793 and 9835), this will not adjust with
				// the flushing of buffer that keeps window a certain max size
				MQSayWnd->OutputBox->SetVScrollPos(iOldVScrollPos);
			}
		}

		// this lets the window draw when we are dead and "hovering"
		if (InHoverState())
		{
			MQSayWnd->DoAllDrawing();
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

	bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override
	{
		MQTypeMember* pMember = MQ2SayType::FindMember(Member);
		if (!pMember)
			return false;

		switch ((SayWndMembers)pMember->ID)
		{
		case Title:
		{
			if (MQSayWnd)
			{
				strcpy_s(DataTypeTemp, MQSayWnd->GetWindowText().c_str());

				Dest.Ptr = &DataTypeTemp[0];
				Dest.Type = mq::datatypes::pStringType;
				return true;
			}
			break;
		}
		default:
			break;
		}
		return false;
	}

	bool ToString(MQVarPtr VarPtr, char* Destination) override
	{
		Destination[0] = '\0';

		if (MQSayWnd)
		{
			strcpy_s(Destination, MAX_STRING, MQSayWnd->GetWindowText().c_str());
		}

		return true;
	}

	bool FromData(MQVarPtr& VarPtr, MQTypeVar& Source) override
	{
		return false;
	}

	bool FromString(MQVarPtr& VarPtr, const char* Source) override
	{
		return false;
	}
};
MQ2SayType* pSayType = nullptr;

bool dataSayWnd(const char* szName, MQTypeVar& Dest)
{
	Dest.DWord = 1;
	Dest.Type = pSayType;
	return true;
}

PLUGIN_API void InitializePlugin()
{
	// Add commands, macro parameters, hooks, etc.
	AddMQ2Data("SayWnd", dataSayWnd);
	pSayType = new MQ2SayType;
	AddCommand("/mqsay", MQSay);
	iStripFirstStmlLines = AddMQ2Benchmark("StripFirstStmlLines");
	LoadSaySettings();
	if (!bSayStatus)
	{
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Loaded sucessfully. Window not displayed due to the plugin being turned off in the ini. Type /mqsay on to turn it back on.\ax");
	}
	else
	{
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Loaded sucessfully.\ax");
	}
}

PLUGIN_API VOID ShutdownPlugin()
{
	sPendingSay.clear();
	// Remove commands, macro parameters, hooks, etc.
	RemoveCommand("/mqsay");
	RemoveMQ2Data("SayWnd");
	delete pSayType;
	RemoveMQ2Benchmark(iStripFirstStmlLines);
	iStripFirstStmlLines = 0;
	DestroySayWnd();
}
