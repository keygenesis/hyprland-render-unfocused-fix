#define WLR_USE_UNSTABLE

#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <vector>

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigValue.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Popup.hpp>
#include <hyprland/src/desktop/view/WLSurface.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/helpers/time/Time.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#define private public
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/utils/ScopeGuard.hpp>
#undef private

#include "globals.hpp"

struct SForcedSurfaceVisibility {
    WP<CWLSurfaceResource> surface;
    CRegion                visibleRegion;
};

struct SSnapshot {
    PHLWINDOWREF window;
    CFramebuffer fb;
    bool         valid = false;
};

struct SPluginState {
    ~SPluginState() {
        if (snapshotTimer)
            snapshotTimer->cancel();

        g_pHyprRenderer->makeEGLCurrent();
        for (auto& snapshot : snapshots) {
            if (snapshot && snapshot->fb.isAllocated())
                snapshot->fb.release();
        }
    }

    CHyprSignalListener windowOpenHook;
    CHyprSignalListener windowCloseHook;
    CHyprSignalListener windowMoveHook;
    CHyprSignalListener windowActiveHook;
    CHyprSignalListener windowRulesHook;
    CHyprSignalListener workspaceActiveHook;
    CHyprSignalListener monitorFocusedHook;
    CHyprSignalListener configReloadHook;

    SP<CEventLoopTimer>      snapshotTimer;
    std::vector<UP<SSnapshot>> snapshots;

    bool                     hasValidSnapshot = false;
};

inline UP<SPluginState> g_pState;
inline bool             g_unloading = false;

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

static bool isRenderableRuleWindow(const PHLWINDOW& window) {
    if (!window || !window->m_workspace || !window->m_monitor || !window->m_ruleApplicator)
        return false;

    if (!window->m_isMapped || window->m_workspace->m_isSpecialWorkspace)
        return false;

    return window->m_ruleApplicator->renderUnfocused().valueOrDefault();
}

static std::vector<PHLWINDOW> getTrackedWindows() {
    const auto focusedMonitor   = Desktop::focusState()->monitor();
    const auto currentWorkspace = focusedMonitor ? focusedMonitor->m_activeWorkspace : nullptr;

    std::vector<PHLWINDOW> tracked;

    for (const auto& window : g_pCompositor->m_windows) {
        if (!isRenderableRuleWindow(window) || window->m_workspace == currentWorkspace)
            continue;

        tracked.push_back(window);
    }

    std::stable_sort(tracked.begin(), tracked.end(), [focusedMonitor](const auto& a, const auto& b) {
        const bool aOnFocused = focusedMonitor && a->m_monitor == focusedMonitor;
        const bool bOnFocused = focusedMonitor && b->m_monitor == focusedMonitor;

        if (aOnFocused != bOnFocused)
            return aOnFocused > bOnFocused;

        if (a->m_workspace->m_id != b->m_workspace->m_id)
            return a->m_workspace->m_id < b->m_workspace->m_id;

        if (a->m_class != b->m_class)
            return a->m_class < b->m_class;

        if (a->m_title != b->m_title)
            return a->m_title < b->m_title;

        return a.get() < b.get();
    });

    return tracked;
}

static uint32_t getFramebufferFormat(PHLMONITOR monitor) {
    if (!monitor)
        return DRM_FORMAT_ARGB8888;

    if (monitor->inHDR())
        return DRM_FORMAT_ABGR16161616F;

    return monitor->m_drmFormat == DRM_FORMAT_INVALID ? DRM_FORMAT_ARGB8888 : monitor->m_drmFormat;
}

static std::optional<Time::steady_dur> snapshotInterval() {
    static const auto PRENDERUNFOCUSEDFPS = CConfigValue<Hyprlang::INT>("misc:render_unfocused_fps");

    const auto fps = std::clamp<int>(*PRENDERUNFOCUSEDFPS, 1, 120);
    return std::chrono::microseconds(std::max(1, 1000000 / fps));
}

static void armSnapshotTimer() {
    if (g_unloading || !g_pState || !g_pState->snapshotTimer)
        return;

    if (getTrackedWindows().empty()) {
        g_pState->snapshotTimer->updateTimeout(std::nullopt);
        return;
    }

    g_pState->snapshotTimer->updateTimeout(snapshotInterval());
}

static void forceSurfaceVisibility(SP<CWLSurfaceResource> surface, std::vector<SForcedSurfaceVisibility>& forcedSurfaces) {
    if (!surface)
        return;

    const auto HLSURFACE = Desktop::View::CWLSurface::fromResource(surface);
    if (!HLSURFACE)
        return;

    forcedSurfaces.push_back({surface, HLSURFACE->m_visibleRegion});
    HLSURFACE->m_visibleRegion = {};
}

static void forceWindowSurfaceVisibility(PHLWINDOW window, std::vector<SForcedSurfaceVisibility>& forcedSurfaces) {
    if (!window || !window->wlSurface() || !window->wlSurface()->resource())
        return;

    window->wlSurface()->resource()->breadthfirst([&forcedSurfaces](SP<CWLSurfaceResource> surface, const Vector2D&, void*) { forceSurfaceVisibility(surface, forcedSurfaces); }, nullptr);

    if (window->m_isX11 || !window->m_popupHead)
        return;

    window->m_popupHead->breadthfirst([&forcedSurfaces](WP<Desktop::View::CPopup> popup, void*) {
        if (!popup || !popup->aliveAndVisible() || !popup->wlSurface() || !popup->wlSurface()->resource())
            return;

        popup->wlSurface()->resource()->breadthfirst([&forcedSurfaces](SP<CWLSurfaceResource> surface, const Vector2D&, void*) { forceSurfaceVisibility(surface, forcedSurfaces); }, nullptr);
    }, nullptr);
}

static void restoreSurfaceVisibility(std::vector<SForcedSurfaceVisibility>& forcedSurfaces) {
    for (auto& entry : forcedSurfaces) {
        const auto surface = entry.surface.lock();
        if (!surface)
            continue;

        const auto HLSURFACE = Desktop::View::CWLSurface::fromResource(surface);
        if (!HLSURFACE)
            continue;

        HLSURFACE->m_visibleRegion = entry.visibleRegion;
    }

    forcedSurfaces.clear();
}

static void clearSnapshot() {
    if (!g_pState)
        return;

    for (auto& snapshot : g_pState->snapshots) {
        if (snapshot && snapshot->fb.isAllocated())
            snapshot->fb.release();
    }

    g_pState->snapshots.clear();
    g_pState->hasValidSnapshot = false;
}

static void syncSnapshots(const std::vector<PHLWINDOW>& windows) {
    std::vector<UP<SSnapshot>> ordered;
    ordered.reserve(windows.size());

    for (const auto& window : windows) {
        auto it = std::find_if(g_pState->snapshots.begin(), g_pState->snapshots.end(), [&](const auto& snapshot) { return snapshot && snapshot->window == window; });

        if (it != g_pState->snapshots.end()) {
            ordered.push_back(std::move(*it));
            g_pState->snapshots.erase(it);
        } else {
            auto snapshot   = makeUnique<SSnapshot>();
            snapshot->window = window;
            ordered.push_back(std::move(snapshot));
        }
    }

    for (auto& snapshot : g_pState->snapshots) {
        if (snapshot && snapshot->fb.isAllocated())
            snapshot->fb.release();
    }

    g_pState->snapshots = std::move(ordered);
}

static bool redrawSnapshotNow() {
    if (g_unloading || !g_pState)
        return false;

    const auto windows = getTrackedWindows();
    if (windows.empty()) {
        clearSnapshot();
        return false;
    }

    syncSnapshots(windows);
    g_pHyprRenderer->makeEGLCurrent();
    g_pState->hasValidSnapshot = false;

    for (auto& snapshot : g_pState->snapshots) {
        if (!snapshot)
            continue;

        snapshot->valid = false;

        const auto window        = snapshot->window.lock();
        const auto sourceMonitor = window ? window->m_monitor.lock() : nullptr;

        if (!isRenderableRuleWindow(window) || !sourceMonitor)
            continue;

        const auto workspace = window->m_workspace;
        if (!workspace || sourceMonitor->m_pixelSize.x <= 0 || sourceMonitor->m_pixelSize.y <= 0)
            continue;

        const auto fbFormat = getFramebufferFormat(sourceMonitor);
        if (snapshot->fb.m_size != sourceMonitor->m_pixelSize || snapshot->fb.m_drmFormat != fbFormat) {
            snapshot->fb.release();
            snapshot->fb.alloc(sourceMonitor->m_pixelSize.x, sourceMonitor->m_pixelSize.y, fbFormat);
        }

        std::vector<SForcedSurfaceVisibility> forcedSurfaces;
        const bool                            wasVisible        = workspace->m_visible;
        const bool                            wasForceRendering = workspace->m_forceRendering;

        workspace->m_visible        = true;
        workspace->m_forceRendering = true;
        forceWindowSurfaceVisibility(window, forcedSurfaces);

        auto restoreState = Hyprutils::Utils::CScopeGuard([workspace, wasVisible, wasForceRendering, &forcedSurfaces] {
            workspace->m_visible        = wasVisible;
            workspace->m_forceRendering = wasForceRendering;
            restoreSurfaceVisibility(forcedSurfaces);
        });

        CRegion fakeDamage{0, 0, INT16_MAX, INT16_MAX};
        if (!g_pHyprRenderer->beginRender(sourceMonitor, fakeDamage, RENDER_MODE_FULL_FAKE, nullptr, &snapshot->fb))
            continue;

        g_pHyprOpenGL->clear(CHyprColor{0, 0, 0, 0});
        g_pHyprRenderer->renderWindow(window, sourceMonitor, Time::steadyNow(), true, RENDER_PASS_ALL, true, true);
        g_pHyprOpenGL->m_renderData.blockScreenShader = true;
        g_pHyprRenderer->endRender();

        snapshot->valid             = true;
        g_pState->hasValidSnapshot = true;
    }

    return g_pState->hasValidSnapshot;
}

static void refreshSnapshotNow() {
    redrawSnapshotNow();
    armSnapshotTimer();
}

static void scheduleBootstrapRefresh() {
    if (g_unloading)
        return;

    g_pEventLoopManager->doLater([] {
        if (g_unloading || !g_pState)
            return;

        refreshSnapshotNow();
    });
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        throw std::runtime_error("[ruf] Version mismatch");
    }

    g_unloading = false;
    g_pState    = makeUnique<SPluginState>();
    g_pState->snapshotTimer = makeShared<CEventLoopTimer>(std::nullopt, [](SP<CEventLoopTimer>, void*) {
        refreshSnapshotNow();
    }, nullptr);
    g_pEventLoopManager->addTimer(g_pState->snapshotTimer);

    g_pState->windowOpenHook = Event::bus()->m_events.window.open.listen([](PHLWINDOW) { refreshSnapshotNow(); });
    g_pState->windowCloseHook = Event::bus()->m_events.window.close.listen([](PHLWINDOW) { refreshSnapshotNow(); });
    g_pState->windowMoveHook = Event::bus()->m_events.window.moveToWorkspace.listen([](PHLWINDOW, PHLWORKSPACE) { refreshSnapshotNow(); });
    g_pState->windowActiveHook = Event::bus()->m_events.window.active.listen([](PHLWINDOW, Desktop::eFocusReason) { refreshSnapshotNow(); });
    g_pState->windowRulesHook = Event::bus()->m_events.window.updateRules.listen([](PHLWINDOW) { refreshSnapshotNow(); });
    g_pState->workspaceActiveHook = Event::bus()->m_events.workspace.active.listen([](PHLWORKSPACE) { refreshSnapshotNow(); });
    g_pState->monitorFocusedHook = Event::bus()->m_events.monitor.focused.listen([](PHLMONITOR) { refreshSnapshotNow(); });
    g_pState->configReloadHook = Event::bus()->m_events.config.reloaded.listen([] { scheduleBootstrapRefresh(); });

    scheduleBootstrapRefresh();

    return {"render-unfocused-fix", "Virtual offscreen rendering for renderUnfocused windows", "Daniel + Codex", "1.0"};
}

APICALL EXPORT void PLUGIN_EXIT() {
    g_unloading = true;

    if (g_pState && g_pState->snapshotTimer) {
        g_pState->snapshotTimer->cancel();
        g_pEventLoopManager->removeTimer(g_pState->snapshotTimer);
    }

    g_pState.reset();
}
