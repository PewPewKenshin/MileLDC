#include "MileLDC.h"

#include <functional>
#include <string>
#include <array>
#include <sstream>
#include <map>
#include <mutex>

#include <GWCA/GWCA.h>
#include <GWCA/Utilities/Hooker.h>
#include <GWCA/Managers/RenderMgr.h>
#include <GWCA/Managers/GameThreadMgr.h>
#include <GWCA/Managers/MemoryMgr.h>
#include <GWCA/Context/PreGameContext.h>
#include <GWCA/Managers/UIMgr.h>
#include <GWCA/Managers/ChatMgr.h>
#include <GWCA/Managers/MapMgr.h>
#include <GWCA/Managers/StoCMgr.h>
#include <GWCA/Managers/SkillbarMgr.h>
#include <GWCA/Managers/AgentMgr.h>
#include <GWCA/GameEntities/Agent.h>
#include <GWCA/GameEntities/Skill.h>

#include <GWCA/Packets/StoC.h>

#include <GWCA/Constants/Constants.h>

#include "_ImGui\\imgui_impl_dx9.h"
#include "_ImGui\\imgui_impl_win32.h"

namespace {
	const char name[] = "MileLDC";
	long OldWndProc = 0;

	bool initialized = false;
	bool destroyed = false;
	bool shutDown = false;
    bool deferedClose = false;
    bool imguiInitialized = false;

	static HWND gw_window_handle = 0;
	bool damageMeterWindowVisible = false;

	GW::HookEntry on_generic_modifier_entry;
	GW::HookEntry on_generic_value_target_entry;
	GW::HookEntry on_opposing_party_guild_entry;
	std::map<int, std::pair<std::wstring, long>> teamLordDamageMap = { {0, {L"", 0L}}, {1, {L"", 0L}}, {2, {L"", 0L}} };
	std::mutex teamLordDamageMutex;
}

namespace GW {
	namespace Packet {
		namespace StoC {
			struct OpposingPartyGuild : Packet<OpposingPartyGuild>
			{
				uint32_t team_id;
				uint32_t unk2;
				uint32_t unk3;
				uint32_t unk4;
				uint32_t unk5;
				uint32_t unk6;
				uint32_t unk7;
				uint32_t unk8;
				uint32_t rank;
				uint32_t rating;
				wchar_t guild_name[32];
				wchar_t guild_tag[6];
			};
			template<> constexpr uint32_t Packet<OpposingPartyGuild>::STATIC_HEADER = 429;
		}
	}
}

void InitializeModule();
void Draw(IDirect3DDevice9* device);
void Terminate();
void DamagePacketCallback(GW::Packet::StoC::GenericModifier* packet);
void ValueTargetPacketCallback(GW::Packet::StoC::GenericValueTarget* packet);
void DrawCounterWindow();
void addTeamDamage(int teamIndex, long damage);
long getTeamDamage(int teamIndex);
std::wstring getTeamName(int teamIndex);
void clearTeamIdDamageAndSetName(GW::Packet::StoC::OpposingPartyGuild* packet);

DWORD __stdcall Entry(LPVOID module)
{
	GW::HookBase::Initialize();
	if (!GW::Initialize())
	{
		goto leave;
	}

	// ImGui render hooks
	GW::Render::SetRenderCallback([](IDirect3DDevice9* device) {
		Draw(device);
	});
	GW::Render::SetResetCallback([](IDirect3DDevice9* device) {
		UNREFERENCED_PARAMETER(device);
		ImGui_ImplDX9_InvalidateDeviceObjects();
	});

	GW::HookBase::EnableHooks();

	GW::GameThread::Enqueue([]() {
		InitializeModule();
	});

	while (!destroyed)
	{
		Sleep(100);

		// Exit via key
		if (GetAsyncKeyState(VK_END) & 1)
		{
			shutDown = true;
		}
	}

	// Wait until all hooks are disabled
	while (GW::HookBase::GetInHookCount())
	{
		Sleep(50);
	}

leave:
	GW::Terminate();

	FreeLibraryAndExitThread((HMODULE)module, EXIT_SUCCESS);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK WndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam) {
    if (Message == WM_CLOSE) {
        deferedClose = true;
        return 0;
    }

    if (!(!GW::GetPreGameContext() && imguiInitialized && initialized && !destroyed)) {
        return CallWindowProc((WNDPROC)OldWndProc, hWnd, Message, wParam, lParam);
    }
    ImGuiIO& io = ImGui::GetIO();

    static bool right_mouse_down = false;
    if (Message == WM_RBUTTONUP) right_mouse_down = false;
    if (Message == WM_RBUTTONDOWN) right_mouse_down = true;
    if (Message == WM_RBUTTONDBLCLK) right_mouse_down = true;
    bool skip_mouse_capture = right_mouse_down || GW::UI::GetIsWorldMapShowing();

    if (!io.WantCaptureMouse) {
        if (Message == WM_LBUTTONDOWN)
            skip_mouse_capture |= true;
    }

    if (!skip_mouse_capture) {
        bool ret = ImGui_ImplWin32_WndProcHandler(hWnd, Message, wParam, lParam);
		if (ret)
		{
			return true;
		}
    }

    if (io.WantCaptureMouse) {
        switch (Message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEMOVE:
        case WM_XBUTTONDOWN:
            if (!skip_mouse_capture)
                return true;
            break;
        }
    }

    if (io.WantTextInput) {
        if ((Message == WM_LBUTTONDOWN || Message == WM_RBUTTONDOWN) && !io.WantCaptureMouse)
            ImGui::SetWindowFocus(nullptr);
        else
            return true;
    }

    return CallWindowProc((WNDPROC)OldWndProc, hWnd, Message, wParam, lParam);
}

LRESULT CALLBACK SafeWndProc(HWND hWnd, UINT Message, WPARAM wParam, LPARAM lParam) noexcept {
	__try {
		return WndProc(hWnd, Message, wParam, lParam);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		return CallWindowProc((WNDPROC)OldWndProc, hWnd, Message, wParam, lParam);
	}
}

void InitializeModule() 
{
	if (initialized || shutDown)
	{
		// just as a safety
		return;
	}

	gw_window_handle = GW::MemoryMgr::GetGWWindowHandle();
	OldWndProc = SetWindowLongPtrW(gw_window_handle, GWL_WNDPROC, (long)SafeWndProc);

	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericModifier>(&on_generic_modifier_entry, [](GW::HookStatus* status, GW::Packet::StoC::GenericModifier* packet) {
		DamagePacketCallback(packet);
	});
	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::GenericValueTarget>(&on_generic_value_target_entry, [](GW::HookStatus* status, GW::Packet::StoC::GenericValueTarget* packet) {
		ValueTargetPacketCallback(packet);
	});
	GW::StoC::RegisterPacketCallback<GW::Packet::StoC::OpposingPartyGuild>(&on_opposing_party_guild_entry, [](GW::HookStatus* status, GW::Packet::StoC::OpposingPartyGuild* packet) {
		clearTeamIdDamageAndSetName(packet);
	});

	initialized = true;
	GW::Chat::WriteChat(GW::Chat::CHANNEL_MODERATOR, L"MileLDC: Module initialized");
}

void Draw(IDirect3DDevice9* device)
{
	// Need to shut down
	if (initialized && shutDown)
	{
		if (imguiInitialized)
		{
			GW::Render::SetResetCallback(nullptr);
			ImGui_ImplDX9_Shutdown();
			ImGui_ImplWin32_Shutdown();
			ImGui::DestroyContext();
			imguiInitialized = false;
		}
		Terminate();
	}

	// runtime
	if (initialized 
		&& !shutDown
		&& GW::Render::GetViewportWidth() > 0
		&& GW::Render::GetViewportHeight() > 0)
	{
		if (!imguiInitialized)
		{
			// Initialize ImGui
			ImGui::CreateContext();
			ImGui::StyleColorsClassic();

			ImGuiIO& io = ImGui::GetIO();
			io.MouseDrawCursor = false;
			ImGui_ImplDX9_Init(device);
			ImGui_ImplWin32_Init(gw_window_handle);

			imguiInitialized = true;
			GW::Chat::WriteChat(GW::Chat::CHANNEL_MODERATOR, L"MileLDC: ImGui initialized");
		}

		if (!GW::UI::GetIsUIDrawn()
			|| GW::GetPreGameContext()
			|| GW::Map::GetIsInCinematic()
			|| GW::Map::GetInstanceType() == GW::Constants::InstanceType::Loading)
		{
			return;
		}

		ImGui_ImplDX9_NewFrame();
		ImGui_ImplWin32_NewFrame();
		ImGui::NewFrame();

		ImGui::GetIO().KeysDown[VK_CONTROL] = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
		ImGui::GetIO().KeysDown[VK_SHIFT] = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
		ImGui::GetIO().KeysDown[VK_MENU] = (GetKeyState(VK_MENU) & 0x8000) != 0;

		int pushCount = 4;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(6.0f, 6.0f));

		ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
		ImGui::SetNextWindowSize(ImVec2(70.f, 0.f));

		ImGui::Begin("Menu parent", nullptr,
			ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_MenuBar);

		ImGui::BeginMenuBar();
		ImGui::PopStyleVar(pushCount);

		if (ImGui::BeginMenu(name))
		{
			if (ImGui::MenuItem("Counter"))
			{
				damageMeterWindowVisible = true;
			}
			if (ImGui::MenuItem("Exit"))
			{
				shutDown = true;
			}
			ImGui::EndMenu();
		}
		ImGui::EndMainMenuBar();
		ImGui::End();

		DrawCounterWindow();

		ImGuiIO& io = ImGui::GetIO();
		io.DisplaySize = ImVec2((float)GW::Render::GetViewportWidth(), (float)GW::Render::GetViewportHeight());

		ImGui::Render();
		ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
	}
	
	// Defered closing
	if (destroyed && deferedClose)
	{
		SendMessageW(gw_window_handle, WM_CLOSE, NULL, NULL);
	}
}

void Terminate()
{
	GW::Chat::WriteChat(GW::Chat::CHANNEL_MODERATOR, L"MileLDC: Terminating");
	GW::StoC::RemoveCallback<GW::Packet::StoC::GenericModifier>(&on_generic_modifier_entry);
	GW::StoC::RemoveCallback<GW::Packet::StoC::GenericValueTarget>(&on_generic_value_target_entry);
	GW::StoC::RemoveCallback<GW::Packet::StoC::OpposingPartyGuild>(&on_opposing_party_guild_entry);

	SetWindowLongPtr(gw_window_handle, GWL_WNDPROC, (long)OldWndProc);
	GW::DisableHooks();
	initialized = false;
	destroyed = true;
}

void DamagePacketCallback(GW::Packet::StoC::GenericModifier* packet)
{
	if (!GW::Map::GetIsObserving())
	{
		return;
	}
	switch (packet->type) 
	{
		case GW::Packet::StoC::P156_Type::damage:
		case GW::Packet::StoC::P156_Type::critical:
		case GW::Packet::StoC::P156_Type::armorignoring:
			break;
		default:
			return;
	}

	GW::Agent* target = GW::Agents::GetAgentByID(packet->target_id);
	GW::Agent* cause = GW::Agents::GetAgentByID(packet->cause_id);
	if (target && cause)
	{
		GW::AgentLiving* targetLiv = target->GetAsAgentLiving();
		GW::AgentLiving* causeLiv = cause->GetAsAgentLiving();
		if (targetLiv && causeLiv && packet->value < 0) {
			if (!(targetLiv->player_number == 170 || targetLiv->player_number == 170) // Need to change 2nd check to the guild lord model once the pumpkin event ends
				|| !(targetLiv->team_id == 1 || targetLiv->team_id == 2)) return;

			int teamIndex = targetLiv->team_id == 1 ? 2 : 1;
			long dmg;
			dmg = (long)std::round(-packet->value * (targetLiv->max_hp > 0 ? targetLiv->max_hp : 1680));
			addTeamDamage(teamIndex, dmg);
		}
	}
}

void ValueTargetPacketCallback(GW::Packet::StoC::GenericValueTarget* packet)
{
	if (!GW::Map::GetIsObserving())
	{
		return;
	}
	GW::Agent* target = GW::Agents::GetAgentByID(packet->caster);
	if (target)
	{
		GW::AgentLiving* targetLiv = target->GetAsAgentLiving();
		if (targetLiv
			&& (targetLiv->player_number == 170 || targetLiv->player_number == 170) // Need to change 2nd check to the guild lord model once the pumpkin event ends
			&& (targetLiv->team_id == 1 || targetLiv->team_id == 2))
		{
			GW::Constants::SkillID skill = static_cast<GW::Constants::SkillID>(packet->value);
			GW::Skill* skillData = GW::SkillbarMgr::GetSkillConstantData(skill);
			if (skillData && (skillData->type == GW::Constants::SkillType::Enchantment || skillData->type == GW::Constants::SkillType::WeaponSpell))
			{
				int teamIndexx = targetLiv->team_id;
				addTeamDamage(teamIndexx, -50L);
			}
		}
	}
}

template<typename ... Args>
static std::string string_format(const std::string& format, Args... args)
{
	int size_s = std::snprintf(nullptr, 0, format.c_str(), args...) + 1;
	if (size_s <= 0)
	{
		return "";
	}
	auto size = static_cast<size_t>(size_s);
	auto buf = std::make_unique<char[]>(size);
	std::snprintf(buf.get(), size, format.c_str(), args...);
	return std::string(buf.get(), buf.get() + size - 1);
}

static std::string WStringToString(const std::wstring& s)
{
	// @Cleanup: ASSERT used incorrectly here; value passed could be from anywhere!
	if (s.empty()) return "";
	// NB: GW uses code page 0 (CP_ACP)
	int size_needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, &s[0], (int)s.size(), NULL, 0, NULL, NULL);
	if (size_needed == 0) return "";
	std::string strTo(size_needed, 0);
	size_needed = WideCharToMultiByte(CP_UTF8, 0, &s[0], (int)s.size(), &strTo[0], size_needed, NULL, NULL);
	if (size_needed == 0)
		return "";
	return strTo;
}

void DrawCounterWindow()
{
	if (!damageMeterWindowVisible)
	{
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(512.f, 248.f), ImGuiCond_FirstUseEver);
	if (ImGui::Begin("Lord damage counter", &damageMeterWindowVisible))
	{
		const char* teams[] = {"none", "blue", "red" };
		const ImColor colors[] = { ImColor::HSV(0.67f, 0.33f, 0.9f), ImColor::HSV(0.67f, 0.33f, 0.9f), ImColor::HSV(0.0f, 0.33f, 0.9f) };
		for (int t = 1; t <= 2; ++t)
		{
			std::stringstream imguiLabel;
			imguiLabel << "##lordDamage_" << t;
			ImGui::PushStyleColor(ImGuiCol_Text, (ImVec4)colors[t]);
			ImGui::BulletText(WStringToString(getTeamName(t)).c_str());
			ImGui::SameLine(300);
			auto buf = string_format("%d", getTeamDamage(t));
			ImGui::InputTextEx(imguiLabel.str().c_str(), NULL, const_cast<char*>(buf.c_str()), buf.length(), ImVec2(100.0f, 0), ImGuiInputTextFlags_ReadOnly);
			ImGui::PopStyleColor(1);
		}
		if (ImGui::TreeNode("Info"))
		{
			ImGui::Text("It will only count damage in observe mode.");
			ImGui::Text("");
			ImGui::Text("Every point of damage the guild lord receives");
			ImGui::Text("is counted as damage done for the enemy team.");
			ImGui::Text("");
			ImGui::Text("Every enchantment or weapon spell being cast");
			ImGui::Text("on the guild lord will decrease the damage");
			ImGui::Text("done for the team by 50 points.");
			ImGui::Text("");
			ImGui::Text("Life steal is counted as lord damage - no way");
			ImGui::Text("of knowing the type of the damage done.");
			ImGui::TreePop();
		}
	}
	ImGui::End();
}

void addTeamDamage(int teamIndex, long damage)
{
	std::lock_guard<std::mutex> guard(teamLordDamageMutex);
	teamLordDamageMap[teamIndex].second += damage;
}

void clearTeamIdDamageAndSetName(GW::Packet::StoC::OpposingPartyGuild* packet)
{
	std::lock_guard<std::mutex> guard(teamLordDamageMutex);
	std::wstring guildName(packet->guild_name);
	teamLordDamageMap[packet->team_id] = { guildName , 0L };
}

std::wstring getTeamName(int teamIndex)
{
	std::lock_guard<std::mutex> guard(teamLordDamageMutex);
	std::wstring name = teamLordDamageMap[teamIndex].first;
	return name;
}

long getTeamDamage(int teamIndex)
{
	std::lock_guard<std::mutex> guard(teamLordDamageMutex);
	long damage = teamLordDamageMap[teamIndex].second;
	return damage;
}