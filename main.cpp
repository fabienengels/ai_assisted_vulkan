// Vulkan bouncing balls with explosion effects
// Requires: Vulkan, GLFW, GLM (all via homebrew)

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

// ─── Config ──────────────────────────────────────────────────────────────────

static constexpr int   WIN_W          = 800;
static constexpr int   WIN_H          = 800;
static constexpr int   MAX_FRAMES     = 2;        // frames in flight
static constexpr int   MAX_VERTS      = 200000;   // pre-allocated vertex budget
static constexpr int   BALL_SEGMENTS  = 32;
static constexpr int   PART_SEGMENTS  = 8;
static constexpr int   SPAWN_COUNT    = 7;        // desired live ball count
static constexpr float BALL_RADIUS    = 0.07f;
static constexpr float PART_RADIUS    = 0.018f;
static constexpr float MIN_SPEED      = 0.35f;
static constexpr float MAX_SPEED      = 0.75f;
static constexpr float PART_SPEED     = 0.9f;
static constexpr float PART_LIFETIME  = 1.0f;     // seconds
static constexpr int   PARTS_PER_HIT  = 28;

// ─── Vertex ──────────────────────────────────────────────────────────────────

struct Vertex {
    glm::vec2 pos;
    glm::vec3 color;
    float     alpha;

    static VkVertexInputBindingDescription binding() {
        return {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::array<VkVertexInputAttributeDescription, 3> attributes() {
        return {{
            {0, 0, VK_FORMAT_R32G32_SFLOAT,       offsetof(Vertex, pos)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT,    offsetof(Vertex, color)},
            {2, 0, VK_FORMAT_R32_SFLOAT,           offsetof(Vertex, alpha)},
        }};
    }
};

// ─── Simulation objects ───────────────────────────────────────────────────────

struct Ball {
    glm::vec2 pos;
    glm::vec2 vel;
    glm::vec3 color;
    float     radius = BALL_RADIUS;
    bool      alive  = true;
};

struct Particle {
    glm::vec2 pos;
    glm::vec2 vel;
    glm::vec3 color;
    float     life    = 1.0f; // 1 = fresh, 0 = dead
    bool      alive   = true;
};

// ─── Simulation ──────────────────────────────────────────────────────────────

class Simulation {
public:
    std::vector<Ball>     balls;
    std::vector<Particle> particles;

    std::mt19937 rng{std::random_device{}()};

    Simulation() { spawnBalls(SPAWN_COUNT); }

    void update(float dt) {
        updateBalls(dt);
        updateParticles(dt);
        checkCollisions();
        // Keep a healthy number of balls on screen
        int alive = (int)std::count_if(balls.begin(), balls.end(),
                                       [](auto& b){ return b.alive; });
        if (alive < SPAWN_COUNT) spawnBalls(SPAWN_COUNT - alive);
    }

    // Build vertex list for current frame
    void buildGeometry(std::vector<Vertex>& verts) {
        verts.clear();
        for (auto& b : balls)
            if (b.alive)
                addCircle(verts, b.pos, b.radius, b.color, 1.0f, BALL_SEGMENTS);
        for (auto& p : particles)
            if (p.alive)
                addCircle(verts, p.pos, PART_RADIUS, p.color, p.life, PART_SEGMENTS);
    }

private:
    float randF(float lo, float hi) {
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    }

    glm::vec3 randColor() {
        // Saturated bright colors
        int chan = std::uniform_int_distribution<int>(0, 2)(rng);
        glm::vec3 c;
        c[chan]            = 1.0f;
        c[(chan + 1) % 3]  = randF(0.0f, 0.5f);
        c[(chan + 2) % 3]  = randF(0.0f, 0.2f);
        return c;
    }

    void spawnBalls(int n) {
        for (int i = 0; i < n; i++) {
            Ball b;
            b.color = randColor();
            // Place without immediate overlap with existing balls
            for (int attempt = 0; attempt < 50; attempt++) {
                b.pos = {randF(-1.0f + BALL_RADIUS, 1.0f - BALL_RADIUS),
                         randF(-1.0f + BALL_RADIUS, 1.0f - BALL_RADIUS)};
                bool ok = true;
                for (auto& o : balls)
                    if (o.alive && glm::length(b.pos - o.pos) < 2.0f * BALL_RADIUS + 0.05f)
                        { ok = false; break; }
                if (ok) break;
            }
            float speed = randF(MIN_SPEED, MAX_SPEED);
            float angle = randF(0.0f, 6.2832f);
            b.vel = {speed * std::cos(angle), speed * std::sin(angle)};
            balls.push_back(b);
        }
    }

    void updateBalls(float dt) {
        for (auto& b : balls) {
            if (!b.alive) continue;
            b.pos += b.vel * dt;
            // Wall bounce
            if (b.pos.x - b.radius < -1.0f) { b.pos.x = -1.0f + b.radius; b.vel.x = std::abs(b.vel.x); }
            if (b.pos.x + b.radius >  1.0f) { b.pos.x =  1.0f - b.radius; b.vel.x = -std::abs(b.vel.x); }
            if (b.pos.y - b.radius < -1.0f) { b.pos.y = -1.0f + b.radius; b.vel.y = std::abs(b.vel.y); }
            if (b.pos.y + b.radius >  1.0f) { b.pos.y =  1.0f - b.radius; b.vel.y = -std::abs(b.vel.y); }
        }
        // Purge dead balls to keep vector tidy
        balls.erase(std::remove_if(balls.begin(), balls.end(),
                                   [](auto& b){ return !b.alive; }), balls.end());
    }

    void checkCollisions() {
        for (size_t i = 0; i < balls.size(); i++) {
            for (size_t j = i + 1; j < balls.size(); j++) {
                Ball& a = balls[i];
                Ball& b = balls[j];
                if (!a.alive || !b.alive) continue;
                float dist = glm::length(a.pos - b.pos);
                if (dist < a.radius + b.radius) {
                    glm::vec2 mid = (a.pos + b.pos) * 0.5f;
                    explode(mid, a.color, b.color);
                    a.alive = false;
                    b.alive = false;
                }
            }
        }
    }

    void explode(glm::vec2 origin, glm::vec3 colorA, glm::vec3 colorB) {
        for (int i = 0; i < PARTS_PER_HIT; i++) {
            Particle p;
            p.pos   = origin;
            p.life  = 1.0f;
            float angle = randF(0.0f, 6.2832f);
            float speed = randF(0.3f, PART_SPEED);
            p.vel   = {speed * std::cos(angle), speed * std::sin(angle)};
            // Alternate colors from each colliding ball, plus bright white flashes
            float t = randF(0.0f, 1.0f);
            if (t < 0.15f)
                p.color = glm::vec3(1.0f);          // white spark
            else if (t < 0.55f)
                p.color = colorA;
            else
                p.color = colorB;
            particles.push_back(p);
        }
    }

    void updateParticles(float dt) {
        for (auto& p : particles) {
            if (!p.alive) continue;
            p.pos  += p.vel * dt;
            p.vel  *= (1.0f - 1.5f * dt);   // drag
            p.life -= dt / PART_LIFETIME;
            if (p.life <= 0.0f) p.alive = false;
        }
        particles.erase(std::remove_if(particles.begin(), particles.end(),
                                       [](auto& p){ return !p.alive; }), particles.end());
    }

    void addCircle(std::vector<Vertex>& verts,
                   glm::vec2 c, float r, glm::vec3 col, float alpha, int segs) {
        static constexpr float PI2 = 6.28318530718f;
        for (int i = 0; i < segs; i++) {
            float a0 = PI2 * i       / segs;
            float a1 = PI2 * (i + 1) / segs;
            verts.push_back({c, col, alpha});
            verts.push_back({c + r * glm::vec2(std::cos(a0), std::sin(a0)), col, alpha});
            verts.push_back({c + r * glm::vec2(std::cos(a1), std::sin(a1)), col, alpha});
        }
    }
};

// ─── Utilities ────────────────────────────────────────────────────────────────

static std::vector<char> readFile(const std::string& path) {
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("Cannot open file: " + path);
    size_t size = (size_t)f.tellg();
    std::vector<char> buf(size);
    f.seekg(0);
    f.read(buf.data(), size);
    return buf;
}

// ─── VulkanApp ────────────────────────────────────────────────────────────────

class VulkanApp {
public:
    void run() {
        initWindow();
        initVulkan();
        mainLoop();
        cleanup();
    }

private:
    // ── Window ────────────────────────────────────────────────────────────────
    GLFWwindow* window = nullptr;

    void initWindow() {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
        window = glfwCreateWindow(WIN_W, WIN_H, "Bouncing Balls", nullptr, nullptr);
    }

    // ── Vulkan handles ────────────────────────────────────────────────────────
    VkInstance               instance       = VK_NULL_HANDLE;
    VkSurfaceKHR             surface        = VK_NULL_HANDLE;
    VkPhysicalDevice         physDev        = VK_NULL_HANDLE;
    VkDevice                 device         = VK_NULL_HANDLE;
    VkQueue                  graphicsQueue  = VK_NULL_HANDLE;
    VkQueue                  presentQueue   = VK_NULL_HANDLE;
    VkSwapchainKHR           swapchain      = VK_NULL_HANDLE;
    std::vector<VkImage>     swapImages;
    std::vector<VkImageView> swapViews;
    VkFormat                 swapFormat{};
    VkExtent2D               swapExtent{};
    VkRenderPass             renderPass     = VK_NULL_HANDLE;
    VkPipelineLayout         pipeLayout     = VK_NULL_HANDLE;
    VkPipeline               pipeline       = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;
    VkCommandPool            cmdPool        = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> cmdBuffers;

    // Vertex buffer (host-visible, persistent mapping)
    VkBuffer       vertexBuffer   = VK_NULL_HANDLE;
    VkDeviceMemory vertexMemory   = VK_NULL_HANDLE;
    void*          vertexMapped   = nullptr;

    // Sync
    std::vector<VkSemaphore> imageAvailable;
    std::vector<VkSemaphore> renderFinished;
    std::vector<VkFence>     inFlightFences;
    int currentFrame = 0;

    uint32_t graphicsFamily = 0;
    uint32_t presentFamily  = 0;

    // ── Init sequence ─────────────────────────────────────────────────────────
    void initVulkan() {
        createInstance();
        createSurface();
        pickPhysicalDevice();
        createLogicalDevice();
        createSwapchain();
        createImageViews();
        createRenderPass();
        createPipeline();
        createFramebuffers();
        createCommandPool();
        createVertexBuffer();
        createCommandBuffers();
        createSyncObjects();
    }

    // ── Instance ──────────────────────────────────────────────────────────────
    void createInstance() {
        VkApplicationInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        ai.pApplicationName   = "BouncingBalls";
        ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        ai.apiVersion         = VK_API_VERSION_1_0;

        uint32_t glfwCount = 0;
        const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwCount);
        std::vector<const char*> exts(glfwExts, glfwExts + glfwCount);

        // MoltenVK requires portability enumeration on macOS
        exts.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        exts.push_back(VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME);

        VkInstanceCreateInfo ci{};
        ci.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo        = &ai;
        ci.enabledExtensionCount   = (uint32_t)exts.size();
        ci.ppEnabledExtensionNames = exts.data();
        ci.flags                   = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;

        if (vkCreateInstance(&ci, nullptr, &instance) != VK_SUCCESS)
            throw std::runtime_error("vkCreateInstance failed");
    }

    // ── Surface ───────────────────────────────────────────────────────────────
    void createSurface() {
        if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS)
            throw std::runtime_error("glfwCreateWindowSurface failed");
    }

    // ── Physical device ───────────────────────────────────────────────────────
    void pickPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (!count) throw std::runtime_error("No Vulkan-capable GPU");
        std::vector<VkPhysicalDevice> devs(count);
        vkEnumeratePhysicalDevices(instance, &count, devs.data());

        for (auto& d : devs) {
            if (deviceSuitable(d)) { physDev = d; return; }
        }
        throw std::runtime_error("No suitable GPU");
    }

    bool deviceSuitable(VkPhysicalDevice d) {
        uint32_t qcount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, nullptr);
        std::vector<VkQueueFamilyProperties> qprops(qcount);
        vkGetPhysicalDeviceQueueFamilyProperties(d, &qcount, qprops.data());

        int gfx = -1, prs = -1;
        for (uint32_t i = 0; i < qcount; i++) {
            if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) gfx = (int)i;
            VkBool32 sup = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(d, i, surface, &sup);
            if (sup) prs = (int)i;
        }
        if (gfx < 0 || prs < 0) return false;

        // Check swapchain support
        uint32_t fmtCount = 0, pmCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(d, surface, &fmtCount, nullptr);
        vkGetPhysicalDeviceSurfacePresentModesKHR(d, surface, &pmCount, nullptr);

        graphicsFamily = (uint32_t)gfx;
        presentFamily  = (uint32_t)prs;
        return fmtCount > 0 && pmCount > 0;
    }

    // ── Logical device ────────────────────────────────────────────────────────
    void createLogicalDevice() {
        float prio = 1.0f;
        std::vector<VkDeviceQueueCreateInfo> qcis;
        for (uint32_t fam : std::vector<uint32_t>{graphicsFamily, presentFamily}) {
            // Deduplicate queue families
            bool dup = false;
            for (auto& q : qcis) if (q.queueFamilyIndex == fam) { dup = true; break; }
            if (dup) continue;
            VkDeviceQueueCreateInfo qi{};
            qi.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = fam;
            qi.queueCount       = 1;
            qi.pQueuePriorities = &prio;
            qcis.push_back(qi);
        }

        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> available(extCount);
        vkEnumerateDeviceExtensionProperties(physDev, nullptr, &extCount, available.data());

        std::vector<const char*> devExts;
        for (const auto& ext : available) {
            if (std::string(ext.extensionName) == VK_KHR_SWAPCHAIN_EXTENSION_NAME ||
                std::string(ext.extensionName) == "VK_KHR_portability_subset") {
                devExts.push_back(ext.extensionName);
            }
        }

        VkDeviceCreateInfo ci{};
        ci.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        ci.queueCreateInfoCount    = (uint32_t)qcis.size();
        ci.pQueueCreateInfos       = qcis.data();
        ci.enabledExtensionCount   = (uint32_t)devExts.size();
        ci.ppEnabledExtensionNames = devExts.data();

        if (vkCreateDevice(physDev, &ci, nullptr, &device) != VK_SUCCESS)
            throw std::runtime_error("vkCreateDevice failed");

        vkGetDeviceQueue(device, graphicsFamily, 0, &graphicsQueue);
        vkGetDeviceQueue(device, presentFamily,  0, &presentQueue);
    }

    // ── Swapchain ─────────────────────────────────────────────────────────────
    void createSwapchain() {
        VkSurfaceCapabilitiesKHR caps;
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDev, surface, &caps);

        uint32_t fmtCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &fmtCount, nullptr);
        std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physDev, surface, &fmtCount, fmts.data());

        VkSurfaceFormatKHR chosen = fmts[0];
        for (auto& f : fmts)
            if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                chosen = f;

        swapFormat = chosen.format;
        swapExtent = caps.currentExtent;
        if (swapExtent.width == UINT32_MAX) {
            swapExtent = {(uint32_t)WIN_W, (uint32_t)WIN_H};
        }

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount)
            imgCount = caps.maxImageCount;

        VkSwapchainCreateInfoKHR sci{};
        sci.sType            = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        sci.surface          = surface;
        sci.minImageCount    = imgCount;
        sci.imageFormat      = chosen.format;
        sci.imageColorSpace  = chosen.colorSpace;
        sci.imageExtent      = swapExtent;
        sci.imageArrayLayers = 1;
        sci.imageUsage       = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.preTransform     = caps.currentTransform;
        sci.compositeAlpha   = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        sci.presentMode      = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped          = VK_TRUE;

        uint32_t qFams[] = {graphicsFamily, presentFamily};
        if (graphicsFamily != presentFamily) {
            sci.imageSharingMode      = VK_SHARING_MODE_CONCURRENT;
            sci.queueFamilyIndexCount = 2;
            sci.pQueueFamilyIndices   = qFams;
        } else {
            sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        if (vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain) != VK_SUCCESS)
            throw std::runtime_error("vkCreateSwapchainKHR failed");

        uint32_t n = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &n, nullptr);
        swapImages.resize(n);
        vkGetSwapchainImagesKHR(device, swapchain, &n, swapImages.data());
    }

    // ── Image views ───────────────────────────────────────────────────────────
    void createImageViews() {
        swapViews.resize(swapImages.size());
        for (size_t i = 0; i < swapImages.size(); i++) {
            VkImageViewCreateInfo ci{};
            ci.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image                           = swapImages[i];
            ci.viewType                        = VK_IMAGE_VIEW_TYPE_2D;
            ci.format                          = swapFormat;
            ci.components.r                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.g                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.b                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.a                    = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            ci.subresourceRange.baseMipLevel   = 0;
            ci.subresourceRange.levelCount     = 1;
            ci.subresourceRange.baseArrayLayer = 0;
            ci.subresourceRange.layerCount     = 1;
            if (vkCreateImageView(device, &ci, nullptr, &swapViews[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateImageView failed");
        }
    }

    // ── Render pass ───────────────────────────────────────────────────────────
    void createRenderPass() {
        VkAttachmentDescription att{};
        att.format         = swapFormat;
        att.samples        = VK_SAMPLE_COUNT_1_BIT;
        att.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        att.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        att.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        att.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription sub{};
        sub.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments    = &ref;

        VkSubpassDependency dep{};
        dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass    = 0;
        dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.srcAccessMask = 0;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo ci{};
        ci.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        ci.attachmentCount = 1;
        ci.pAttachments    = &att;
        ci.subpassCount    = 1;
        ci.pSubpasses      = &sub;
        ci.dependencyCount = 1;
        ci.pDependencies   = &dep;

        if (vkCreateRenderPass(device, &ci, nullptr, &renderPass) != VK_SUCCESS)
            throw std::runtime_error("vkCreateRenderPass failed");
    }

    // ── Pipeline ──────────────────────────────────────────────────────────────
    VkShaderModule createShaderModule(const std::vector<char>& code) {
        VkShaderModuleCreateInfo ci{};
        ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        ci.codeSize = code.size();
        ci.pCode    = reinterpret_cast<const uint32_t*>(code.data());
        VkShaderModule mod;
        if (vkCreateShaderModule(device, &ci, nullptr, &mod) != VK_SUCCESS)
            throw std::runtime_error("vkCreateShaderModule failed");
        return mod;
    }

    void createPipeline() {
        auto vertCode = readFile(std::string(SHADER_DIR) + "ball.vert.spv");
        auto fragCode = readFile(std::string(SHADER_DIR) + "ball.frag.spv");
        VkShaderModule vertMod = createShaderModule(vertCode);
        VkShaderModule fragMod = createShaderModule(fragCode);

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertMod;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragMod;
        stages[1].pName  = "main";

        auto binding   = Vertex::binding();
        auto attribs   = Vertex::attributes();

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &binding;
        vi.vertexAttributeDescriptionCount = (uint32_t)attribs.size();
        vi.pVertexAttributeDescriptions    = attribs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkViewport viewport{0, 0, (float)swapExtent.width, (float)swapExtent.height, 0, 1};
        VkRect2D   scissor{{0,0}, swapExtent};

        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.pViewports    = &viewport;
        vp.scissorCount  = 1;
        vp.pScissors     = &scissor;

        VkPipelineRasterizationStateCreateInfo rast{};
        rast.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rast.polygonMode = VK_POLYGON_MODE_FILL;
        rast.cullMode    = VK_CULL_MODE_NONE;
        rast.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        // Alpha blending — balls opaque (alpha=1), particles fade
        VkPipelineColorBlendAttachmentState blendAtt{};
        blendAtt.blendEnable         = VK_TRUE;
        blendAtt.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAtt.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAtt.colorBlendOp        = VK_BLEND_OP_ADD;
        blendAtt.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAtt.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAtt.alphaBlendOp        = VK_BLEND_OP_ADD;
        blendAtt.colorWriteMask      = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                       VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blendAtt;

        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        if (vkCreatePipelineLayout(device, &plci, nullptr, &pipeLayout) != VK_SUCCESS)
            throw std::runtime_error("vkCreatePipelineLayout failed");

        VkGraphicsPipelineCreateInfo gci{};
        gci.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gci.stageCount          = 2;
        gci.pStages             = stages;
        gci.pVertexInputState   = &vi;
        gci.pInputAssemblyState = &ia;
        gci.pViewportState      = &vp;
        gci.pRasterizationState = &rast;
        gci.pMultisampleState   = &ms;
        gci.pColorBlendState    = &blend;
        gci.layout              = pipeLayout;
        gci.renderPass          = renderPass;
        gci.subpass             = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gci, nullptr, &pipeline) != VK_SUCCESS)
            throw std::runtime_error("vkCreateGraphicsPipelines failed");

        vkDestroyShaderModule(device, vertMod, nullptr);
        vkDestroyShaderModule(device, fragMod, nullptr);
    }

    // ── Framebuffers ──────────────────────────────────────────────────────────
    void createFramebuffers() {
        framebuffers.resize(swapViews.size());
        for (size_t i = 0; i < swapViews.size(); i++) {
            VkFramebufferCreateInfo ci{};
            ci.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            ci.renderPass      = renderPass;
            ci.attachmentCount = 1;
            ci.pAttachments    = &swapViews[i];
            ci.width           = swapExtent.width;
            ci.height          = swapExtent.height;
            ci.layers          = 1;
            if (vkCreateFramebuffer(device, &ci, nullptr, &framebuffers[i]) != VK_SUCCESS)
                throw std::runtime_error("vkCreateFramebuffer failed");
        }
    }

    // ── Command pool ──────────────────────────────────────────────────────────
    void createCommandPool() {
        VkCommandPoolCreateInfo ci{};
        ci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        ci.queueFamilyIndex = graphicsFamily;
        ci.flags            = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        if (vkCreateCommandPool(device, &ci, nullptr, &cmdPool) != VK_SUCCESS)
            throw std::runtime_error("vkCreateCommandPool failed");
    }

    // ── Vertex buffer (host-visible, persistent map) ──────────────────────────
    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp;
        vkGetPhysicalDeviceMemoryProperties(physDev, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
            if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props)
                return i;
        throw std::runtime_error("No suitable memory type");
    }

    void createVertexBuffer() {
        VkDeviceSize size = MAX_VERTS * sizeof(Vertex);

        VkBufferCreateInfo bi{};
        bi.sType       = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bi.size        = size;
        bi.usage       = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (vkCreateBuffer(device, &bi, nullptr, &vertexBuffer) != VK_SUCCESS)
            throw std::runtime_error("vkCreateBuffer failed");

        VkMemoryRequirements mr;
        vkGetBufferMemoryRequirements(device, vertexBuffer, &mr);

        VkMemoryAllocateInfo ai{};
        ai.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        ai.allocationSize  = mr.size;
        ai.memoryTypeIndex = findMemoryType(mr.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(device, &ai, nullptr, &vertexMemory) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateMemory failed");
        vkBindBufferMemory(device, vertexBuffer, vertexMemory, 0);
        vkMapMemory(device, vertexMemory, 0, size, 0, &vertexMapped);
    }

    // ── Command buffers ───────────────────────────────────────────────────────
    void createCommandBuffers() {
        cmdBuffers.resize(MAX_FRAMES);
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = cmdPool;
        ai.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = MAX_FRAMES;
        if (vkAllocateCommandBuffers(device, &ai, cmdBuffers.data()) != VK_SUCCESS)
            throw std::runtime_error("vkAllocateCommandBuffers failed");
    }

    // ── Sync objects ──────────────────────────────────────────────────────────
    void createSyncObjects() {
        imageAvailable.resize(MAX_FRAMES);
        renderFinished.resize(MAX_FRAMES);
        inFlightFences.resize(MAX_FRAMES);

        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo     fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (int i = 0; i < MAX_FRAMES; i++) {
            if (vkCreateSemaphore(device, &sci, nullptr, &imageAvailable[i]) != VK_SUCCESS ||
                vkCreateSemaphore(device, &sci, nullptr, &renderFinished[i]) != VK_SUCCESS ||
                vkCreateFence    (device, &fci, nullptr, &inFlightFences[i]) != VK_SUCCESS)
                throw std::runtime_error("Sync object creation failed");
        }
    }

    // ── Record command buffer ─────────────────────────────────────────────────
    void recordCommandBuffer(VkCommandBuffer cb, uint32_t imgIdx, uint32_t vertCount) {
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        vkBeginCommandBuffer(cb, &bi);

        VkClearValue clear{{{0.05f, 0.05f, 0.08f, 1.0f}}};  // dark blue-grey

        VkRenderPassBeginInfo rpi{};
        rpi.sType             = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpi.renderPass        = renderPass;
        rpi.framebuffer       = framebuffers[imgIdx];
        rpi.renderArea.offset = {0, 0};
        rpi.renderArea.extent = swapExtent;
        rpi.clearValueCount   = 1;
        rpi.pClearValues      = &clear;

        vkCmdBeginRenderPass(cb, &rpi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(cb, 0, 1, &vertexBuffer, &offset);

        if (vertCount > 0)
            vkCmdDraw(cb, vertCount, 1, 0, 0);

        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
    }

    // ── Draw frame ────────────────────────────────────────────────────────────
    void drawFrame(const std::vector<Vertex>& verts) {
        vkWaitForFences(device, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
        vkResetFences  (device, 1, &inFlightFences[currentFrame]);

        uint32_t imgIdx;
        vkAcquireNextImageKHR(device, swapchain, UINT64_MAX,
                              imageAvailable[currentFrame], VK_NULL_HANDLE, &imgIdx);

        // Upload vertices
        size_t bytes = verts.size() * sizeof(Vertex);
        if (bytes > 0)
            std::memcpy(vertexMapped, verts.data(), bytes);

        vkResetCommandBuffer(cmdBuffers[currentFrame], 0);
        recordCommandBuffer(cmdBuffers[currentFrame], imgIdx, (uint32_t)verts.size());

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo si{};
        si.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.waitSemaphoreCount   = 1;
        si.pWaitSemaphores      = &imageAvailable[currentFrame];
        si.pWaitDstStageMask    = &waitStage;
        si.commandBufferCount   = 1;
        si.pCommandBuffers      = &cmdBuffers[currentFrame];
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores    = &renderFinished[currentFrame];

        if (vkQueueSubmit(graphicsQueue, 1, &si, inFlightFences[currentFrame]) != VK_SUCCESS)
            throw std::runtime_error("vkQueueSubmit failed");

        VkPresentInfoKHR pi{};
        pi.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores    = &renderFinished[currentFrame];
        pi.swapchainCount     = 1;
        pi.pSwapchains        = &swapchain;
        pi.pImageIndices      = &imgIdx;

        vkQueuePresentKHR(presentQueue, &pi);
        currentFrame = (currentFrame + 1) % MAX_FRAMES;
    }

    // ── Main loop ─────────────────────────────────────────────────────────────
    void mainLoop() {
        Simulation sim;
        std::vector<Vertex> verts;
        verts.reserve(MAX_VERTS);

        auto lastTime = std::chrono::high_resolution_clock::now();

        while (!glfwWindowShouldClose(window)) {
            glfwPollEvents();

            auto now = std::chrono::high_resolution_clock::now();
            float dt = std::chrono::duration<float>(now - lastTime).count();
            lastTime = now;
            // Clamp dt to avoid large jumps on focus-loss/resume
            if (dt > 0.05f) dt = 0.05f;

            sim.update(dt);
            sim.buildGeometry(verts);
            drawFrame(verts);
        }

        vkDeviceWaitIdle(device);
    }

    // ── Cleanup ───────────────────────────────────────────────────────────────
    void cleanup() {
        for (int i = 0; i < MAX_FRAMES; i++) {
            vkDestroySemaphore(device, imageAvailable[i], nullptr);
            vkDestroySemaphore(device, renderFinished[i], nullptr);
            vkDestroyFence    (device, inFlightFences[i], nullptr);
        }
        vkDestroyCommandPool(device, cmdPool, nullptr);
        if (vertexMapped) vkUnmapMemory(device, vertexMemory);
        vkDestroyBuffer(device, vertexBuffer, nullptr);
        vkFreeMemory   (device, vertexMemory, nullptr);
        for (auto fb : framebuffers)  vkDestroyFramebuffer(device, fb, nullptr);
        vkDestroyPipeline      (device, pipeline,   nullptr);
        vkDestroyPipelineLayout(device, pipeLayout, nullptr);
        vkDestroyRenderPass    (device, renderPass, nullptr);
        for (auto iv : swapViews) vkDestroyImageView(device, iv, nullptr);
        vkDestroySwapchainKHR(device, swapchain, nullptr);
        vkDestroyDevice      (device, nullptr);
        vkDestroySurfaceKHR  (instance, surface, nullptr);
        vkDestroyInstance    (instance, nullptr);
        glfwDestroyWindow(window);
        glfwTerminate();
    }
};

// ─── Entry point ─────────────────────────────────────────────────────────────

int main() {
    try {
        VulkanApp app;
        app.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
