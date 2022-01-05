// MQ2Say.cpp : Say Detection and Alerting
//

#include <mq/Plugin.h>

#include <time.h>
#include <ctime>
#include <iostream>
#include <chrono>
#include <mq/imgui/ImGuiUtils.h>

PreSetup("MQ2Say");
PLUGIN_VERSION(1.3);

constexpr int CMD_HIST_MAX = 50;
constexpr int MAX_CHAT_SIZE = 700;
constexpr int LINES_PER_FRAME = 3;

std::list<CXStr> sPendingSay;
std::map<std::string, std::chrono::steady_clock::time_point> mAlertTimers;

int iOldVScrollPos = 0;
int intIgnoreDelay = 300;
char szSayINISection[MAX_STRING] = { 0 };
char strSayAlertCommand[MAX_STRING] = { 0 };
bool bSayStatus = true;
bool bSayDebug = false;
bool bAutoScroll = true;
bool bSaveByChar = true;
bool bSayAlerts = true;
bool bSayTimestamps = true;
bool bIgnoreGroup = false;
bool bIgnoreGuild = false;
bool bIgnoreFellowship = false;
bool bIgnoreRaid = false;
bool bAlertPerSpeaker = true;
std::string strLastSay = { };
std::string strLastSpeaker = { };

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
						DoCommand(pLocalPlayer, text.c_str());
					}
					else
					{
						char szBuffer[MAX_STRING] = { 0 };
						strcpy_s(szBuffer, text.c_str());
						Echo(pLocalPlayer, szBuffer);
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
								const std::string& s = sCmdHistory.at(iCurrentCmd);
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
	}

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
	}

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
	strLastSpeaker = SaySender;
	strLastSay = SayText;
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

void DoAlerts(const std::string& Line)
{
	if (bSayStatus)
	{
		std::vector<std::string> SaySplit = SplitSay(Line);
		if (SaySplit.size() == 2)
		{
			const char* speakerName = &SaySplit[0][0];
			if (bIgnoreGroup && IsGroupMember(speakerName)
				|| bIgnoreGuild && IsGuildMember(speakerName)
				|| bIgnoreFellowship && IsFellowshipMember(speakerName)
				|| bIgnoreRaid && IsRaidMember(speakerName))
			{
				if (bSayDebug)
				{
					WriteChatf("[\arMQ2Say\ax] \ayDebug\ax: Skipping output due to ignore settings.");
				}
			}
			else
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
						const std::string theSpeaker = bAlertPerSpeaker ? SaySplit[0] : "TheSpeaker";
						if (mAlertTimers.find(theSpeaker) == mAlertTimers.end())
						{
							EzCommand(strSayAlertCommand);
							mAlertTimers[theSpeaker] = std::chrono::steady_clock::now();
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
}

void LoadSaySettings()
{
	bSayStatus = GetPrivateProfileBool("Settings", "SayStatus", bSayStatus, INIFileName);
	bSayDebug = GetPrivateProfileBool("Settings", "SayDebug", bSayDebug, INIFileName);
	bAutoScroll = GetPrivateProfileBool("Settings", "AutoScroll", bAutoScroll, INIFileName);
	bSaveByChar = GetPrivateProfileBool("Settings", "SaveByChar", bSaveByChar, INIFileName);
	bAlertPerSpeaker = GetPrivateProfileBool("Settings", "AlertPerSpeaker", bAlertPerSpeaker, INIFileName);
	intIgnoreDelay = GetPrivateProfileInt("Settings", "IgnoreDelay", intIgnoreDelay, INIFileName);
}

void UpdateszSayINISection()
{
	if (bSaveByChar && pLocalPlayer)
	{
		sprintf_s(szSayINISection, "%s.%s", EQADDR_SERVERNAME, pLocalPlayer->Name);
	}
	else
	{
		strcpy_s(szSayINISection, "Default");
	}
}

void LoadSayFromINI(CSidlScreenWnd* pWindow)
{
	char szTemp[MAX_STRING] = { 0 };

	LoadSaySettings();

	UpdateszSayINISection();

	bIgnoreGroup = GetPrivateProfileBool(szSayINISection, "IgnoreGroup", bIgnoreGroup, INIFileName);
	bIgnoreGuild = GetPrivateProfileBool(szSayINISection, "IgnoreGuild", bIgnoreGuild, INIFileName);
	bIgnoreFellowship = GetPrivateProfileBool(szSayINISection, "IgnoreFellowship", bIgnoreFellowship, INIFileName);
	bIgnoreRaid = GetPrivateProfileBool(szSayINISection, "IgnoreRaid", bIgnoreRaid, INIFileName);

	bSayAlerts = GetPrivateProfileBool(szSayINISection, "Alerts", bSayAlerts, INIFileName);
	const int i = GetPrivateProfileString(szSayINISection, "AlertCommand", "/multiline ; /beep ; /timed 1 /beep ; /timed 2 /beep", strSayAlertCommand, MAX_STRING, INIFileName);
	if (i == 0)
	{
		strcpy_s(strSayAlertCommand, "/multiline ; /beep ; /timed 1 /beep ; /timed 2 /beep");
	}
	bSayTimestamps = GetPrivateProfileBool(szSayINISection, "Timestamps", bSayTimestamps, INIFileName);

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
	pWindow->SetWindowText(szTemp);
}

void SaveSayToINI(CSidlScreenWnd* pWindow)
{
	WritePrivateProfileBool("Settings", "SayStatus", bSayStatus, INIFileName);
	WritePrivateProfileBool("Settings", "SayDebug", bSayDebug, INIFileName);
	WritePrivateProfileBool("Settings", "AutoScroll", bAutoScroll, INIFileName);
	WritePrivateProfileBool("Settings", "SaveByChar", bSaveByChar, INIFileName);
	WritePrivateProfileBool("Settings", "AlertPerSpeaker", bAlertPerSpeaker, INIFileName);
	WritePrivateProfileInt("Settings", "IgnoreDelay", intIgnoreDelay, INIFileName);
	WritePrivateProfileBool(szSayINISection, "IgnoreGroup", bIgnoreGroup, INIFileName);
	WritePrivateProfileBool(szSayINISection, "IgnoreGuild", bIgnoreGuild, INIFileName);
	WritePrivateProfileBool(szSayINISection, "IgnoreFellowship", bIgnoreFellowship, INIFileName);
	WritePrivateProfileBool(szSayINISection, "IgnoreRaid", bIgnoreRaid, INIFileName);
	WritePrivateProfileBool(szSayINISection, "Alerts", bSayAlerts, INIFileName);
	WritePrivateProfileString(szSayINISection, "AlertCommand", strSayAlertCommand, INIFileName);
	WritePrivateProfileBool(szSayINISection, "Timestamps", bSayTimestamps, INIFileName);
	WritePrivateProfileString(szSayINISection, "WindowTitle", pWindow->GetWindowText().c_str(), INIFileName);
	if (pWindow->IsMinimized())
	{
		WritePrivateProfileString(szSayINISection, "SayTop", std::to_string(pWindow->GetOldLocation().top), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayBottom", std::to_string(pWindow->GetOldLocation().bottom), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayLeft", std::to_string(pWindow->GetOldLocation().left), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayRight", std::to_string(pWindow->GetOldLocation().right), INIFileName);
	}
	else
	{
		WritePrivateProfileString(szSayINISection, "SayTop", std::to_string(pWindow->GetLocation().top), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayBottom", std::to_string(pWindow->GetLocation().bottom), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayLeft", std::to_string(pWindow->GetLocation().left), INIFileName);
		WritePrivateProfileString(szSayINISection, "SayRight", std::to_string(pWindow->GetLocation().right), INIFileName);
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
}

void CreateSayWnd()
{
	if (MQSayWnd)
	{
		return;
	}

	MQSayWnd = new CMQSayWnd("ChatWindow");

	LoadSayFromINI(MQSayWnd);
	SaveSayToINI(MQSayWnd);
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
	if (MQSayWnd && Line[0] != '\0')
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

void ShowSetting(bool bSetting, char* szSettingName) {
	WriteChatf("[\arMQ2Say\ax] \ap%s\aw is currently %s", szSettingName, (bSetting ? "\agOn" : "\arOff"));
}

void MQSay(SPAWNINFO* pChar, char* Line)
{
	char Arg[MAX_STRING] = { 0 };
	GetArg(Arg, Line, 1);
	//Display Command Syntax
	if (Arg[0] == '\0' || !_stricmp(Arg, "help"))
	{
		WriteChatf("[\arMQ2Say\ax] \awPlugin is:\ax %s", bSayStatus ? "\agOn\ax" : "\arOff\ax");
		WriteChatf("Usage: /mqsay <on/off>");
		WriteChatf("/mqsay [Option Name] <value/on/off>");
		WriteChatf("Valid options are Reset, Clear, Alerts, AlertPerSpeaker, Autoscroll, IgnoreDelay, Fellowship, Group, Guild, Raid, Reload, Timestamps, Title, Settings");
		WriteChatf("/mqsay ui -> will display the say settings panel.");
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
		LoadSayFromINI(MQSayWnd);
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Settings Reloaded.\ax");
	}
	else if (!_stricmp(Line, "reset"))
	{
		if (MQSayWnd)
		{
			MQSayWnd->SetLocked(false);
			// The settings below are also in the LoadSayFromINI function
			CXRect rc = { 300, 10, 600, 210 };
			MQSayWnd->Move(rc, false);

			SaveSayToINI(MQSayWnd);
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
			if (MQSayWnd != nullptr)
			{
				WriteChatf("[\arMQ2Say\ax] \awThe Window title is currently:\ax \ar%s\ax", MQSayWnd->GetWindowText().c_str());
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
			MQSayWnd->SetWindowText(Arg);
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
		else if (const int fontSize = GetIntFromString(Arg, 0))
		{
			MQSayWnd->SetSayFont(fontSize);
			WritePrivateProfileString(szSayINISection, "FontSize", std::to_string(MQSayWnd->FontSize), INIFileName);
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
			const int ignoreDelay = GetIntFromString(Arg, -1);
			if (ignoreDelay >= 0)
			{
				intIgnoreDelay = ignoreDelay;
				WriteChatf("[\arMQ2Say\ax] \awThe Ignore Delay is now set to:\ax %i", intIgnoreDelay);
				WritePrivateProfileString("Settings", "IgnoreDelay", std::to_string(intIgnoreDelay), INIFileName);
			}
			else
			{
				WriteChatf("Usage: /mqsay ignoredelay <Time In Seconds>\n IE: /mqsay ignoredelay 300");
			}
		}
	}
	else if (!_stricmp(Arg, "group"))
	{
		GetArg(Arg, Line, 2);
		bIgnoreGroup = AdjustBoolSetting("group", szSayINISection, "IgnoreGroup", Arg, bIgnoreGroup);
	}
	else if (!_stricmp(Arg, "guild"))
	{
		GetArg(Arg, Line, 2);
		bIgnoreGuild = AdjustBoolSetting("guild", szSayINISection, "IgnoreGuild", Arg, bIgnoreGuild);
	}
	else if (!_stricmp(Arg, "fellowship"))
	{
		GetArg(Arg, Line, 2);
		bIgnoreFellowship = AdjustBoolSetting("fellowship", szSayINISection, "IgnoreFellowship", Arg, bIgnoreFellowship);
	}
	else if (!_stricmp(Arg, "raid"))
	{
		GetArg(Arg, Line, 2);
		bIgnoreRaid = AdjustBoolSetting("raid", szSayINISection, "IgnoreRaid", Arg, bIgnoreRaid);
	}
	else if (!_stricmp(Arg, "alerts"))
	{
		GetArg(Arg, Line, 2);
		bSayAlerts = AdjustBoolSetting("alerts", szSayINISection, "Alerts", Arg, bSayAlerts);
	}
	else if (!_stricmp(Arg, "alertperspeaker"))
	{
		GetArg(Arg, Line, 2);
		bAlertPerSpeaker = AdjustBoolSetting("AlertPerSpeaker", "Settings", "AlertPerSpeaker", Arg, bAlertPerSpeaker);
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
		UpdateszSayINISection();
	}
	else if (!_stricmp(Arg, "Settings")) {
		ShowSetting(bSayStatus, "Plugin");
		ShowSetting(bSayDebug, "Debug");
		ShowSetting(bAutoScroll, "Autoscroll");
		ShowSetting(bSaveByChar, "SaveByChar");
		ShowSetting(bSayAlerts, "Alerts");
		ShowSetting(bSayTimestamps, "Timestamps");
		ShowSetting(bIgnoreGroup, "Ignore Group");
		ShowSetting(bIgnoreGuild, "Ignore Guild");
		ShowSetting(bIgnoreFellowship, "Ignore Fellowship");
		ShowSetting(bIgnoreRaid, "Ignore Raid");
	}
	else if (!_stricmp(Arg, "ui") || !_stricmp(Arg, "gui")) {
		EzCommand("/mqsettings plugins/say");
	}
	else
	{
		WriteChatf("[\arMQ2Say\ax] %s was not a valid option. Use \ag/mqsay help\ax to show options.", Arg);
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
	if (GetGameState() == GAMESTATE_INGAME
		&& bSayStatus
		&& Color == USERCOLOR_SAY)
	{
		DoAlerts(Line);
	}

	return false;
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
		LastSay,
		LastSpeaker
	};

	MQ2SayType() :MQ2Type("saywnd")
	{
		TypeMember(Title);
		TypeMember(LastSay);
		TypeMember(LastSpeaker);
	}

	bool GetMember(MQVarPtr VarPtr, const char* Member, char* Index, MQTypeVar& Dest) override
	{
		MQTypeMember* pMember = MQ2SayType::FindMember(Member);
		if (!pMember)
			return false;

		// Default to string type since that's our most common return
		Dest.Type = mq::datatypes::pStringType;

		switch ((SayWndMembers)pMember->ID)
		{
			case LastSay:
				Dest.Ptr = &strLastSay[0];
				return !strLastSay.empty();
			case LastSpeaker:
				Dest.Ptr = &strLastSpeaker[0];
				return !strLastSpeaker.empty();
			case Title:
			{
				if (MQSayWnd)
				{
					strcpy_s(DataTypeTemp, MQSayWnd->GetWindowText().c_str());

					Dest.Ptr = &DataTypeTemp[0];
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
};
MQ2SayType* pSayType = nullptr;

bool dataSayWnd(const char* szName, MQTypeVar& Dest)
{
	Dest.DWord = 1;
	Dest.Type = pSayType;
	return true;
}

struct PluginCheckbox {
	const char* name;
	const char* visiblename;
	const char* inisection;
	bool* value;
	const char* helptext;
};

static const PluginCheckbox checkboxes[] = {
	{ "SayStatus", "Plugin On / Off", "Settings", &bSayStatus, "Toggle the plugin On / Off.\n\nINI Setting: SayStatus" },
	{ "SayDebug", "Plugin Debbuging", "Settings", &bSayDebug, "Toggle plugin debugging.\n\nINI Setting: SayDebug" },
	{ "AutoScroll", "AutoScroll Chat", "Settings", &bAutoScroll, "Toggle autoScrolling of the chat window.\n\nINI Setting: AutoScroll" },
	{ "TimeStamps", "Display Timestamps", szSayINISection, &bSayTimestamps, "Toggle to display timestamps.\n\nINI Setting: TimeStamps" },
};

static const PluginCheckbox ignores[] = {
	{ "IgnoreGroup", "Ignore Group Members", szSayINISection, &bIgnoreGroup, "Toggle to ignore your group from triggering the say alert.\n\nINI Setting: IgnoreGroup" },
	{ "IgnoreGuild", "Ignore Guild Members", szSayINISection, &bIgnoreGuild,  "Toggle to ignore your guild from triggering the say alert.\n\nINI Setting: IgnoreGuild" },
	{ "IgnoreFellowship", "Ignore Fellowship Members", szSayINISection, &bIgnoreFellowship,  "Toggle to ignore your fellowship from triggering the say alert.\n\nINI Setting: IgnoreFellowship" },
	{ "IgnoreRaid", "Ignore Raid Members", szSayINISection, &bIgnoreRaid, "Toggle to ignore your raid from triggering the say alert.\n\nINI Setting: IgnoreRaid" },
};

void SayImGuiSettingsPanel()
{
	ImGui::Text("General");
	for (const PluginCheckbox& cb : checkboxes)
	{
		if (ImGui::Checkbox(cb.visiblename, cb.value))
		{
			WritePrivateProfileBool(cb.inisection, cb.name, cb.value, INIFileName);
		}
		ImGui::SameLine();
		mq::imgui::HelpMarker(cb.helptext);
	}

	// We need to handle SaveByChar individicually so we reload so the szSayINISection is updated
	if (ImGui::Checkbox("Per Character Settings", &bSaveByChar))
	{
		WritePrivateProfileBool("Settings", "SaveByChar", bSaveByChar, INIFileName);
		UpdateszSayINISection();
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Toggle Saving your options per character.\n\nINI Setting: SaveByChar");

	ImGui::NewLine();
	ImGui::Text("Ignores");
	ImGui::Separator();
	for (const PluginCheckbox cb : ignores)
	{
		if (ImGui::Checkbox(cb.visiblename, cb.value))
		{
			WritePrivateProfileBool(cb.inisection, cb.name, cb.value, INIFileName);
		}
		ImGui::SameLine();
		mq::imgui::HelpMarker(cb.helptext);
	}

	ImGui::NewLine();
	ImGui::Text("Alerts");
	ImGui::Separator();
	if (ImGui::Checkbox("Alert Per Speaker", &bAlertPerSpeaker)) {
		WritePrivateProfileBool("Settings", "AlertPerSpeaker", bAlertPerSpeaker, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Toggle to only alert once per speaker per alert delay.\n\nINISetting: AlertPerSpeaker");

	if (ImGui::Checkbox("Use Alert Command", &bSayAlerts)) {
		WritePrivateProfileBool(szSayINISection, "Alerts", bSayAlerts, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("Toggle to use the Alert Command.\n\nINISetting: Alerts");

	ImGui::SetNextItemWidth(-125);
	if (ImGui::InputInt("Alert Delay", &intIgnoreDelay)) {
		if (intIgnoreDelay < 0)
			intIgnoreDelay = 0;
		WritePrivateProfileInt("Settings", "IgnoreDelay", intIgnoreDelay, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("The time in seconds that you would like to delay from triggering an additional alert per speaker.\n\nINISetting: Alerts");

	ImGui::SetNextItemWidth(-125);
	if (ImGui::InputTextWithHint("Alert Command", "/ command to execute upon alert", strSayAlertCommand, IM_ARRAYSIZE(strSayAlertCommand)))
	{
		WritePrivateProfileString(szSayINISection, "AlertCommand", strSayAlertCommand, INIFileName);
	}
	ImGui::SameLine();
	mq::imgui::HelpMarker("This is the slash command you would like to execute an alert triggers. Example: /multiline; /beep; /timed 5 /beep\n\nINISetting: Command Line");
}

PLUGIN_API void InitializePlugin()
{
	// Add commands, macro parameters, hooks, etc.
	AddMQ2Data("SayWnd", dataSayWnd);
	pSayType = new MQ2SayType;
	AddCommand("/mqsay", MQSay);
	LoadSaySettings();
	if (!bSayStatus)
	{
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Loaded successfully. Window not displayed due to the plugin being turned off in the ini. Type /mqsay on to turn it back on.\ax");
	}
	else
	{
		WriteChatf("[\arMQ2Say\ax] \a-wPlugin Loaded successfully.\ax");
	}

	AddSettingsPanel("plugins/Say", SayImGuiSettingsPanel);
}

PLUGIN_API VOID ShutdownPlugin()
{
	sPendingSay.clear();
	// Remove commands, macro parameters, hooks, etc.
	RemoveCommand("/mqsay");
	RemoveMQ2Data("SayWnd");
	delete pSayType;
	DestroySayWnd();
	RemoveSettingsPanel("plugins/Say");
}
