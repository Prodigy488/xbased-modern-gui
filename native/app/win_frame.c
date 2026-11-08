/*
 * xbased — the game's frame (win_frame.h): Windows, then Linux (X11).
 */
#include "native/app/win_frame.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define COBJMACROS
#include <windows.h>
#include <dwmapi.h>
#include <shobjidl.h>
#include "volk.h"
#include "hw/xbox/hle/vk/share.h"

#define WM_FRAME_TOGGLE (WM_APP + 1)
#define WM_FRAME_CLOSE  (WM_APP + 2)
#define FRAME_TICK_MS   50
#define FRAME_LINGER_MS 3000    /* no run's window in it this long: the session's over */
#define FRAME_WAIT_MS   10000   /* the first run's window this long at most */

/* ------------------------------------------------------------ the frame's process */

static HWND g_fw;
static bool g_fs, g_fs60, g_mode_set, g_had_child;
static RECT g_wrect;                /* windowed, the place to go back to */
static char g_dev[CCHDEVICENAME];   /* the monitor whose mode was set */
static ULONGLONG g_t0, g_gone_at;

static HWND top_child(void)
{
    return GetWindow(g_fw, GW_CHILD);
}

static void fit_children(void)
{
    RECT rc;
    GetClientRect(g_fw, &rc);
    for (HWND c = top_child(); c; c = GetWindow(c, GW_HWNDNEXT)) {
        RECT cr;
        GetWindowRect(c, &cr);
        if (cr.right - cr.left <= 1) {
            continue;   /* a run's window kept out of sight (the frame presents its pictures) */
        }
        SetWindowPos(c, NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

/* fsrate=60: the monitor's highest mode at its size that's a whole number
 * of 60 Hz (59 too: 59.94), no faster than now; the current one if it's
 * one already (win_main.c fs_mode's, in Win32 terms) */
static void mode_set(const char *dev)
{
    DEVMODEA cur;
    memset(&cur, 0, sizeof(cur));
    cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsA(dev, ENUM_CURRENT_SETTINGS, &cur)) {
        return;
    }
    const DWORD f0 = cur.dmDisplayFrequency;
    if (f0 % 60 == 0 || (f0 + 1) % 60 == 0) {
        return;
    }
    DEVMODEA best, dm;
    bool have = false;
    for (DWORD i = 0;; i++) {
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        if (!EnumDisplaySettingsA(dev, i, &dm)) {
            break;
        }
        const DWORD f = dm.dmDisplayFrequency;
        if (dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight &&
            dm.dmBitsPerPel == cur.dmBitsPerPel && !(dm.dmDisplayFlags & DM_INTERLACED) && f <= f0 && f >= 59 &&
            f <= 241 && (f % 60 == 0 || (f + 1) % 60 == 0) && (!have || f > best.dmDisplayFrequency)) {
            best = dm;
            have = true;
        }
    }
    if (have) {
        best.dmFields &= DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;
        if (ChangeDisplaySettingsExA(dev, &best, NULL, CDS_FULLSCREEN, NULL) == DISP_CHANGE_SUCCESSFUL) {
            g_mode_set = true;
            snprintf(g_dev, sizeof(g_dev), "%s", dev);
        }
    }
}

static void mode_restore(void)
{
    if (g_mode_set) {
        ChangeDisplaySettingsExA(g_dev, NULL, NULL, 0, NULL);
        g_mode_set = false;
    }
}

/* The taskbar told the frame's a full screen window: it stays under it.
 * Left to its own look, the taskbar came on top for a moment after the
 * frame took the foreground (the run's window coming into it), over the
 * game on screens where it doesn't hide itself. */
static void mark_fullscreen(bool on)
{
    static ITaskbarList2 *tb;
    static bool tried;
    if (!tried) {
        tried = true;
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        if (FAILED(CoCreateInstance(&CLSID_TaskbarList, NULL, CLSCTX_INPROC_SERVER, &IID_ITaskbarList2, (void **)&tb))) {
            tb = NULL;
        } else if (FAILED(ITaskbarList2_HrInit(tb))) {
            ITaskbarList2_Release(tb);
            tb = NULL;
        }
    }
    if (tb) {
        ITaskbarList2_MarkFullscreenWindow(tb, g_fw, on ? TRUE : FALSE);
    }
}

static void set_fullscreen(bool on)
{
    if (on == g_fs) {
        return;
    }
    mark_fullscreen(on);
    if (on) {
        GetWindowRect(g_fw, &g_wrect);
        MONITORINFOEXA mi;
        memset(&mi, 0, sizeof(mi));
        mi.cbSize = sizeof(mi);
        GetMonitorInfoA(MonitorFromWindow(g_fw, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi);
        if (g_fs60) {
            mode_set(mi.szDevice);
            GetMonitorInfoA(MonitorFromWindow(g_fw, MONITOR_DEFAULTTONEAREST), (MONITORINFO *)&mi);
        }
        const LONG_PTR vis = IsWindowVisible(g_fw) ? WS_VISIBLE : 0;   /* not shown by this, if it wasn't yet */
        SetWindowLongPtrA(g_fw, GWL_STYLE, WS_POPUP | vis | WS_CLIPCHILDREN);
        const RECT r = mi.rcMonitor;
        SetWindowPos(g_fw, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_FRAMECHANGED | SWP_NOACTIVATE);
    } else {
        mode_restore();
        SetWindowLongPtrA(g_fw, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN);
        const RECT r = g_wrect;
        SetWindowPos(g_fw, HWND_NOTOPMOST, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_FRAMECHANGED);
    }
    g_fs = on;
    fit_children();
}


/* ------------------------------------------------------------ the frame's presenter */

/* The runs' pictures (hw/xbox/hle/vk/share.h) presented to the frame's own
 * window, on a thread of its own: the newest a run has drawn, copied into
 * the swapchain's image and presented (FIFO, or immediate, as the run
 * asks); the driver sees this process presenting to a full screen window,
 * so independent flip, G-Sync and FreeSync apply as for any game. */

static HANDLE g_map, g_ev;
static HleShare *g_sh;
static volatile LONG g_presenting;  /* the presenter draws the window: no GDI black over it */

#define FP_SLOTS 3

static struct {
    bool up, failed;
    VkInstance inst;
    VkSurfaceKHR surf;
    VkPhysicalDevice phys;
    VkDevice dev;
    VkQueue q;
    uint32_t qfam;
    VkPhysicalDeviceMemoryProperties mem;
    VkSwapchainKHR sc;
    uint32_t nimg, sw, sh, mode;
    VkImage simg[8];
    VkSemaphore done[8];        /* a swapchain image's copy done: its present waits */
    VkCommandPool pool;
    VkCommandBuffer cb[FP_SLOTS];
    VkFence fence[FP_SLOTS];
    VkSemaphore acq[FP_SLOTS];
    uint32_t slot;
    /* the source's images, opened */
    int src;                    /* its slot in the block, -1 none */
    int64_t gen;
    uint32_t w, h;
    VkImage img[HLE_SHARE_IMAGES];
    VkDeviceMemory imem[HLE_SHARE_IMAGES];
    VkSemaphore drawn, freed;
    uint64_t shown;
} P = {.src = -1};

static void fp_fail(const char *why)
{
    (void)why;
    P.failed = true;
    if (g_sh) {
        g_sh->failed = 1;       /* the runs present to their own windows */
    }
    InterlockedExchange(&g_presenting, 0);
}

static bool fp_instance(void)
{
    if (volkInitialize() != VK_SUCCESS) {
        return false;
    }
    const char *ie[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkApplicationInfo ai = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "xbased frame",
                            .apiVersion = VK_API_VERSION_1_2};
    VkInstanceCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &ai,
                                .enabledExtensionCount = 2, .ppEnabledExtensionNames = ie};
    if (vkCreateInstance(&ici, NULL, &P.inst) != VK_SUCCESS) {
        return false;
    }
    volkLoadInstance(P.inst);
    VkWin32SurfaceCreateInfoKHR sci = {.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
                                       .hinstance = GetModuleHandleA(NULL), .hwnd = g_fw};
    return vkCreateWin32SurfaceKHR(P.inst, &sci, NULL, &P.surf) == VK_SUCCESS;
}

/* the run's GPU (its LUID), a queue that presents to the window */
static bool fp_device(const uint8_t luid[8])
{
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(P.inst, &n, NULL);
    VkPhysicalDevice pd[16];
    n = n > 16 ? 16 : n;
    vkEnumeratePhysicalDevices(P.inst, &n, pd);
    for (uint32_t i = 0; i < n && !P.phys; i++) {
        VkPhysicalDeviceIDProperties idp = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
        VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &idp};
        vkGetPhysicalDeviceProperties2(pd[i], &p2);
        if (idp.deviceLUIDValid && !memcmp(idp.deviceLUID, luid, 8) && p2.properties.apiVersion >= VK_API_VERSION_1_2) {
            P.phys = pd[i];
        }
    }
    if (!P.phys) {
        return false;
    }
    vkGetPhysicalDeviceMemoryProperties(P.phys, &P.mem);
    uint32_t nq = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(P.phys, &nq, NULL);
    VkQueueFamilyProperties qp[16];
    nq = nq > 16 ? 16 : nq;
    vkGetPhysicalDeviceQueueFamilyProperties(P.phys, &nq, qp);
    P.qfam = UINT32_MAX;
    for (uint32_t i = 0; i < nq && P.qfam == UINT32_MAX; i++) {
        VkBool32 ok = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(P.phys, i, P.surf, &ok);
        if (ok && (qp[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_TRANSFER_BIT))) {
            P.qfam = i;
        }
    }
    if (P.qfam == UINT32_MAX) {
        return false;
    }
    const char *de[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME,
                        VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME};
    VkPhysicalDeviceVulkan12Features f12 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
                                            .timelineSemaphore = VK_TRUE};
    VkPhysicalDeviceSynchronization2Features s2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
                                                   .pNext = &f12, .synchronization2 = VK_TRUE};
    const float prio = 1.0f;
    VkDeviceQueueCreateInfo qci = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, .queueFamilyIndex = P.qfam,
                                   .queueCount = 1, .pQueuePriorities = &prio};
    const char *de2[4] = {de[0], de[1], de[2], VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME};
    VkDeviceCreateInfo dci = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, .pNext = &s2, .queueCreateInfoCount = 1,
                              .pQueueCreateInfos = &qci, .enabledExtensionCount = 4, .ppEnabledExtensionNames = de2};
    if (vkCreateDevice(P.phys, &dci, NULL, &P.dev) != VK_SUCCESS) {
        return false;
    }
    volkLoadDevice(P.dev);
    vkGetDeviceQueue(P.dev, P.qfam, 0, &P.q);
    VkCommandPoolCreateInfo pci = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                   .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, .queueFamilyIndex = P.qfam};
    vkCreateCommandPool(P.dev, &pci, NULL, &P.pool);
    VkCommandBufferAllocateInfo cai = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, .commandPool = P.pool,
                                       .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY, .commandBufferCount = FP_SLOTS};
    vkAllocateCommandBuffers(P.dev, &cai, P.cb);
    for (int i = 0; i < FP_SLOTS; i++) {
        VkFenceCreateInfo fci = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, .flags = VK_FENCE_CREATE_SIGNALED_BIT};
        vkCreateFence(P.dev, &fci, NULL, &P.fence[i]);
        VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(P.dev, &sci, NULL, &P.acq[i]);
    }
    return true;
}

static void fp_drop_swapchain(void)
{
    if (!P.sc) {
        return;
    }
    vkDeviceWaitIdle(P.dev);
    for (uint32_t i = 0; i < P.nimg; i++) {
        vkDestroySemaphore(P.dev, P.done[i], NULL);
    }
    vkDestroySwapchainKHR(P.dev, P.sc, NULL);
    P.sc = VK_NULL_HANDLE;
    P.nimg = 0;
}

/* the window's swapchain: its size, FIFO (vsync) or immediate (else mailbox, else FIFO) */
static bool fp_swapchain(uint32_t vsync)
{
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(P.phys, P.surf, &caps) != VK_SUCCESS) {
        return false;
    }
    uint32_t w = caps.currentExtent.width, h = caps.currentExtent.height;
    if (w == UINT32_MAX) {
        w = g_sh->cw;
        h = g_sh->ch;
    }
    if (!w || !h) {
        return false;   /* minimized */
    }
    VkPresentModeKHR pm = VK_PRESENT_MODE_FIFO_KHR;
    if (!vsync) {
        uint32_t nm = 0;
        VkPresentModeKHR modes[16];
        vkGetPhysicalDeviceSurfacePresentModesKHR(P.phys, P.surf, &nm, NULL);
        nm = nm > 16 ? 16 : nm;
        vkGetPhysicalDeviceSurfacePresentModesKHR(P.phys, P.surf, &nm, modes);
        for (uint32_t i = 0; i < nm; i++) {
            if (modes[i] == VK_PRESENT_MODE_IMMEDIATE_KHR) {
                pm = modes[i];
            } else if (modes[i] == VK_PRESENT_MODE_MAILBOX_KHR && pm == VK_PRESENT_MODE_FIFO_KHR) {
                pm = modes[i];
            }
        }
    }
    uint32_t nf = 0;
    VkSurfaceFormatKHR fm[32];
    vkGetPhysicalDeviceSurfaceFormatsKHR(P.phys, P.surf, &nf, NULL);
    nf = nf > 32 ? 32 : nf;
    vkGetPhysicalDeviceSurfaceFormatsKHR(P.phys, P.surf, &nf, fm);
    VkSurfaceFormatKHR fmt = fm[0];
    for (uint32_t i = 0; i < nf; i++) {
        if (fm[i].format == VK_FORMAT_B8G8R8A8_UNORM && fm[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            fmt = fm[i];
        }
    }
    uint32_t n = caps.minImageCount + 1;
    if (caps.maxImageCount && n > caps.maxImageCount) {
        n = caps.maxImageCount;
    }
    VkSwapchainKHR old = P.sc;
    VkSwapchainCreateInfoKHR ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, .surface = P.surf, .minImageCount = n,
        .imageFormat = fmt.format, .imageColorSpace = fmt.colorSpace, .imageExtent = {w, h}, .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT, .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = caps.currentTransform, .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = pm, .clipped = VK_TRUE, .oldSwapchain = old};
    if (old) {
        vkDeviceWaitIdle(P.dev);
        for (uint32_t i = 0; i < P.nimg; i++) {
            vkDestroySemaphore(P.dev, P.done[i], NULL);
        }
    }
    VkSwapchainKHR sc = VK_NULL_HANDLE;
    const VkResult r = vkCreateSwapchainKHR(P.dev, &ci, NULL, &sc);
    if (old) {
        vkDestroySwapchainKHR(P.dev, old, NULL);
    }
    P.sc = r == VK_SUCCESS ? sc : VK_NULL_HANDLE;
    P.nimg = 0;
    if (!P.sc) {
        return false;
    }
    uint32_t ni = 8;
    vkGetSwapchainImagesKHR(P.dev, P.sc, &ni, P.simg);
    for (uint32_t i = 0; i < ni; i++) {
        VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        vkCreateSemaphore(P.dev, &sci, NULL, &P.done[i]);
    }
    P.nimg = ni;
    P.sw = w;
    P.sh = h;
    P.mode = vsync;
    return true;
}

static void fp_drop_source(void)
{
    if (P.src < 0) {
        return;
    }
    vkDeviceWaitIdle(P.dev);
    for (uint32_t i = 0; i < HLE_SHARE_IMAGES; i++) {
        if (P.img[i]) {
            vkDestroyImage(P.dev, P.img[i], NULL);
        }
        if (P.imem[i]) {
            vkFreeMemory(P.dev, P.imem[i], NULL);
        }
        P.img[i] = VK_NULL_HANDLE;
        P.imem[i] = VK_NULL_HANDLE;
    }
    if (P.drawn) {
        vkDestroySemaphore(P.dev, P.drawn, NULL);
    }
    if (P.freed) {
        vkDestroySemaphore(P.dev, P.freed, NULL);
    }
    P.drawn = P.freed = VK_NULL_HANDLE;
    P.src = -1;
    P.gen = 0;
}

static bool fp_import_sem(VkSemaphore *sem, const uint16_t *name)
{
    VkSemaphoreTypeCreateInfo tci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                     .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE};
    VkSemaphoreCreateInfo sci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &tci};
    if (vkCreateSemaphore(P.dev, &sci, NULL, sem) != VK_SUCCESS) {
        return false;
    }
    VkImportSemaphoreWin32HandleInfoKHR ii = {.sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR,
                                              .semaphore = *sem,
                                              .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT,
                                              .name = (LPCWSTR)name};
    return vkImportSemaphoreWin32HandleKHR(P.dev, &ii) == VK_SUCCESS;
}

/* source k's images and timelines, by their names */
static bool fp_import(int k)
{
    fp_drop_source();
    HleShareSrc *s = &g_sh->src[k];
    const int64_t gen = s->gen;
    if (!gen || !fp_import_sem(&P.drawn, s->drawn) || !fp_import_sem(&P.freed, s->freed)) {
        return false;
    }
    for (uint32_t i = 0; i < HLE_SHARE_IMAGES; i++) {
        VkExternalMemoryImageCreateInfo xi = {.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                                              .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT};
        VkImageCreateInfo ici = {.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, .pNext = &xi, .imageType = VK_IMAGE_TYPE_2D,
                                 .format = (VkFormat)s->fmt, .extent = {s->w, s->h, 1}, .mipLevels = 1, .arrayLayers = 1,
                                 .samples = VK_SAMPLE_COUNT_1_BIT, .tiling = VK_IMAGE_TILING_OPTIMAL,
                                 .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT,
                                 .sharingMode = VK_SHARING_MODE_EXCLUSIVE, .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
        if (vkCreateImage(P.dev, &ici, NULL, &P.img[i]) != VK_SUCCESS) {
            return false;
        }
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(P.dev, P.img[i], &mr);
        uint32_t type = UINT32_MAX;
        for (uint32_t t = 0; t < P.mem.memoryTypeCount && type == UINT32_MAX; t++) {
            if ((mr.memoryTypeBits & (1u << t)) &&
                (P.mem.memoryTypes[t].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                type = t;
            }
        }
        VkImportMemoryWin32HandleInfoKHR ii = {.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR,
                                               .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT,
                                               .name = (LPCWSTR)s->mem[i]};
        VkMemoryDedicatedAllocateInfo di = {.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, .pNext = &ii,
                                            .image = P.img[i]};
        VkMemoryAllocateInfo ai = {.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, .pNext = &di,
                                   .allocationSize = s->size[i] ? s->size[i] : mr.size, .memoryTypeIndex = type};
        if (type == UINT32_MAX || vkAllocateMemory(P.dev, &ai, NULL, &P.imem[i]) != VK_SUCCESS ||
            vkBindImageMemory(P.dev, P.img[i], P.imem[i], 0) != VK_SUCCESS) {
            return false;
        }
    }
    if (s->gen != gen) {
        return false;   /* made again meanwhile: next time */
    }
    P.src = k;
    P.gen = gen;
    P.w = s->w;
    P.h = s->h;
    P.shown = 0;
    return true;
}

/* the newest source with a picture: the run started last (the one before
 * goes on drawing until then, and is shown until then) */
static int fp_source(void)
{
    int best = -1;
    for (int k = 0; k < 2; k++) {
        const HleShareSrc *s = &g_sh->src[k];
        if (s->gen && s->latest && (best < 0 || s->order > g_sh->src[best].order)) {
            best = k;
        }
    }
    return best;
}

static void fp_barrier(VkCommandBuffer cb, VkImage img, VkImageLayout from, VkImageLayout to, uint32_t sq, uint32_t dq,
                       VkPipelineStageFlags2 ss, VkAccessFlags2 sa, VkPipelineStageFlags2 ds, VkAccessFlags2 da)
{
    VkImageMemoryBarrier2 b = {.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2, .srcStageMask = ss, .srcAccessMask = sa,
                               .dstStageMask = ds, .dstAccessMask = da, .oldLayout = from, .newLayout = to,
                               .srcQueueFamilyIndex = sq, .dstQueueFamilyIndex = dq, .image = img,
                               .subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1}};
    VkDependencyInfo di = {.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1,
                           .pImageMemoryBarriers = &b};
    vkCmdPipelineBarrier2(cb, &di);
}

/* picture v of image i to the window */
static bool fp_present(uint32_t i, uint64_t v)
{
    const uint32_t vs = g_sh->src[P.src].vsync;
    RECT rc;
    GetClientRect(g_fw, &rc);
    if (!P.sc || P.mode != vs || P.sw != (uint32_t)(rc.right - rc.left) || P.sh != (uint32_t)(rc.bottom - rc.top)) {
        if (!fp_swapchain(vs)) {
            return false;
        }
    }
    const uint32_t k = P.slot++ % FP_SLOTS;
    vkWaitForFences(P.dev, 1, &P.fence[k], VK_TRUE, 1000000000ull);
    uint32_t si = 0;
    VkResult r = vkAcquireNextImageKHR(P.dev, P.sc, 100000000ull, P.acq[k], VK_NULL_HANDLE, &si);
    if (r == VK_ERROR_OUT_OF_DATE_KHR) {
        fp_drop_swapchain();
        return false;
    }
    if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
        return false;
    }
    vkResetFences(P.dev, 1, &P.fence[k]);
    VkCommandBuffer cb = P.cb[k];
    vkResetCommandBuffer(cb, 0);
    VkCommandBufferBeginInfo bi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                   .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    vkBeginCommandBuffer(cb, &bi);
    const VkImage src = P.img[i], dst = P.simg[si];
    /* the run's image ours (from the external queue family), the swapchain's for the copy */
    fp_barrier(cb, src, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_QUEUE_FAMILY_EXTERNAL, P.qfam,
               VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
    fp_barrier(cb, dst, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_QUEUE_FAMILY_IGNORED,
               VK_QUEUE_FAMILY_IGNORED, VK_PIPELINE_STAGE_2_BLIT_BIT, 0, VK_PIPELINE_STAGE_2_BLIT_BIT,
               VK_ACCESS_2_TRANSFER_WRITE_BIT);
    if (P.w == P.sw && P.h == P.sh) {
        VkImageCopy c = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                         .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}, .extent = {P.w, P.h, 1}};
        vkCmdCopyImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    } else {
        /* a size changing (the window's, before the run's next pictures): scaled meanwhile */
        VkImageBlit b = {.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                         .srcOffsets = {{0, 0, 0}, {(int32_t)P.w, (int32_t)P.h, 1}},
                         .dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
                         .dstOffsets = {{0, 0, 0}, {(int32_t)P.sw, (int32_t)P.sh, 1}}};
        vkCmdBlitImage(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b,
                       VK_FILTER_LINEAR);
    }
    fp_barrier(cb, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_QUEUE_FAMILY_IGNORED,
               VK_QUEUE_FAMILY_IGNORED, VK_PIPELINE_STAGE_2_BLIT_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_2_NONE, 0);
    fp_barrier(cb, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL, P.qfam, VK_QUEUE_FAMILY_EXTERNAL,
               VK_PIPELINE_STAGE_2_BLIT_BIT, 0, VK_PIPELINE_STAGE_2_NONE, 0);
    vkEndCommandBuffer(cb);
    VkSemaphoreSubmitInfo wait[2] = {
        {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = P.drawn, .value = v,
         .stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT},
        {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = P.acq[k],
         .stageMask = VK_PIPELINE_STAGE_2_BLIT_BIT}};
    VkSemaphoreSubmitInfo sig[2] = {
        {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = P.freed, .value = v,
         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT},
        {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO, .semaphore = P.done[si],
         .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT}};
    VkCommandBufferSubmitInfo cbi = {.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = cb};
    VkSubmitInfo2 sub = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2, .waitSemaphoreInfoCount = 2, .pWaitSemaphoreInfos = wait,
                         .commandBufferInfoCount = 1, .pCommandBufferInfos = &cbi, .signalSemaphoreInfoCount = 2,
                         .pSignalSemaphoreInfos = sig};
    if (vkQueueSubmit2(P.q, 1, &sub, P.fence[k]) != VK_SUCCESS) {
        return false;
    }
    VkPresentInfoKHR pi = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, .waitSemaphoreCount = 1,
                           .pWaitSemaphores = &P.done[si], .swapchainCount = 1, .pSwapchains = &P.sc,
                           .pImageIndices = &si};
    r = vkQueuePresentKHR(P.q, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) {
        fp_drop_swapchain();
    }
    P.shown = v;
    g_sh->src[P.src].shown = (int64_t)v;
    if (!g_presenting) {
        InterlockedExchange(&g_presenting, 1);
    }
    return true;
}

static DWORD WINAPI fp_thread(void *arg)
{
    (void)arg;
    for (;;) {
        WaitForSingleObject(g_ev, 50);
        const int k = fp_source();
        if (k < 0) {
            continue;
        }
        HleShareSrc *s = &g_sh->src[k];
        /* Vulkan from the first picture on, not as the window comes up:
         * loading the driver holds the process's loader lock a while, and
         * the window's thread, waiting on it, painted nothing meanwhile (the
         * desktop and the taskbar through it for a moment) */
        if (!P.inst && !fp_instance()) {
            fp_fail("no Vulkan instance or surface");
            return 0;
        }
        if (!P.dev) {
            if (!s->luid_valid || !fp_device(s->luid)) {
                fp_fail("no device for the run's GPU");
                return 0;
            }
        }
        if (k != P.src || s->gen != P.gen) {
            if (!fp_import(k)) {
                fp_drop_source();
                continue;   /* being made again: the next picture */
            }
        }
        const int64_t latest = s->latest;
        const uint64_t v = (uint64_t)latest >> 2;
        if (!v || v <= P.shown) {
            continue;
        }
        fp_present((uint32_t)(latest & 3), v);
    }
}

static void fp_start(const char *token)
{
    wchar_t n[128];
    swprintf(n, 128, HLE_SHARE_BLOCK_FMT, token);
    g_map = CreateFileMappingW(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(HleShare), n);
    g_sh = g_map ? (HleShare *)MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(HleShare)) : NULL;
    swprintf(n, 128, HLE_SHARE_EVENT_FMT, token);
    g_ev = CreateEventW(NULL, FALSE, FALSE, n);
    if (!g_sh || !g_ev) {
        return;
    }
    memset(g_sh, 0, sizeof(*g_sh));
    RECT rc;
    GetClientRect(g_fw, &rc);
    g_sh->cw = (uint32_t)(rc.right - rc.left);
    g_sh->ch = (uint32_t)(rc.bottom - rc.top);
    g_sh->version = HLE_SHARE_VERSION;
    g_sh->magic = HLE_SHARE_MAGIC;      /* last: the runs open it from now on */
    CloseHandle(CreateThread(NULL, 0, fp_thread, NULL, 0, NULL));
}

static void frame_mouse(uint32_t type, LPARAM l, int button, int wheel)
{
    if (!g_sh) {
        return;
    }
    POINT pt = {(short)LOWORD(l), (short)HIWORD(l)};
    if (type == 4) {
        ScreenToClient(g_fw, &pt);  /* the wheel's is the screen's */
    }
    const int64_t at = g_sh->mouse_head;
    HleShareMouse *m = &g_sh->mouse[at % HLE_SHARE_MICE];
    m->type = type;
    m->x = pt.x;
    m->y = pt.y;
    m->button = button;
    m->wheel = wheel;
    InterlockedExchange64((volatile LONG64 *)&g_sh->mouse_head, at + 1);
}

/* The session's over: the dashboard the game came from shown and in front
 * before the frame goes (this process has the foreground, it may hand it
 * on; the game's couldn't always). The frame gone first, the desktop and the
 * taskbar showed until the dashboard was back. */
static void hand_back(void)
{
    const char *e = getenv("XBASED_DASH_HWND");
    HWND d = e && e[0] ? (HWND)(uintptr_t)strtoull(e, NULL, 16) : NULL;
    if (d && IsWindow(d)) {
        ShowWindow(d, SW_SHOW);
        SetForegroundWindow(d);
    }
}

static LRESULT CALLBACK frame_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    switch (m) {
    case WM_ERASEBKGND: {
        if (g_presenting) {
            return 1;   /* the presenter's pictures: no GDI over them */
        }
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)w, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
    }
    case WM_SIZE:
        fit_children();
        if (g_sh && LOWORD(l) && HIWORD(l)) {
            g_sh->cw = LOWORD(l);   /* the runs draw at this size from their next picture */
            g_sh->ch = HIWORD(l);
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(l) == HTCLIENT && g_presenting) {
            SetCursor(g_sh && g_sh->cursor ? LoadCursorW(NULL, (LPCWSTR)IDC_ARROW) : NULL);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        frame_mouse(1, l, 0, 0);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
        frame_mouse(2, l, m == WM_LBUTTONDOWN ? 1 : m == WM_MBUTTONDOWN ? 2 : 3, 0);
        return 0;
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
    case WM_MBUTTONUP:
        frame_mouse(3, l, m == WM_LBUTTONUP ? 1 : m == WM_MBUTTONUP ? 2 : 3, 0);
        return 0;
    case WM_MOUSEWHEEL:
        frame_mouse(4, l, 0, GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA);
        return 0;
    case WM_SYSCOMMAND:
        /* the system asks the window in front before starting the screen
         * saver or turning the panel off: never while a game's on (a pad
         * doesn't count as input), as SDL's own windows answer */
        if ((w & 0xFFF0) == SC_SCREENSAVE || (w & 0xFFF0) == SC_MONITORPOWER) {
            return 0;
        }
        break;
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE && top_child()) {
            SetFocus(top_child());     /* the keyboard to the run's window */
            return 0;
        }
        break;
    case WM_SETFOCUS:
        if (top_child()) {
            SetFocus(top_child());
            return 0;
        }
        break;
    case WM_CLOSE:
        /* the window's close button: the run's own quit (its window's) */
        if (top_child()) {
            for (HWND c = top_child(); c; c = GetWindow(c, GW_HWNDNEXT)) {
                PostMessageA(c, WM_CLOSE, 0, 0);
            }
            return 0;
        }
        DestroyWindow(h);
        return 0;
    case WM_DPICHANGED: {
        const RECT *r = (const RECT *)l;
        if (!g_fs) {
            SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    case WM_FRAME_TOGGLE:
        set_fullscreen(!g_fs);
        return 0;
    case WM_FRAME_CLOSE:
        hand_back();
        ShowWindow(h, SW_HIDE);
        DestroyWindow(h);
        return 0;
    case WM_TIMER: {
        /* a run's window in it, or none: gone a moment between two runs
         * never (the next one is in before this one goes), so none for a
         * while is the session's end */
        const ULONGLONG now = GetTickCount64();
        if (top_child()) {
            g_had_child = true;
            g_gone_at = 0;
        } else if (g_had_child) {
            if (!g_gone_at) {
                g_gone_at = now;
                mode_restore();
                hand_back();                /* the run went without a word (it fell): the dashboard */
                ShowWindow(h, SW_HIDE);
            } else if (now - g_gone_at > FRAME_LINGER_MS) {
                DestroyWindow(h);
            }
        } else if (now - g_t0 > FRAME_WAIT_MS) {
            DestroyWindow(h);   /* the first run never came */
        }
        return 0;
    }
    case WM_DESTROY:
        mode_restore();
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(h, m, w, l);
}

int nc_frame_main(int argc, char **argv)
{
    if (argc < 6) {
        return 2;
    }
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const char *token = argv[2];
    const bool fs = atoi(argv[3]) != 0;
    g_fs60 = atoi(argv[4]) != 0;
    wchar_t title[512];
    if (!MultiByteToWideChar(CP_UTF8, 0, argv[5], -1, title, 512)) {
        wcscpy(title, L"xbased");
    }
    char cls[96];
    snprintf(cls, sizeof(cls), "xbased_frame_%s", token);
    HINSTANCE hi = GetModuleHandleA(NULL);
    wchar_t wcls[96];
    MultiByteToWideChar(CP_UTF8, 0, cls, -1, wcls, 96);
    WNDCLASSEXW wc;
    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = frame_proc;
    wc.hInstance = hi;
    wc.hIcon = LoadIconW(hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = wcls;
    if (!RegisterClassExW(&wc)) {
        return 3;
    }
    /* a window's size as the run's own was (1280x960 at the desktop's
     * scale), centered */
    const UINT dpi = GetDpiForSystem();
    RECT r = {0, 0, MulDiv(1280, (int)dpi, 96), MulDiv(960, (int)dpi, 96)};
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
    const int ww = r.right - r.left, wh = r.bottom - r.top;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoA(MonitorFromPoint((POINT){0, 0}, MONITOR_DEFAULTTOPRIMARY), &mi);
    const int x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - ww) / 2;
    const int y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - wh) / 2;
    g_fw = CreateWindowExW(0, wcls, title, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, x, y, ww, wh, NULL, NULL, hi, NULL);
    if (!g_fw) {
        return 4;
    }
    g_t0 = GetTickCount64();
    fp_start(token);    /* the runs' pictures presented here (hw/xbox/hle/vk/share.h) */
    if (fs) {
        set_fullscreen(true);   /* before it's shown: up as a window first, the desktop showed around it */
    }
    /* painted before it's seen: shown cloaked, painted black, then in
     * sight (a window shows nothing until it has painted: what's behind
     * it, the desktop as the dashboard goes, showed through) */
    BOOL cloak = TRUE;
    DwmSetWindowAttribute(g_fw, DWMWA_TRANSITIONS_FORCEDISABLED, &cloak, sizeof(cloak));
    DwmSetWindowAttribute(g_fw, DWMWA_CLOAK, &cloak, sizeof(cloak));
    ShowWindow(g_fw, SW_SHOW);
    UpdateWindow(g_fw);
    cloak = FALSE;
    DwmSetWindowAttribute(g_fw, DWMWA_CLOAK, &cloak, sizeof(cloak));
    SetForegroundWindow(g_fw);
    SetTimer(g_fw, 1, FRAME_TICK_MS, NULL);
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    mode_restore();
    return 0;
}

/* ------------------------------------------------------------ the game's side */

void *nc_frame_get(const char *title, bool fullscreen, bool fs60)
{
    const char *had = getenv("XBASED_FRAME_HWND");
    if (had && had[0]) {
        HWND h = (HWND)(uintptr_t)strtoull(had, NULL, 16);
        if (IsWindow(h)) {
            return h;     /* the session's, a run started again */
        }
    }
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    char token[48], cmd[MAX_PATH * 2 + 600], t[400];
    snprintf(token, sizeof(token), "%lu_%llu", (unsigned long)GetCurrentProcessId(),
             (unsigned long long)GetTickCount64());
    snprintf(t, sizeof(t), "%s", title && title[0] ? title : "xbased");
    for (char *p = t; *p; p++) {
        if (*p == '"') {
            *p = '\'';
        }
    }
    snprintf(cmd, sizeof(cmd), "\"%s\" frame %s %d %d \"%s\"", exe, token, fullscreen ? 1 : 0, fs60 ? 1 : 0, t);
    STARTUPINFOA si;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    /* suspended until it may take the foreground: up first, it asked before
     * this process let it, Windows refused and flashed its taskbar button
     * instead (the taskbar over the full screen for a moment) */
    if (!CreateProcessA(exe, cmd, NULL, NULL, FALSE, CREATE_SUSPENDED, NULL, NULL, &si, &pi)) {
        return NULL;
    }
    AllowSetForegroundWindow(pi.dwProcessId);
    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    char cls[96];
    snprintf(cls, sizeof(cls), "xbased_frame_%s", token);
    HWND h = NULL;
    for (int i = 0; i < 500 && !h; i++) {
        h = FindWindowA(cls, NULL);
        if (!h) {
            if (WaitForSingleObject(pi.hProcess, 10) == WAIT_OBJECT_0) {
                break;  /* it went */
            }
        }
    }
    CloseHandle(pi.hProcess);
    if (h) {
        SetForegroundWindow(h);     /* this process may (the dashboard started it): the frame's in front */
        SetEnvironmentVariableA("XBASED_FRAME_TOKEN", token);
        _putenv_s("XBASED_FRAME_TOKEN", token);
        char v[32];
        snprintf(v, sizeof(v), "%llx", (unsigned long long)(uintptr_t)h);
        SetEnvironmentVariableA("XBASED_FRAME_HWND", v);   /* the runs this one starts: the same frame */
        _putenv_s("XBASED_FRAME_HWND", v);
    }
    return h;
}

static HWND hwnd_of(SDL_Window *w)
{
    return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

void nc_frame_fill(void *frame, SDL_Window *child)
{
    RECT rc;
    GetClientRect((HWND)frame, &rc);
    SetWindowPos(hwnd_of(child), NULL, 0, 0, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

const char *nc_frame_token(void)
{
    return getenv("XBASED_FRAME_TOKEN");
}

void nc_frame_adopt(void *frame, SDL_Window *child, const char *title, bool hidden)
{
    HWND f = (HWND)frame, c = hwnd_of(child);
    wchar_t wt[512];
    if (title && MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 512)) {
        SendMessageW(f, WM_SETTEXT, 0, (LPARAM)wt);  /* this run's title (another executable's) */
    }
    SetWindowLongPtrA(c, GWL_STYLE, WS_CHILD | WS_CLIPSIBLINGS);
    SetWindowLongPtrA(c, GWL_EXSTYLE, 0);
    SetParent(c, f);
    RECT rc;
    GetClientRect(f, &rc);
    if (hidden) {
        /* 1x1, outside the client area: nothing over the frame's pictures
         * (they keep their independent flip), the keyboard still its */
        SetWindowPos(c, HWND_BOTTOM, -8, -8, 1, 1, SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    } else {
        SetWindowPos(c, HWND_BOTTOM, 0, 0, rc.right - rc.left, rc.bottom - rc.top,
                     SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    }
}

void nc_frame_raise(void *frame, SDL_Window *child)
{
    HWND f = (HWND)frame, c = hwnd_of(child);
    SetWindowPos(c, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    if (GetForegroundWindow() == f || GetForegroundWindow() == c) {
        SetFocus(c);
    } else {
        SetForegroundWindow(f);     /* the frame activated: its WM_ACTIVATE hands the keyboard down */
    }
}

void nc_frame_toggle_fullscreen(void *frame)
{
    PostMessageA((HWND)frame, WM_FRAME_TOGGLE, 0, 0);
}

bool nc_frame_minimized(void *frame)
{
    return IsIconic((HWND)frame) != 0;
}

void nc_frame_close(void *frame)
{
    if (frame) {
        PostMessageA((HWND)frame, WM_FRAME_CLOSE, 0, 0);
    }
}

#else /* ------------------------------------------------------------ Linux: X11 */

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <dlfcn.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define FRAME_TICK_MS   50
#define FRAME_LINGER_MS 3000
#define FRAME_WAIT_MS   10000
#define FRAME_MSG_TOGGLE 1
#define FRAME_MSG_CLOSE  2

/* Xlib's calls by name (SDL loads the library too; no link of our own: a
 * desktop without X11 still starts) */
static struct {
    bool ok;
    int (*ReparentWindow)(Display *, Window, Window, int, int);
    int (*MoveResizeWindow)(Display *, Window, int, int, unsigned, unsigned);
    int (*MapWindow)(Display *, Window);
    int (*RaiseWindow)(Display *, Window);
    int (*LowerWindow)(Display *, Window);
    int (*SetInputFocus)(Display *, Window, int, Time);
    Status (*QueryTree)(Display *, Window, Window *, Window *, Window **, unsigned *);
    int (*Free)(void *);
    Status (*GetWindowAttributes)(Display *, Window, XWindowAttributes *);
    int (*Flush)(Display *);
    Status (*SendEvent)(Display *, Window, Bool, long, XEvent *);
    Atom (*InternAtom)(Display *, const char *, Bool);
    int (*ChangeProperty)(Display *, Window, Atom, Atom, int, int, const unsigned char *, int);
    int (*GetWindowProperty)(Display *, Window, Atom, long, long, Bool, Atom, Atom *, int *, unsigned long *,
                             unsigned long *, unsigned char **);
    int (*StoreName)(Display *, Window, const char *);
    int (*Sync)(Display *, Bool);
    XErrorHandler (*SetErrorHandler)(XErrorHandler);
} X;

static bool x_load(void)
{
    static int tried;
    if (tried) {
        return X.ok;
    }
    tried = 1;
    void *h = dlopen("libX11.so.6", RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        return false;
    }
#define L(n) (*(void **)&X.n = dlsym(h, "X" #n)) != NULL
    X.ok = L(ReparentWindow) && L(MoveResizeWindow) && L(MapWindow) && L(RaiseWindow) && L(LowerWindow) &&
           L(SetInputFocus) && L(QueryTree) && L(Free) && L(GetWindowAttributes) && L(Flush) && L(SendEvent) &&
           L(InternAtom) && L(ChangeProperty) && L(GetWindowProperty) && L(StoreName) && L(Sync) &&
           L(SetErrorHandler);
#undef L
    return X.ok;
}

/* a window gone meanwhile (the run before's, as it ends): its BadWindow
 * isn't the end of this process, as Xlib's own handler makes it */
static int x_errors(Display *d, XErrorEvent *e)
{
    (void)d;
    (void)e;
    return 0;
}

static uint64_t ms_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

static Display *x_display(SDL_Window *w)
{
    return (Display *)SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
}

static Window x_window(SDL_Window *w)
{
    return (Window)SDL_GetNumberProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
}

/* the frame's children, topmost last (QueryTree's order); n returned */
static unsigned children(Display *d, Window f, Window *top)
{
    Window root, parent, *kids = NULL;
    unsigned n = 0;
    if (!X.QueryTree(d, f, &root, &parent, &kids, &n)) {
        n = 0;
    }
    if (top) {
        *top = n ? kids[n - 1] : 0;
    }
    if (kids) {
        X.Free(kids);
    }
    return n;
}

static Atom g_msg_atom, g_min_atom;
static volatile int g_toggle, g_quit;

static bool frame_hook(void *ud, XEvent *e)
{
    (void)ud;
    if (e->type == ClientMessage && e->xclient.message_type == g_msg_atom) {
        if (e->xclient.data.l[0] == FRAME_MSG_TOGGLE) {
            g_toggle = 1;
        } else if (e->xclient.data.l[0] == FRAME_MSG_CLOSE) {
            g_quit = 1;
        }
        return false;
    }
    return true;
}

/* the frame's process: an SDL window of its own on X11, the runs' windows
 * inside it */
static int frame_run(int wfd, pid_t run, bool fs, bool fs60)
{
    signal(SIGPIPE, SIG_IGN);
    if (!SDL_Init(SDL_INIT_VIDEO) || strcmp(SDL_GetCurrentVideoDriver(), "x11") || !x_load()) {
        /* not X11 (Wayland): no frame; the run on its own, this the
         * process the dashboard waits on until it's done */
        if (write(wfd, "0\n", 2) < 0) {
        }
        close(wfd);
        SDL_Quit();
        int st = 0;
        while (waitpid(run, &st, 0) < 0) {
        }
        return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    SDL_Window *w = SDL_CreateWindow("xbased", 1280, 960, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    Display *d = w ? x_display(w) : NULL;
    const Window fw = w ? x_window(w) : 0;
    if (!d || !fw) {
        if (write(wfd, "0\n", 2) < 0) {
        }
        close(wfd);
        int st = 0;
        while (waitpid(run, &st, 0) < 0) {
        }
        return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
    }
    X.SetErrorHandler(x_errors);
    g_msg_atom = X.InternAtom(d, "_XBASED_FRAME", False);
    g_min_atom = X.InternAtom(d, "_XBASED_MINIMIZED", False);
    SDL_SetX11EventHook(frame_hook, NULL);
    if (fs) {
        if (fs60) {
            /* the panel's highest mode a multiple of 60 Hz (win_main.c fs_mode's choice) */
            const SDL_DisplayID dd = SDL_GetDisplayForWindow(w);
            const SDL_DisplayMode *dm = SDL_GetDesktopDisplayMode(dd);
            int n = 0;
            SDL_DisplayMode **ms = SDL_GetFullscreenDisplayModes(dd, &n);
            const SDL_DisplayMode *best = NULL;
            for (int i = 0; dm && ms && i < n; i++) {
                const int k = (int)(ms[i]->refresh_rate / 60.0f + 0.5f);
                if (ms[i]->w == dm->w && ms[i]->h == dm->h && k >= 1 && k <= 4 &&
                    ms[i]->refresh_rate <= dm->refresh_rate + 0.5f &&
                    SDL_fabsf(ms[i]->refresh_rate - 60.0f * (float)k) <= 0.6f * (float)k &&
                    (!best || ms[i]->refresh_rate > best->refresh_rate)) {
                    best = ms[i];
                }
            }
            const int k0 = dm ? (int)(dm->refresh_rate / 60.0f + 0.5f) : 0;
            const bool already = dm && k0 >= 1 && SDL_fabsf(dm->refresh_rate - 60.0f * (float)k0) <= 0.6f * (float)k0;
            SDL_SetWindowFullscreenMode(w, already ? NULL : best);
            SDL_free(ms);
        }
        SDL_SetWindowFullscreen(w, true);
    }
    char v[32];
    const int vn = snprintf(v, sizeof(v), "%lx\n", (unsigned long)fw);
    if (write(wfd, v, (size_t)vn) < 0) {
    }
    close(wfd);
    const uint64_t t0 = ms_now();
    uint64_t gone = 0;
    bool had = false, shown = false;
    int pw = 0, ph = 0;
    while (!g_quit) {
        SDL_Event e;
        int fitw = 0;
        if (SDL_WaitEventTimeout(&e, FRAME_TICK_MS)) {
            do {
                switch (e.type) {
                case SDL_EVENT_WINDOW_CLOSE_REQUESTED: {
                    /* the window's close: the run's own quit (its window's) */
                    Window root, parent, *kids = NULL;
                    unsigned n = 0;
                    if (X.QueryTree(d, fw, &root, &parent, &kids, &n) && n) {
                        const Atom proto = X.InternAtom(d, "WM_PROTOCOLS", False);
                        const Atom del = X.InternAtom(d, "WM_DELETE_WINDOW", False);
                        for (unsigned i = 0; i < n; i++) {
                            XEvent m;
                            memset(&m, 0, sizeof(m));
                            m.xclient.type = ClientMessage;
                            m.xclient.window = kids[i];
                            m.xclient.message_type = proto;
                            m.xclient.format = 32;
                            m.xclient.data.l[0] = (long)del;
                            m.xclient.data.l[1] = CurrentTime;
                            X.SendEvent(d, kids[i], False, NoEventMask, &m);
                        }
                    } else {
                        g_quit = 1;
                    }
                    if (kids) {
                        X.Free(kids);
                    }
                    break;
                }
                case SDL_EVENT_WINDOW_FOCUS_GAINED: {
                    Window top = 0;
                    XWindowAttributes a;
                    if (children(d, fw, &top) && X.GetWindowAttributes(d, top, &a) && a.map_state == IsViewable) {
                        X.SetInputFocus(d, top, RevertToParent, CurrentTime);    /* the keyboard to the run */
                    }
                    break;
                }
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_RESTORED: {
                    const long m = e.type == SDL_EVENT_WINDOW_MINIMIZED;
                    X.ChangeProperty(d, fw, g_min_atom, XA_CARDINAL, 32, PropModeReplace, (const unsigned char *)&m, 1);
                    break;
                }
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                case SDL_EVENT_WINDOW_RESIZED:
                    fitw = 1;
                    break;
                default:
                    break;
                }
            } while (SDL_PollEvent(&e));
        }
        if (g_toggle) {
            g_toggle = 0;
            SDL_SetWindowFullscreen(w, !(SDL_GetWindowFlags(w) & SDL_WINDOW_FULLSCREEN));
        }
        int cw = 0, ch = 0;
        SDL_GetWindowSizeInPixels(w, &cw, &ch);
        if (fitw || cw != pw || ch != ph) {
            pw = cw;
            ph = ch;
            Window root, parent, *kids = NULL;
            unsigned n = 0;
            if (X.QueryTree(d, fw, &root, &parent, &kids, &n)) {
                for (unsigned i = 0; i < n; i++) {
                    X.MoveResizeWindow(d, kids[i], 0, 0, (unsigned)(cw > 0 ? cw : 1), (unsigned)(ch > 0 ? ch : 1));
                }
            }
            if (kids) {
                X.Free(kids);
            }
            X.Flush(d);
        }
        const uint64_t now = ms_now();
        if (children(d, fw, NULL)) {
            if (!shown) {
                shown = true;
                SDL_ShowWindow(w);
                SDL_RaiseWindow(w);
            }
            had = true;
            gone = 0;
        } else if (had) {
            if (!gone) {
                gone = now;
                SDL_HideWindow(w);      /* what's behind (the dashboard) at once */
            } else if (now - gone > FRAME_LINGER_MS) {
                break;
            }
        } else if (now - t0 > FRAME_WAIT_MS) {
            break;  /* the first run never came */
        }
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            /* the runs that ended (the first is this process's child) */
        }
    }
    SDL_DestroyWindow(w);
    SDL_Quit();
    return 0;
}

void nc_frame_fork(bool fullscreen, bool fs60)
{
    int p[2];
    if (getenv("XBASED_FRAME_XID") || !getenv("DISPLAY") || pipe(p) < 0) {
        return;
    }
    const pid_t run = fork();
    if (run < 0) {
        close(p[0]);
        close(p[1]);
        return;
    }
    if (run > 0) {
        close(p[0]);
        _exit(frame_run(p[1], run, fullscreen, fs60));     /* the parent: the frame */
    }
    close(p[1]);
    /* the run: the frame's window, or "0" for none */
    char v[32] = "";
    struct pollfd pf = {p[0], POLLIN, 0};
    if (poll(&pf, 1, 5000) > 0) {
        const ssize_t n = read(p[0], v, sizeof(v) - 1);
        v[n > 0 ? n : 0] = 0;
    }
    close(p[0]);
    /* "0" too: no frame (Wayland), and the runs started again (exec'd,
     * the same process) don't fork another */
    char s[32];
    snprintf(s, sizeof(s), "%lx", strtoul(v, NULL, 16));
    setenv("XBASED_FRAME_XID", s, 1);
}

void *nc_frame_get(const char *title, bool fullscreen, bool fs60)
{
    (void)title;
    (void)fullscreen;
    (void)fs60;
    const char *v = getenv("XBASED_FRAME_XID");
    const unsigned long xid = v ? strtoul(v, NULL, 16) : 0;
    if (!xid || !x_load() || strcmp(SDL_GetCurrentVideoDriver(), "x11")) {
        return NULL;
    }
    return (void *)(uintptr_t)xid;
}

static SDL_Window *g_child;     /* the run's window, for the calls without it */

void nc_frame_fill(void *frame, SDL_Window *child)
{
    (void)frame;
    (void)child;
}

const char *nc_frame_token(void)
{
    return NULL;
}

void nc_frame_adopt(void *frame, SDL_Window *child, const char *title, bool hidden)
{
    (void)hidden;
    Display *d = x_display(child);
    const Window f = (Window)(uintptr_t)frame, c = x_window(child);
    if (!d || !c) {
        return;
    }
    g_child = child;
    X.SetErrorHandler(x_errors);
    if (title && title[0]) {
        /* this run's title (another executable's) */
        X.ChangeProperty(d, f, X.InternAtom(d, "_NET_WM_NAME", False), X.InternAtom(d, "UTF8_STRING", False), 8,
                         PropModeReplace, (const unsigned char *)title, (int)strlen(title));
        X.StoreName(d, f, title);
    }
    XWindowAttributes a;
    memset(&a, 0, sizeof(a));
    X.GetWindowAttributes(d, f, &a);
    X.ReparentWindow(d, c, f, 0, 0);
    X.MoveResizeWindow(d, c, 0, 0, (unsigned)(a.width > 0 ? a.width : 1280), (unsigned)(a.height > 0 ? a.height : 960));
    X.MapWindow(d, c);
    X.LowerWindow(d, c);    /* under the run before's, until this one has drawn */
    X.Flush(d);
}

void nc_frame_raise(void *frame, SDL_Window *child)
{
    Display *d = x_display(child);
    const Window f = (Window)(uintptr_t)frame, c = x_window(child);
    if (!d || !c) {
        return;
    }
    X.RaiseWindow(d, c);
    XWindowAttributes a;
    if (X.GetWindowAttributes(d, f, &a) && a.map_state == IsViewable && X.GetWindowAttributes(d, c, &a) &&
        a.map_state == IsViewable) {
        X.SetInputFocus(d, c, RevertToParent, CurrentTime);
    }
    X.Flush(d);
}

static void send_msg(void *frame, long what)
{
    Display *d = g_child ? x_display(g_child) : NULL;
    if (!d) {
        return;
    }
    XEvent m;
    memset(&m, 0, sizeof(m));
    m.xclient.type = ClientMessage;
    m.xclient.window = (Window)(uintptr_t)frame;
    m.xclient.message_type = X.InternAtom(d, "_XBASED_FRAME", False);
    m.xclient.format = 32;
    m.xclient.data.l[0] = what;
    X.SendEvent(d, (Window)(uintptr_t)frame, False, NoEventMask, &m);
    X.Flush(d);
}

void nc_frame_toggle_fullscreen(void *frame)
{
    send_msg(frame, FRAME_MSG_TOGGLE);
}

bool nc_frame_minimized(void *frame)
{
    Display *d = g_child ? x_display(g_child) : NULL;
    if (!d) {
        return false;
    }
    Atom type = 0;
    int fmt = 0;
    unsigned long n = 0, left = 0;
    unsigned char *data = NULL;
    bool m = false;
    if (X.GetWindowProperty(d, (Window)(uintptr_t)frame, X.InternAtom(d, "_XBASED_MINIMIZED", False), 0, 1, False,
                            XA_CARDINAL, &type, &fmt, &n, &left, &data) == Success &&
        data && n == 1 && fmt == 32) {
        m = *(long *)data != 0;
    }
    if (data) {
        X.Free(data);
    }
    return m;
}

void nc_frame_close(void *frame)
{
    if (frame) {
        send_msg(frame, FRAME_MSG_CLOSE);
    }
}

#endif
