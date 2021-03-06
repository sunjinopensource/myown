#include "FsGame/SocialSystemModule/TeamModule.h"
#include "FsGame/Define/PubDefine.h"
#include "FsGame/Define/ClientCustomDefine.h"
#include "FsGame/Define/ServerCustomDefine.h"
#include "FsGame/Define/CommandDefine.h"
#include "FsGame/Define/TeamDefine.h"
#include "FsGame/Define/BufferDefine.h"
#include "FsGame/SocialSystemModule/RequestModule.h"
#include "FsGame/CommonModule/LuaExtModule.h"
#include "FsGame/Define/GameDefine.h"
#include "utils/extend_func.h"
#include "utils/util_func.h"
#include "utils/custom_func.h"
#include "FsGame/SocialSystemModule/FriendModule.h"
#include <string>
#include <cmath>
#include <algorithm>
#include "utils/string_util.h"
//#include "SceneBaseModule/TeamSceneModule.h"
#include "CommonModule/AsynCtrlModule.h"
#include "NpcBaseModule/AI/Template/AITemplateBase.h"
#include "CommonModule/LogModule.h"
#include "utils/XmlFile.h"
//#include "FsGame\CrossBattle\PVPWar.h"
//#include "FsGame/SocialSystemModule/EscortModule.h"
#include "FsGame/CommonModule/LevelModule.h"
//#include "FsGame/SocialSystemModule/TeamOfferModule.h"
#include "Define/PlayerBaseDefine.h"
#include "CommonModule/EnvirValueModule.h"
#include "Interface/ILuaExtModule.h"
#include "public/Inlines.h"
#include "SkillModule/BufferModule.h"
#include "Define/SnsDefine.h"

TeamModule* TeamModule::m_pTeamModule = NULL;
RequestModule* TeamModule::m_pRequestModule = NULL;
FriendModule* TeamModule::m_pFriendModule = NULL;
std::wstring TeamModule::m_domainName = L"";  // 公共数据名称


int GMCreateTeam(void *state)
{

	IKernel* pKernel = LuaExtModule::GetKernel(state);

	//检查参数数量
	CHECK_ARG_NUM(state, GMCreateTeam, 1);

	// 检查参数类型
	CHECK_ARG_OBJECT(state, GMCreateTeam, 1);
	//读取参数
	PERSISTID self = pKernel->LuaToObject(state, 1);
	int nScore = pKernel->LuaToInt(state, 2);
	if (!pKernel->Exists(self))
	{
		return 1;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 1;
	}

	if (TeamModule::m_pTeamModule->IsInTeam(pKernel, self))
	{
		// 已经在队伍中，不能创建队伍
		return 1;
	}
	//int nation = pSelfObj->QueryInt(FIELD_PROP_NATION);
	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< TeamModule::m_pTeamModule->GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_CREATE
		<< pSelfObj->QueryWideStr("Name")
		<< L"";
	pKernel->SendPublicMessage(pub_msg);
	return 0;
}



std::map<int, TeamModule::TeamRule> TeamModule::m_TeamRule;
std::map<int, CheckTeamCondition>	TeamModule::m_check_team_condition;

std::map<int, CheckTeamResult> TeamModule::m_check_team_result;



// 初始化
bool TeamModule::Init(IKernel* pKernel)
{
    m_pTeamModule = this;

    m_pRequestModule = dynamic_cast<RequestModule*>(pKernel->GetLogicModule("RequestModule"));
	m_pFriendModule = dynamic_cast<FriendModule*>(pKernel->GetLogicModule("FriendModule"));

    Assert(m_pTeamModule != NULL && m_pRequestModule != NULL && m_pFriendModule != NULL);

    // 注册请求
	// 申请加入
    m_pRequestModule->RegisterRequestCallBack(REQUESTTYPE_JOIN_SINGLEPLAYER,
                                              TeamModule::CanRequestJoinTeam,
                                              TeamModule::CanBeRequestJoinTeam,
                                              TeamModule::RequestJoinTeamSuccess,
                                              TeamModule::GetRequestParas,
                                              TeamModule::GetRequestParas,
                                              TeamModule::RequestJoinTeamResult);

	// 邀请
    m_pRequestModule->RegisterRequestCallBack(REQUESTTYPE_INVITE_SINGLEPLAYER,
                                              TeamModule::CanRequestJoinTeam,
                                              TeamModule::CanBeRequestJoinTeam,
                                              TeamModule::RequestJoinTeamSuccess,
                                              TeamModule::GetRequestParas,
                                              TeamModule::GetRequestParas,
                                              TeamModule::RequestJoinTeamResult);

    // 注册事件回调
	// 上线
    pKernel->AddEventCallback("player", "OnRecover", OnRecover);
	// 客户端准备就绪
    pKernel->AddEventCallback("player", "OnReady", OnReady);
	// 玩家离线回调
	pKernel->AddEventCallback("player", "OnStore", OnStore);

	// 玩家离线回调
	pKernel->AddEventCallback("player", "OnContinue", OnContinue);

    pKernel->AddEventCallback("scene", "OnPublicMessage", OnPublicMessage);
    //pKernel->AddEventCallback("player", "OnPublicMessage", OnPublicMessage);
    // 组队客户端系统
    pKernel->AddIntCustomHook("player", CLIENT_CUSTOMMSG_TEAM, OnCustomMessage);
	// 组队服务器端
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_MSG, OnCommandMessage);
    // 成员上线
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_MEMBER_RECOVER, OnCommandTeamMemberRecover);
    // 职位改变
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_MEMBER_CHANGE, OnCommandTeamMemberChange);
    // 创建队伍成功
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_CREATE, OnCommandCreateTeamResult);
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_JOIN, OnCommandJoinTeam);
    // 添加成员
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_ADD_MEMBER, OnCommandAddMember);
    // 删除成员
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_REMOVE_MEMBER, OnCommandRemoveMember);
    // 退出队伍
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_CLEAR, OnCommandClearTeam);
    // 更新成员信息
	pKernel->AddIntCommandHook("player", COMMAND_TEAM_UPDATE_MEMBER_INFO, OnCommandUpdateMemberInfo);
    // 更新队伍信息
    pKernel->AddIntCommandHook("player", COMMAND_TEAM_UPDATE_TEAM_INFO, OnCommandUpdateTeamInfo);
	// 清除附近队伍列表信息
    pKernel->AddIntCommandHook("player", COMMAND_NEARBYTEAM_CLEAR, OnCommandClearNearbyTeam);
	pKernel->AddIntCommandHook("player", COMMAND_TEAM_CHECK_CONDITION, OnCommandCheckCondition);
	// 玩家升级
	pKernel->AddIntCommandHook("player", COMMAND_LEVELUP_CHANGE, TeamModule::OnCommandLevelUp);
//	pKernel->AddIntCommandHook("player", COMMAND_QUEST_PROGRESS_MSG, TeamModule::OnCommandTask, 0);
    // 属性回调
    DECL_CRITICAL(TeamModule::C_OnJobChanged);
    DECL_CRITICAL(TeamModule::C_OnMaxHPChange);
    DECL_CRITICAL(TeamModule::C_OnGuildNameChange);
    DECL_CRITICAL(TeamModule::C_OnHPChange);

	DECL_CRITICAL(TeamModule::C_OnHighestBattleAbilityChange);

	DECL_CRITICAL(TeamModule::C_OnFightState);

	// 更新队员位置心跳
	DECL_HEARTBEAT(TeamModule::H_UpdateMemberPos);
	DECL_CRITICAL(TeamModule::C_OnVIPLevelChanged);
	LoadRes(pKernel);
	DECL_LUA_EXT(GMCreateTeam);

    return true;
}

bool TeamModule::Shut(IKernel* pKernel)
{
    return true;
}

void TeamModule::ReloadConfig(IKernel*pKernel)
{
	m_pTeamModule->LoadRes(pKernel);
}


bool TeamModule::LoadRes(IKernel*pKernel)
{
	return LoadTeamRule(pKernel)&&
		LoadActivitySccene(pKernel);
		
}

bool TeamModule::LoadTeamRule(IKernel*pKernel)
{
	std::string pathName = pKernel->GetResourcePath();
	//配置文件路径
	pathName += TEAM_SCENE_CONFIG;
	CXmlFile xmlfile(pathName.c_str());
	if (!xmlfile.LoadFromFile())
	{
		std::string err_msg = pathName;
		err_msg.append(" does not exists.");
		::extend_warning(LOG_ERROR, err_msg.c_str());
		return false;
	}
	m_TeamRule.clear();
	std::string strSectionName = "";
	const size_t iSectionCount = xmlfile.GetSectionCount();
	LoopBeginCheck(a)
		for (unsigned i = 0; i < iSectionCount; i++)
		{
			LoopDoCheck(a);
			strSectionName = xmlfile.GetSectionByIndex(i);
			if (StringUtil::CharIsNull(strSectionName.c_str())){
				continue;
			}
			int id = StringUtil::StringAsInt(strSectionName.c_str());
			TeamRule teamInfo;
			teamInfo.m_teamType = xmlfile.ReadInteger(strSectionName.c_str(), "TeamType", 0);
			teamInfo.m_objectID = xmlfile.ReadInteger(strSectionName.c_str(), "ObjectID", 0);
			teamInfo.m_limitLevel = xmlfile.ReadInteger(strSectionName.c_str(), "LimitLevel", 0);
			std::string openTimes = xmlfile.ReadString(strSectionName.c_str(), "OpenTime", "");
			CVarList args;
			StringUtil::ParseToVector(openTimes.c_str(), ':', args);
			if (args.GetCount() >= 4)
			{
				teamInfo.m_openTime.weekDay = args.IntVal(0);
				teamInfo.m_openTime.daySec = args.IntVal(1) * 60 * 60 + args.IntVal(2) * 60 + args.IntVal(3);
			}
			std::string endTimes = xmlfile.ReadString(strSectionName.c_str(), "EndTimes", "");
			args.Clear();
			StringUtil::ParseToVector(endTimes.c_str(), ':', args);
			if (args.GetCount() >= 4)
			{
				teamInfo.m_endTime.weekDay = (unsigned char)args.IntVal(0);
				teamInfo.m_endTime.daySec = args.IntVal(1) * 60 * 60 + args.IntVal(2) * 60 + args.IntVal(3);
			}
			teamInfo.m_finishAward = xmlfile.ReadString(strSectionName.c_str(), "FinishAward", "");
			m_TeamRule.insert(std::make_pair(id, teamInfo));
		}
	return true;
}


bool TeamModule::LoadActivitySccene(IKernel*pKernel)
{
	std::string pathName = pKernel->GetResourcePath();
	//配置文件路径
	pathName += TEAM_ACTIVITY_SCENE_CONFIG;
	CXmlFile xmlfile(pathName.c_str());
	if (!xmlfile.LoadFromFile())
	{
		std::string err_msg = pathName;
		err_msg.append(" does not exists.");
		::extend_warning(LOG_ERROR, err_msg.c_str());
		return false;
	}
	m_activityScene.clear();
	std::string strSectionName = "";
	const size_t iSectionCount = xmlfile.GetSectionCount();
	LoopBeginCheck(a)
		for (unsigned i = 0; i < iSectionCount; i++)
		{
			LoopDoCheck(a);
			strSectionName = xmlfile.GetSectionByIndex(i);
			if (StringUtil::CharIsNull(strSectionName.c_str())){
				continue;
			}
			int id = StringUtil::StringAsInt(strSectionName.c_str());
			
			int sceneID = xmlfile.ReadInteger(strSectionName.c_str(), "SceneID", 0);
			m_activityScene.insert(sceneID);
			
		}
	return true;
}

// 自己是否在队伍中
bool TeamModule::IsInTeam(IKernel* pKernel, const PERSISTID& self)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

    return pSelfObj->FindAttr("TeamID") && pSelfObj->QueryInt("TeamID") > 0;
}





// 自己上线
void TeamModule::OnSelfRecover(IKernel* pKernel, const PERSISTID& self)
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

    // 如果自己下线前在队伍中
    int team_id = pSelfObj->QueryInt("TeamID");

    if (team_id > 0)
    {		
        // 取得自己的名字
        const wchar_t* self_name = pSelfObj->QueryWideStr("Name");
		
		CVarList pub_msg;
		pub_msg << PUBSPACE_DOMAIN 
					  << GetDomainName(pKernel).c_str()
					  << SP_DOMAIN_MSG_TEAM_ON_RECOVER 
					  << team_id 
					  << self_name;
        // 向服务器验证自己是否在队伍中
        pKernel->SendPublicMessage(pub_msg);
    }
    else
    {
		ClearTeamInfo(pKernel, self);
    }
}

// 自己是否是队长
bool TeamModule::IsTeamCaptain(IKernel* pKernel, const PERSISTID& self)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *objSelf = pKernel->GetGameObj(self); 
	if(NULL == objSelf)
	{
		return false;
	}

    // 保护
    int team_id = objSelf->QueryInt("TeamID");

    if (team_id <= 0)
    {
        return false;
    }

	IRecord *pTeamRec = objSelf->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return false;
	}

    // 自己名称
    const wchar_t* name = objSelf->QueryWideStr("Name");
    // 查找
    int row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, name);
	if (row < 0)
	{
		return false;
	}

    int work = pTeamRec->QueryInt(row, TEAM_REC_COL_TEAMWORK);

    if (TYPE_TEAM_CAPTAIN == work)
    {
        return true;
    }

    // 队长名称
    const wchar_t* captain = objSelf->QueryWideStr("TeamCaptain");

    return wcscmp(name, captain) == 0;
}

// 对方是否是自己的队友
bool TeamModule::IsTeamMate(IKernel* pKernel, const PERSISTID& self,
                            const wchar_t* target_name)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

    // 保护
    int team_id = pSelfObj->QueryInt("TeamID");
    if (team_id <= 0)
    {
        return false;
    }

	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return false;
	}

    // 查找
    int row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, target_name);

    return row >= 0;
}

bool TeamModule::IsBeKickTeam(IKernel* pKernel, const PERSISTID& self, const wchar_t* target_name)
{

	if (!pKernel->Exists(self))
	{
		return false;
	}
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

	// 保护
	int team_id = pSelfObj->QueryInt("TeamID");
	if (team_id <= 0)
	{
		return false;
	}

	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return false;
	}

	// 查找
	int row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, target_name);

	if (row < 0){
		return false;
	}

	int value = pTeamRec->QueryInt(row, TEAM_REC_COL_BE_KICK);
	return value == TEAM_IS_KICK_OFF;
}

// 获得队友名称列表
bool TeamModule::GetTeamMemberList(IKernel* pKernel, const PERSISTID& self, IVarList& lst)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

    // 保护
    int team_id = pSelfObj->QueryInt("TeamID");
    if (team_id <= 0)
    {
        return false;
    }

	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return false;
	}

    // 遍历
    int rows = pTeamRec->GetRows();
	LoopBeginCheck(a);
    for (int i = 0; i < rows; ++i)
    {
		LoopDoCheck(a);
        const wchar_t* member_name = pTeamRec->QueryWideStr(i, TEAM_REC_COL_NAME);
        lst << member_name;
    }

    return lst.GetCount() > 0;
}

// 获得队员名称列表
void TeamModule::GetTeamMemberList(IKernel* pKernel, const int team_id, IVarList& lst)
{
	// 公共数据指针
	IPubData *pTeamData = m_pTeamModule->GetTeamPubData(pKernel);
	if (NULL == pTeamData)
	{
		return;
	}

	// 取得队伍成员表名
	char team_member_rec_name[256];
	SPRINTF_S(team_member_rec_name, "team_%d_member_rec", team_id);

	// 遍历队伍成员表
	IRecord *pTeamMemRec = pTeamData->GetRecord(team_member_rec_name);
	if (NULL == pTeamMemRec)
	{
		return;
	}

	int rows = pTeamMemRec->GetRows();
	LoopBeginCheck(e);
	for (int i = 0; i < rows; ++i)
	{
		LoopDoCheck(e);
		const wchar_t* member_name = pTeamMemRec->QueryWideStr(i, TEAM_REC_COL_NAME);

		lst << member_name;
	}
}

// 获取用来更新附近队伍表的信息
bool TeamModule::GetNearByTeamInfo(IKernel* pKernel, PERSISTID playID, IVarList& lst)
{
	if (!pKernel->Exists(playID))
	{
		return false;
	}
	IGameObj *pSelfObj = pKernel->GetGameObj(playID);
	if (NULL == pSelfObj)
	{
		return false;
	}

	// 自己名称
	const wchar_t* name = pSelfObj->QueryWideStr("Name");

	// 查找
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return false;
	}
	//人数
	int numbers = pTeamRec->GetRows();
	CVarList values;
	int row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, name);
	if (row < 0)
	{
		return false;
	}
	if (!pTeamRec->QueryRowValue(row, values))
	{
		return false;
	}
	const wchar_t* guildName = pSelfObj->QueryWideStr("GuildName");


	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return false;
	}

	IPubData* pTeamData = pDomainSpace->GetPubData(GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return false;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec)
	{
		return false;
	}

	int teamRow = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, lst.IntVal(0));
	if (teamRow < 0)
	{
		return false;
	}

	if (pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OFF_JOIN_TEAM) == TEAM_JOIN_TEAM_OFF){
		return false;
	};

	int teamObj = GetTeamObjIndex(pKernel, lst.IntVal(0));
	// 同步成员表中队长的信息到附近队伍表中
	lst <<values.WideStrVal(TEAM_REC_COL_NAME)
		<<values.IntVal(TEAM_REC_COL_BGLEVEL)
		<< values.IntVal(TEAM_REC_COL_BGJOB)
		<< values.IntVal(TEAM_REC_COL_SEX)
		<< values.IntVal(TEAM_REC_COL_FIGHT)
		<< teamObj
		<< numbers;

	return true;
}

// 获得队长名称
const wchar_t* TeamModule::GetCaptain(IKernel* pKernel, const PERSISTID& self)
{
	if (!pKernel->Exists(self))
	{
		return NULL;
	}
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return NULL;
	}

    return pSelfObj->QueryWideStr("TeamCaptain");
}

// 获得队伍公共数据
IPubData* TeamModule::GetTeamPubData(IKernel* pKernel)
{

    IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
    if (pDomainSpace == NULL)
    {
        return NULL;
    }

    return pDomainSpace->GetPubData(GetDomainName(pKernel).c_str());
}

// 上线
int TeamModule::OnRecover(IKernel* pKernel, const PERSISTID& self,
                          const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	// 添加队伍成员属性回调
	m_pTeamModule->AddCriticalCallBack(pKernel,self);
	
	// 上线后处理队伍信息
    m_pTeamModule->OnSelfRecover(pKernel, self);
	
    return 0;
}

// 进入场景就绪
int TeamModule::OnReady(IKernel* pKernel, const PERSISTID& self,
                        const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	
	if (m_pTeamModule->IsActivityScene(pKernel))
	{
		OnCustomLeave(pKernel, self, self, CVarList());
		if (!m_pTeamModule->IsInTeam(pKernel, self))
		{
			AutoMatch(pKernel, self, TEAM_AUTO_MATCH_OFF);
		}
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17402, CVarList());
		return 0;
	}
	

    // 是否在队伍中
    if (!m_pTeamModule->IsInTeam(pKernel, self))
    {
        return 0;
    }
	
	// 在队伍中，更新位置
	m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "Scene" << "Online");


    return 0;
}

// 玩家离开游戏
int TeamModule::OnStore(IKernel* pKernel, const PERSISTID& self,
						const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

	int reason = args.IntVal(0);
	if (reason != STORE_EXIT)
	{
		return 0;
	}
	
	// 在队伍中
    if (m_pTeamModule->IsInTeam(pKernel, self))
    {
		// 在队伍中，设置下线
		int team_id = pSelfObj->QueryInt("TeamID");
		const wchar_t* self_name = pSelfObj->QueryWideStr("Name");
		
		CVarList msg;
		msg << PUBSPACE_DOMAIN 
			<< GetDomainName(pKernel).c_str() 
			<< SP_DOMAIN_MSG_TEAM_UPDATE_MEMBER 
			<< team_id 
			<< self_name
			<< TEAM_REC_COL_ONLINE 
			<< OFFLINE;
		pKernel->SendPublicMessage(msg);

		


        // 下线时间
        int64_t nNowTime = util_get_time_64();
		// 发送消息
		CVarList pub_msg;
		pub_msg << PUBSPACE_DOMAIN 
			<< GetDomainName(pKernel).c_str() 
			<< SP_DOMAIN_MSG_TEAM_LEAVE
			<< team_id 
			<< self_name 
			<< LEAVE_TYPE_OFFLINE
			<< nNowTime;
		pKernel->SendPublicMessage(pub_msg);
       
    }
    else
    {

		TeamModule::AutoMatch(pKernel, self, TEAM_AUTO_MATCH_OFF);
        m_pTeamModule->ClearTeamInfo(pKernel, self);
    }

	
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_PLAYER_STORE
		<< pSelfObj->QueryWideStr(FIELD_PROP_NAME);
		
	pKernel->SendPublicMessage(pub_msg);
    return 0;
}

// 顶号
int TeamModule::OnContinue(IKernel* pKernel, const PERSISTID& self,
						const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	// 是否在队伍中
	if (!m_pTeamModule->IsInTeam(pKernel, self))
	{
		return 0;
	}

	// 在队伍中，更新在线状态
	m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "Online");

	return 0;
}

// 公共数据服务器下传的消息
int TeamModule::OnPublicMessage(IKernel* pKernel, const PERSISTID& self,
                                const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	const char* space_name = args.StringVal(0);
	const wchar_t* data_name = args.WideStrVal(1);

	if (::strcmp(space_name, PUBSPACE_DOMAIN) != 0 || ::wcscmp(data_name, GetDomainName(pKernel).c_str()) != 0)
	{
		// 不是队伍公共数据，不需要处理
		return 0;
	}

	// 消息处理
	int msg_id = args.IntVal(2);

	switch (msg_id)
	{
		// 设置成员在上线时候的状态
		case PS_DOMAIN_SERVERMSG_TEAM_SET_PLAYERONRECOVER:
		{
			const wchar_t* member_name = args.WideStrVal(3);
			const int nTeamId = args.IntVal(4);
			// 通知member_name
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_MEMBER_RECOVER << nTeamId);
		}
		break;
		// 创建队伍结果
		case PS_DOMAIN_SERVERMSG_TEAM_CREATE:
		{
			const wchar_t* captain_name = args.WideStrVal(3);
			const wchar_t* member_name = args.WideStrVal(4);

			int team_id = args.IntVal(5);
			if (team_id <= 0)
			{
				// 创建失败
				return 0;
			}

			// 通知capitan_name
			pKernel->CommandByName(captain_name, CVarList() << COMMAND_TEAM_CREATE << team_id << member_name);

			// 通知member_name
			if (wcslen(member_name) > 0)
			{
				pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_CREATE << team_id);
			}
		}
		break;
		// 解散队伍结果
		case PS_DOMAIN_SERVERMSG_TEAM_DESTROY:
		{
			const wchar_t* member_name = args.WideStrVal(3);
			//通知player_name
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_CLEAR << 2);
		}
		break;
		// 加入队伍
		case PS_DOMAIN_SERVERMSG_TEAM_JOIN:
		{
			const wchar_t* player_name = args.WideStrVal(3);
			int team_id = args.IntVal(4);
			// 通知player_name
			pKernel->CommandByName(player_name, CVarList() << COMMAND_TEAM_JOIN << team_id);
		}
		break;
		case PS_DOMAIN_SERVERMSG_TEAM_ADD_MEMBER:
		{
			const wchar_t* member_name = args.WideStrVal(3);
			const wchar_t* player_name = args.WideStrVal(4);
			// 通知member_name
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_ADD_MEMBER << player_name);

		}break;
		// 离开队伍
		case PS_DOMAIN_SERVERMSG_TEAM_LEAVE:
		{
			const wchar_t* member_name = args.WideStrVal(3);
			const wchar_t* player_name = args.WideStrVal(4);
			int reason = args.IntVal(5);

			// 通知player_name
			if (wcscmp(member_name, player_name) == 0)
			{
				pKernel->CommandByName(player_name, CVarList() << COMMAND_TEAM_CLEAR << reason);
			}
			//通知member_name
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_REMOVE_MEMBER
				<< player_name << reason);
		}
		break;
		// 更新成员信息
		case PS_DOMAIN_SERVERMSG_TEAM_UPDATE_MEMBER:
		{
			const wchar_t* member_name = args.WideStrVal(3);
			const wchar_t* player_name = args.WideStrVal(4);
			int col = args.IntVal(5);
			// 通知member_name
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_UPDATE_MEMBER_INFO
				<< player_name << col);
		}
		break;
		// 更新队伍信息
		case PS_DOMAIN_SERVERMSG_TEAM_UPDATE_INFO:
		{
			const wchar_t* member_name = args.WideStrVal(3);
			int col = args.IntVal(4);
			int teamID = args.IntVal(5);
			// 通知member_name
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_UPDATE_TEAM_INFO
				<< col << teamID);
		}
		break;
		// 设置成员职位
		case PS_DOMAIN_SERVERMSG_TEAM_SET_MEMBERS_WORK:
		{
			const wchar_t* member_name = args.WideStrVal(3);
			const wchar_t* player_name = args.WideStrVal(4);
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_MEMBER_CHANGE
				<< player_name);
		}
		break;

		default:
			break;
	}

	return 0;
}

// 是否可以发出组队请求
bool TeamModule::CanRequestJoinTeam(IKernel* pKernel, const PERSISTID& self,
                                    REQUESTTYPE type, const wchar_t* targetname, const IVarList& paras)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

    if(pSelfObj->FindData("PVP_STATE"))
    {
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17403, CVarList());
        return false;
    }


	if (pKernel->GetPlayerScene(targetname) <= 0)
	{
		// 玩家[{@0:name}]不存在或不在线，邀请失败
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17404, CVarList() << targetname);
		return false;

	}

    PERSISTID targetnamePID = pKernel->FindPlayer(targetname);
    IGameObj *pTargetname = pKernel->GetGameObj(targetnamePID);
    if(NULL != pTargetname)
    {
        if(pTargetname->FindData("PVP_STATE"))
        {
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17403, CVarList());
            return false;
        }
    }
	
	if (m_pTeamModule->IsActivityScene(pKernel)){
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17402, CVarList());
		return false;
	}
	
	// 请求者名字
	const wchar_t* selfname = pSelfObj->QueryWideStr("Name");
	// 国籍判断
	// 请求发起者国籍
	int selfNation = pSelfObj->QueryInt("Nation");

	// 被请求者国籍
	int targetNation = paras.IntVal(2);

	//// 不同国家的玩家无法进行组队活动
	//if (selfNation != targetNation)
	//{
	//	CustomSysInfo(pKernel, self, TIPSTYPE_SYSFUNCTION_PROMPT_MESSAGE,
	//			STR_TEAM_SYSINFO_DIFFERENTNATION, CVarList());
	//	return false;
	//}

	

    // 自己的队友，不能再进行组队行为
    if (m_pTeamModule->IsTeamMate(pKernel, self, targetname))
    {
        // [{@0:name}]已经是队伍成员，无需发出组队邀请
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17405, CVarList() << targetname);
        return false;
    }

    if (type == REQUESTTYPE_JOIN_SINGLEPLAYER)// 申请加入
    {
		


        // 如果有队伍，不能再申请加入其它队伍
        if (m_pTeamModule->IsInTeam(pKernel, self))
        {
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17406, CVarList());
            return false;
        }
		int teamID = paras.IntVal(1);
		if (!m_pTeamModule->IsAchieveTeamLevel(pKernel, self, teamID))
		{
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17407, CVarList());
			return false;
		}
		if (!m_pTeamModule->CanJoinTeam(pKernel, teamID))
		{
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17408, CVarList());
			return false;
		}


		if (!TeamModule::DealCheckTeamCondition(pKernel, self,TeamModule::GetTeamObjIndex(pKernel, teamID), selfname))
		{
            return false;
		};


		if (m_pTeamModule->IsAutoJoin(pKernel, teamID))
		{
			CVarList msg;
			msg << PUBSPACE_DOMAIN
				<< GetDomainName(pKernel).c_str()
				<< SP_DOMAIN_MSG_TEAM_JOIN << teamID << selfname;
			pKernel->SendPublicMessage(msg);
			return false;
		}

    }
    else if (type == REQUESTTYPE_INVITE_SINGLEPLAYER)// 邀请
    {
        if (m_pTeamModule->IsFull(pKernel, self))
        {
            // 队伍已满，不能发出组队邀请
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17409, CVarList());
            return false;
        }

		IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
		if (NULL == pTeamRec)
		{
			return false;
		}
        // 如果邀请人数达到上限（最大人数 - 当前人数）
        int cur_membs = pTeamRec->GetRows();
        int max_membs = pTeamRec->GetRowMax();


		
    }

    return true;
}

bool TeamModule::CanBeRequestJoinTeam(IKernel* pKernel, const PERSISTID& self, REQUESTTYPE type,
                                      const wchar_t* srcname, const IVarList& paras)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

    if(pSelfObj->FindData("PVP_STATE"))
    {
		CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17403, CVarList());
        return false;
    }
	
	if (m_pTeamModule->IsActivityScene(pKernel)){
// 		CustomSysInfo(pKernel, self, TIPSTYPE_SYSFUNCTION_PROMPT_MESSAGE,
// 			STR_TEAM_SYSINFO_FORBID_TEAM, CVarList());

		CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17402, CVarList());
		return false;
	}
	// 被请求者名字
	const wchar_t* selfname = pSelfObj->QueryWideStr("Name");
	// 国籍判断
	// 被请求者国籍
	//int selfNation = pSelfObj->QueryInt("Nation");
	//// 请求者国籍
	//int targetNation = paras.IntVal(2);

	// 请求者的队伍号
	int src_team_id = paras.IntVal(1);

	//// 不同国家的玩家无法进行组队活动
	//if (selfNation != targetNation)
	//{
	//	CustomSysInfoByName(pKernel, srcname, TIPSTYPE_SYSFUNCTION_PROMPT_MESSAGE,
	//			STR_TEAM_SYSINFO_DIFFERENTNATION, CVarList());
	//	return false;
	//}


    // 自己队伍号（被请求者）
    int self_team_id = pSelfObj->QueryInt("TeamID");    

    // 请求者掉线了
    if (pKernel->GetPlayerScene(srcname) <= 0)
    {
        // [{@0:name}]不在线，组队失败
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17410, CVarList() << srcname);
        return false;
    }
	
    if (type == REQUESTTYPE_INVITE_SINGLEPLAYER) // self被邀请
    {
		

		int playerSceneID = pKernel->GetPlayerScene(selfname);
		if (playerSceneID >= OUTDOOR_SCENE_MAX)
		{
			CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17411, CVarList());
		}


        if (self_team_id > 0) // 被邀请方有队伍，邀请失败
        {
			if (self_team_id == src_team_id)
			{
				// [{@0:name}]已经在其他队伍中，不能发出组队邀请。
				CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17412, CVarList() << selfname);
			}
			else
			{
				// [{@0:name}]已经在其他队伍中，不能发出组队邀请。
				CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17413, CVarList() << selfname);
			}
            return false;
        }

		if (src_team_id > 0)
		{
			if (!m_pTeamModule->IsAchieveTeamLevel(pKernel, self, src_team_id))
			{
				CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17414, CVarList() << selfname);
				return false;
			}
			if (!m_pTeamModule->CanJoinTeam(pKernel, src_team_id))
			{
				CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17408, CVarList());
				return false;
			}


			if (!TeamModule::DealCheckTeamCondition(pKernel, self, TeamModule::GetTeamObjIndex(pKernel, src_team_id), srcname))
			{
				
				return false;
			};
		}
    }
	// 申请加入
    else if (type == REQUESTTYPE_JOIN_SINGLEPLAYER) // self被申请
    {
	
        if (src_team_id > 0) // 申请者有队伍
        {
			// [{@0:name}]已经在队伍中，不能发出入队请求
			CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17406, CVarList() << selfname);
			return false;
        }
        else
        {
            if (self_team_id > 0) // 被申请者有队伍
            {
				if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
				{
					// [{@0:name}]已经不是队长，请重新申请
					CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17415, CVarList() << selfname);

					return false;
				}

				if (!m_pTeamModule->CanJoinTeam(pKernel, self_team_id))
				{
					CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17408, CVarList());
					return false;
				}


                if (m_pTeamModule->IsFull(pKernel, self))
                {
                    // 对方队伍已满，不能发送入队请求
					CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17416, CVarList());

                    return false;
                }
            }
            else
            {
                //没有队伍，不能被申请
				CustomSysInfoByName(pKernel, srcname, SYSTEM_INFO_ID_17417, CVarList());
                return false;
            }
        }
    }
    else
    {
        return false;
    }

    return true;
}

bool TeamModule::RequestJoinTeamSuccess(IKernel* pKernel, const PERSISTID& self, REQUESTTYPE type,
                                        const wchar_t* targetname, const IVarList& paras)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

    // 信息参数
    int target_team_id = paras.IntVal(1);
    int self_team_id = pSelfObj->QueryInt("TeamID");
   
	// 请求者
	const wchar_t* self_name = pSelfObj->QueryWideStr("Name");
	//int naiton = pSelfObj->QueryInt(FIELD_PROP_NATION);

    // 向公共数据服务器发送消息
    CVarList msg;
    msg << PUBSPACE_DOMAIN << GetDomainName(pKernel).c_str();

    int request_type = paras.IntVal(3);

    if (request_type == REQUESTTYPE_INVITE_SINGLEPLAYER)// self 邀请 target
    {
        if (self_team_id > 0)// self in team
        {
            if (target_team_id > 0)// target in team
            {
                // [{@0:name}]已经有队伍，组队失败
				CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17418, CVarList() << targetname);
                return false;
            }
            else
            {
                // target加入self所在队
                msg << SP_DOMAIN_MSG_TEAM_JOIN << self_team_id << targetname;
            }
        }
        else// self not in team
        {
            if (target_team_id > 0)// target in team
            {
                // error：self没有队 不能邀请有队的target
				// [{@0:name}]已经有队伍，组队失败
				CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17418, CVarList() << targetname);
                return false;
            }
            else
            {
                // creat team   self为队长
				msg << SP_DOMAIN_MSG_TEAM_CREATE << self_name << targetname << 1/*naiton*/;
            }
        }

    }
    else if (request_type == REQUESTTYPE_JOIN_SINGLEPLAYER) // self 申请 target
    {
        if (self_team_id > 0)// self in team
        {
            // [{@0:name}]在其他队伍中，加入队伍失败
			CustomSysInfoByName(pKernel, targetname, SYSTEM_INFO_ID_17419, CVarList() << self_name);
			OnCustomNearByTeam(pKernel,self,self,CVarList());
            return false;
        }
        else// self not in team
        {
            if (target_team_id > 0)// target in team
            {
				if (!m_pTeamModule->IsAchieveTeamLevel(pKernel, self, target_team_id))
				{
					CustomSysInfoByName(pKernel, targetname, SYSTEM_INFO_ID_17407, CVarList());

					return false;
				}

				if (!TeamModule::DealCheckTeamCondition(pKernel, self, TeamModule::GetTeamObjIndex(pKernel, target_team_id), targetname))
				{
					return false;
				};

                // self加入target所在队
                msg << SP_DOMAIN_MSG_TEAM_JOIN << target_team_id << self_name;

            }
			// 考虑被申请者没队伍，认为请求失败，不再重新创建队伍了
            else
            {
				// 对方没有队伍，申请失败
				CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17417, CVarList());

				OnCustomNearByTeam(pKernel,self,self,CVarList());

            }
        }
    }
    else
    {
        return false;
    }

    return pKernel->SendPublicMessage(msg);
}

bool TeamModule::GetRequestParas(IKernel* pKernel, const PERSISTID& self,
                                 REQUESTTYPE type, IVarList& paras)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

    IGameObj *pSelfObj = pKernel->GetGameObj(self);
    if (NULL == pSelfObj)
    {
        return false;
    }

	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return false;
	}

    int self_cur_rows = pTeamRec->GetRows();

	int is_captain = m_pTeamModule->IsTeamCaptain(pKernel, self) ? 1 : 0;
	int request =  1;
	int teamid = pSelfObj->QueryInt("TeamID");
	int nation = pSelfObj->QueryInt("Nation");
    paras << teamid
			 << nation
			 << type
			 << self_cur_rows
			 << pSelfObj->QueryInt("Level")
			 << request
		 	 << is_captain;

    return true;
}

bool TeamModule::RequestJoinTeamResult(IKernel* pKernel, const PERSISTID& self, REQUESTTYPE type,
									   const wchar_t* targetname, int result)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

	IRecord *pReqRec = pSelfObj->GetRecord(REQUEST_REC);
    if (result == 0 && pReqRec != NULL)
    {
        int rows = pReqRec->GetRows();
		LoopBeginCheck(b);
        for (int i = rows - 1; i >= 0; --i)
        {
			LoopDoCheck(b);
            const wchar_t* name = pReqRec->QueryWideStr(i, REQUEST_REC_NAME);
            int reqtype = pReqRec->QueryInt(i, REQUEST_REC_TYPE);
            if (wcscmp(targetname, name) == 0 && reqtype == type)
            {
                // 删除此玩家此种请求
                pReqRec->RemoveRow(i);
            }
        }
    }

    return true;
}

// 组队客户端消息
int TeamModule::OnCustomMessage(IKernel* pKernel, const PERSISTID& self, 
								const PERSISTID& sender, const IVarList& args)
{
    //PVP战场状态中不可以操作队伍
    IGameObj *pSelf = pKernel->GetGameObj(self);
    if(NULL == pSelf)
    {
        return 0;
    }
    if(pSelf->FindData("PVP_STATE"))
    {
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17403, CVarList());
        IRecord *requestRec = pSelf->GetRecord(REQUEST_REC);
        if (requestRec == NULL)
        {
            return 0;
        }
        requestRec->ClearRow();
        return 0;
    }

	if (!pKernel->Exists(self))
	{
		return 0;
	}
    int msg_type = args.IntVal(1);
    switch (msg_type)
    {
        // 创建队伍
        case CLIENT_CUSTOMMSG_TEAM_CREATE:
        {
            OnCustomCreateTeam(pKernel, self, sender, args);
        }
        break;
        // 踢出队伍
        case CLIENT_CUSTOMMSG_TEAM_KICKOUT:
        {
            OnCustomKickOut(pKernel, self, sender, args);
        }
        break;
        // 解散队伍（暂时没有此功能）
        case CLIENT_CUSTOMMSG_TEAM_DESTROY:
        {
            OnCustomDestroy(pKernel, self, sender, args);
        }
        break;
        // 离开队伍
        case CLIENT_CUSTOMMSG_TEAM_LEAVE:
        {
            OnCustomLeave(pKernel, self, sender, args);
        }
        break;
        // 移交队长
        case CLIENT_CUSTOMMSG_TEAM_CHANGE_CAPTAIN:
        {
            OnCustomChangeCaptain(pKernel, self, sender, args);
        }
        break;
		// 附近队伍
		case CLIENT_CUSTOMMSG_TEAM_NEARBY:
			{
				OnCustomNearByTeam(pKernel, self, sender, args);
			}
			break;
			// 刷新队伍成员位置
		case  CLIENT_CUSTOMMSG_TEAM_MEMBER_POSITION_UPDATE:
			{
				if (args.GetCount() < 3)
				{
					return 0;
				}
				// 更新队员位置开关
				int update_posi_switch = args.IntVal(2);

				// 打开
				if (UPDATE_POSI_ON == update_posi_switch)
				{
					OnCustomUpdateMemberPosition(pKernel, self, sender, args);
					ADD_HEART_BEAT(pKernel, self, "TeamModule::H_UpdateMemberPos", 1000);
				}

				// 关闭
				if (UPDATE_POSI_OFF == update_posi_switch)
				{
					DELETE_HEART_BEAT(pKernel, self, "TeamModule::H_UpdateMemberPos");
				}
			}
			break;
        case CLIENT_CUSTOMMSG_TEAM_CLEAR_APPLY:
            {
                //清除申请列表
                OnCustomClearApply(pKernel, self, sender, args);
            }
            break;
        case CLIENT_CUSTOMMSG_TEAM_APPLY_LIST:
            {
                //获得申请列表
                OnCustomApplyList(pKernel, self, sender, args);
            }
            break;
		case CLIENT_CUSTOMMSG_TEAM_SET_VIEW:
		{
			//设置公开属性
			OnCustomSetViewState(pKernel, self, sender, args);
		
		}break;
		case CLIENT_CUSTOMMSG_TEAM_SET_AUTO_JOIN:
		{
			//设置自动加入
			OnCustomSetAutoJoin(pKernel, self, sender, args);
		
		}break;
		case CLIENT_CUSTOMMSG_TEAM_GET_TEAM_INFO:
		{
			//获取队伍信息	
			OnCustomGetTeamInfo(pKernel, self);
		}break;
		case CLIENT_CUSTOMMSG_TEAM_SET_TEAM_OBJECT_INDEX:
		{
			int index = args.IntVal(2);
			OnCustomSetTeamObject(pKernel,self,index);
		}break;
		case CLIENT_CUSTOMMSG_TEAM_SET_TEAM_LIMIT_LEVEL:
		{
			OnCustomSetTeamLimitLevel(pKernel,self,args);
		}break;
		case CLIENT_CUSTOMMSG_TEAM_ENTRY_SECRET_SCENE:
		{
			//进入副本
//			TeamSceneModule::m_pTeamSceneModule->TeamInRequest(pKernel, self, args);
		}break;
		case CLIENT_CUSTOMMSG_TEAM_AUTO_MATCH:
		{
			TeamModule::AutoMatch(pKernel, self, args.IntVal(2), args.IntVal(3));
		}break;
		case CLIENT_CUSTOMMSG_TEAM_GET_TEAM_LIST:
		{
			OnCustomSendTeamList(pKernel, self, args.IntVal(2));
		}break;
		case CLIENT_CUSTOMMSG_TEAM_MATCH_TEAM_INFO:
		{
			CVarList pub_msg;
			pub_msg << PUBSPACE_DOMAIN
				<< GetDomainName(pKernel).c_str()
				<< SP_DOMAIN_MSG_TEAM_MATCH_INFO
				<< pSelf->QueryWideStr(FIELD_PROP_NAME);
			//<< pSelf->QueryInt(FIELD_PROP_NATION);
			pKernel->SendPublicMessage(pub_msg);

		}break;
		case CLIENT_CUSTOMMSG_TEAM_TRANSFER_CAPTAIN_SITE:
		{//传送到队长附近
			OnCustomTransferCapTainSite(pKernel, self);
		}break;
		case CLIENT_CUSTOMMSG_TEAM_CONVENE:
		{
			OnCustomConvene(pKernel, self);
		}break;
		case CLIENT_CUSTOMMSG_SET_TEAM_FOLLOW:
		{
			OnCustomSetFollow(pKernel,self,args.IntVal(2));
		
		}break;
		case CLIENT_CUSTOMMSG_TEAM_REQUEST_RECOMMAND_LIST:
		{
			OnCustomRequestRecommandList(pKernel,self);
		
		}break;
    }
    return 0;
}

// 组队服务端消息
int TeamModule::OnCommandMessage(IKernel* pKernel, const PERSISTID& self,
								 const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	int msg_type = args.IntVal(1);
	switch (msg_type)
	{
		// 创建队伍
	case COMMAND_TEAM_CREATE_MSG:
		{
			OnCustomCreateTeam(pKernel, self, sender, args);
		}
		break;
		//踢出队伍
	case COMMAND_TEAM_KICKOUT_MSG:
		{
			OnCustomKickOut(pKernel, self, sender, args);
		}
		break;
		// 添加成员
	case COMMAND_TEAM_ADDMEM_MSG:
		{
			const wchar_t* targetname = args.WideStrVal(2);
			int team_id = args.IntVal(3);
			int clone_id = 0;
			if (args.GetCount() > 4)
			{
				clone_id = args.IntVal(4);
			}

			CVarList msg;
			msg << PUBSPACE_DOMAIN 
				    << GetDomainName(pKernel).c_str()
					<< SP_DOMAIN_MSG_TEAM_JOIN 
					<< team_id 
					<< targetname 
					<< clone_id;
			pKernel->SendPublicMessage(msg);

		}
		break;
		// 离开队伍
	case COMMAND_TEAM_LEAVE_MSG:
		{
			OnCustomLeave(pKernel, self, sender, args);
		}
		break;
		// 移交队长
	case COMMAND_TEAM_CHGCAP_MSG:
		{
			OnCustomChangeCaptain(pKernel, self, sender, args);
		}
		break;
		// 队员位置更新
	case COMMAND_TEAM_UPDATE_MEMBER_POSI_MSG:
		{
			OnCommandUpdateMemberPosition(pKernel, self, sender, args);
		}
		break;
		// 加入队伍
	case COMMAND_TEAM_TEAM_JOIN:
	{
		
		const wchar_t* player_name = args.WideStrVal(2);
		int team_id = args.IntVal(3);
		// 通知player_name
		pKernel->CommandByName(player_name, CVarList() << COMMAND_TEAM_JOIN << team_id);
		
	}break;
	case COMMAND_TEAM_TEAM_ADD_MUMBER:
	{
			const wchar_t* member_name = args.WideStrVal(2);
			const wchar_t* player_name = args.WideStrVal(3);
			// 通知member_name
			pKernel->CommandByName(member_name, CVarList() << COMMAND_TEAM_ADD_MEMBER << player_name);
	
	}break;
	case COMMAND_GET_TARGET_POS_REQ:
	{
			OnCommandGetTargetPos(pKernel, self, args);

	}break;
	case COMMAND_GET_TARE_POS_RSP:
	{
			OnCommandTransferToTarget(pKernel,self,args);
				
	}break;
	case COMMAND_DEL_PLAYER_APPLAY:
	{//清除这个玩家的请求

		RequestJoinTeamResult(pKernel, self, (REQUESTTYPE)args.IntVal(2), args.WideStrVal(3), 0);

	}break;

	}
	return 0;
	
}


// 创建队伍
int TeamModule::OnCustomCreateTeam(IKernel* pKernel, const PERSISTID& self,
                                   const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	
	if (m_pTeamModule->IsActivityScene(pKernel)){
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17402, CVarList());
		return false;
	}



	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

    if (m_pTeamModule->IsInTeam(pKernel, self))
    {
        // 已经在队伍中，不能创建队伍
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
        return 0;
    }

	const wchar_t* selfName = pSelfObj->QueryWideStr("Name");
	int objIndex = 0;
	if (args.GetCount() > 2)
	{
		objIndex = args.IntVal(2);
	}
	if (!TeamModule::IsTeamValid(pKernel, self, objIndex))
	{
		return 0;
	}
	//int nation = pSelfObj->QueryInt(FIELD_PROP_NATION);
	
	TeamModule::AutoMatch(pKernel, self, TEAM_AUTO_MATCH_OFF);
	
	/*if (!m_pTeamModule->DealCheckTeamCondition(pKernel, self, objIndex, pSelfObj->QueryWideStr(FIELD_PROP_NAME)))
	{
		return 0;
	}*/
	

    // 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN 
				   << GetDomainName(pKernel).c_str() 
				   << SP_DOMAIN_MSG_TEAM_CREATE
				   << selfName 
				   << L"" 
				   << objIndex;
    pKernel->SendPublicMessage(pub_msg);

    return 0;
}


// T出队
int TeamModule::OnCustomKickOut(IKernel* pKernel, const PERSISTID& self,
                                const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

	// 要T出的队员名
    const wchar_t* target_name = args.WideStrVal(2);
	// T人者
    const wchar_t* self_name = pSelfObj->QueryWideStr("Name");

    // 不能T自己
    if (wcscmp(target_name, self_name) == 0)
    {
        return 0;
    }

    // 不是队长不能T人
    if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
    {
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
        return 0;
    }

    
    if (!m_pTeamModule->IsTeamMate(pKernel, self, target_name))
    {
        return 0;
    }
	// 目标不在队伍里不能T
	if (!m_pTeamModule->IsBeKickTeam(pKernel, self, target_name))
	{
		return 0;
	}

    // 队伍ID
    int team_id = pSelfObj->QueryInt("TeamID");

    // 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN 
				   << GetDomainName(pKernel).c_str() 
				   << SP_DOMAIN_MSG_TEAM_LEAVE
				   << team_id 
				   << target_name 
				   << LEAVE_TYPE_EXPEL;
    pKernel->SendPublicMessage(pub_msg);

    return 0;
}


// 附近队伍
int TeamModule::OnCustomNearByTeam(IKernel* pKernel, const PERSISTID& self,
									const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

	IRecord *pNearbyRec = pSelfObj->GetRecord(FIELD_RECORD_NEARBY_TEAM_REC);
	if (NULL == pNearbyRec)
	{
		return 0;
	}

	pNearbyRec->ClearRow();

	int nation = pSelfObj->QueryInt("Nation");
	int sceneid = pKernel->GetSceneId();
	int self_teamID = 0;
	if (pSelfObj->FindAttr("TeamID") )
	{
		self_teamID = pSelfObj->QueryInt("TeamID");
	}
	
	CVarList playerlist;

	// 查找附近玩家
	pKernel->GetAroundList(self, TEAM_NEARBY_RANGE, TYPE_PLAYER, pNearbyRec->GetRowMax(), playerlist);

	if (playerlist.GetCount() <= 0)
	{
		return 0;
	}

	const wchar_t * name = L"";
	PERSISTID playerid;
	IGameObj *player = NULL;
	float fDistance = 0.0f;
	int team_id = 0;

	std::vector<NearbyTeamRecommend> vecNearby;
	LoopBeginCheck(c);
	for (int i = 0; i < (int)playerlist.GetCount(); ++i)
	{
		LoopDoCheck(c);
		playerid = playerlist.ObjectVal(i);
		if (!pKernel->Exists(playerid))
		{
			continue;
		}

		if (playerid == self)
		{
			continue;
		}

		player = pKernel->GetGameObj(playerid);
		if (NULL == player)
		{
			continue;
		}

		// 只找同一国家的
		/*if (player->QueryInt("Nation") != nation)
		{
			continue;
		}*/

		// 距离太远
		fDistance = pKernel->Distance2D(playerid, self);
		if (fDistance > TEAM_NEARBY_RANGE)
		{
			continue;
		}

		// 是否有队伍
		if (!TeamModule::m_pTeamModule->IsInTeam(pKernel, playerid))
		{
			continue;
		}
		if (!TeamModule::m_pTeamModule->IsTeamCaptain(pKernel, playerid))
		{
			continue;
		}

		team_id =  player->QueryInt("TeamID");
		// 是否是自己的队伍
		if (self_teamID == team_id)
		{
			continue;
		}

		NearbyTeamRecommend nearby;
		nearby.teamid = team_id;
		nearby.distance = fDistance;
		nearby.playid = playerid;
		vecNearby.push_back(nearby);

		if ((int)vecNearby.size() >= pNearbyRec->GetRowMax())
		{
			break;
		}
	}

	if (vecNearby.empty())
	{
		return 1;
	}

	// 按距离排序
	//std::sort(vecNearby.begin(), vecNearby.end(), TeamModule::CompareDistance);
	random_shuffle(vecNearby.begin(), vecNearby.end());
	// 为玩家更新附近队伍表
	LoopBeginCheck(d);
	for (int i = 0; i < (int)vecNearby.size(); ++i)
	{
		LoopDoCheck(d);
		team_id = vecNearby[i].teamid;
		if(team_id <= 0)
		{
			continue;
		}

		// 获取队伍信息
		CVarList varlist;
		varlist << team_id;
		if (!TeamModule::m_pTeamModule->GetNearByTeamInfo(pKernel, vecNearby[i].playid, varlist))
		{
			continue;
		}
		
		// 添加附近队伍信息表
		pNearbyRec->AddRowValue(-1, varlist);

		if (pNearbyRec->GetRows() >= pNearbyRec->GetRowMax())
		{
			break;
		}
	}

	return 1;
}

// 距离比较
bool TeamModule::CompareDistance(const NearbyTeamRecommend& first, 
								 const NearbyTeamRecommend& second)
{
	return first.distance < second.distance;
}

// 更新队员位置
int TeamModule::OnCustomUpdateMemberPosition( IKernel* pKernel, const PERSISTID& self,
											 const PERSISTID& sender, const IVarList& args )
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

	// 无队伍
	if (!m_pTeamModule->IsInTeam(pKernel, self))
	{
		DELETE_HEART_BEAT(pKernel,self,"TeamModule::H_UpdateMemberPos");
		return 0;
	}

	// 队伍ID
	int team_id = pSelfObj->QueryInt("TeamID");

	// 公共数据指针
	IPubData *pTeamData = m_pTeamModule->GetTeamPubData(pKernel);
	if (NULL == pTeamData)
	{
		return 0;
	}

	// 取得队伍成员表名
	char team_member_rec_name[256];
	SPRINTF_S(team_member_rec_name, "team_%d_member_rec", team_id);

	// 玩家信息
	int group_id = pSelfObj->QueryInt("GroupID");
	const wchar_t *self_name = pSelfObj->QueryWideStr("Name");

	// 遍历队伍成员表
	IRecord *pTeamMemRec = pTeamData->GetRecord(team_member_rec_name);
	if (NULL == pTeamMemRec)
	{
		return 0;
	}

	int rows =pTeamMemRec->GetRows();
	CVarList member_args;	// 队员位置更新消息
	member_args << SERVER_CUSTOMMSG_TEAM 
						  << CLIENT_CUSTOMMSG_TEAM_MEMBER_POSITION_UPDATE;

	LoopBeginCheck(e);
	for (int i = 0; i < rows; ++i)
	{
		LoopDoCheck(e);
		const wchar_t* member_name = pTeamMemRec->QueryWideStr(i, TEAM_REC_COL_NAME);
		
		// 通知队员更新位置
		CVarList msg;
		msg << COMMAND_TEAM_MSG 
			   << COMMAND_TEAM_UPDATE_MEMBER_POSI_MSG;
		pKernel->CommandByName(member_name, msg);

		// 排除自己
		if (wcscmp(self_name, member_name) == 0)
		{
			continue;
		}

		// 查找当前场景队伍成员
		PERSISTID member_id = pKernel->FindPlayer(member_name);
		if (!pKernel->Exists(member_id))
		{
			continue;
		}

		IGameObj *pMemberObj = pKernel->GetGameObj(member_id);
		if (NULL == pMemberObj)
		{
			continue;
		}

		// 与当前玩家不属于同一分组
		if (pMemberObj->QueryInt("GroupID") != group_id)
		{
			continue;
		}

		// 取该成员坐标
		member_args << member_name
							  << pMemberObj->GetPosiX()
							  << pMemberObj->GetPosiZ();
	}

	// 通知客户端更新队员位置
	pKernel->Custom(self, member_args);

	return 0;

}

// 退队
int TeamModule::OnCustomLeave(IKernel* pKernel, const PERSISTID& self,
                              const PERSISTID& sender, const IVarList& args)
{

	LeaveTeam(pKernel, self);
    return 0;

}

// 解散队伍
int TeamModule::OnCustomDestroy(IKernel* pKernel, const PERSISTID& self,
                                const PERSISTID& sender, const IVarList& args)
{
    if (!pKernel->Exists(self))
    {
        return 0;
    }

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

    // 如果自己不在队伍中
    if (!m_pTeamModule->IsInTeam(pKernel, self))
    {
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
        return 0;
    }
    // 自己不是队长
    if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
    {
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
        return 0;
    }

// 	if (!EscortModule::m_pEscortModule->CanDestoryTeam(pKernel, self))
// 	{
// 		return 0;
// 	}

    // 玩家名字
    const wchar_t* self_name = pSelfObj->QueryWideStr("Name");

    // 队伍ID
    int team_id = pSelfObj->QueryInt("TeamID");
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN 
				   << GetDomainName(pKernel).c_str() 
				   << SP_DOMAIN_MSG_TEAM_DESTROY
				   << team_id 
				   << self_name 
				   << 0;
    pKernel->SendPublicMessage(pub_msg);
	
    return 0;
}

// 移交队长（团长）
int TeamModule::OnCustomChangeCaptain(IKernel* pKernel, const PERSISTID& self,
                                      const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

    IGameObj *pSelf = pKernel->GetGameObj(self);
    if (NULL == pSelf)
    {
        return 0;
    }
    const wchar_t* target_name = args.WideStrVal(2);
    const wchar_t* self_name = pSelf->QueryWideStr("Name");

    if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
    {
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
        // 自已不是队长，不能移交
        return 0;
    }

    if (!m_pTeamModule->IsTeamMate(pKernel, self, target_name))
    {
        // [{@0:name}]不是队友，不能移交
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17421, CVarList() << target_name);
        return 0;
    }

    // 查找
	IRecord *pTeamRec = pSelf->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return 0;
	}
    int row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, target_name);
	if (row < 0)
	{
		return 0;
	}

    int64_t is_offLine = pTeamRec->QueryInt64(row, TEAM_REC_COL_ISOFFLINE);

    if (is_offLine > 0)
    {
        // 对方不在线 不能移交
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17422, CVarList() << target_name);
        return 0;
    }

    // 队伍ID
    int team_id = pSelf->QueryInt("TeamID");

    // 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN 
				   << GetDomainName(pKernel).c_str() 
				   << SP_DOMAIN_MSG_TEAM_UPDATE_INFO
				   << team_id 
				   << self_name 
				   << TEAM_MAIN_REC_COL_TEAM_CAPTAIN 
				   << target_name;
    pKernel->SendPublicMessage(pub_msg);

	pub_msg.Clear();
	pub_msg << PUBSPACE_DOMAIN 
				   << GetDomainName(pKernel).c_str() 
				   << SP_DOMAIN_MSG_TEAM_SET_MEMBER
				   << team_id 
				   << TEAM_REC_COL_TEAMWORK 
				   << TYPE_TEAM_CAPTAIN  
				   << target_name;
    pKernel->SendPublicMessage(pub_msg);

    return 0;
}

int TeamModule::OnCommandTeamMemberRecover(IKernel* pKernel, const PERSISTID& self,
                                           const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

    const int team_id = args.IntVal(1);
    pSelfObj->SetInt("TeamID", team_id);

    // 如果在团队中
    if (team_id > 0)
    {
        // 复制所有信息到角色属性中
        m_pTeamModule->RefreshAllTeamInfo(pKernel, self, team_id);

        // 更新所有数据到公共数据
        m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "IsOffLine");
    }
    else
    {
        m_pTeamModule->ClearTeamInfo(pKernel, self);
    }

    return 0;
}

int TeamModule::OnCommandTeamMemberChange(IKernel* pKernel, const PERSISTID& self,
                                          const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    const wchar_t* target_name = args.WideStrVal(1);

    // 更新信息
    m_pTeamModule->RefreshTeamMemberInfo(pKernel, self, target_name, TEAM_REC_COL_TEAMWORK);

    return 0;
}

// 创建队伍成功
int TeamModule::OnCommandCreateTeamResult(IKernel* pKernel, const PERSISTID& self,
                                          const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    int team_id = args.IntVal(1);
	TeamModule::AutoMatch(pKernel, self, TEAM_AUTO_MATCH_OFF);
    // 复制所有信息到角色属性中
    m_pTeamModule->RefreshAllTeamInfo(pKernel, self, team_id);

    // 更新所有数据到公共数据
	CVarList values;
	values << "UID"  
		<< "Nation"  
		<< "Sex"  
		<< "BattleAbility" 
		<< "Resource" 
		<< "Scene"
		<< "Buffers" 
		<< "IsOffLine" 
		<< "Level" 
		<< "Job" 
		<< "HP" 
		<< "MP" 
		<< "MaxHP" 
		<< "MaxMP"
		<< "PosiX"
		<< "PosiZ"
		<< "GuildName"
		<< "FightState"
		<< "VipLevel";
    m_pTeamModule->UpdatePropToDomain(pKernel, self, values);

	OnCustomGetTeamInfo(pKernel,self);

    // 组建队伍成功
	CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17423, CVarList());
	pKernel->Custom(self, CVarList() << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_CREATE << GetTeamObjIndex(pKernel, team_id));

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (pSelfObj != NULL)
	{
		if (wcscmp(pSelfObj->QueryWideStr(FIELD_PROP_NAME), pSelfObj->QueryWideStr(FIELD_PROP_TEAM_CAPTAIN)) == 0)
		{
			ADD_HEART_BEAT(pKernel, self, "TeamModule::H_UpdateMemberPos", 3000);
			ADD_CRITICAL(pKernel, self, FIELD_PROP_FIGHT_STATE, "TeamModule::C_OnFightState");
			ResetTeamLimitLevel(pKernel, self, GetTeamObjIndex(pKernel, team_id));
		}
	}
	
    return 0;
}

int TeamModule::OnCommandJoinTeam(IKernel* pKernel, const PERSISTID& self,
                                  const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    int team_id = args.IntVal(1);
	TeamModule::AutoMatch(pKernel, self, TEAM_AUTO_MATCH_OFF);
    // 复制所有信息到角色属性中
    m_pTeamModule->RefreshAllTeamInfo(pKernel, self, team_id);

    // 更新所有数据到公共数据
	CVarList values;
	values << "UID"  
		<< "Nation"  
		<< "Sex"  
		<< "BattleAbility" 
		<< "Resource" 
		<< "Scene"
		<< "Buffers" 
		<< "IsOffLine" 
		<< "Level" 
		<< "Job" 
		<< "HP" 
		<< "MP" 
		<< "MaxHP" 
		<< "MaxMP"
		<< "PosiX"
		<< "PosiZ"
		<< "GuildName"
		<< "VipLevel";
	m_pTeamModule->UpdatePropToDomain(pKernel, self, values);

	pKernel->Custom(self, CVarList() << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_JOIN_TEAM_RESULT);
	

    return 0;
}

int TeamModule::OnCommandAddMember(IKernel* pKernel, const PERSISTID& self,
                                   const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

    const wchar_t* member_name = args.WideStrVal(1);

    // 添加一个成员
    m_pTeamModule->AddTeamMember(pKernel, self, member_name);
    const wchar_t* selfname = pSelfObj->QueryWideStr("Name");
	// 通知加入者
    if (wcscmp(selfname, member_name) == 0)
    {
        // 加入队伍成功
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17424, CVarList());
    }
	// 通知其他成员
    else
    {
        // [{@0:name}]加入队伍
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17425, CVarList() << member_name);

    }

	if (m_pTeamModule->IsTeamCaptain(pKernel, self))
	{
		// 公共数据指针
		IPubData *pTeamData = m_pTeamModule->GetTeamPubData(pKernel);
		if (NULL == pTeamData)
		{
			return 0;
		}
		int team_id = pSelfObj->QueryInt("TeamID");
		// 取得队伍成员表名
		char team_member_rec_name[256];
		SPRINTF_S(team_member_rec_name, "team_%d_member_rec", team_id);

		// 遍历队伍成员表
		IRecord *pTeamMemRec = pTeamData->GetRecord(team_member_rec_name);
		if (NULL == pTeamMemRec)
		{
			return 0;
		}
		

		if (pTeamMemRec->GetRows() == pTeamMemRec->GetRowMax())
		{
			IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
			if (NULL == pTeamManiRec)
			{
				return 0;
			}
			int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, team_id);
			if (row < 0)
			{
				return 0;
			}
			int index = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX);
			CVarList msg;
			msg << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_FULL<<index;
			pKernel->Custom(self, msg);

		}
	}




    return 0;
}

int TeamModule::OnCommandRemoveMember(IKernel* pKernel, const PERSISTID& self,
                                      const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

    const wchar_t* member_name = args.WideStrVal(1);

    int reason = args.IntVal(2);

    // 删除一个成员
    m_pTeamModule->RemoveTeamMember(pKernel, self, member_name);

    const wchar_t* self_name = pSelfObj->QueryWideStr("Name");

	// 不提示自己
    if (wcscmp(self_name, member_name) != 0)
    {
		if (reason == 0)
		{
			// [{@0:name}]离开队伍
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17426, CVarList() << member_name);
		}
		else
		{
			// 队长将[{@0:name}]从队伍中踢出
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17427, CVarList() << member_name);
		}    
    }

    return 0;
}

int TeamModule::OnCommandClearTeam(IKernel* pKernel, const PERSISTID& self,
                                   const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    int reason = args.IntVal(1);

	// 获得队伍ID，挖宝使用
	 int teamID = pKernel->QueryInt(self, "TeamID");
    // 清除队伍信息
    m_pTeamModule->ClearTeamInfo(pKernel, self);

    if (reason == LEAVE_TYPE_EXPEL)
    {
        // 队长将你从队伍中踢出
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17428, CVarList());
    }
    else if (reason == LEAVE_TYPE_OFFLINE)
    {
        // 队伍解散
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17429, CVarList());
    }
    else if (reason == LEAVE_TYPE_EXIT)
    {
        // 你退出了队伍
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17430, CVarList());
    }

	// 通知组队秘境已离开队伍
//	pKernel->Command(self, self, CVarList() << COMMAND_TEAMSCENE_CLEAR << teamID);
	
    return 0;
}

int TeamModule::OnCommandUpdateMemberInfo(IKernel* pKernel, const PERSISTID& self,
                                          const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    const wchar_t* member_name = args.WideStrVal(1);

    int col = args.IntVal(2);

    // 更新信息
    m_pTeamModule->RefreshTeamMemberInfo(pKernel, self, member_name, col);

    return 0;
}

int TeamModule::OnCommandUpdateTeamInfo(IKernel* pKernel, const PERSISTID& self,
                                        const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    int col = args.IntVal(1);
	int teamID = args.IntVal(2);

    // 更新信息
    m_pTeamModule->RefreshTeamMainInfo(pKernel, self, col);
	
    return 0;
}

// 更新队伍成员位置
int TeamModule::OnCommandUpdateMemberPosition( IKernel* pKernel, const PERSISTID& self,
											  const PERSISTID& sender, const IVarList& args )
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	if (!m_pTeamModule->IsInTeam(pKernel, self))
	{
		return 0;
	}

	// 更新属性到公共数据
	m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "PosiX" << "PosiZ");

	return 0;
}


void TeamModule::OnCommandGetTargetPos(IKernel*pKernel, const PERSISTID& self, const IVarList& args)
{

	IGameObj* pSceneObj = pKernel->GetGameObj(self);
	if (NULL == pSceneObj)
	{
		return;
	}

	const wchar_t* wsPlayerName = args.WideStrVal(2);
	IGameObj* pTargetObj = pKernel->GetGameObj(self);
	if (NULL == pTargetObj)
	{
		return;
	}
	

	float fPosX = pTargetObj->GetPosiX();
	float y = pTargetObj->GetPosiY();
	float fPosZ = pTargetObj->GetPosiZ();
	float fPosY = GetCurFloorHeight(pKernel, fPosX, y, fPosZ);

	int sceneID  = pKernel->GetSceneId();
	if (sceneID >= OUTDOOR_SCENE_MAX)
	{
		return;
	}

	CVarList msg;
	msg << COMMAND_TEAM_MSG << COMMAND_GET_TARE_POS_RSP <<sceneID  << fPosX << fPosY << fPosZ;
	

	pKernel->CommandByName(wsPlayerName, msg);





}

void TeamModule::OnCommandTransferToTarget(IKernel*pKernel, const PERSISTID& self, const IVarList& args)
{

	int objSceneID = args.IntVal(2);
	float x = args.FloatVal(3);
	float y = args.FloatVal(4);
	float z = args.FloatVal(5);

	int selfSceneID = pKernel->GetSceneId();
	if (selfSceneID == objSceneID)
	{
		pKernel->MoveTo(self, x, y, z, 0);
	}
	else
	{
		AsynCtrlModule::m_pAsynCtrlModule->SwitchLocate(pKernel, self, objSceneID,x,y,z,0);
	}



}

// 清除附近队伍列表信息
int TeamModule::OnCommandClearNearbyTeam(IKernel* pKernel, const PERSISTID& self,
										const PERSISTID& sender, const IVarList& args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

	int teamID = args.IntVal(1);

	// 加入队伍后，清除附近队伍表中该队伍的数据
	IRecord *pNearbyRec = pSelfObj->GetRecord(FIELD_RECORD_NEARBY_TEAM_REC);
	if (NULL == pNearbyRec)
	{
		return 0;
	}
	int row = pNearbyRec->FindInt(COLUMN_NEARBY_TEAM_REC_0000, teamID);
	if (row >= 0)
	{
		pNearbyRec->RemoveRow(row);
	}

	return 0;
}

int TeamModule::OnCommandLevelUp(IKernel *pKernel, const PERSISTID &self, 
								 const PERSISTID &sender, const IVarList &args)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    if (!m_pTeamModule->IsInTeam(pKernel, self))
    {
        return 0;
    }

    // 更新属性到公共数据
    m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "Level");

    return 0;
}

int TeamModule::C_OnMaxHPChange(IKernel* pKernel, const PERSISTID& self, 
								const char* property, const IVar& old)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    if (!m_pTeamModule->IsInTeam(pKernel, self))
    {
        return 0;
    }

    // 更新属性到公共数据
    m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "MaxHP");

    return 0;
}

int TeamModule::C_OnGuildNameChange(IKernel* pKernel, const PERSISTID& self,
								const char* property, const IVar& old)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    if (!m_pTeamModule->IsInTeam(pKernel, self))
    {
        return 0;
    }

	// 更新属性到公共数据
	m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "GuildName");

    return 0;
}

int TeamModule::C_OnJobChanged(IKernel* pKernel, const PERSISTID& self, 
							   const char* property, const IVar& old)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	if (!m_pTeamModule->IsInTeam(pKernel, self))
	{
		return 0;
	}

	// 更新属性到公共数据
	m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "Job");


	return 0;
}

// 战斗力变化回调
int TeamModule::C_OnHighestBattleAbilityChange(IKernel* pKernel, const PERSISTID& self,
										const char* property, const IVar& old)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	if (!m_pTeamModule->IsInTeam(pKernel, self))
	{
		return 0;
	}

	// 更新属性到公共数据
	m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "HighestBattleAbility");
	return 0;
}

int TeamModule::C_OnFightState(IKernel* pKernel, const PERSISTID& self, const char* property, const IVar& old)
{

		
		if (!pKernel->Exists(self))
		{
			return 0;
		}
		if (!m_pTeamModule->IsInTeam(pKernel, self))
		{
			DELETE_CRITICAL(pKernel, self, FIELD_PROP_FIGHT_STATE, "TeamModule::C_OnFightState");
			return 0;
		}

		// 更新属性到公共数据
		m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << FIELD_PROP_FIGHT_STATE);
		return 0;
}

int TeamModule::C_OnHPChange(IKernel* pKernel, const PERSISTID& self,
							 const char* property, const IVar& old)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
    if (!m_pTeamModule->IsInTeam(pKernel, self))
    {
        return 0;
    }

    // 更新属性到公共数据
    m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "HP");

    return 0;
}

// 队伍是否已满
bool TeamModule::IsFull(IKernel* pKernel, const PERSISTID& self)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}
	
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return false;
	}
    int max_rows = pTeamRec->GetRowMax();
	
    // 如果队伍类型为小队
	if (max_rows > MAX_MEMBER_IN_NORMALTEAM)
	{
		max_rows = MAX_MEMBER_IN_NORMALTEAM;
	}

    int rows = pTeamRec->GetRows();
    return rows >= max_rows;
}

bool TeamModule::IsAutoJoin(IKernel*pKernel, int teamID)
{
	/*if (!pKernel->Exists(obj))
	{
		return false;
	}
	IGameObj *pSelfObj = pKernel->GetGameObj(obj);
	if (NULL == pSelfObj)
	{
		return false;
	}

	int team_id = pSelfObj->QueryInt("TeamID");
	if (team_id <= 0){
		return false;
	}*/
	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return false;
	}

	IPubData* pTeamData = pDomainSpace->GetPubData(GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return false;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec)
	{
		return false;
	}

	int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
	if (row < 0)
	{
		return false;
	}
	int autoJoin = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_AUTO_JOIN);
	return autoJoin == TEAM_AUTO_JOIN_OPEN;


}

bool TeamModule::IsAchieveTeamLevel(IKernel*pKernel, const PERSISTID& self, int teamID)
{
	if (!pKernel->Exists(self))
	{
		return false;
	}
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}
	int selfLevel = pSelfObj->QueryInt(FIELD_PROP_LEVEL );

	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return false;
	}
	IPubData* pTeamData = pDomainSpace->GetPubData(GetDomainName(pKernel).c_str());
	if (pTeamData == NULL){
		return false;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec){
		return false;
	}

	int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
	if (row < 0)
	{
		return false;
	}
	int minLevel = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_LIMIT_MIN_LEVEL);
	int maxLevel = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_LIMIT_MAX_LEVEL);
	if (selfLevel<minLevel || selfLevel >maxLevel)
	{
		return false;
	}

	return true;


}

void TeamModule::SetTeamMemKick(IKernel*pKernel, const PERSISTID& self, int kickState)
{



	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}
	// 保护
	int team_id = pSelfObj->QueryInt("TeamID");
	if (team_id <= 0)
	{
		return;
	}
	const wchar_t*self_name = pSelfObj->QueryWideStr(FIELD_PROP_NAME);
	CVarList msg;
	msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_MEMBER
		<< team_id
		<< self_name
		<< TEAM_REC_COL_BE_KICK
		<< kickState;
	pKernel->SendPublicMessage(msg);
	
}

// 复制整个队伍信息到角色属性中
void TeamModule::RefreshAllTeamInfo(IKernel* pKernel, const PERSISTID& self, int team_id)
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

    IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
    if (pDomainSpace == NULL)
    {
        return;
    }

    IPubData* pTeamData = pDomainSpace->GetPubData(GetDomainName(pKernel).c_str());
    if (pTeamData == NULL)
    {
        return;
    }
	
	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec)
	{
		return;
	}

    int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, team_id);
    if (row < 0)
    {
        return;
    }

    CVarList values;
    if (!pTeamManiRec->QueryRowValue(row, values))
    {
        return;
    }

    const char* member_rec_name = values.StringVal(TEAM_MAIN_REC_COL_TEAM_MEMBER_REC_NAME);     // 成员表名称
    const wchar_t* captain_name = values.WideStrVal(TEAM_MAIN_REC_COL_TEAM_CAPTAIN);            // 队长名称

    // 成员表
	IRecord *pTeamMemRec = pTeamData->GetRecord(member_rec_name);
	if (NULL == pTeamMemRec)
	{
		return;
	}

    // 先清除（玩家队伍成员表）
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}
    pTeamRec->ClearRow();
	
    // 复制公共数据的表格内容到本表
    int rows = pTeamMemRec->GetRows();
	LoopBeginCheck(f);
    for (int i = 0; i < rows; ++i)
    {
		LoopDoCheck(f);
        CVarList valueList;
        pTeamMemRec->QueryRowValue(i, valueList);
        pTeamRec->AddRowValue(-1, valueList);
    }

    // 设置队伍属性
    pSelfObj->SetInt("TeamID", team_id);
    pSelfObj->SetWideStr("TeamCaptain", captain_name);

	// 添加队伍成员属性回调
	m_pTeamModule->AddCriticalCallBack(pKernel,self);

}

// 清除角色属性中的所有队伍信息
void TeamModule::ClearTeamInfo(IKernel* pKernel, const PERSISTID& self)
{
	if (!pKernel->Exists(self))
	{
		return;
	}
	IGameObj* pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

    // 设置队伍属性
    pSelfObj->SetInt("TeamID", 0);
    pSelfObj->SetWideStr("TeamCaptain", L"");

	// 删除属性回调
	m_pTeamModule->RemoveCriticalCallBack(pKernel,self);

    // 清除成员表
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}
    pTeamRec->ClearRow();

	IRecord *requestRec = pSelfObj->GetRecord(REQUEST_REC);
	if (requestRec == NULL)
	{
		return;
	}
	int rows = requestRec->GetRows();
	if (rows <= 0)
	{
		return ;
	}
	int type = REQUESTTYPE_JOIN_SINGLEPLAYER;
	LoopBeginCheck(h);
	for (int i = rows - 1; i >= 0; --i)
	{
		LoopDoCheck(h);
		int applyType = requestRec->QueryInt(i, REQUEST_REC_TYPE);
		if (applyType == type)
		{
			requestRec->RemoveRow(i);
			continue;
		}
	}
	



}

// 更新队伍主表的信息到角色属性中
void TeamModule::RefreshTeamMainInfo(IKernel* pKernel, const PERSISTID& self, int col)
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

    int team_id = pSelfObj->QueryInt("TeamID");
    if (team_id <= 0)
    {
        return;
    }

    IPubData* pTeamData = m_pTeamModule->GetTeamPubData(pKernel);
    if (pTeamData == NULL)
    {
        return;
    }
	
	IRecord *pTeamMainRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
    if (NULL == pTeamMainRec)
    {
        return;
    }

    int row = pTeamMainRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, team_id);
    if (row < 0)
    {
        return;
    }

    // 设置队伍属性
    switch (col)
    {
        case TEAM_MAIN_REC_COL_TEAM_CAPTAIN:
        {
            // 队长
            const wchar_t* captain_name = pTeamMainRec->QueryWideStr(row, col);
            pSelfObj->SetWideStr("TeamCaptain", captain_name);
            const wchar_t* self_name = pSelfObj->QueryWideStr("Name");
			if (wcscmp(self_name, captain_name) == 0)
			{
				// 你成为队长
				CustomSysInfoByName(pKernel, self_name, SYSTEM_INFO_ID_17431, CVarList());

				ADD_HEART_BEAT(pKernel,self,"TeamModule::H_UpdateMemberPos",3000);
				ADD_CRITICAL(pKernel, self, FIELD_PROP_FIGHT_STATE,"TeamModule::C_OnFightState");
			}
			else
			{
				// [{@0:name}]成为队长
				CustomSysInfoByName(pKernel, self_name, SYSTEM_INFO_ID_17432, CVarList() << captain_name);
				DELETE_HEART_BEAT(pKernel,self,"TeamModule::H_UpdateMemberPos");
				DELETE_CRITICAL(pKernel,self,FIELD_PROP_FIGHT_STATE,"TeamModule::C_OnFightState");
			}
        }
        break;
		case TEAM_MAIN_REC_COL_TEAM_VIEW:
		case TEAM_MAIN_REC_COL_TEAM_AUTO_JOIN:
		case TEAM_MAIN_REC_COL_TEAM_AUTO_MATCH:
		case TEAM_MAIN_REC_COL_TEAM_LIMIT_MIN_LEVEL:
		case TEAM_MAIN_REC_COL_TEAM_LIMIT_MAX_LEVEL:
		{
			OnCustomGetTeamInfo(pKernel,self);
		}break;

		case TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX:
		{
			// 队长
			const wchar_t* captain_name = pSelfObj->QueryWideStr(FIELD_PROP_TEAM_CAPTAIN);
			const wchar_t* self_name = pSelfObj->QueryWideStr("Name");
			if (wcscmp(self_name, captain_name) == 0)
			{
				ResetTeamLimitLevel(pKernel, self, GetTeamObjIndex(pKernel, team_id));
			}
			OnCustomGetTeamInfo(pKernel, self);


			pKernel->Custom(self, CVarList() << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_SET_TEAM_OBJECT_INDEX << GetTeamObjIndex(pKernel, team_id));

		}break;
		default:
			break;
    }
}

// 更新队伍成员的信息到角色属性中
void TeamModule::RefreshTeamMemberInfo(IKernel* pKernel, const PERSISTID& self,
                                       const wchar_t* member_name, int col)
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

    int team_id = pSelfObj->QueryInt("TeamID");
    if (team_id <= 0)
    {
        return;
    }

    IPubData* pTeamData = GetTeamPubData(pKernel);
    if (pTeamData == NULL)
    {
        return;
    }
	
	IRecord *pTeamMainRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
    if (NULL == pTeamMainRec)
    {
        return;
    }

    int row = pTeamMainRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, team_id);

    if (row < 0)
    {
        return;
    }

    // 成员表中的内容
    const char* member_rec_name = pTeamMainRec->QueryString(row, TEAM_MAIN_REC_COL_TEAM_MEMBER_REC_NAME);

	IRecord *pTeamMemRec = pTeamData->GetRecord(member_rec_name);
    if (NULL == pTeamMemRec)
    {
        return;
    }

    row = pTeamMemRec->FindWideStr(TEAM_REC_COL_NAME, member_name);
    if (row < 0)
    {
        return;
    }

    // 在角色队伍中的行号
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}
    int self_row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, member_name);

    if (self_row < 0)
    {
        // 未找到，添加
        CVarList values;
        pTeamMemRec->QueryRowValue(row, values);

        pTeamRec->AddRowValue(-1, values);
    }
    else
    {
        if (col < 0)
        {
            return;
        }

        // 已找到，重新设置
        switch (pTeamMemRec->GetColType(col))
        {
            case VTYPE_INT:
            {
                pTeamRec->SetInt(self_row, col, pTeamMemRec->QueryInt(row, col));
            }
            break;

            case VTYPE_INT64:
            {
                pTeamRec->SetInt64(self_row, col, pTeamMemRec->QueryInt64(row, col));
            }
            break;

            case VTYPE_FLOAT:
            {
                pTeamRec->SetFloat(self_row, col, pTeamMemRec->QueryFloat(row, col));
            }
            break;

            case VTYPE_DOUBLE:
            {
                pTeamRec->SetDouble(self_row, col, pTeamMemRec->QueryDouble(row, col));
            }
            break;

            case VTYPE_STRING:
            {
                pTeamRec->SetString(self_row, col, pTeamMemRec->QueryString(row, col));
            }
            break;

            case VTYPE_WIDESTR:
            {
                pTeamRec->SetWideStr(self_row, col, pTeamMemRec->QueryWideStr(row, col));
            }
            break;

            default:
                break;
        }


        switch (col)
        {
            case TEAM_REC_COL_ISOFFLINE:
            {
                // 下线通知
                int work = pTeamMemRec->QueryInt(row, TEAM_REC_COL_TEAMWORK);

                int64_t mblastleavetime = pTeamMemRec->QueryInt64(row, TEAM_REC_COL_ISOFFLINE);

                if (mblastleavetime > 0)
                {
        //            if (work == TYPE_TEAM_CAPTAIN)
        //            {
        //                // 队长掉线了，5分钟后系统将自动移交队长
        //                CustomSysInfo(pKernel, self, TIPSTYPE_SYSFUNCTION_PROMPT_MESSAGE,
								//STR_TEAM_SYSINFO_NTCAPTAINOFFLINE, CVarList());
        //            }
                    if (work == TYPE_TEAM_PLAYER)
                    {
                        // 队友掉线
						CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17433, CVarList() << member_name);
                    }
                }
            }
            break;
            case TEAM_REC_COL_TEAMWORK:
            {
                // 改变队员职位
                int rows = pTeamRec->GetRows();
				LoopBeginCheck(k);
                for (int i = 0; i < rows; ++i)
                {
					LoopDoCheck(k);
                    if (self_row >= 0 && i != self_row)
                    {
                        pTeamRec->SetInt(i, TEAM_REC_COL_TEAMWORK, TYPE_TEAM_PLAYER);
                    }
                }
            }
            break;		
			case TEAM_REC_COL_READY:	// 更新准备状态				
				break;
            default:
                break;
        }
    }
}

// 添加一个成员
void TeamModule::AddTeamMember(IKernel* pKernel, const PERSISTID& self,
                               const wchar_t* member_name)
{
	if (!pKernel->Exists(self))
	{
		return;
	}
    // 刷新属性
    RefreshTeamMemberInfo(pKernel, self, member_name, -1);
	
}

// 删除一个成员
void TeamModule::RemoveTeamMember(IKernel* pKernel, const PERSISTID& self,
                                  const wchar_t* member_name)
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}
	
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}
    // 在角色队伍中的行号
    int row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, member_name);

    if (row >= 0)
    {
        // 删除
        pTeamRec->RemoveRow(row);
    }
	
}


// 更新角色属性到公共数据服务器
void TeamModule::UpdatePropToDomain(IKernel* pKernel, const PERSISTID& self,
                                    const IVarList& prop_list)
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

    int team_id = pSelfObj->QueryInt("TeamID");
    const wchar_t* self_name = pSelfObj->QueryWideStr("Name");

    CVarList msg;
    msg << PUBSPACE_DOMAIN 
		   << GetDomainName(pKernel).c_str() 
		   << SP_DOMAIN_MSG_TEAM_UPDATE_MEMBER 
		   << team_id 
		   << self_name;

    int counts = (int)prop_list.GetCount();
	LoopBeginCheck(g);
    for (int i = 0; i < counts; ++i)
    {
		LoopDoCheck(g);
        const char* prop = prop_list.StringVal(i);

		if (strcmp(prop, "UID") == 0)
		{
			const char * pSelfUID = pKernel->SeekRoleUid(self_name);
			if (StringUtil::CharIsNull(pSelfUID))
			{
				return;
			}
			msg << TEAM_REC_COL_UID << pSelfUID;
		}
		else if (strcmp(prop, "Sex") == 0)
		{
			msg << TEAM_REC_COL_SEX << pSelfObj->QueryInt("Sex");
		}
		else if (strcmp(prop, "BattleAbility") == 0)
		{
			msg << TEAM_REC_COL_FIGHT << pSelfObj->QueryInt("HighestBattleAbility");
		}
        else if (strcmp(prop, "Scene") == 0)
        {
            msg << TEAM_REC_COL_SCENE << pKernel->GetSceneId();
        }
        else if (strcmp(prop, "IsOffLine") == 0)
        {
            msg << TEAM_REC_COL_ISOFFLINE << int64_t(0);
        }
        else if (strcmp(prop, "Level") == 0)
        {
            msg << TEAM_REC_COL_BGLEVEL << pSelfObj->QueryInt("Level");
        }

        else if (strcmp(prop, "MaxHP") == 0)
        {
            msg << TEAM_REC_COL_MAXHP << pSelfObj->QueryInt64("MaxHP");
        }
        else if (strcmp(prop, "HP") == 0)
        {
            msg << TEAM_REC_COL_HP << pSelfObj->QueryInt64("HP");
        }
        else if (strcmp(prop, "Job") == 0)
        {
            msg << TEAM_REC_COL_BGJOB << pSelfObj->QueryInt("Job");
        }
		else if (strcmp(prop, "Online") == 0)
		{
			int online = pKernel->GetPlayerScene(self_name) > 0 ? ONLINE : OFFLINE;
			msg << TEAM_REC_COL_ONLINE << online;
		}
		else if (strcmp(prop, "PosiX") == 0)
		{
			msg << TEAM_REC_COL_POSITION_X << pSelfObj->GetPosiX();
		}
		else if (strcmp(prop, "PosiZ") == 0)
		{
			msg << TEAM_REC_COL_POSITION_Z << pSelfObj->GetPosiZ();
		}
		else if (strcmp(prop, "GuildName") == 0)
		{
			msg << TEAM_REC_COL_GUILDNAME << pSelfObj->QueryWideStr("GuildName");
		}
		else if (strcmp(prop, "FightState") == 0)
		{
			msg << TEAM_REC_COL_FIGHT_STATE << pSelfObj->QueryInt("FightState");
		}
		else if (strcmp(prop, "VipLevel") == 0)
		{
			msg << TEAM_REC_COL_VIP_LV << pSelfObj->QueryInt("VipLevel");
		}
    }

    pKernel->SendPublicMessage(msg);
}

// 更新副本ID到公共数据服务器
void TeamModule::UpdateCloneToDomain(IKernel* pKernel, const PERSISTID& self,int clone_id)
{
	/*if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

    int team_id = pSelfObj->QueryInt("TeamID");
    const wchar_t* self_name = pSelfObj->QueryWideStr("Name");

    CVarList msg;
    msg << PUBSPACE_DOMAIN 
		   << GetDomainName(pKernel).c_str() 
		   << SP_DOMAIN_MSG_TEAM_UPDATE_MEMBER 
		   << team_id 
		   << self_name 
		   << TEAM_REC_COL_CLONE_ID 
		   << clone_id;
  
    pKernel->SendPublicMessage(msg)*/;
}

// 是否有拾取物品的权利
bool TeamModule::HaveRightPickUp(IKernel* pKernel, const PERSISTID& self,
                                 const PERSISTID& target_obj , const wchar_t* member_name)
{
    if (!pKernel->Exists(self) || !pKernel->Exists(target_obj) || !member_name)
    {
        return false;
    }

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}
	
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
    if (pTeamRec != NULL)
    {
        int member_row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, member_name);

        const char* member_scene = pTeamRec->QueryString(member_row, TEAM_REC_COL_SCENE);

        const char* target_scene = pKernel->GetConfig(pKernel->GetScene());
        // 如果在同一场景
        if (strcmp(member_scene, target_scene) == 0)
        {
            return true;
        }
    }

    return false;
}




// 获取Domain的名字
const std::wstring& TeamModule::GetDomainName(IKernel * pKernel)
{
	if (m_domainName.empty())
	{
		wchar_t wstr[256];
		const int server_id = pKernel->GetServerId();
		SWPRINTF_S(wstr, L"Domain_Team_%d", server_id);

		m_domainName = wstr;
	}

	return m_domainName;
}

// 清除队伍成员属性回调
void TeamModule::RemoveCriticalCallBack( IKernel* pKernel, const PERSISTID& self )
{
	if (!pKernel->Exists(self))
	{
		return;
	}
	// 在队伍里
	if (m_pTeamModule->IsInTeam(pKernel, self))
	{
		return;
	}

	// 删除属性回调
	pKernel->RemoveCriticalFunc(self, "Job", "TeamModule::C_OnJobChanged");
	pKernel->RemoveCriticalFunc(self, "MaxHP", "TeamModule::C_OnMaxHPChange");
	pKernel->RemoveCriticalFunc(self, "HP", "TeamModule::C_OnHPChange");
	pKernel->RemoveCriticalFunc(self, "HighestBattleAbility", "TeamModule::C_OnHighestBattleAbilityChange");
	pKernel->RemoveCriticalFunc(self, "GuildName", "TeamModule::C_OnGuildNameChange");
	pKernel->RemoveCriticalFunc(self, "GuildName", "TeamModule::C_OnVIPLevelChanged");
	pKernel->RemoveCriticalFunc(self, FIELD_PROP_FIGHT_STATE, "TeamModule::C_OnFightState");
	return;
}

int TeamModule::OnCommandTask(IKernel* pKernel, const PERSISTID& self, const PERSISTID& sender, const IVarList& args)
{
// 	int taskState = args.IntVal(1);
// 	if (taskState != QUEST_PROGRESS_ACCEPT){
// 		return 0;
// 	}
// 
// 	int taskID = args.IntVal(2);
// 	if (QuestModule::m_pQuestModule->IsThisType(taskID, QUEST_TYPE_OFFER))
// 	{
// 		int index = m_pTeamModule->GetTeamIndexByType(TeamWanted);
// 		m_pTeamModule->AutoChangeTeamIndex(pKernel,self,index);
// 	}
// 	else if (QuestModule::m_pQuestModule->IsThisType(taskID, QUEST_TYPE_ESCORT))
// 	{
// 		int index = m_pTeamModule->GetTeamIndexByType(TeamEscort);
// 		m_pTeamModule->AutoChangeTeamIndex(pKernel,self,index);
// 	}
	return 0;
}

// 添加队伍成员属性回调
void TeamModule::AddCriticalCallBack( IKernel* pKernel, const PERSISTID& self )
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	if (m_pTeamModule->IsInTeam(pKernel,self))
	{
		// 属性回调
		ADD_CRITICAL(pKernel, self, "Job", "TeamModule::C_OnJobChanged");
		ADD_CRITICAL(pKernel, self, "MaxHP", "TeamModule::C_OnMaxHPChange");
		ADD_CRITICAL(pKernel, self, "HP", "TeamModule::C_OnHPChange");
		ADD_CRITICAL(pKernel, self, "HighestBattleAbility", "TeamModule::C_OnHighestBattleAbilityChange");
		ADD_CRITICAL(pKernel, self, "GuildName", "TeamModule::C_OnGuildNameChange");
		ADD_CRITICAL(pKernel, self, "GuildName", "TeamModule::C_OnVIPLevelChanged");
	}
}

//清除入队申请
int TeamModule::OnCustomClearApply(IKernel* pKernel, const PERSISTID& self, const PERSISTID& sender, const IVarList& args)
{
    if (!pKernel->Exists(self))
    {
        return 0;
    }

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

    if (args.GetCount() < 3)
    {
        return 0;
    }
    int type = args.IntVal(2);

    IRecord *requestRec = pSelfObj->GetRecord(REQUEST_REC);
    if (requestRec == NULL)
    {
        return 0;
    }
    int rows = requestRec->GetRows();
    if (rows <= 0)
    {
        return 0;
    }

	LoopBeginCheck(h);
    for (int i = rows - 1; i >= 0; --i)
    {
		LoopDoCheck(h);
        int applyType = requestRec->QueryInt(i, REQUEST_REC_TYPE);
        if (applyType == type)
        {
            requestRec->RemoveRow(i);
            continue;
        }
    }
    pKernel->Custom(self, CVarList() << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_CLEAR_APPLY << 1);
    
    return 1;
}

//获取申请列表
int TeamModule::OnCustomApplyList(IKernel* pKernel, const PERSISTID& self, const PERSISTID& sender, const IVarList& args)
{
    if (!pKernel->Exists(self))
    {
        return 0;
    }

    IGameObj *pSelfObj = pKernel->GetGameObj(self);
    if (NULL == pSelfObj)
    {
        return 0;
    }
    int applyType = args.IntVal(2);

    if (applyType != REQUESTTYPE_JOIN_SINGLEPLAYER &&
        applyType != REQUESTTYPE_INVITE_SINGLEPLAYER)
    {
        return 0;
    }

    IRecord *requestRec = pSelfObj->GetRecord(REQUEST_REC);
    if (requestRec == NULL)
    {
        return 0;
    }
    int rows = requestRec->GetRows();
    CVarList playerList;
    if (rows > 0)
    {
        LoopBeginCheck(j);
        for (int i = 0; i < rows; ++i)
        {
			LoopDoCheck(j);
            int type = requestRec->QueryInt(i, REQUEST_REC_TYPE);
            if (type != applyType)
            {
                continue;
            }
            const wchar_t* playerName = requestRec->QueryWideStr(i, REQUEST_REC_NAME);
            int pic = requestRec->QueryInt(i, REQUEST_REC_JOB);
            int battle = requestRec->QueryInt(i, REQUEST_REC_BATTLE_ABILITY);
			int level = requestRec->QueryInt(i, REQUEST_REC_LEVEL);
			int sex = requestRec->QueryInt(i, REQUEST_REC_SEX);
            playerList << playerName << pic << battle<<level<<sex;
        }
    }
    int listSize = (int)playerList.GetCount();
    CVarList returnList;
    returnList << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_APPLY_LIST << listSize << playerList;
    pKernel->Custom(self, returnList);
    return 1;
}

int TeamModule::OnCustomSetViewState(IKernel* pKernel, const PERSISTID& self, const PERSISTID& sender, const IVarList& args)
{

	
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelf = pKernel->GetGameObj(self);
	if (NULL == pSelf)
	{
		return 0;
	}
	int viewValue = args.IntVal(2);
	const wchar_t* self_name = pSelf->QueryWideStr("Name");

	if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
	{
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
		// 自已不是队长，不能设置
		return 0;
	}


	// 队伍ID
	int team_id = pSelf->QueryInt("TeamID");

	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
		<< team_id
		<< self_name
		<< TEAM_MAIN_REC_COL_TEAM_VIEW
		<< viewValue;
	pKernel->SendPublicMessage(pub_msg);

	return 0;


}

int TeamModule::OnCustomSetAutoJoin(IKernel* pKernel, const PERSISTID& self, const PERSISTID& sender, const IVarList& args)
{
	
	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelf = pKernel->GetGameObj(self);
	if (NULL == pSelf)
	{
		return 0;
	}
	int viewValue = args.IntVal(2);
	const wchar_t* self_name = pSelf->QueryWideStr("Name");

	if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
	{
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
		// 自已不是队长，不能设置
		return 0;
	}


	// 队伍ID
	int team_id = pSelf->QueryInt("TeamID");
	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
		<< team_id
		<< self_name
		<< TEAM_MAIN_REC_COL_TEAM_AUTO_JOIN
		<< viewValue;
	pKernel->SendPublicMessage(pub_msg);
	return 0;

}

int TeamModule::OnCustomGetTeamInfo(IKernel* pKernel, const PERSISTID& self)
{

	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return 0;
	}

	int team_id = pSelfObj->QueryInt("TeamID");
	if (team_id <= 0)
	{
		return 0;
	}

	IPubData* pTeamData = m_pTeamModule->GetTeamPubData(pKernel);
	if (pTeamData == NULL)
	{
		return 0;
	}

	IRecord *pTeamMainRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamMainRec)
	{
		return 0;
	}

	int row = pTeamMainRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, team_id);
	if (row < 0)
	{
		return 0;
	}

	int autoJoin = pTeamMainRec->QueryInt(row,TEAM_MAIN_REC_COL_TEAM_AUTO_JOIN);
	int view = pTeamMainRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_VIEW);
	int index = pTeamMainRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX);
	int autoMatch = pTeamMainRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_AUTO_MATCH);
	int minLevel = pTeamMainRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_LIMIT_MIN_LEVEL);
	int maxLevel = pTeamMainRec->QueryInt(row,TEAM_MAIN_REC_COL_TEAM_LIMIT_MAX_LEVEL);
	pKernel->Custom(self, CVarList() << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_GET_TEAM_INFO<<autoJoin<<view<<index<<autoMatch<<minLevel<<maxLevel);
	return 0;
}

int TeamModule::OnCustomSetTeamObject(IKernel* pKernel, const PERSISTID& self,int index)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	IGameObj *pSelf = pKernel->GetGameObj(self);
	if (NULL == pSelf)
	{
		return 0;
	}
	
	const wchar_t* self_name = pSelf->QueryWideStr("Name");
	// 队伍ID
	int team_id = pSelf->QueryInt("TeamID");
	int indexNow = GetTeamObjIndex(pKernel, team_id);
	if (indexNow == index){
		return 0;
	}

	int teamID = pSelf->QueryInt(FIELD_PROP_TEAM_ID);
	if (!TeamModule::IsTeamValid(pKernel, self, index))
	{
		return 0;
	}

	if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
	{
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
		// 自已不是队长，不能设置
		return 0;
	}


	if (!m_pTeamModule->IsAutoMatch(pKernel, self))
	{
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17434, CVarList());
		return 0;
	}
	
	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
		<< team_id
		<< self_name
		<< TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX
		<< index;
	pKernel->SendPublicMessage(pub_msg);

	return 0;
}

int TeamModule::OnCustomSetTeamLimitLevel(IKernel* pKernel, const PERSISTID& self, const IVarList& args)
{

	if (!pKernel->Exists(self))
	{
		return 0;
	}

	IGameObj *pSelf = pKernel->GetGameObj(self);
	if (NULL == pSelf)
	{
		return 0;
	}
	int limit_min_level = args.IntVal(2);
	int limit_max_level = args.IntVal(3);
	const wchar_t* self_name = pSelf->QueryWideStr("Name");

	if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
	{
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
		// 自已不是队长，不能设置
		return 0;
	}

	// 队伍ID
	int team_id = pSelf->QueryInt("TeamID");
	int teamMinLimitLevel = GetObjMinLimitLevel(GetTeamObjIndex(pKernel, team_id));
	if (limit_min_level < teamMinLimitLevel)
	{
		return 0;
	}


	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
		<< team_id
		<< self_name
		<< TEAM_MAIN_REC_COL_TEAM_LIMIT_MIN_LEVEL
		<< limit_min_level
		<< TEAM_MAIN_REC_COL_TEAM_LIMIT_MAX_LEVEL
		<< limit_max_level;
	pKernel->SendPublicMessage(pub_msg);
	
	return 0;

}

void TeamModule::OnCustomSendTeamList(IKernel*pKernel, const PERSISTID& self, int index)
{
	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return;
	}
	IPubData* pTeamData = pDomainSpace->GetPubData(TeamModule::GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return;
	}
	int level = pKernel->QueryInt(self,FIELD_PROP_LEVEL);

	const wchar_t* selfGuildName = L"";//pKernel->QueryWideStr(self, FIELD_PROP_GUILD_NAME);
	int selfNation = 1;// pKernel->QueryInt(self, FIELD_PROP_NATION);
	int teamSize = 0;
	CVarList msg;
	msg << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_GET_TEAM_LIST;
	CVarList teamList;
	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL != pTeamManiRec){
		
		int rowMax = pTeamManiRec->GetRows();
		LoopBeginCheck(a);
		for (int row = 0; row < rowMax; row++)
		{
			LoopDoCheck(a);
			int teamindex = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX);
			//int nation = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_NATION);
			if (teamindex == index || index == 0)
			{
				std::string team_member_rec_name = pTeamManiRec->QueryString(row, TEAM_MAIN_REC_COL_TEAM_MEMBER_REC_NAME);
				IRecord *pTeamMemRec = pTeamData->GetRecord(team_member_rec_name.c_str());
				if (pTeamManiRec == NULL){
					continue;
				}

				std::map<int, TeamRule>::iterator it = m_TeamRule.find(teamindex);
				if (it == m_TeamRule.end()){
					continue;
				}

				if (index == 0){

					if (level < it->second.m_limitLevel){
						continue;
					}
				}

// 				if (it->second.m_teamType == COMMAND_TEAM_CHECK_GUILD_INBREAK)
// 				{
// 					const wchar_t* teamGuildName = pTeamMemRec->QueryWideStr(0, TEAM_REC_COL_GUILDNAME);
// 					if (StringUtil::CharIsNull(teamGuildName)){
// 						continue;
// 					}
// 					if (wcscmp(selfGuildName, teamGuildName) != 0)
// 					{
// 						continue;
// 					}
// 				}

				int nIsOff = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OFF_JOIN_TEAM);
				teamList << teamindex;
				int memberMax = pTeamMemRec->GetRows();
				CVarList memberList;
				LoopBeginCheck(a);
				for (int member = 0; member < memberMax; member++)
				{
					LoopDoCheck(a);
					memberList << pTeamMemRec->QueryWideStr(member, TEAM_REC_COL_NAME)
						<< pTeamMemRec->QueryInt(member, TEAM_REC_COL_BGLEVEL)
						<< pTeamMemRec->QueryInt(member, TEAM_REC_COL_BGJOB)
						<< pTeamMemRec->QueryInt(member, TEAM_REC_COL_SEX)
						<< pTeamMemRec->QueryInt(member, TEAM_REC_COL_TEAMWORK)
						<< pTeamMemRec->QueryInt(member, TEAM_REC_COL_FIGHT);
				}

				teamList << memberMax
					<< memberList
					<< pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_LIMIT_MIN_LEVEL)		// 最低等级
					<< pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_LIMIT_MAX_LEVEL)		// 最高等级;
					<< nIsOff;
				teamSize++;
			}
		}
	}
	msg << teamSize << teamList;
	pKernel->Custom(self, msg);

}

bool TeamModule::IsAutoMatch(IKernel*pKernel, const PERSISTID& self)
{

	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return false;
	}
	IPubData* pTeamData = pDomainSpace->GetPubData(TeamModule::GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return false;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec){
		return false;
	}
	const wchar_t* selfName = pKernel->QueryWideStr(self, FIELD_PROP_NAME);
	int row = pTeamManiRec->FindWideStr(TEAM_MAIN_REC_COL_TEAM_CAPTAIN, selfName);
	if (row >= 0)
	{
		int autoMatch = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_AUTO_MATCH);
		if (autoMatch != TEAM_AUTO_MATCH_ON)
		{
			return true;
		}
	}

	return false;
}

void TeamModule::OnCustomTransferCapTainSite(IKernel*pKernel, const PERSISTID& self)
{


	if (!pKernel->Exists(self))
	{
		return ;
	}

	IGameObj *selfObj = pKernel->GetGameObj(self);
	if (NULL == selfObj)
	{
		return ;
	}

	// 保护
	int team_id = selfObj->QueryInt("TeamID");

	if (team_id <= 0)
	{
		return ;
	}

	IRecord *pTeamRec = selfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}

	// 自己名称
	const wchar_t* name = selfObj->QueryWideStr("Name");

	if (selfObj->QueryInt(FIELD_PROP_FIGHT_STATE)>=1)
	{//战斗中不能传送
			return ;	
	}

	// 查找
	int row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, name);
	if (row < 0)
	{
		return ;
	}

	int work = pTeamRec->QueryInt(row, TEAM_REC_COL_TEAMWORK);
	//自己是队长传送无效
	if (TYPE_TEAM_CAPTAIN == work)
	{
		return ;
	}


	// 队长名称
	const wchar_t* captain = selfObj->QueryWideStr("TeamCaptain");

	row = pTeamRec->FindWideStr(TEAM_REC_COL_NAME, captain);
	if (row < 0)
	{
		return;
	}

	int capSceneID = pKernel->GetPlayerScene(captain);
	if (capSceneID<= 0)
	{
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17435, CVarList());
	
		return ;
	}

	const wchar_t *selfName = selfObj->QueryWideStr(FIELD_PROP_NAME);

	if (!AsynCtrlModule::m_pAsynCtrlModule->CanSwitchLocate(pKernel, selfName, capSceneID))
	{
		return;
	}
	CVarList cmd;
	cmd << COMMAND_TEAM_MSG
		<< COMMAND_GET_TARGET_POS_REQ
		<< selfName;


	pKernel->CommandByName(captain, cmd);



}

void TeamModule::OnCustomConvene(IKernel*pKernel, const PERSISTID& self)
{

	IGameObj *selfObj = pKernel->GetGameObj(self);
	if (NULL == selfObj)
	{
		return;
	}

	// 保护
	int team_id = selfObj->QueryInt("TeamID");

	if (team_id <= 0)
	{
		return;
	}

	IRecord *pTeamRec = selfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}

	int teamRows = pTeamRec->GetRows();
	for (int row = 0; row < teamRows; row++)
	{
		int work = pTeamRec->QueryInt(row, TEAM_REC_COL_TEAMWORK);
		if (TYPE_TEAM_PLAYER == work)
		{
			int onLine = pTeamRec->QueryInt(row, TEAM_REC_COL_ONLINE);
			if (onLine)
			{
				const wchar_t * pName = pTeamRec->QueryWideStr(row, TEAM_REC_COL_NAME);
				pKernel->CustomByName(pName, CVarList() << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_CONVENE);
			}
		}
	}
	
}



void TeamModule::OnCustomSetFollow(IKernel*pKernel, const PERSISTID& self, int state)
{
	int teamID = pKernel->QueryInt(self, FIELD_PROP_TEAM_ID);
	if (teamID <= 0){
		return;
	}

	const wchar_t * pSelfName = pKernel->QueryWideStr(self, FIELD_PROP_NAME);

	CVarList msg;
	msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_MEMBER
		<< teamID
		<< pSelfName
		<< TEAM_REC_COL_FOLLOW_STATE
		<< state;

	pKernel->SendPublicMessage(msg);
}

//获取推荐组队列表
void TeamModule::OnCustomRequestRecommandList(IKernel*pKernel, const PERSISTID& self)
{
	IGameObj* pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

	IRecord * pOnlinePlayerRec = FriendModule::m_pFriendModule->GetPubOnlineRec(pKernel);
	if (NULL == pOnlinePlayerRec)
	{
		return;
	}

	int nLevelDiff = EnvirValueModule::EnvirQueryInt(ENV_VALUE_TEAM_RECOMMAND_LVDIFF);
	int nPlayerNum = EnvirValueModule::EnvirQueryInt(ENV_VALUE_TEAM_RECOMMAND_NUM);

	int nRowCount = pOnlinePlayerRec->GetRows();
	int nQueryNum = nPlayerNum > nRowCount ? nRowCount : nPlayerNum;

	int nSelfLv = pSelfObj->QueryInt(FIELD_PROP_LEVEL);
	const wchar_t* wsSelfName = pSelfObj->QueryWideStr(FIELD_PROP_NAME);

	CVarList msg;
	msg << SERVER_CUSTOMMSG_TEAM << CLIENT_CUSTOMMSG_TEAM_REQUEST_RECOMMAND_LIST;
	CVarList playerlist;
	int nQueryCount = 0;
	LoopBeginCheck(y);
	for (int i = 0; i < nQueryNum;++i)
	{
		LoopDoCheck(y);
		const wchar_t* wsName = pOnlinePlayerRec->QueryWideStr(i, PUB_COL_PLAYER_NAME);
		// 去除自己
		if (wcscmp(wsName, wsSelfName) == 0)
		{
			continue;
		}

		// 等级过滤
		int nLevel = pOnlinePlayerRec->QueryInt(i, PUB_COL_PLAYER_LEVEL);
		int nTmpDiff = abs(nSelfLv - nLevel);
		if (nTmpDiff > nLevelDiff)
		{
			continue;
		}
		int nJob = pOnlinePlayerRec->QueryInt(i, PUB_COL_PLAYER_JOB);
		int nSex = pOnlinePlayerRec->QueryInt(i, PUB_COL_PLAYER_SEX);
		int nBattleAbility = pOnlinePlayerRec->QueryInt(i, PUB_COL_PLAYER_BATTLE_ABLITITY);
	
		playerlist << wsName << nLevel << nJob << nSex << nBattleAbility;
		++nQueryCount;
	}


	msg << nQueryCount << playerlist;
	pKernel->Custom(self, msg);
}

int TeamModule::GetTeamObjType(IKernel*pKernel, int teamID)
{
	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return 0;
	}

	IPubData* pTeamData = pDomainSpace->GetPubData(TeamModule::GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return 0;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec)
	{
		return 0;
	}

	int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
	if (row < 0)
	{
		return 0;
	}

	int index = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX);
	std::map<int, TeamRule>::iterator it = m_TeamRule.find(index);
	if (it != m_TeamRule.end()){

		return it->second.m_teamType;
	}
	return 0;
}

int TeamModule::GetTeamIndexByType(int type)
{
	for (auto it : m_TeamRule)
	{
		if (it.second.m_teamType == type)
		{
			return it.first;
		}
	}
	return -1;
}

int TeamModule::GetTeamIndexByObjectID(int objID)
{
	for (auto it : m_TeamRule)
	{
		if (it.second.m_objectID == objID)
		{
			return it.first;
		}
	}
	return -1;

}

void TeamModule::AutoChangeTeamIndex(IKernel*pKernel, const PERSISTID& self,int index)
{
	if (m_pTeamModule->IsTeamCaptain(pKernel, self))
	{
		int team_id = pKernel->QueryInt(self, FIELD_PROP_TEAM_ID);
		int indexNow = GetTeamObjIndex(pKernel, team_id);
		if (indexNow == index){
			return ;
		}
		const wchar_t* self_name = pKernel->QueryWideStr(self,"Name");
		if (!m_pTeamModule->IsAutoMatch(pKernel, self))
		{
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17436, CVarList());
			AutoMatch(pKernel, self, TEAM_AUTO_MATCH_OFF);
		}

		OnCustomSetTeamObject(pKernel, self, index);
		// 发送消息
		CVarList pub_msg;
		pub_msg << PUBSPACE_DOMAIN
			<< GetDomainName(pKernel).c_str()
			<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
			<< team_id
			<< self_name
			<< TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX
			<< index;
		pKernel->SendPublicMessage(pub_msg);
	}
}

// 更新队员位置心跳
int TeamModule::H_UpdateMemberPos( IKernel* pKernel, const PERSISTID& self, int slice )
{
	return OnCustomUpdateMemberPosition(pKernel, self, self, CVarList());
}

int TeamModule::C_OnVIPLevelChanged(IKernel* pKernel, const PERSISTID& self, const char* property, const IVar& old)
{
	if (!pKernel->Exists(self))
	{
		return 0;
	}
	if (!m_pTeamModule->IsInTeam(pKernel, self))
	{
		return 0;
	}

	// 更新属性到公共数据
	m_pTeamModule->UpdatePropToDomain(pKernel, self, CVarList() << "VipLevel");

	return 0;
}

const wchar_t * TeamModule::GetTeamCaptain(IKernel*pKernel, int teamID)
{

	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return NULL;
	}

	IPubData* pTeamData = pDomainSpace->GetPubData(GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return NULL;
	}

	

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec)
	{
		return NULL;
	}

	int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
	if (row < 0)
	{
		return NULL;
	}

	CVarList values;
	if (!pTeamManiRec->QueryRowValue(row, values))
	{
		return NULL;
	}
	return pTeamManiRec->QueryWideStr(row, TEAM_MAIN_REC_COL_TEAM_CAPTAIN);

}

bool TeamModule::AddCheckTeamRule(int typeID, CheckTeamCondition rule)
{
	return (m_check_team_condition.insert(std::make_pair(typeID, rule))).second;
}


bool TeamModule::DealCheckTeamCondition(IKernel*pKernel, const PERSISTID& self, int index, const wchar_t*ResultinfromName)
{

	std::map<int, TeamRule>::iterator it = m_TeamRule.find(index);
	if (it == m_TeamRule.end()){
		return false;
	}
	int teamType = it->second.m_teamType;
    std::map<int, CheckTeamCondition>::iterator its = m_check_team_condition.find(teamType);
	if (its != m_check_team_condition.end())
	{
		return its->second(pKernel, self, CVarList() << it->second.m_objectID << ResultinfromName<<index);
	}

	return true;
}


bool TeamModule::IsTeamValid(IKernel*pKernel, const PERSISTID& self, int objIndex)
{
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (pSelfObj == NULL){
		return false;
	}
	std::map<int, TeamRule>::iterator it = m_TeamRule.find(objIndex);
	if (it == m_TeamRule.end()){
		return false;
	}
	int level = pSelfObj->QueryInt(FIELD_PROP_LEVEL);

	if (level < it->second.m_limitLevel)
	{
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17407, CVarList());
		return false;
	}
	int hour = 0;
	int minute = 0;
	int seconds = 0;
	int weekday = 0;

	weekday = util_get_day_of_week(hour, minute, seconds);

	TeamRule::TeamWeekTime openTime = it->second.m_openTime;

	TeamRule::TeamWeekTime endTime = it->second.m_endTime;



	const int dayPassTimeSec = hour * 60 * 60 + minute * 60 + seconds;

	const int dayBeginTimeSec = openTime.daySec;
	const int dayEndTimeSec = endTime.daySec;
	if (weekday >= openTime.weekDay&&weekday <= endTime.weekDay)
	{

		if (dayPassTimeSec >= dayBeginTimeSec && dayPassTimeSec <= dayEndTimeSec)
		{
			return true;
		}
	}

	CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17437, CVarList());
	return false;

}

int TeamModule::GetObjMinLimitLevel( int objIndex)
{
	std::map<int, TeamRule>::iterator it = m_TeamRule.find(objIndex);
	if (it == m_TeamRule.end()){
		return 0;
	}
	return it->second.m_limitLevel;
}

void TeamModule::ResetTeamLimitLevel(IKernel*pKernel, const PERSISTID& self, int objIndex)
{
	if (!pKernel->Exists(self))
	{
		return;
	}

	IGameObj *pSelf = pKernel->GetGameObj(self);
	if (NULL == pSelf)
	{
		return ;
	}

	int teamMinLimitLevel = GetObjMinLimitLevel(objIndex);
	int team_id = pSelf->QueryInt("TeamID");
	if (!pKernel->Exists(self))
	{
		return ;
	}
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return ;
	}

	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return ;
	}
	IPubData* pTeamData = pDomainSpace->GetPubData(GetDomainName(pKernel).c_str());
	if (pTeamData == NULL){
		return ;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec){
		return ;
	}

	int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, team_id);
	if (row < 0)
	{
		return;
	}

	int minLevel = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_LIMIT_MIN_LEVEL);
	if (minLevel > teamMinLimitLevel)
	{
		return;
	}

	// 队伍ID
	const wchar_t* self_name = pSelf->QueryWideStr("Name");
	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
		<< team_id
		<< self_name
		<< TEAM_MAIN_REC_COL_TEAM_LIMIT_MIN_LEVEL
		<< teamMinLimitLevel
		<< TEAM_MAIN_REC_COL_TEAM_LIMIT_MAX_LEVEL
		<< LevelModule::m_pLevelModule->GetPlayerMaxLevel(pKernel);
	pKernel->SendPublicMessage(pub_msg);
}


int TeamModule::GetTeamIndexObjID(int index)
{
	std::map<int, TeamRule>::iterator it = m_TeamRule.find(index);
	if (it != m_TeamRule.end()){
		return it->second.m_objectID;
	}
	return 0;
}


bool TeamModule::AddCheckTeamResult(int typeID, CheckTeamResult rule)
{
	return m_check_team_result.insert(std::make_pair(typeID, rule)).second;
}
void TeamModule::OnCommandCheckConditionRsp(IKernel* pKernel, const PERSISTID& self, const IVarList& args)
{
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (pSelfObj == NULL){
		return;
	}

	int ReqType = args.IntVal(2);
	int index = args.IntVal(3);
	int result = args.IntVal(4);
	const wchar_t * beCheckplayerName = args.WideStrVal(6);
	if (result == false)
	{
		//有一个人不满足条件
		if (!pSelfObj->FindData("TeamCheckFalse"))
		{
			pSelfObj->AddDataInt("TeamCheckFalse", 1);
		}
		
	}


	if (!pSelfObj->FindData("TeamCheckVerify"))
	{
		pSelfObj->AddDataInt("TeamCheckVerify", 0);
	}

	int value = pSelfObj->QueryDataInt("TeamCheckVerify");
	value++;
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}

	int teamMemberSize = pTeamRec->GetRows();
	if (teamMemberSize == value)
	{
		int result = true;
		//有一个人不满足条件
		if (pSelfObj->FindData("TeamCheckFalse"))
		{
			pSelfObj->RemoveData("TeamCheckFalse");
			result = false;
		}
		std::map<int, CheckTeamResult>::iterator it = m_check_team_result.find(ReqType);
		if (it != m_check_team_result.end())
		{
			it->second(pKernel, self, CVarList() << result << index);
		}
		pSelfObj->RemoveData("TeamCheckVerify");
	}
	else
	{
		pSelfObj->SetDataInt("TeamCheckVerify", value);
	}
}



bool TeamModule::OnCommandCheckConditionReq(IKernel* pKernel, const PERSISTID& self, int reqType, int index, const wchar_t*ResultinfromName)
{
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (pSelfObj == NULL){
		return false;
	}
	std::map<int, TeamRule>::iterator it = m_TeamRule.find(index);
	if (it == m_TeamRule.end()){
		return false;
	}
	int teamType = it->second.m_teamType;
	int result = 1;
	std::map<int, CheckTeamCondition>::iterator its = m_check_team_condition.find(teamType);
	if (its != m_check_team_condition.end())
	{
		result = its->second(pKernel, self, CVarList() << it->second.m_objectID << ResultinfromName<<index);
	}
	if (reqType != COMMAND_TEAM_CHECK_NONE)
	{
		CVarList rsp;
		rsp << COMMAND_TEAM_CHECK_CONDITION
			<< COMMAND_TEAM_CHECK_RSP
			<< reqType
			<< index
			<< result
			<< pSelfObj->QueryWideStr(FIELD_PROP_NAME);
		const wchar_t* teamCaptain = pSelfObj->QueryWideStr("TeamCaptain");
		pKernel->CommandByName(teamCaptain, rsp);
	}

	return result > 0 ? true : false;

}

int TeamModule::GetTeamObjIndex(IKernel *pKernel, int teamID)
{
	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return 0;
	}
	IPubData* pTeamData = pDomainSpace->GetPubData(TeamModule::GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return 0;
	}
	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec){
		return 0;
	}
	int teamRow = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
	if (teamRow < 0)
	{
		return 0;
	}
	return  pTeamManiRec->QueryInt(teamRow, TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX);

}


//返回队伍目标（例如组队副本相当于sceneid）
int TeamModule::GetTeamObjID(IKernel*pKernel, int teamID)
{

	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return 0;
	}

	IPubData* pTeamData = pDomainSpace->GetPubData(TeamModule::GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return 0;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec)
	{
		return 0;
	}

	int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
	if (row < 0)
	{
		return 0;
	}

	int index = pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX);

	std::map<int, TeamRule>::iterator it = m_TeamRule.find(index);
	if (it != m_TeamRule.end()){

		return it->second.m_objectID;
	}
	return 0;
}

bool TeamModule::CreateTeamByType(IKernel*pKernel, const PERSISTID& self, int teamType)
{

	if (!pKernel->Exists(self))
	{
		return false;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return false;
	}

	if (m_pTeamModule->IsActivityScene(pKernel)){
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17402, CVarList());
		return false;
	}



	if (m_pTeamModule->IsInTeam(pKernel, self))
	{
		// 已经在队伍中，不能创建队伍
		CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
		return false;
	}

	const wchar_t* selfName = pSelfObj->QueryWideStr("Name");
	int objIndex = 0;

	std::map<int, TeamRule> ::iterator it = m_TeamRule.begin();
	LoopBeginCheck(a);
	for (; it != m_TeamRule.end(); it++)
	{
		LoopDoCheck(a);
		if (it->second.m_teamType == teamType)
		{
			objIndex = it ->first;
			objIndex = it->first;
		}
	}


	if (!TeamModule::IsTeamValid(pKernel, self, objIndex))
	{
		return false;
	}
	int nation = 1;// pSelfObj->QueryInt(FIELD_PROP_NATION);

	TeamModule::AutoMatch(pKernel, self, TEAM_AUTO_MATCH_OFF);

	if (!m_pTeamModule->DealCheckTeamCondition(pKernel, self, objIndex, pSelfObj->QueryWideStr(FIELD_PROP_NAME)))
	{
		return false;
	}

	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_CREATE
		<< selfName
		<< L""
		<< nation
		<< objIndex;
	pKernel->SendPublicMessage(pub_msg);

	return true;
}

int TeamModule::OnCommandCheckCondition(IKernel* pKernel, const PERSISTID& self, const PERSISTID& sender, const IVarList& args)
{

	int type = args.IntVal(1);
	if (type == COMMAND_TEAM_CHECK_REQ)
	{
		int ReqType = args.IntVal(2);
		int index = args.IntVal(3);
		const wchar_t*resultInformName = args.WideStrVal(4);
		OnCommandCheckConditionReq(pKernel, self, ReqType, index, resultInformName);
	}
	else if (type == COMMAND_TEAM_CHECK_RSP)
	{
		OnCommandCheckConditionRsp(pKernel, self, args);
	}

	return 0;

}

void TeamModule::SetJoinTeamState(IKernel* pKernel, const PERSISTID& self, int state)
{
	if (!pKernel->Exists(self))
	{
		return ;
	}

	IGameObj *pSelf = pKernel->GetGameObj(self);
	if (NULL == pSelf)
	{
		return ;
	}

	const wchar_t* self_name = pSelf->QueryWideStr("Name");

	// 队伍ID
	int team_id = pSelf->QueryInt("TeamID");
	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
		<< team_id
		<< self_name
		<< TEAM_MAIN_REC_COL_TEAM_OFF_JOIN_TEAM
		<< state;
	pKernel->SendPublicMessage(pub_msg);

}

// 更新准备状态
void TeamModule::UpdateReadyState(IKernel* pKernel, const PERSISTID& self, int ready_state)
{
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (NULL == pSelfObj)
	{
		return;
	}

	int team_id = pSelfObj->QueryInt("TeamID");
	const wchar_t* self_name = pSelfObj->QueryWideStr("Name");
	CVarList msg;
	msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_UPDATE_MEMBER
		<< team_id
		<< self_name
		<< TEAM_REC_COL_READY
		<< ready_state;
	pKernel->SendPublicMessage(msg);
}

void TeamModule::CustomMessageToTeamMember(IKernel*pKernel, const PERSISTID& self, const IVarList&msg, bool customSelf)
{
	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (pSelfObj == NULL){
		return;
	}
	IRecord *pTeamRec = pSelfObj->GetRecord(TEAM_REC_NAME);
	if (NULL == pTeamRec)
	{
		return;
	}
	// 自己名称
	const wchar_t* selfName = pSelfObj->QueryWideStr("Name");
	int rowMax = pTeamRec->GetRows();
	LoopBeginCheck(c);
	for (int row = 0; row < rowMax; row++)
	{
		LoopDoCheck(c);
		const  wchar_t* memberName = pTeamRec->QueryWideStr(row, TEAM_REC_COL_NAME);
		if (!customSelf && wcscmp(memberName, selfName) == 0){
			continue;
		}

		pKernel->CustomByName(memberName, msg);
	}

}

bool TeamModule::IsActivityScene(IKernel*pKernel)
{
	int sceneID = pKernel->GetSceneId();

	// 克隆场景
	if (pKernel->GetSceneClass() == 2)
	{
		sceneID = pKernel->GetPrototypeSceneId(sceneID);
	}

	auto it = m_activityScene.find(sceneID);
	if (it != m_activityScene.end()){
		return true;
	}

	return false;
}

bool TeamModule::CanJoinTeam(IKernel*pKernel, int teamID)
{
												   
	IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
	if (pDomainSpace == NULL)
	{
		return false;
	}

	IPubData* pTeamData = pDomainSpace->GetPubData(TeamModule::GetDomainName(pKernel).c_str());
	if (pTeamData == NULL)
	{
		return false;
	}

	IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
	if (NULL == pTeamManiRec)
	{
		return false;
	}

	int row = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
	if (row < 0)
	{
		return false;
	}
	return pTeamManiRec->QueryInt(row, TEAM_MAIN_REC_COL_TEAM_OFF_JOIN_TEAM) == TEAM_JOIN_TEAM_ON;
}
void TeamModule::AutoMatch(IKernel*pKernel, const PERSISTID &self, int autoMatch, int objIndex)
{

	IGameObj *pSelfObj = pKernel->GetGameObj(self);
	if (pSelfObj == NULL)
	{
		return;
	}

	const wchar_t* self_name = pKernel->QueryWideStr(self, FIELD_PROP_NAME);
	int teamID = pSelfObj->QueryInt(FIELD_PROP_TEAM_ID);
	if (teamID > 0)
	{//有队伍

		if (!m_pTeamModule->IsTeamCaptain(pKernel, self))
		{
			CustomSysInfo(pKernel, self, SYSTEM_INFO_ID_17420, CVarList());
			// 自已不是队长，不能设置
			return;
		}

		IPubSpace* pDomainSpace = pKernel->GetPubSpace(PUBSPACE_DOMAIN);
		if (pDomainSpace == NULL)
		{
			return;
		}
		IPubData* pTeamData = pDomainSpace->GetPubData(TeamModule::GetDomainName(pKernel).c_str());
		if (pTeamData == NULL)
		{
			return;
		}
		IRecord *pTeamManiRec = pTeamData->GetRecord(TEAM_MAIN_REC_NAME);
		if (NULL == pTeamManiRec){
			return;
		}
		int teamRow = pTeamManiRec->FindInt(TEAM_MAIN_REC_COL_TEAM_ID, teamID);
		if (teamRow < 0)
		{
			return;
		}
		int index = pTeamManiRec->QueryInt(teamRow, TEAM_MAIN_REC_COL_TEAM_OBJECT_INDEX);


		if (!TeamModule::IsTeamValid(pKernel, self, index))
		{
			return;
		}

	
		if (autoMatch == TEAM_AUTO_MATCH_ON)
		{
			std::string team_member_rec_name = pTeamManiRec->QueryString(teamRow, TEAM_MAIN_REC_COL_TEAM_MEMBER_REC_NAME);
			IRecord *pTeamMemRec = pTeamData->GetRecord(team_member_rec_name.c_str());
			if (pTeamManiRec == NULL){
				return;
			}
			int memNumNow = pTeamMemRec->GetRows();
			if (memNumNow >= pTeamMemRec->GetRowMax()){
				return;
			}
		}

		CVarList pub_msg;
		pub_msg << PUBSPACE_DOMAIN
			<< TeamModule::GetDomainName(pKernel).c_str()
			<< SP_DOMAIN_MSG_TEAM_UPDATE_INFO
			<< teamID
			<< self_name
			<< TEAM_MAIN_REC_COL_TEAM_AUTO_MATCH
			<< autoMatch;
		pKernel->SendPublicMessage(pub_msg);
	}
	else
	{
		int index = 0;
		if (autoMatch == TEAM_AUTO_MATCH_ON)
		{
			if (!m_pTeamModule->DealCheckTeamCondition(pKernel, self, objIndex, self_name))
			{
				return;
			}

			if (!TeamModule::IsTeamValid(pKernel, self, objIndex))
			{
				return;
			}
		}

		CVarList pub_msg;
		pub_msg << PUBSPACE_DOMAIN
			<< TeamModule::GetDomainName(pKernel).c_str()
			<< SP_DOMAIN_MSG_TEAM_ADD_AUTO_MATCH_LIST
			<< autoMatch
			<< self_name
			<< pSelfObj->QueryInt(FIELD_PROP_LEVEL)
			<< objIndex;
			//<< pSelfObj->QueryInt(FIELD_PROP_NATION);

		pKernel->SendPublicMessage(pub_msg);
		pSelfObj->SetInt("AutoMatch", autoMatch);
	}

}


void TeamModule::LeaveTeam(IKernel* pKernel, const PERSISTID& player)
{
	// 保护
	if (!pKernel->Exists(player))
	{
		return;
	}

	IGameObj *pSelfObj = pKernel->GetGameObj(player);
	if (NULL == pSelfObj)
	{
		return;
	}

	// 玩家是否在队伍中
	if (!m_pTeamModule->IsInTeam(pKernel, player))
	{
		return;
	}
	const wchar_t* self_name = pSelfObj->QueryWideStr("Name");

	const int team_id = pSelfObj->QueryInt("TeamID");

	// 离开队伍，队长离开，则移交队长给下个队员
	// 发送消息
	CVarList pub_msg;
	pub_msg << PUBSPACE_DOMAIN
		<< GetDomainName(pKernel).c_str()
		<< SP_DOMAIN_MSG_TEAM_LEAVE
		<< team_id
		<< self_name
		<< LEAVE_TYPE_EXIT;
	pKernel->SendPublicMessage(pub_msg);
}
