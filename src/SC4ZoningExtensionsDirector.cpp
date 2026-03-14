#include "SC4ZoningExtensionsDirector.hpp"

#include "GZServPtrs.h"
#include "cIGZFrameWork.h"
#include "cIGZMessage2Standard.h"
#include "cIGZMessageServer2.h"
#include "cIGZWin.h"
#include "cISC4App.h"
#include "cISC4View3DWin.h"
#include "public/ImGuiPanelAdapter.h"
#include "public/ImGuiServiceIds.h"
#include "public/cIGZImGuiService.h"
#include "util/Logger.h"
#include "util/VersionDetection.h"
#include "zoning/ZoneDeveloperHooks.hpp"
#include "zoning/ZoneStatusPanel.hpp"
#include "zoning/ZoneViewInputControl.hpp"

namespace
{
    constexpr uint32_t kGZWin_WinSC4App = 0x6104489A;
    constexpr uint32_t kGZWin_SC4View3DWin = 0x9A47B417;
    constexpr uint32_t kZoneStatusPanelId = 0x57C44A14;
    constexpr int32_t kZoneStatusPanelOrder = 101;
}

SC4ZoningExtensionsDirector::~SC4ZoningExtensionsDirector() = default;

uint32_t SC4ZoningExtensionsDirector::GetDirectorID() const
{
    return kSC4ZoningExtensionsDirectorId;
}

bool SC4ZoningExtensionsDirector::DoMessage(cIGZMessage2* message)
{
    auto* standardMessage = static_cast<cIGZMessage2Standard*>(message);
    if (!standardMessage) {
        return false;
    }

    switch (standardMessage->GetType()) {
    case kSC4MessagePostCityInit:
        PostCityInit_();
        return true;
    case kSC4MessagePreCityShutdown:
        PreCityShutdown_();
        return true;
    default:
        return false;
    }
}

bool SC4ZoningExtensionsDirector::OnStart(cIGZCOM* com)
{
    cRZMessage2COMDirector::OnStart(com);

    if (mpFrameWork && !hookRegistered_) {
        hookRegistered_ = mpFrameWork->AddHook(this);
    }

    return true;
}

bool SC4ZoningExtensionsDirector::PostAppInit()
{
    Logger::Initialize("SC4ZoningExtensions", "", false);
    LOG_INFO("SC4ZoningExtensions initializing");
    LOG_INFO("Detected game version {}", VersionDetection::GetInstance().GetGameVersion());

    settings_.Load();
    toolState_.SetZoneDefaults(settings_.GetZoneDefaults());

    cIGZMessageServer2Ptr messageServer;
    if (messageServer) {
        const bool postInitRegistered = messageServer->AddNotification(this, kSC4MessagePostCityInit);
        const bool preShutdownRegistered = postInitRegistered &&
                                           messageServer->AddNotification(this, kSC4MessagePreCityShutdown);
        if (preShutdownRegistered) {
            messageServer_ = messageServer;
            messageServer_->AddRef();
        }
        else if (postInitRegistered) {
            messageServer->RemoveNotification(this, kSC4MessagePostCityInit);
        }
    }

    ZoneDeveloperHooks::HookContext hookContext{};
    hookContext.context = this;
    hookContext.invokeZoningTool = &SC4ZoningExtensionsDirector::InvokeZoningToolCallback_;
    hookContext.toolState = &toolState_;

    if (!ZoneDeveloperHooks::Install(hookContext)) {
        LOG_WARN("Zone hooks unavailable; stock zoning tool will remain active");
    }

    if (mpFrameWork &&
        mpFrameWork->GetSystemService(
            kImGuiServiceID,
            GZIID_cIGZImGuiService,
            reinterpret_cast<void**>(&imguiService_))) {
        LOG_INFO("Acquired ImGui service");
    }

    return true;
}

bool SC4ZoningExtensionsDirector::PostAppShutdown()
{
    PreCityShutdown_();

    ZoneDeveloperHooks::Uninstall();

    if (messageServer_) {
        messageServer_->RemoveNotification(this, kSC4MessagePostCityInit);
        messageServer_->RemoveNotification(this, kSC4MessagePreCityShutdown);
        messageServer_->Release();
        messageServer_ = nullptr;
    }

    ReleaseRenderServices_();

    if (mpFrameWork && hookRegistered_) {
        mpFrameWork->RemoveHook(this);
        hookRegistered_ = false;
    }

    LOG_INFO("SC4ZoningExtensions shutdown");
    Logger::Shutdown();
    return true;
}

bool SC4ZoningExtensionsDirector::InvokeZoningToolCallback_(void* context,
                                                            cISC4View3DWin* view3D,
                                                            const cISC4ZoneManager::ZoneType zoneType)
{
    auto* director = static_cast<SC4ZoningExtensionsDirector*>(context);
    return director ? director->HandleInvokeZoningTool_(view3D, zoneType) : false;
}

bool SC4ZoningExtensionsDirector::HandleInvokeZoningTool_(cISC4View3DWin* view3D,
                                                          const cISC4ZoneManager::ZoneType zoneType)
{
    if (!view3D) {
        return false;
    }

    if (zoneType == cISC4ZoneManager::ZoneType::None ||
        zoneType == cISC4ZoneManager::ZoneType::Landfill) {
        toolState_.SetToolActive(false);

        if (zoneControl_ && view3D->GetCurrentViewInputControl() == zoneControl_) {
            view3D->RemoveCurrentViewInputControl(false);
        }

        return false;
    }

    if (!EnsureZoneControl_(view3D)) {
        return false;
    }

    if (view3D->GetCurrentViewInputControl() == zoneControl_) {
        zoneControl_->SetWindow(view3D->AsIGZWin());
        zoneControl_->SetZoneType(zoneType);
        return true;
    }

    zoneControl_->SetZoneType(zoneType);

    if (!view3D->SetCurrentViewInputControl(
            zoneControl_,
            cISC4View3DWin::ViewInputControlStackOperation_RemoveCurrentControl)) {
        LOG_WARN("Failed to activate ZoneViewInputControl");
        return false;
    }

    return true;
}

void SC4ZoningExtensionsDirector::PostCityInit_()
{
    cISC4AppPtr app;
    cIGZWin* mainWindow = app ? app->GetMainWindow() : nullptr;
    cIGZWin* appWindow = mainWindow ? mainWindow->GetChildWindowFromID(kGZWin_WinSC4App) : nullptr;
    if (!appWindow) {
        return;
    }

    if (view3D_) {
        view3D_->Release();
        view3D_ = nullptr;
    }

    if (appWindow->GetChildAs(
            kGZWin_SC4View3DWin,
            kGZIID_cISC4View3DWin,
            reinterpret_cast<void**>(&view3D_))) {
        LOG_INFO("Acquired View3D");
        RegisterStatusPanel_();
    }
}

void SC4ZoningExtensionsDirector::PreCityShutdown_()
{
    toolState_.SetToolActive(false);
    UnregisterStatusPanel_();

    if (view3D_ && zoneControl_ && view3D_->GetCurrentViewInputControl() == zoneControl_) {
        view3D_->RemoveCurrentViewInputControl(false);
    }

    ReleaseZoneControl_();

    if (view3D_) {
        view3D_->Release();
        view3D_ = nullptr;
    }
}

bool SC4ZoningExtensionsDirector::EnsureZoneControl_(cISC4View3DWin* view3D)
{
    if (!view3D) {
        return false;
    }

    if (!zoneControl_) {
        auto* control = new ZoneViewInputControl(toolState_);
        control->AddRef();

        if (!control->Init()) {
            LOG_ERROR("Failed to initialize ZoneViewInputControl");
            control->Release();
            return false;
        }

        zoneControl_ = control;
    }

    zoneControl_->SetWindow(view3D->AsIGZWin());
    return true;
}

void SC4ZoningExtensionsDirector::ReleaseZoneControl_()
{
    if (!zoneControl_) {
        return;
    }

    zoneControl_->Shutdown();
    zoneControl_->Release();
    zoneControl_ = nullptr;
}

void SC4ZoningExtensionsDirector::RegisterStatusPanel_()
{
    if (!imguiService_ || statusPanelRegistered_) {
        return;
    }

    auto* panel = new ZoneStatusPanel(toolState_);
    const ImGuiPanelDesc desc =
        ImGuiPanelAdapter<ZoneStatusPanel>::MakeDesc(panel, kZoneStatusPanelId, kZoneStatusPanelOrder, true);

    if (!imguiService_->RegisterPanel(desc)) {
        delete panel;
        LOG_WARN("Failed to register zone status panel");
        return;
    }

    statusPanel_ = panel;
    statusPanelRegistered_ = true;
    LOG_INFO("Registered zone status panel");
}

void SC4ZoningExtensionsDirector::UnregisterStatusPanel_()
{
    if (!imguiService_ || !statusPanelRegistered_) {
        return;
    }

    imguiService_->UnregisterPanel(kZoneStatusPanelId);
    statusPanelRegistered_ = false;
    delete statusPanel_;
    statusPanel_ = nullptr;
}

void SC4ZoningExtensionsDirector::ReleaseRenderServices_()
{
    UnregisterStatusPanel_();

    if (imguiService_) {
        imguiService_->Release();
        imguiService_ = nullptr;
    }
}
