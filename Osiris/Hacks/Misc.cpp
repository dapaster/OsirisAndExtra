#include <mutex>
#include <numeric>
#include <sstream>
#include <string>

#include "../imgui/imgui.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "../imgui/imgui_internal.h"

#include "../Config.h"
#include "../Interfaces.h"
#include "../Memory.h"
#include "../Netvars.h"
#include "../GUI.h"
#include "../Helpers.h"
#include "../GameData.h"

#include "EnginePrediction.h"
#include "Misc.h"

#include "../SDK/Client.h"
#include "../SDK/ClientMode.h"
#include "../SDK/ConVar.h"
#include "../SDK/Entity.h"
#include "../SDK/FrameStage.h"
#include "../SDK/GameEvent.h"
#include "../SDK/GlobalVars.h"
#include "../SDK/Input.h"
#include "../SDK/ItemSchema.h"
#include "../SDK/Localize.h"
#include "../SDK/LocalPlayer.h"
#include "../SDK/NetworkChannel.h"
#include "../SDK/Panorama.h"
#include "../SDK/Prediction.h"
#include "../SDK/Surface.h"
#include "../SDK/UserCmd.h"
#include "../SDK/ViewSetup.h"
#include "../SDK/WeaponData.h"
#include "../SDK/WeaponSystem.h"

#include "../imguiCustom.h"

bool Misc::isInChat() noexcept
{
    if (!localPlayer)
        return false;

    const auto hudChat = memory->findHudElement(memory->hud, "CCSGO_HudChat");
    if (!hudChat)
        return false;

    const bool isInChat = *(bool*)((uintptr_t)hudChat + 0xD);

    return isInChat;
}

bool shouldEdgebug = false;
float zVelBackup = 0.0f;
float bugSpeed = 0.0f;
int edgebugButtons = 0;

void Misc::edgeBug(UserCmd* cmd, Vector& angView) noexcept
{
    if (!config->misc.edgebug || !config->misc.edgebugkey.isActive())
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (localPlayer->flags() & 1)
        return;

    shouldEdgebug = zVelBackup < -bugSpeed && round(localPlayer->velocity().z) == -round(bugSpeed) && localPlayer->moveType() != MoveType::LADDER;
    if (shouldEdgebug)
        return;

    const int commandsPredicted = interfaces->prediction->split->commandsPredicted;

    const Vector angViewOriginal = angView;
    const Vector angCmdViewOriginal = cmd->viewangles;
    const int buttonsOriginal = cmd->buttons;
    Vector vecMoveOriginal;
    vecMoveOriginal.x = cmd->sidemove;
    vecMoveOriginal.y = cmd->forwardmove;

    static Vector vecMoveLastStrafe;
    static Vector angViewLastStrafe;
    static Vector angViewOld = angView;
    static Vector angViewDeltaStrafe;
    static bool appliedStrafeLast = false;
    if (!appliedStrafeLast)
    {
        angViewLastStrafe = angView;
        vecMoveLastStrafe = vecMoveOriginal;
        angViewDeltaStrafe = (angView - angViewOld);
        angViewDeltaStrafe;
    }
    appliedStrafeLast = false;
    angViewOld = angView;

    for (int t = 0; t < 4; t++)
    {
        static int lastType = 0;
        if (lastType)
        {
            t = lastType;
            lastType = 0;
        }
        memory->restoreEntityToPredictedFrame(0, commandsPredicted - 1);
        if (buttonsOriginal& UserCmd::IN_DUCK&& t < 2)
            t = 2;
        bool applyStrafe = !(t % 2);
        bool applyDuck = t > 1;

        cmd->viewangles = angViewLastStrafe;
        cmd->buttons = buttonsOriginal;
        cmd->sidemove = vecMoveLastStrafe.x;
        cmd->forwardmove = vecMoveLastStrafe.y;

        for (int i = 0; i < config->misc.edgebugPredAmnt; i++)
        {
            if (applyDuck)
                cmd->buttons |= UserCmd::IN_DUCK;
            else
                cmd->buttons &= ~UserCmd::IN_DUCK;
            if (applyStrafe)
            {
                cmd->viewangles += angViewDeltaStrafe;
                cmd->viewangles.normalize();
                cmd->viewangles.clamp();
            }
            else
            {
                cmd->sidemove = 0.f;
                cmd->forwardmove = 0.f;
            }
            EnginePrediction::run(cmd);
            shouldEdgebug = zVelBackup < -bugSpeed && round(localPlayer->velocity().z) == -round(bugSpeed) && localPlayer->moveType() != MoveType::LADDER;
            zVelBackup = localPlayer->velocity().z;
            if (shouldEdgebug)
            {
                edgebugButtons = cmd->buttons;
                if (applyStrafe)
                {
                    appliedStrafeLast = true;
                    angView = (angViewLastStrafe + angViewDeltaStrafe);
                    angView.normalize();
                    angView.clamp();
                    angViewLastStrafe = angView;
                    cmd->sidemove = vecMoveLastStrafe.x;
                    cmd->forwardmove = vecMoveLastStrafe.y;
                }
                cmd->viewangles = angCmdViewOriginal;
                lastType = t;
                return;
            }

            if (localPlayer->flags() & 1 || localPlayer->moveType() == MoveType::LADDER)
                break;
        }
    }
    cmd->viewangles = angCmdViewOriginal;
    angView = angViewOriginal;
    cmd->buttons = buttonsOriginal;
    cmd->sidemove = vecMoveOriginal.x;
    cmd->forwardmove = vecMoveOriginal.y;
}

void Misc::prePrediction(UserCmd* cmd) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    zVelBackup = localPlayer->velocity().z;

    if (shouldEdgebug)
        cmd->buttons = edgebugButtons;

    static auto gravity = interfaces->cvar->findVar("sv_gravity");
    bugSpeed = (gravity->getFloat() * 0.5f * memory->globalVars->intervalPerTick);

    shouldEdgebug = zVelBackup < -bugSpeed && round(localPlayer->velocity ().z) == -round(bugSpeed) && localPlayer->moveType() != MoveType::LADDER;
}

void Misc::drawPlayerList() noexcept
{
    if (!config->misc.playerList.enabled)
        return;

    if (config->misc.playerList.pos != ImVec2{}) {
        ImGui::SetNextWindowPos(config->misc.playerList.pos);
        config->misc.playerList.pos = {};
    }

    static bool changedName = true;
    static std::string nameToChange = "";

    if (!changedName && nameToChange != "")
        changedName = changeName(false, (nameToChange + '\x1').c_str(), 1.0f);

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!gui->isOpen())
    {
        windowFlags |= ImGuiWindowFlags_NoInputs;
        return;
    }

    GameData::Lock lock;
    if ((GameData::players().empty()) && !gui->isOpen())
        return;

    ImGui::SetNextWindowSize(ImVec2(300.0f, 300.0f), ImGuiCond_Once);

    if (ImGui::Begin("Player List", nullptr, windowFlags)) {
        if (ImGui::beginTable("", 9, ImGuiTableFlags_Borders | ImGuiTableFlags_Hideable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide);
            ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoHide, 120.0f);
            ImGui::TableSetupColumn("Steam ID", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupColumn("Rank", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupColumn("Wins", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupColumn("Health", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupColumn("Armor", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupColumn("Money", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoResize);
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetColumnEnabled(2, config->misc.playerList.steamID);
            ImGui::TableSetColumnEnabled(3, config->misc.playerList.rank);
            ImGui::TableSetColumnEnabled(4, config->misc.playerList.wins);
            ImGui::TableSetColumnEnabled(5, config->misc.playerList.health);
            ImGui::TableSetColumnEnabled(6, config->misc.playerList.armor);
            ImGui::TableSetColumnEnabled(7, config->misc.playerList.money);

            ImGui::TableHeadersRow();

            std::vector<std::reference_wrapper<const PlayerData>> playersOrdered{ GameData::players().begin(), GameData::players().end() };
            std::ranges::sort(playersOrdered, [](const PlayerData& a, const PlayerData& b) {
                // enemies first
                if (a.enemy != b.enemy)
                    return a.enemy && !b.enemy;

                return a.handle < b.handle;
                });

            ImGui::PushFont(gui->getUnicodeFont());

            for (const PlayerData& player : playersOrdered) {
                ImGui::TableNextRow();
                ImGui::PushID(ImGui::TableGetRowIndex());

                if (ImGui::TableNextColumn())
                    ImGui::Text("%d", player.userId);

                if (ImGui::TableNextColumn())
                {
                    ImGui::Image(player.getAvatarTexture(), { ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight() });
                    ImGui::SameLine();
                    ImGui::textEllipsisInTableCell(player.name.c_str());
                }

                if (ImGui::TableNextColumn() && ImGui::smallButtonFullWidth("Copy", player.steamID == 0))
                    ImGui::SetClipboardText(std::to_string(player.steamID).c_str());

                if (ImGui::TableNextColumn()) {
                    ImGui::Image(player.getRankTexture(), { 2.45f /* -> proportion 49x20px */ * ImGui::GetTextLineHeight(), ImGui::GetTextLineHeight() });
                    if (ImGui::IsItemHovered()) {
                        ImGui::BeginTooltip();
                        ImGui::PushFont(nullptr);
                        ImGui::TextUnformatted(player.getRankName().data());
                        ImGui::PopFont();
                        ImGui::EndTooltip();
                    }
                    
                }

                if (ImGui::TableNextColumn())
                    ImGui::Text("%d", player.competitiveWins);

                if (ImGui::TableNextColumn()) {
                    if (!player.alive)
                        ImGui::TextColored({ 1.0f, 0.0f, 0.0f, 1.0f }, "%s", "Dead");
                    else
                        ImGui::Text("%d HP", player.health);
                }

                if (ImGui::TableNextColumn())
                    ImGui::Text("%d", player.armor);

                if (ImGui::TableNextColumn())
                    ImGui::TextColored({ 0.0f, 1.0f, 0.0f, 1.0f }, "$%d", player.money);

                if (ImGui::TableNextColumn()){
                    if (ImGui::smallButtonFullWidth("...", false))
                        ImGui::OpenPopup("");

                    if (ImGui::BeginPopup("")) {
                        if (ImGui::Button("Steal name"))
                        {
                            changedName = changeName(false, (std::string{ player.name } + '\x1').c_str(), 1.0f);
                            nameToChange = player.name;

                            if (PlayerInfo playerInfo; interfaces->engine->isInGame() && localPlayer
                                && interfaces->engine->getPlayerInfo(localPlayer->index(), playerInfo) && (playerInfo.name == std::string{ "?empty" } || playerInfo.name == std::string{ "\n\xAD\xAD\xAD" }))
                                changedName = false;
                        }

                        if (ImGui::Button("Steal clantag"))
                            memory->setClanTag(player.clanTag.c_str(), player.clanTag.c_str());

                        if (GameData::local().exists && player.team == GameData::local().team && player.steamID != 0)
                        {
                            if (ImGui::Button("Kick"))
                            {
                                const std::string cmd = "callvote kick " + std::to_string(player.userId);
                                interfaces->engine->clientCmdUnrestricted(cmd.c_str());
                            }
                        }

                        ImGui::EndPopup();
                    }
                }

                ImGui::PopID();
            }

            ImGui::PopFont();
            ImGui::EndTable();
        }
    }
    ImGui::End();
}

void Misc::blockBot(UserCmd* cmd) noexcept
{
    if (!localPlayer || !localPlayer->isAlive())
        return;

    static int blockTargetHandle = 0;

    if (!config->misc.blockBot || !config->misc.blockBotKey.isActive())
    {
        blockTargetHandle = 0;
        return;
    }

    float best = 1024.0f;
    if (!blockTargetHandle)
    {
        for (int i = 1; i <= interfaces->engine->getMaxClients(); i++)
        {
            Entity* entity = interfaces->entityList->getEntity(i);

            if (!entity || !entity->isPlayer() || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive())
                continue;

            const auto distance = entity->getAbsOrigin().distTo(localPlayer->getAbsOrigin());
            if (distance < best)
            {
                best = distance;
                blockTargetHandle = entity->handle();
            }
        }
    }

    const auto target = interfaces->entityList->getEntityFromHandle(blockTargetHandle);
    if (target && target->isPlayer() && target != localPlayer.get() && !target->isDormant() && target->isAlive())
    {
        const auto targetVec = (target->getAbsOrigin() + target->velocity() * memory->globalVars->intervalPerTick - localPlayer->getAbsOrigin());
        const auto z1 = target->getAbsOrigin().z - localPlayer->getEyePosition().z;
        const auto z2 = target->getEyePosition().z - localPlayer->getAbsOrigin().z;
        if (z1 >= 0.0f || z2 <= 0.0f)
        {
            Vector fwd = Vector::fromAngle2D(cmd->viewangles.y);
            Vector side = fwd.crossProduct(Vector::up());
            Vector move = Vector{ fwd.dotProduct2D(targetVec), side.dotProduct2D(targetVec), 0.0f };
            move *= 45.0f;

            const float l = move.length2D();
            if (l > 450.0f)
                move *= 450.0f / l;

            cmd->forwardmove = move.x;
            cmd->sidemove = move.y;
        }
        else
        {
            Vector fwd = Vector::fromAngle2D(cmd->viewangles.y);
            Vector side = fwd.crossProduct(Vector::up());
            Vector tar = (targetVec / targetVec.length2D()).crossProduct(Vector::up());
            tar = tar.snapTo4();
            tar *= tar.dotProduct2D(targetVec);
            Vector move = Vector{ fwd.dotProduct2D(tar), side.dotProduct2D(tar), 0.0f };
            move *= 45.0f;

            const float l = move.length2D();
            if (l > 450.0f)
                move *= 450.0f / l;

            cmd->forwardmove = move.x;
            cmd->sidemove = move.y;
        }
    }
}

static int buttons = 0;

void Misc::runFreeCam(UserCmd* cmd, Vector viewAngles) noexcept
{
    static Vector currentViewAngles = Vector{ };
    static Vector realViewAngles = Vector{ };
    static bool wasCrouching = false;
    static bool hasSetAngles = false;

    buttons = cmd->buttons;
    if (!config->visuals.freeCam || !config->visuals.freeCamKey.isActive())
    {
        if (hasSetAngles)
        {
            interfaces->engine->setViewAngles(realViewAngles);
            cmd->viewangles = currentViewAngles;
            if (wasCrouching)
                cmd->buttons |= UserCmd::IN_DUCK;
            wasCrouching = false;
            hasSetAngles = false;
        }
        currentViewAngles = Vector{};
        return;
    }

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (currentViewAngles.null())
    {
        currentViewAngles = cmd->viewangles;
        realViewAngles = viewAngles;
        wasCrouching = cmd->buttons & UserCmd::IN_DUCK;
    }

    cmd->forwardmove = 0;
    cmd->sidemove = 0;
    if (wasCrouching)
        cmd->buttons = UserCmd::IN_DUCK;
    else
        cmd->buttons = 0;
    cmd->viewangles = currentViewAngles;
    hasSetAngles = true;
}

void Misc::freeCam(ViewSetup* setup) noexcept
{
    static Vector newOrigin = Vector{ };

    if (!config->visuals.freeCam || !config->visuals.freeCamKey.isActive())
    {
        newOrigin = Vector{ };
        return;
    }

    if (!localPlayer || !localPlayer->isAlive())
        return;

    float freeCamSpeed = fabsf(static_cast<float>(config->visuals.freeCamSpeed));

    if (newOrigin.null())
        newOrigin = setup->origin;

    Vector forward{ }, right{ }, up{ };

    Vector::fromAngleAll(setup->angles, &forward, &right, &up);

    const bool backBtn = buttons & UserCmd::IN_BACK;
    const bool forwardBtn = buttons & UserCmd::IN_FORWARD;
    const bool rightBtn = buttons & UserCmd::IN_MOVERIGHT;
    const bool leftBtn = buttons & UserCmd::IN_MOVELEFT;
    const bool shiftBtn = buttons & UserCmd::IN_SPEED;
    const bool duckBtn = buttons & UserCmd::IN_DUCK;
    const bool jumpBtn = buttons & UserCmd::IN_JUMP;

    if (duckBtn)
        freeCamSpeed *= 0.45f;

    if (shiftBtn)
        freeCamSpeed *= 1.65f;

    if (forwardBtn)
        newOrigin += forward * freeCamSpeed;

    if (rightBtn)
        newOrigin += right * freeCamSpeed;

    if (leftBtn)
        newOrigin -= right * freeCamSpeed;

    if (backBtn)
        newOrigin -= forward * freeCamSpeed;

    if (jumpBtn)
        newOrigin += up * freeCamSpeed;

    setup->origin = newOrigin;
}

void Misc::viewModelChanger(ViewSetup* setup) noexcept
{
    if (!localPlayer)
        return;

    constexpr auto setViewmodel = [](Entity* viewModel, const Vector& angles) constexpr noexcept
    {
        if (viewModel)
        {
            Vector forward = Vector::fromAngle(angles);
            Vector up = Vector::fromAngle(angles - Vector{ 90.0f, 0.0f, 0.0f });
            Vector side = forward.cross(up);
            Vector offset = side * config->visuals.viewModel.x + forward * config->visuals.viewModel.y + up * config->visuals.viewModel.z;
            memory->setAbsOrigin(viewModel, viewModel->getRenderOrigin() + offset);
            memory->setAbsAngle(viewModel, angles + Vector{ 0.0f, 0.0f, config->visuals.viewModel.roll });
        }
    };

    if (localPlayer->isAlive())
    {
        if (config->visuals.viewModel.enabled && !localPlayer->isScoped() && !memory->input->isCameraInThirdPerson)
            setViewmodel(interfaces->entityList->getEntityFromHandle(localPlayer->viewModel()), setup->angles);
    }
    else if (auto observed = localPlayer->getObserverTarget(); observed && localPlayer->getObserverMode() == ObsMode::InEye)
    {
        if (config->visuals.viewModel.enabled && !observed->isScoped())
            setViewmodel(interfaces->entityList->getEntityFromHandle(observed->viewModel()), setup->angles);
    }
}

static Vector peekPosition{};

void Misc::drawAutoPeek(ImDrawList* drawList) noexcept
{
    if (!config->misc.autoPeek.enabled)
        return;

    if (peekPosition.notNull())
    {
        constexpr float step = 3.141592654f * 2.0f / 20.0f;
        std::vector<ImVec2> points;
        for (float lat = 0.f; lat <= 3.141592654f * 2.0f; lat += step)
        {
            const auto& point3d = Vector{ std::sin(lat), std::cos(lat), 0.f } *15.f;
            ImVec2 point2d;
            if (Helpers::worldToScreen(peekPosition + point3d, point2d))
                points.push_back(point2d);
        }

        const ImU32 color = (Helpers::calculateColor(config->misc.autoPeek));
        auto flags_backup = drawList->Flags;
        drawList->Flags |= ImDrawListFlags_AntiAliasedFill;
        drawList->AddConvexPolyFilled(points.data(), points.size(), color);
        drawList->AddPolyline(points.data(), points.size(), color, true, 2.f);
        drawList->Flags = flags_backup;
    }
}

void Misc::autoPeek(UserCmd* cmd, Vector currentViewAngles) noexcept
{
    static bool hasShot = false;

    if (!config->misc.autoPeek.enabled)
    {
        hasShot = false;
        peekPosition = Vector{};
        return;
    }

    if (!localPlayer)
        return;

    if (!localPlayer->isAlive())
    {
        hasShot = false;
        peekPosition = Vector{};
        return;
    }

    if (const auto mt = localPlayer->moveType(); mt == MoveType::LADDER || mt == MoveType::NOCLIP || !(localPlayer->flags() & 1))
        return;

    if (config->misc.autoPeekKey.isActive())
    {
        if (peekPosition.null())
            peekPosition = localPlayer->getRenderOrigin();

        if (cmd->buttons & UserCmd::IN_ATTACK)
            hasShot = true;

        if (hasShot)
        {
            const float yaw = currentViewAngles.y;
            const auto difference = localPlayer->getRenderOrigin() - peekPosition;

            if (difference.length2D() > 5.0f)
            {
                const auto velocity = Vector{
                    difference.x * std::cos(yaw / 180.0f * 3.141592654f) + difference.y * std::sin(yaw / 180.0f * 3.141592654f),
                    difference.y * std::cos(yaw / 180.0f * 3.141592654f) - difference.x * std::sin(yaw / 180.0f * 3.141592654f),
                    difference.z };

                cmd->forwardmove = -velocity.x * 20.f;
                cmd->sidemove = velocity.y * 20.f;
            }
            else
            {
                hasShot = false;
                peekPosition = Vector{};
                if (config->misc.autoPeekKey.keyMode == KeyMode::Toggle)
                    config->misc.autoPeekKey.setToggleTo(false);
            }
        }
    }
    else
    {
        hasShot = false;
        peekPosition = Vector{};
    }
}

void Misc::forceRelayCluster() noexcept
{
    const std::string dataCentersList[] = { "", "syd", "vie", "gru", "scl", "dxb", "par", "fra", "hkg",
    "maa", "bom", "tyo", "lux", "ams", "limc", "man", "waw", "sgp", "jnb",
    "mad", "sto", "lhr", "atl", "eat", "ord", "lax", "mwh", "okc", "sea", "iad" };

    *memory->relayCluster = dataCentersList[config->misc.forceRelayCluster];
}

void Misc::jumpBug(UserCmd* cmd) noexcept
{
    if (!config->misc.jumpBug || !config->misc.jumpBugKey.isActive())
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (const auto mt = localPlayer->moveType(); mt == MoveType::LADDER || mt == MoveType::NOCLIP)
        return;

    if (!(EnginePrediction::getFlags() & 1) && (localPlayer->flags() & 1))
    {
        if(config->misc.fastDuck)
            cmd->buttons &= ~UserCmd::IN_BULLRUSH;
        cmd->buttons |= UserCmd::IN_DUCK;
    }

    if (localPlayer->flags() & 1)
        cmd->buttons &= ~UserCmd::IN_JUMP;
}

void Misc::unlockHiddenCvars() noexcept
{
    auto iterator = **reinterpret_cast<conCommandBase***>(interfaces->cvar + 0x34);
    for (auto c = iterator->next; c != nullptr; c = c->next)
    {
        conCommandBase* cmd = c;
        cmd->flags &= ~(1 << 1);
        cmd->flags &= ~(1 << 4);
    }
}

void Misc::fakeDuck(UserCmd* cmd, bool& sendPacket) noexcept
{
    if (!config->misc.fakeduck || !config->misc.fakeduckKey.isActive())
        return;

    if (!localPlayer || !localPlayer->isAlive() || !(localPlayer->flags() & 1))
        return;

    auto netChannel = interfaces->engine->getNetworkChannel();
    if (!netChannel)
        return;

    cmd->buttons |= UserCmd::IN_BULLRUSH;
    bool crouch = netChannel->chokedPackets >= (maxUserCmdProcessTicks / 2);
    if (crouch)
        cmd->buttons |= UserCmd::IN_DUCK;
    else
        cmd->buttons &= ~UserCmd::IN_DUCK;
    sendPacket = netChannel->chokedPackets >= maxUserCmdProcessTicks;
}


void Misc::edgejump(UserCmd* cmd) noexcept
{
    if (!config->misc.edgejump || !config->misc.edgejumpkey.isActive())
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (const auto mt = localPlayer->moveType(); mt == MoveType::LADDER || mt == MoveType::NOCLIP)
        return;

    if ((EnginePrediction::getFlags() & 1) && !(localPlayer->flags() & 1))
        cmd->buttons |= UserCmd::IN_JUMP;
}

void Misc::slowwalk(UserCmd* cmd) noexcept
{
    if (!config->misc.slowwalk || !config->misc.slowwalkKey.isActive())
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (!activeWeapon)
        return;

    const auto weaponData = activeWeapon->getWeaponData();
    if (!weaponData)
        return;

    const float maxSpeed = config->misc.slowwalkAmnt ? config->misc.slowwalkAmnt : (localPlayer->isScoped() ? weaponData->maxSpeedAlt : weaponData->maxSpeed) / 3;

    if (cmd->forwardmove && cmd->sidemove) {
        const float maxSpeedRoot = maxSpeed * static_cast<float>(M_SQRT1_2);
        cmd->forwardmove = cmd->forwardmove < 0.0f ? -maxSpeedRoot : maxSpeedRoot;
        cmd->sidemove = cmd->sidemove < 0.0f ? -maxSpeedRoot : maxSpeedRoot;
    } else if (cmd->forwardmove) {
        cmd->forwardmove = cmd->forwardmove < 0.0f ? -maxSpeed : maxSpeed;
    } else if (cmd->sidemove) {
        cmd->sidemove = cmd->sidemove < 0.0f ? -maxSpeed : maxSpeed;
    }
}

void Misc::inverseRagdollGravity() noexcept
{
    static auto ragdollGravity = interfaces->cvar->findVar("cl_ragdoll_gravity");
    ragdollGravity->setValue(config->visuals.inverseRagdollGravity ? -600 : 600);
}

void Misc::updateClanTag(bool tagChanged) noexcept
{
    static std::string clanTag;

    if (tagChanged) {
        clanTag = config->misc.clanTag;
        if (!clanTag.empty() && clanTag.front() != ' ' && clanTag.back() != ' ')
            clanTag.push_back(' ');
        return;
    }
    
    static auto lastTime = 0.0f;

    if (config->misc.clocktag) {
        if (memory->globalVars->realtime - lastTime < 1.0f)
            return;

        const auto time = std::time(nullptr);
        const auto localTime = std::localtime(&time);
        char s[11];
        s[0] = '\0';
        snprintf(s, sizeof(s), "[%02d:%02d:%02d]", localTime->tm_hour, localTime->tm_min, localTime->tm_sec);
        lastTime = memory->globalVars->realtime;
        memory->setClanTag(s, s);
    } else if (config->misc.customClanTag) {
        if (memory->globalVars->realtime - lastTime < 0.6f)
            return;

        if (config->misc.animatedClanTag && !clanTag.empty()) {
            const auto offset = Helpers::utf8SeqLen(clanTag[0]);
            if (offset != -1 && static_cast<std::size_t>(offset) <= clanTag.length())
                std::rotate(clanTag.begin(), clanTag.begin() + offset, clanTag.end());
        }
        lastTime = memory->globalVars->realtime;
        memory->setClanTag(clanTag.c_str(), clanTag.c_str());
    }
}

const bool anyActiveKeybinds() noexcept
{
    const bool rageBot = config->ragebotKey.canShowKeybind();
    const bool minDamageOverride = config->minDamageOverrideKey.canShowKeybind();
    const bool fakeAngle = config->fakeAngle.enabled && config->fakeAngle.invert.canShowKeybind();
    const bool legitAntiAim = config->legitAntiAim.enabled && config->legitAntiAim.invert.canShowKeybind();
    const bool legitBot = config->legitbotKey.canShowKeybind();
    const bool triggerBot = config->triggerbotKey.canShowKeybind();
    const bool glow = config->glowKey.canShowKeybind();
    const bool chams = config->chamsKey.canShowKeybind();
    const bool esp = config->streamProofESP.key.canShowKeybind();

    const bool zoom = config->visuals.zoom && config->visuals.zoomKey.canShowKeybind();
    const bool thirdperson = config->visuals.thirdperson && config->visuals.thirdpersonKey.canShowKeybind();
    const bool freeCam = config->visuals.freeCam && config->visuals.freeCamKey.canShowKeybind();

    const bool blockbot = config->misc.blockBot && config->misc.blockBotKey.canShowKeybind();
    const bool edgejump = config->misc.edgejump && config->misc.edgejumpkey.canShowKeybind();
    const bool edgebug = config->misc.edgebug && config->misc.edgebugkey.canShowKeybind();
    const bool jumpBug = config->misc.jumpBug && config->misc.jumpBugKey.canShowKeybind();
    const bool slowwalk = config->misc.slowwalk && config->misc.slowwalkKey.canShowKeybind();
    const bool fakeduck = config->misc.fakeduck && config->misc.fakeduckKey.canShowKeybind();
    const bool autoPeek = config->misc.autoPeek.enabled && config->misc.autoPeekKey.canShowKeybind();
    const bool prepareRevolver = config->misc.prepareRevolver && config->misc.prepareRevolverKey.canShowKeybind();

    return rageBot || minDamageOverride || fakeAngle || legitAntiAim || legitBot || triggerBot || chams || glow || esp
        || zoom || thirdperson || freeCam || blockbot || edgejump || edgebug || jumpBug || slowwalk || fakeduck || autoPeek || prepareRevolver;
}

void Misc::showKeybinds() noexcept
{
    if (!config->misc.keybindList.enabled)
        return;

    if (!anyActiveKeybinds() && !gui->isOpen())
        return;

    if (config->misc.keybindList.pos != ImVec2{}) {
        ImGui::SetNextWindowPos(config->misc.keybindList.pos);
        config->misc.keybindList.pos = {};
    }

    ImGui::SetNextWindowSize({ 250.f, 0.f }, ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints({ 250.f, 0.f }, { 250.f, FLT_MAX });

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!gui->isOpen())
        windowFlags |= ImGuiWindowFlags_NoInputs;

    if (config->misc.keybindList.noTitleBar)
        windowFlags |= ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, { 0.5f, 0.5f });
    ImGui::Begin("Keybind list", nullptr, windowFlags);
    ImGui::PopStyleVar();

    config->ragebotKey.showKeybind();
    config->minDamageOverrideKey.showKeybind();
    if (config->fakeAngle.enabled)
        config->fakeAngle.invert.showKeybind();
    if (config->legitAntiAim.enabled)
        config->legitAntiAim.invert.showKeybind();
    config->legitbotKey.showKeybind();
    config->triggerbotKey.showKeybind();
    config->chamsKey.showKeybind();
    config->glowKey.showKeybind();
    config->streamProofESP.key.showKeybind();

    if (config->visuals.zoom)
        config->visuals.zoomKey.showKeybind();
    if (config->visuals.thirdperson)
        config->visuals.thirdpersonKey.showKeybind();
    if (config->visuals.freeCam)
        config->visuals.freeCamKey.showKeybind();

    if (config->misc.blockBot)
        config->misc.blockBotKey.showKeybind();
    if (config->misc.edgejump)
        config->misc.edgejumpkey.showKeybind();
    if (config->misc.edgebug)
        config->misc.edgebugkey.showKeybind();
    if (config->misc.jumpBug)
        config->misc.jumpBugKey.showKeybind();
    if (config->misc.slowwalk)
        config->misc.slowwalkKey.showKeybind();
    if (config->misc.fakeduck)
        config->misc.fakeduckKey.showKeybind();
    if (config->misc.autoPeek.enabled)
        config->misc.autoPeekKey.showKeybind();
    if (config->misc.prepareRevolver)
        config->misc.prepareRevolverKey.showKeybind();

    ImGui::End();
}

void Misc::spectatorList() noexcept
{
    if (!config->misc.spectatorList.enabled)
        return;

    GameData::Lock lock;

    const auto& observers = GameData::observers();

    if (std::ranges::none_of(observers, [](const auto& obs) { return obs.targetIsLocalPlayer; }) && !gui->isOpen())
        return;

    if (config->misc.spectatorList.pos != ImVec2{}) {
        ImGui::SetNextWindowPos(config->misc.spectatorList.pos);
        config->misc.spectatorList.pos = {};
    }

    ImGui::SetNextWindowSize({ 250.f, 0.f }, ImGuiCond_Once);
    ImGui::SetNextWindowSizeConstraints({ 250.f, 0.f }, { 250.f, FLT_MAX });

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    if (!gui->isOpen())
        windowFlags |= ImGuiWindowFlags_NoInputs;

    if (config->misc.spectatorList.noTitleBar)
        windowFlags |= ImGuiWindowFlags_NoTitleBar;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, { 0.5f, 0.5f });
    ImGui::Begin("Spectator list", nullptr, windowFlags);
    ImGui::PopStyleVar();

    for (const auto& observer : observers) {
        if (!observer.targetIsLocalPlayer)
            continue;

        if (const auto it = std::ranges::find(GameData::players(), observer.playerHandle, &PlayerData::handle); it != GameData::players().cend()) {
            ImGui::TextWrapped("%s", it->name.c_str());
        }
    }

    ImGui::End();
}

static void drawCrosshair(ImDrawList* drawList, const ImVec2& pos, ImU32 color) noexcept
{
    // dot
    drawList->AddRectFilled(pos - ImVec2{ 1, 1 }, pos + ImVec2{ 2, 2 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(pos, pos + ImVec2{ 1, 1 }, color);

    // left
    drawList->AddRectFilled(ImVec2{ pos.x - 11, pos.y - 1 }, ImVec2{ pos.x - 3, pos.y + 2 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x - 10, pos.y }, ImVec2{ pos.x - 4, pos.y + 1 }, color);

    // right
    drawList->AddRectFilled(ImVec2{ pos.x + 4, pos.y - 1 }, ImVec2{ pos.x + 12, pos.y + 2 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x + 5, pos.y }, ImVec2{ pos.x + 11, pos.y + 1 }, color);

    // top (left with swapped x/y offsets)
    drawList->AddRectFilled(ImVec2{ pos.x - 1, pos.y - 11 }, ImVec2{ pos.x + 2, pos.y - 3 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x, pos.y - 10 }, ImVec2{ pos.x + 1, pos.y - 4 }, color);

    // bottom (right with swapped x/y offsets)
    drawList->AddRectFilled(ImVec2{ pos.x - 1, pos.y + 4 }, ImVec2{ pos.x + 2, pos.y + 12 }, color & IM_COL32_A_MASK);
    drawList->AddRectFilled(ImVec2{ pos.x, pos.y + 5 }, ImVec2{ pos.x + 1, pos.y + 11 }, color);
}

void Misc::noscopeCrosshair(ImDrawList* drawList) noexcept
{
    if (!config->misc.noscopeCrosshair.enabled)
        return;

    {
        GameData::Lock lock;
        if (const auto& local = GameData::local(); !local.exists || !local.alive || !local.noScope)
            return;
    }

    drawCrosshair(drawList, ImGui::GetIO().DisplaySize / 2, Helpers::calculateColor(config->misc.noscopeCrosshair));
}

void Misc::recoilCrosshair(ImDrawList* drawList) noexcept
{
    if (!config->misc.recoilCrosshair.enabled)
        return;

    GameData::Lock lock;
    const auto& localPlayerData = GameData::local();

    if (!localPlayerData.exists || !localPlayerData.alive)
        return;

    if (!localPlayerData.shooting)
        return;

    if (ImVec2 pos; Helpers::worldToScreen(localPlayerData.aimPunch, pos))
        drawCrosshair(drawList, pos, Helpers::calculateColor(config->misc.recoilCrosshair));
}

void Misc::watermark() noexcept
{
    if (!config->misc.watermark.enabled)
        return;

    if (config->misc.watermark.pos != ImVec2{}) {
        ImGui::SetNextWindowPos(config->misc.watermark.pos);
        config->misc.watermark.pos = {};
    }

    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize;
    if (!gui->isOpen())
        windowFlags |= ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowBgAlpha(0.3f);
    ImGui::Begin("Watermark", nullptr, windowFlags);

    static auto frameRate = 1.0f;
    frameRate = 0.9f * frameRate + 0.1f * memory->globalVars->absoluteFrameTime;

    ImGui::Text("Osiris | %d fps | %d ms", frameRate != 0.0f ? static_cast<int>(1 / frameRate) : 0, GameData::getNetOutgoingLatency());
    ImGui::End();
}

void Misc::prepareRevolver(UserCmd* cmd) noexcept
{
    constexpr auto timeToTicks = [](float time) {  return static_cast<int>(0.5f + time / memory->globalVars->intervalPerTick); };
    constexpr float revolverPrepareTime{ 0.234375f };

    static float readyTime;
    if (!config->misc.prepareRevolver)
        return;

    if (!config->misc.prepareRevolverKey.isActive())
        return;

    if (!localPlayer)
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (activeWeapon && activeWeapon->itemDefinitionIndex2() == WeaponId::Revolver) {
        if (!readyTime) readyTime = memory->globalVars->serverTime() + revolverPrepareTime;
        auto ticksToReady = timeToTicks(readyTime - memory->globalVars->serverTime() - interfaces->engine->getNetworkChannel()->getLatency(0));
        if (ticksToReady > 0 && ticksToReady <= timeToTicks(revolverPrepareTime))
            cmd->buttons |= UserCmd::IN_ATTACK;
        else
            readyTime = 0.0f;
    }
}

void Misc::fastPlant(UserCmd* cmd) noexcept
{
    if (!config->misc.fastPlant)
        return;

    static auto plantAnywhere = interfaces->cvar->findVar("mp_plant_c4_anywhere");

    if (plantAnywhere->getInt())
        return;

    if (!localPlayer || !localPlayer->isAlive() || (localPlayer->inBombZone() && localPlayer->flags() & 1))
        return;

    const auto activeWeapon = localPlayer->getActiveWeapon();
    if (!activeWeapon || activeWeapon->getClientClass()->classId != ClassId::C4)
        return;

    cmd->buttons &= ~UserCmd::IN_ATTACK;

    constexpr auto doorRange = 200.0f;

    Trace trace;
    const auto startPos = localPlayer->getEyePosition();
    const auto endPos = startPos + Vector::fromAngle(cmd->viewangles) * doorRange;
    interfaces->engineTrace->traceRay({ startPos, endPos }, 0x46004009, localPlayer.get(), trace);

    if (!trace.entity || trace.entity->getClientClass()->classId != ClassId::PropDoorRotating)
        cmd->buttons &= ~UserCmd::IN_USE;
}

void Misc::fastStop(UserCmd* cmd) noexcept
{
    if (!config->misc.fastStop)
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (localPlayer->moveType() == MoveType::NOCLIP || localPlayer->moveType() == MoveType::LADDER || !(localPlayer->flags() & 1) || cmd->buttons & UserCmd::IN_JUMP)
        return;

    if (cmd->buttons & (UserCmd::IN_MOVELEFT | UserCmd::IN_MOVERIGHT | UserCmd::IN_FORWARD | UserCmd::IN_BACK))
        return;
    
    const auto velocity = localPlayer->velocity();
    const auto speed = velocity.length2D();
    if (speed < 15.0f)
        return;
    
    Vector direction = velocity.toAngle();
    direction.y = cmd->viewangles.y - direction.y;

    const auto negatedDirection = Vector::fromAngle(direction) * -speed;
    cmd->forwardmove = negatedDirection.x;
    cmd->sidemove = negatedDirection.y;
}

void Misc::drawBombTimer() noexcept
{
    if (!config->misc.bombTimer.enabled)
        return;

    GameData::Lock lock;
    
    const auto& plantedC4 = GameData::plantedC4();
    if (plantedC4.blowTime == 0.0f && !gui->isOpen())
        return;

    if (!gui->isOpen()) {
        ImGui::SetNextWindowBgAlpha(0.3f);
    }

    static float windowWidth = 200.0f;
    ImGui::SetNextWindowPos({ (ImGui::GetIO().DisplaySize.x - 200.0f) / 2.0f, 60.0f }, ImGuiCond_Once);
    ImGui::SetNextWindowSize({ windowWidth, 0 }, ImGuiCond_Once);

    if (!gui->isOpen())
        ImGui::SetNextWindowSize({ windowWidth, 0 });

    ImGui::SetNextWindowSizeConstraints({ 0, -1 }, { FLT_MAX, -1 });
    ImGui::Begin("Bomb Timer", nullptr, ImGuiWindowFlags_NoTitleBar | (gui->isOpen() ? 0 : ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration));

    std::ostringstream ss; ss << "Bomb on " << (!plantedC4.bombsite ? 'A' : 'B') << " : " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.blowTime - memory->globalVars->currenttime, 0.0f) << " s";

    ImGui::textUnformattedCentered(ss.str().c_str());

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Helpers::calculateColor(config->misc.bombTimer));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f });
    ImGui::progressBarFullWidth((plantedC4.blowTime - memory->globalVars->currenttime) / plantedC4.timerLength, 5.0f);

    if (plantedC4.defuserHandle != -1) {
        const bool canDefuse = plantedC4.blowTime >= plantedC4.defuseCountDown;

        if (plantedC4.defuserHandle == GameData::local().handle) {
            if (canDefuse) {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
                ImGui::textUnformattedCentered("You can defuse!");
            } else {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 0, 0, 255));
                ImGui::textUnformattedCentered("You can not defuse!");
            }
            ImGui::PopStyleColor();
        } else if (const auto defusingPlayer = GameData::playerByHandle(plantedC4.defuserHandle)) {
            std::ostringstream ss; ss << defusingPlayer->name << " is defusing: " << std::fixed << std::showpoint << std::setprecision(3) << (std::max)(plantedC4.defuseCountDown - memory->globalVars->currenttime, 0.0f) << " s";

            ImGui::textUnformattedCentered(ss.str().c_str());

            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, canDefuse ? IM_COL32(0, 255, 0, 255) : IM_COL32(255, 0, 0, 255));
            ImGui::progressBarFullWidth((plantedC4.defuseCountDown - memory->globalVars->currenttime) / plantedC4.defuseLength, 5.0f);
            ImGui::PopStyleColor();
        }
    }

    windowWidth = ImGui::GetCurrentWindow()->SizeFull.x;

    ImGui::PopStyleColor(2);
    ImGui::End();
}

void Misc::hurtIndicator() noexcept
{
    if (!config->misc.hurtIndicator.enabled)
        return;

    GameData::Lock lock;
    const auto& local = GameData::local();
    if ((!local.exists || !local.alive) && !gui->isOpen())
        return;

    if (local.velocityModifier >= 0.99f && !gui->isOpen())
        return;

    if (!gui->isOpen()) {
        ImGui::SetNextWindowBgAlpha(0.3f);
    }

    static float windowWidth = 140.0f;
    ImGui::SetNextWindowPos({ (ImGui::GetIO().DisplaySize.x - 140.0f) / 2.0f, 260.0f }, ImGuiCond_Once);
    ImGui::SetNextWindowSize({ windowWidth, 0 }, ImGuiCond_Once);

    if (!gui->isOpen())
        ImGui::SetNextWindowSize({ windowWidth, 0 });

    ImGui::SetNextWindowSizeConstraints({ 0, -1 }, { FLT_MAX, -1 });
    ImGui::Begin("Hurt Indicator", nullptr, ImGuiWindowFlags_NoTitleBar | (gui->isOpen() ? 0 : ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDecoration));

    std::ostringstream ss; ss << "Slowed down " << static_cast<int>(local.velocityModifier * 100.f) << "%";
    ImGui::textUnformattedCentered(ss.str().c_str());

    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, Helpers::calculateColor(config->misc.hurtIndicator));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4{ 0.2f, 0.2f, 0.2f, 1.0f });
    ImGui::progressBarFullWidth(local.velocityModifier, 1.0f);

    windowWidth = ImGui::GetCurrentWindow()->SizeFull.x;

    ImGui::PopStyleColor(2);
    ImGui::End();
}

void Misc::stealNames() noexcept
{
    if (!config->misc.nameStealer)
        return;

    if (!localPlayer)
        return;

    static std::vector<int> stolenIds;

    for (int i = 1; i <= memory->globalVars->maxClients; ++i) {
        const auto entity = interfaces->entityList->getEntity(i);

        if (!entity || entity == localPlayer.get())
            continue;

        PlayerInfo playerInfo;
        if (!interfaces->engine->getPlayerInfo(entity->index(), playerInfo))
            continue;

        if (playerInfo.fakeplayer || std::find(stolenIds.cbegin(), stolenIds.cend(), playerInfo.userId) != stolenIds.cend())
            continue;

        if (changeName(false, (std::string{ playerInfo.name } +'\x1').c_str(), 1.0f))
            stolenIds.push_back(playerInfo.userId);

        return;
    }
    stolenIds.clear();
}

void Misc::disablePanoramablur() noexcept
{
    static auto blur = interfaces->cvar->findVar("@panorama_disable_blur");
    blur->setValue(config->misc.disablePanoramablur);
}

bool Misc::changeName(bool reconnect, const char* newName, float delay) noexcept
{
    static auto exploitInitialized{ false };

    static auto name{ interfaces->cvar->findVar("name") };

    if (reconnect) {
        exploitInitialized = false;
        return false;
    }

    if (!exploitInitialized && interfaces->engine->isInGame()) {
        if (PlayerInfo playerInfo; localPlayer && interfaces->engine->getPlayerInfo(localPlayer->index(), playerInfo) && (!strcmp(playerInfo.name, "?empty") || !strcmp(playerInfo.name, "\n\xAD\xAD\xAD"))) {
            exploitInitialized = true;
        } else {
            name->onChangeCallbacks.size = 0;
            name->setValue("\n\xAD\xAD\xAD");
            return false;
        }
    }

    static auto nextChangeTime{ 0.0f };
    if (nextChangeTime <= memory->globalVars->realtime) {
        name->setValue(newName);
        nextChangeTime = memory->globalVars->realtime + delay;
        return true;
    }
    return false;
}

void Misc::bunnyHop(UserCmd* cmd) noexcept
{
    if (!localPlayer)
        return;

    if (config->misc.jumpBug && config->misc.jumpBugKey.isActive())
        return;

    static auto wasLastTimeOnGround{ localPlayer->flags() & 1 };

    if (config->misc.bunnyHop && !(localPlayer->flags() & 1) && localPlayer->moveType() != MoveType::LADDER && !wasLastTimeOnGround)
        cmd->buttons &= ~UserCmd::IN_JUMP;

    wasLastTimeOnGround = localPlayer->flags() & 1;
}

void Misc::fixTabletSignal() noexcept
{
    if (config->misc.fixTabletSignal && localPlayer) {
        if (auto activeWeapon{ localPlayer->getActiveWeapon() }; activeWeapon && activeWeapon->getClientClass()->classId == ClassId::Tablet)
            activeWeapon->tabletReceptionIsBlocked() = false;
    }
}

void Misc::killfeedChanger(GameEvent& event) noexcept
{
    if (!config->misc.killfeedChanger.enabled)
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    if (config->misc.killfeedChanger.headshot)
        event.setInt("headshot", 1);

    if (config->misc.killfeedChanger.dominated)
        event.setInt("Dominated", 1);

    if (config->misc.killfeedChanger.revenge)
        event.setInt("Revenge", 1);

    if (config->misc.killfeedChanger.penetrated)
        event.setInt("penetrated", 1);

    if (config->misc.killfeedChanger.noscope)
        event.setInt("noscope", 1);

    if (config->misc.killfeedChanger.thrusmoke)
        event.setInt("thrusmoke", 1);

    if (config->misc.killfeedChanger.attackerblind)
        event.setInt("attackerblind", 1);
}

void Misc::killMessage(GameEvent& event) noexcept
{
    if (!config->misc.killMessage)
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    std::string cmd = "say \"";
    cmd += config->misc.killMessageString;
    cmd += '"';
    interfaces->engine->clientCmdUnrestricted(cmd.c_str());
}

void Misc::fixMovement(UserCmd* cmd, float yaw) noexcept
{
    float oldYaw = yaw + (yaw < 0.0f ? 360.0f : 0.0f);
    float newYaw = cmd->viewangles.y + (cmd->viewangles.y < 0.0f ? 360.0f : 0.0f);
    float yawDelta = newYaw < oldYaw ? fabsf(newYaw - oldYaw) : 360.0f - fabsf(newYaw - oldYaw);
    yawDelta = 360.0f - yawDelta;

    const float forwardmove = cmd->forwardmove;
    const float sidemove = cmd->sidemove;
    cmd->forwardmove = std::cos(Helpers::deg2rad(yawDelta)) * forwardmove + std::cos(Helpers::deg2rad(yawDelta + 90.0f)) * sidemove;
    cmd->sidemove = std::sin(Helpers::deg2rad(yawDelta)) * forwardmove + std::sin(Helpers::deg2rad(yawDelta + 90.0f)) * sidemove;
}

void Misc::antiAfkKick(UserCmd* cmd) noexcept
{
    if (config->misc.antiAfkKick && cmd->commandNumber % 2)
        cmd->buttons |= 1 << 27;
}

void Misc::fixAnimationLOD(FrameStage stage) noexcept
{
    if (stage != FrameStage::RENDER_START)
        return;

    if (!localPlayer)
        return;

    for (int i = 1; i <= interfaces->engine->getMaxClients(); i++) {
        Entity* entity = interfaces->entityList->getEntity(i);
        if (!entity || entity == localPlayer.get() || entity->isDormant() || !entity->isAlive())
            continue;
        *reinterpret_cast<int*>(entity + 0xA28) = 0;
        *reinterpret_cast<int*>(entity + 0xA30) = memory->globalVars->framecount;
    }
}

void Misc::autoPistol(UserCmd* cmd) noexcept
{
    if (config->misc.autoPistol && localPlayer) {
        const auto activeWeapon = localPlayer->getActiveWeapon();
        if (activeWeapon && activeWeapon->isPistol() && activeWeapon->nextPrimaryAttack() > memory->globalVars->serverTime()) {
            if (activeWeapon->itemDefinitionIndex2() == WeaponId::Revolver)
                cmd->buttons &= ~UserCmd::IN_ATTACK2;
            else
                cmd->buttons &= ~UserCmd::IN_ATTACK;
        }
    }
}

void Misc::autoReload(UserCmd* cmd) noexcept
{
    if (config->misc.autoReload && localPlayer) {
        const auto activeWeapon = localPlayer->getActiveWeapon();
        if (activeWeapon && getWeaponIndex(activeWeapon->itemDefinitionIndex2()) && !activeWeapon->clip())
            cmd->buttons &= ~(UserCmd::IN_ATTACK | UserCmd::IN_ATTACK2);
    }
}

void Misc::revealRanks(UserCmd* cmd) noexcept
{
    if (config->misc.revealRanks && cmd->buttons & UserCmd::IN_SCORE)
        interfaces->client->dispatchUserMessage(50, 0, 0, nullptr);
}

void Misc::autoStrafe(UserCmd* cmd, Vector& currentViewAngles) noexcept
{
    if (!config->misc.autoStrafe)
        return;
    
    if (!localPlayer || !localPlayer->isAlive())
        return;
    
    const float speed = localPlayer->velocity().length2D();
    if (speed < 5.0f)
        return;

    static float angle = 0.f;

    bool back = cmd->buttons & UserCmd::IN_BACK;
    bool forward = cmd->buttons & UserCmd::IN_FORWARD;
    bool right = cmd->buttons & UserCmd::IN_MOVERIGHT;
    bool left = cmd->buttons & UserCmd::IN_MOVELEFT;

    if (back) {
        angle = -180.f;
        if (left)
            angle -= 45.f;
        else if (right)
            angle += 45.f;
    }
    else if (left) {
        angle = 90.f;
        if (back)
            angle += 45.f;
        else if (forward)
            angle -= 45.f;
    }
    else if (right) {
        angle = -90.f;
        if (back)
            angle -= 45.f;
        else if (forward)
            angle += 45.f;
    }
    else {
        angle = 0.f;
    }

    if ((EnginePrediction::getFlags() & 1 && !(cmd->buttons & UserCmd::IN_JUMP)) || localPlayer->moveType() == MoveType::NOCLIP || localPlayer->moveType() == MoveType::LADDER)
        return;

    if (EnginePrediction::getFlags() & 1)
        return;

    currentViewAngles.y += angle;

    cmd->forwardmove = 0.f;
    cmd->sidemove = 0.f;

    const auto delta = Helpers::normalizeYaw(currentViewAngles.y - Helpers::rad2deg(std::atan2(EnginePrediction::getVelocity().y, EnginePrediction::getVelocity().x)));

    cmd->sidemove = delta > 0.f ? -450.f : 450.f;

    currentViewAngles.y = Helpers::normalizeYaw(currentViewAngles.y - delta);
}

void Misc::removeCrouchCooldown(UserCmd* cmd) noexcept
{
    if (config->misc.fastDuck)
        cmd->buttons |= UserCmd::IN_BULLRUSH;
}

void Misc::moonwalk(UserCmd* cmd) noexcept
{
    if (config->misc.moonwalk && localPlayer && localPlayer->moveType() != MoveType::LADDER)
        cmd->buttons ^= UserCmd::IN_FORWARD | UserCmd::IN_BACK | UserCmd::IN_MOVELEFT | UserCmd::IN_MOVERIGHT;
}

void Misc::playHitSound(GameEvent& event) noexcept
{
    if (!config->misc.hitSound)
        return;

    if (!localPlayer)
        return;

    if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    constexpr std::array hitSounds{
        "play physics/metal/metal_solid_impact_bullet2",
        "play buttons/arena_switch_press_02",
        "play training/timer_bell",
        "play physics/glass/glass_impact_bullet1"
    };

    if (static_cast<std::size_t>(config->misc.hitSound - 1) < hitSounds.size())
        interfaces->engine->clientCmdUnrestricted(hitSounds[config->misc.hitSound - 1]);
    else if (config->misc.hitSound == 5)
        interfaces->engine->clientCmdUnrestricted(("play " + config->misc.customHitSound).c_str());
}

void Misc::killSound(GameEvent& event) noexcept
{
    if (!config->misc.killSound)
        return;

    if (!localPlayer || !localPlayer->isAlive())
        return;

    if (const auto localUserId = localPlayer->getUserId(); event.getInt("attacker") != localUserId || event.getInt("userid") == localUserId)
        return;

    constexpr std::array killSounds{
        "play physics/metal/metal_solid_impact_bullet2",
        "play buttons/arena_switch_press_02",
        "play training/timer_bell",
        "play physics/glass/glass_impact_bullet1"
    };

    if (static_cast<std::size_t>(config->misc.killSound - 1) < killSounds.size())
        interfaces->engine->clientCmdUnrestricted(killSounds[config->misc.killSound - 1]);
    else if (config->misc.killSound == 5)
        interfaces->engine->clientCmdUnrestricted(("play " + config->misc.customKillSound).c_str());
}

void Misc::autoBuy(GameEvent* event) noexcept
{
    std::array<std::string, 17> primary = {
    "",
    "buy mac10;buy mp9;",
    "buy mp7;",
    "buy ump45;",
    "buy p90;",
    "buy bizon;",
    "buy galilar;buy famas;",
    "buy ak47;buy m4a1;",
    "buy ssg08;",
    "buy sg556;buy aug;",
    "buy awp;",
    "buy g3sg1; buy scar20;",
    "buy nova;",
    "buy xm1014;",
    "buy sawedoff;buy mag7;",
    "buy m249;",
    "buy negev;"
    };
    std::array<std::string, 6> secondary = {
        "",
        "buy glock;buy hkp2000",
        "buy elite;",
        "buy p250;",
        "buy tec9;buy fiveseven;",
        "buy deagle;buy revolver;"
    };
    std::array<std::string, 3> armor = {
        "",
        "buy vest;",
        "buy vesthelm;",
    };
    std::array<std::string, 2> utility = {
        "buy defuser;",
        "buy taser;"
    };
    std::array<std::string, 5> nades = {
        "buy hegrenade;",
        "buy smokegrenade;",
        "buy molotov;buy incgrenade;",
        "buy flashbang;buy flashbang;",
        "buy decoy;"
    };

    if (!config->misc.autoBuy.enabled)
        return;

    std::string cmd = "";

    if (event) 
    {
        cmd += primary[config->misc.autoBuy.primaryWeapon];
        cmd += secondary[config->misc.autoBuy.secondaryWeapon];
        cmd += armor[config->misc.autoBuy.armor];

        for (size_t i = 0; i < utility.size(); i++)
        {
            if ((config->misc.autoBuy.utility & 1 << i) == 1 << i)
                cmd += utility[i];
        }

        for (size_t i = 0; i < nades.size(); i++)
        {
            if ((config->misc.autoBuy.grenades & 1 << i) == 1 << i)
                cmd += nades[i];
        }

        interfaces->engine->clientCmdUnrestricted(cmd.c_str());
    }
}

void Misc::purchaseList(GameEvent* event) noexcept
{
    static std::mutex mtx;
    std::scoped_lock _{ mtx };

    struct PlayerPurchases {
        int totalCost;
        std::unordered_map<std::string, int> items;
    };

    static std::unordered_map<int, PlayerPurchases> playerPurchases;
    static std::unordered_map<std::string, int> purchaseTotal;
    static int totalCost;

    static auto freezeEnd = 0.0f;

    if (event) {
        switch (fnv::hashRuntime(event->getName())) {
        case fnv::hash("item_purchase"): {
            const auto player = interfaces->entityList->getEntity(interfaces->engine->getPlayerForUserID(event->getInt("userid")));

            if (player && localPlayer && memory->isOtherEnemy(player, localPlayer.get())) {
                if (const auto definition = memory->itemSystem()->getItemSchema()->getItemDefinitionByName(event->getString("weapon"))) {
                    auto& purchase = playerPurchases[player->handle()];
                    if (const auto weaponInfo = memory->weaponSystem->getWeaponInfo(definition->getWeaponId())) {
                        purchase.totalCost += weaponInfo->price;
                        totalCost += weaponInfo->price;
                    }
                    const std::string weapon = interfaces->localize->findAsUTF8(definition->getItemBaseName());
                    ++purchaseTotal[weapon];
                    ++purchase.items[weapon];
                }
            }
            break;
        }
        case fnv::hash("round_start"):
            freezeEnd = 0.0f;
            playerPurchases.clear();
            purchaseTotal.clear();
            totalCost = 0;
            break;
        case fnv::hash("round_freeze_end"):
            freezeEnd = memory->globalVars->realtime;
            break;
        }
    } else {
        if (!config->misc.purchaseList.enabled)
            return;

        static const auto mp_buytime = interfaces->cvar->findVar("mp_buytime");

        if ((!interfaces->engine->isInGame() || freezeEnd != 0.0f && memory->globalVars->realtime > freezeEnd + (!config->misc.purchaseList.onlyDuringFreezeTime ? mp_buytime->getFloat() : 0.0f) || playerPurchases.empty() || purchaseTotal.empty()) && !gui->isOpen())
            return;

        if (config->misc.purchaseList.pos != ImVec2{}) {
            ImGui::SetNextWindowPos(config->misc.purchaseList.pos);
            config->misc.purchaseList.pos = {};
        }

        ImGui::SetNextWindowSize({ 200.0f, 200.0f }, ImGuiCond_Once);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoCollapse;
        if (!gui->isOpen())
            windowFlags |= ImGuiWindowFlags_NoInputs;
        if (config->misc.purchaseList.noTitleBar)
            windowFlags |= ImGuiWindowFlags_NoTitleBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowTitleAlign, { 0.5f, 0.5f });
        ImGui::Begin("Purchases", nullptr, windowFlags);
        ImGui::PopStyleVar();

        if (config->misc.purchaseList.mode == PurchaseList::Details) {
            GameData::Lock lock;

            for (const auto& [handle, purchases] : playerPurchases) {
                std::string s;
                s.reserve(std::accumulate(purchases.items.begin(), purchases.items.end(), 0, [](int length, const auto& p) { return length + p.first.length() + 2; }));
                for (const auto& purchasedItem : purchases.items) {
                    if (purchasedItem.second > 1)
                        s += std::to_string(purchasedItem.second) + "x ";
                    s += purchasedItem.first + ", ";
                }

                if (s.length() >= 2)
                    s.erase(s.length() - 2);

                if (const auto player = GameData::playerByHandle(handle)) {
                    if (config->misc.purchaseList.showPrices)
                        ImGui::TextWrapped("%s $%d: %s", player->name.c_str(), purchases.totalCost, s.c_str());
                    else
                        ImGui::TextWrapped("%s: %s", player->name.c_str(), s.c_str());
                }
            }
        } else if (config->misc.purchaseList.mode == PurchaseList::Summary) {
            for (const auto& purchase : purchaseTotal)
                ImGui::TextWrapped("%d x %s", purchase.second, purchase.first.c_str());

            if (config->misc.purchaseList.showPrices && totalCost > 0) {
                ImGui::Separator();
                ImGui::TextWrapped("Total: $%d", totalCost);
            }
        }
        ImGui::End();
    }
}

void Misc::oppositeHandKnife(FrameStage stage) noexcept
{
    if (!config->misc.oppositeHandKnife)
        return;

    if (!localPlayer)
        return;

    if (stage != FrameStage::RENDER_START && stage != FrameStage::RENDER_END)
        return;

    static const auto cl_righthand = interfaces->cvar->findVar("cl_righthand");
    static bool original;

    if (stage == FrameStage::RENDER_START) {
        original = cl_righthand->getInt();

        if (const auto activeWeapon = localPlayer->getActiveWeapon()) {
            if (const auto classId = activeWeapon->getClientClass()->classId; classId == ClassId::Knife || classId == ClassId::KnifeGG)
                cl_righthand->setValue(!original);
        }
    } else {
        cl_righthand->setValue(original);
    }
}

static std::vector<std::uint64_t> reportedPlayers;
static int reportbotRound;

void Misc::runReportbot() noexcept
{
    if (!config->misc.reportbot.enabled)
        return;

    if (!localPlayer)
        return;

    static auto lastReportTime = 0.0f;

    if (lastReportTime + config->misc.reportbot.delay > memory->globalVars->realtime)
        return;

    if (reportbotRound >= config->misc.reportbot.rounds)
        return;

    for (int i = 1; i <= interfaces->engine->getMaxClients(); ++i) {
        const auto entity = interfaces->entityList->getEntity(i);

        if (!entity || entity == localPlayer.get())
            continue;

        if (config->misc.reportbot.target != 2 && (entity->isOtherEnemy(localPlayer.get()) ? config->misc.reportbot.target != 0 : config->misc.reportbot.target != 1))
            continue;

        PlayerInfo playerInfo;
        if (!interfaces->engine->getPlayerInfo(i, playerInfo))
            continue;

        if (playerInfo.fakeplayer || std::find(reportedPlayers.cbegin(), reportedPlayers.cend(), playerInfo.xuid) != reportedPlayers.cend())
            continue;

        std::string report;

        if (config->misc.reportbot.textAbuse)
            report += "textabuse,";
        if (config->misc.reportbot.griefing)
            report += "grief,";
        if (config->misc.reportbot.wallhack)
            report += "wallhack,";
        if (config->misc.reportbot.aimbot)
            report += "aimbot,";
        if (config->misc.reportbot.other)
            report += "speedhack,";

        if (!report.empty()) {
            memory->submitReport(std::to_string(playerInfo.xuid).c_str(), report.c_str());
            lastReportTime = memory->globalVars->realtime;
            reportedPlayers.push_back(playerInfo.xuid);
        }
        return;
    }

    reportedPlayers.clear();
    ++reportbotRound;
}

void Misc::resetReportbot() noexcept
{
    reportbotRound = 0;
    reportedPlayers.clear();
}

void Misc::preserveKillfeed(bool roundStart) noexcept
{
    if (!config->misc.preserveKillfeed.enabled)
        return;

    static auto nextUpdate = 0.0f;

    if (roundStart) {
        nextUpdate = memory->globalVars->realtime + 10.0f;
        return;
    }

    if (nextUpdate > memory->globalVars->realtime)
        return;

    nextUpdate = memory->globalVars->realtime + 2.0f;

    const auto deathNotice = std::uintptr_t(memory->findHudElement(memory->hud, "CCSGO_HudDeathNotice"));
    if (!deathNotice)
        return;

    const auto deathNoticePanel = (*(UIPanel**)(*reinterpret_cast<std::uintptr_t*>(deathNotice - 20 + 88) + sizeof(std::uintptr_t)));

    const auto childPanelCount = deathNoticePanel->getChildCount();

    for (int i = 0; i < childPanelCount; ++i) {
        const auto child = deathNoticePanel->getChild(i);
        if (!child)
            continue;

        if (child->hasClass("DeathNotice_Killer") && (!config->misc.preserveKillfeed.onlyHeadshots || child->hasClass("DeathNoticeHeadShot")))
            child->setAttributeFloat("SpawnTime", memory->globalVars->currenttime);
    }
}

void Misc::voteRevealer(GameEvent& event) noexcept
{
    if (!config->misc.revealVotes)
        return;

    const auto entity = interfaces->entityList->getEntity(event.getInt("entityid"));
    if (!entity || !entity->isPlayer())
        return;
    
    const auto votedYes = event.getInt("vote_option") == 0;
    const auto isLocal = localPlayer && entity == localPlayer.get();
    const char color = votedYes ? '\x06' : '\x07';

    memory->clientMode->getHudChat()->printf(0, " \x0C\u2022Osiris\u2022 %c%s\x01 voted %c%s\x01", isLocal ? '\x01' : color, isLocal ? "You" : entity->getPlayerName().c_str(), color, votedYes ? "Yes" : "No");
}

// ImGui::ShadeVertsLinearColorGradientKeepAlpha() modified to do interpolation in HSV
static void shadeVertsHSVColorGradientKeepAlpha(ImDrawList* draw_list, int vert_start_idx, int vert_end_idx, ImVec2 gradient_p0, ImVec2 gradient_p1, ImU32 col0, ImU32 col1)
{
    ImVec2 gradient_extent = gradient_p1 - gradient_p0;
    float gradient_inv_length2 = 1.0f / ImLengthSqr(gradient_extent);
    ImDrawVert* vert_start = draw_list->VtxBuffer.Data + vert_start_idx;
    ImDrawVert* vert_end = draw_list->VtxBuffer.Data + vert_end_idx;

    ImVec4 col0HSV = ImGui::ColorConvertU32ToFloat4(col0);
    ImVec4 col1HSV = ImGui::ColorConvertU32ToFloat4(col1);
    ImGui::ColorConvertRGBtoHSV(col0HSV.x, col0HSV.y, col0HSV.z, col0HSV.x, col0HSV.y, col0HSV.z);
    ImGui::ColorConvertRGBtoHSV(col1HSV.x, col1HSV.y, col1HSV.z, col1HSV.x, col1HSV.y, col1HSV.z);
    ImVec4 colDelta = col1HSV - col0HSV;

    for (ImDrawVert* vert = vert_start; vert < vert_end; vert++)
    {
        float d = ImDot(vert->pos - gradient_p0, gradient_extent);
        float t = ImClamp(d * gradient_inv_length2, 0.0f, 1.0f);

        float h = col0HSV.x + colDelta.x * t;
        float s = col0HSV.y + colDelta.y * t;
        float v = col0HSV.z + colDelta.z * t;

        ImVec4 rgb;
        ImGui::ColorConvertHSVtoRGB(h, s, v, rgb.x, rgb.y, rgb.z);
        vert->col = (ImGui::ColorConvertFloat4ToU32(rgb) & ~IM_COL32_A_MASK) | (vert->col & IM_COL32_A_MASK);
    }
}

void Misc::drawOffscreenEnemies(ImDrawList* drawList) noexcept
{
    if (!config->misc.offscreenEnemies.enabled)
        return;

    const auto yaw = Helpers::deg2rad(interfaces->engine->getViewAngles().y);

    GameData::Lock lock;
    for (auto& player : GameData::players()) {
        if ((player.dormant && player.fadingAlpha() == 0.0f) || !player.alive || !player.enemy || player.inViewFrustum)
            continue;

        const auto positionDiff = GameData::local().origin - player.origin;

        auto x = std::cos(yaw) * positionDiff.y - std::sin(yaw) * positionDiff.x;
        auto y = std::cos(yaw) * positionDiff.x + std::sin(yaw) * positionDiff.y;
        if (const auto len = std::sqrt(x * x + y * y); len != 0.0f) {
            x /= len;
            y /= len;
        }

        constexpr auto avatarRadius = 13.0f;
        constexpr auto triangleSize = 10.0f;

        const auto pos = ImGui::GetIO().DisplaySize / 2 + ImVec2{ x, y } * 200;
        const auto trianglePos = pos + ImVec2{ x, y } * (avatarRadius + (config->misc.offscreenEnemies.healthBar.enabled ? 5 : 3));

        Helpers::setAlphaFactor(player.fadingAlpha());
        const auto white = Helpers::calculateColor(255, 255, 255, 255);
        const auto background = Helpers::calculateColor(0, 0, 0, 80);
        const auto color = Helpers::calculateColor(config->misc.offscreenEnemies);
        const auto healthBarColor = config->misc.offscreenEnemies.healthBar.type == HealthBar::HealthBased ? Helpers::healthColor(std::clamp(player.health / 100.0f, 0.0f, 1.0f)) : Helpers::calculateColor(config->misc.offscreenEnemies.healthBar);
        Helpers::setAlphaFactor(1.0f);

        const ImVec2 trianglePoints[]{
            trianglePos + ImVec2{  0.4f * y, -0.4f * x } * triangleSize,
            trianglePos + ImVec2{  1.0f * x,  1.0f * y } * triangleSize,
            trianglePos + ImVec2{ -0.4f * y,  0.4f * x } * triangleSize
        };

        drawList->AddConvexPolyFilled(trianglePoints, 3, color);
        drawList->AddCircleFilled(pos, avatarRadius + 1, white & IM_COL32_A_MASK, 40);

        const auto texture = player.getAvatarTexture();

        const bool pushTextureId = drawList->_TextureIdStack.empty() || texture != drawList->_TextureIdStack.back();
        if (pushTextureId)
            drawList->PushTextureID(texture);

        const int vertStartIdx = drawList->VtxBuffer.Size;
        drawList->AddCircleFilled(pos, avatarRadius, white, 40);
        const int vertEndIdx = drawList->VtxBuffer.Size;
        ImGui::ShadeVertsLinearUV(drawList, vertStartIdx, vertEndIdx, pos - ImVec2{ avatarRadius, avatarRadius }, pos + ImVec2{ avatarRadius, avatarRadius }, { 0, 0 }, { 1, 1 }, true);

        if (pushTextureId)
            drawList->PopTextureID();

        if (config->misc.offscreenEnemies.healthBar.enabled) {
            const auto radius = avatarRadius + 2;
            const auto healthFraction = std::clamp(player.health / 100.0f, 0.0f, 1.0f);

            drawList->AddCircle(pos, radius, background, 40, 3.0f);

            const int vertStartIdx = drawList->VtxBuffer.Size;
            if (healthFraction == 1.0f) { // sometimes PathArcTo is missing one top pixel when drawing a full circle, so draw it with AddCircle
                drawList->AddCircle(pos, radius, healthBarColor, 40, 2.0f);
            } else {
                constexpr float pi = std::numbers::pi_v<float>;
                drawList->PathArcTo(pos, radius - 0.5f, pi / 2 - pi * healthFraction, pi / 2 + pi * healthFraction, 40);
                drawList->PathStroke(healthBarColor, false, 2.0f);
            }
            const int vertEndIdx = drawList->VtxBuffer.Size;

            if (config->misc.offscreenEnemies.healthBar.type == HealthBar::Gradient)
                shadeVertsHSVColorGradientKeepAlpha(drawList, vertStartIdx, vertEndIdx, pos - ImVec2{ 0.0f, radius }, pos + ImVec2{ 0.0f, radius }, IM_COL32(0, 255, 0, 255), IM_COL32(255, 0, 0, 255));
        }
    }
}

void Misc::autoAccept(const char* soundEntry) noexcept
{
    if (!config->misc.autoAccept)
        return;

    if (std::strcmp(soundEntry, "UIPanorama.popup_accept_match_beep"))
        return;

    if (const auto idx = memory->registeredPanoramaEvents->find(memory->makePanoramaSymbol("MatchAssistedAccept")); idx != -1) {
        if (const auto eventPtr = memory->registeredPanoramaEvents->memory[idx].value.makeEvent(nullptr))
            interfaces->panoramaUIEngine->accessUIEngine()->dispatchEvent(eventPtr);
    }

    auto window = FindWindowW(L"Valve001", NULL);
    FLASHWINFO flash{ sizeof(FLASHWINFO), window, FLASHW_TRAY | FLASHW_TIMERNOFG, 0, 0 };
    FlashWindowEx(&flash);
    ShowWindow(window, SW_RESTORE);
}

void Misc::updateEventListeners(bool forceRemove) noexcept
{
    class PurchaseEventListener : public GameEventListener {
    public:
        void fireGameEvent(GameEvent* event) { purchaseList(event); }
    };

    static PurchaseEventListener listener;
    static bool listenerRegistered = false;

    if (config->misc.purchaseList.enabled && !listenerRegistered) {
        interfaces->gameEventManager->addListener(&listener, "item_purchase");
        listenerRegistered = true;
    } else if ((!config->misc.purchaseList.enabled || forceRemove) && listenerRegistered) {
        interfaces->gameEventManager->removeListener(&listener);
        listenerRegistered = false;
    }
}

void Misc::updateInput() noexcept
{
    config->misc.blockBotKey.handleToggle();
    config->misc.edgejumpkey.handleToggle();
    config->misc.edgebugkey.handleToggle();
    config->misc.jumpBugKey.handleToggle();
    config->misc.slowwalkKey.handleToggle();
    config->misc.fakeduckKey.handleToggle();
    config->misc.autoPeekKey.handleToggle();
    config->misc.prepareRevolverKey.handleToggle();
}

void Misc::reset(int resetType) noexcept
{
    if (resetType == 1)
    {
        static auto ragdollGravity = interfaces->cvar->findVar("cl_ragdoll_gravity");
        static auto blur = interfaces->cvar->findVar("@panorama_disable_blur");
        ragdollGravity->setValue(600);
        blur->setValue(0);
    }
}