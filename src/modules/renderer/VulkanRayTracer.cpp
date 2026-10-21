#include "VulkanRayTracer.h"
#include <QFile>
#include <QElapsedTimer>
#include <QVector4D>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef ENGINE_HAS_VULKAN
#include <vulkan/vulkan.h>
#endif

RaySceneSnapshot RaySceneSnapshot::capture(const Scene& scene)
{
    SoftwareRayTracer tracer; tracer.capture(scene, false);
    return {tracer.triangles(), tracer.materials(), collectLights(scene), hasSceneLights(scene)};
}
#ifdef ENGINE_HAS_VULKAN
namespace
{
void vkCheck(VkResult result,const char* message)
{
    if(result!=VK_SUCCESS) throw std::runtime_error(std::string(message)+" (Vulkan "+std::to_string(result)+")");
}
struct Buffer { VkBuffer handle=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkDeviceSize size=0; void* mapped=nullptr; };
struct Texture { VkImage image=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; VkImageView view=VK_NULL_HANDLE; };
struct Acceleration { VkAccelerationStructureKHR handle=VK_NULL_HANDLE; Buffer storage; };
struct alignas(16) GpuTriangle { QVector4D a,b,c,na,nb,nc,uvAB,uvCMaterial; };
struct alignas(16) GpuMaterial { QVector4D colorRoughness,emissionMetallic,settings; };
struct alignas(16) CameraData { float inverseViewProjection[16]; QVector4D eye,sun,options,render,filter,visualSun; };
struct alignas(16) GpuLight { QVector4D positionType,directionRange,colorIntensity,rightWidth,upHeight,shape,parameters; };
struct GpuPixel { float sum[4]; float normalDepth[4]; };
struct Push { quint32 width,height,sampleStart,sampleCount,bounces,triangleCount,pad0,pad1; };

class Device
{
public:
    VkInstance instance=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t family=0;
    VkCommandPool commands=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout=VK_NULL_HANDLE;
    VkDescriptorPool descriptors=VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE, filterPipeline=VK_NULL_HANDLE;
    VkShaderModule shader=VK_NULL_HANDLE, filterShader=VK_NULL_HANDLE;
    VkSampler sampler=VK_NULL_HANDLE;
    VkQueryPool timestamps=VK_NULL_HANDLE;
    uint32_t timestampBits=0;
    VkPhysicalDeviceProperties properties {};
    PFN_vkCreateAccelerationStructureKHR createAcceleration=nullptr;
    PFN_vkDestroyAccelerationStructureKHR destroyAcceleration=nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR buildSizes=nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR buildAcceleration=nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR accelerationAddress=nullptr;
    std::vector<Buffer> buffers;
    std::vector<Texture> textures;
    std::vector<Acceleration> structures;
    ~Device()
    {
        if(device)
        {
            vkDeviceWaitIdle(device);
            if(filterPipeline) vkDestroyPipeline(device,filterPipeline,nullptr);
            if(filterShader) vkDestroyShaderModule(device,filterShader,nullptr);
            if(pipeline) vkDestroyPipeline(device,pipeline,nullptr);
            if(shader) vkDestroyShaderModule(device,shader,nullptr);
            if(pipelineLayout) vkDestroyPipelineLayout(device,pipelineLayout,nullptr);
            if(descriptors) vkDestroyDescriptorPool(device,descriptors,nullptr);
            if(descriptorLayout) vkDestroyDescriptorSetLayout(device,descriptorLayout,nullptr);
            if(sampler) vkDestroySampler(device,sampler,nullptr);
            if(timestamps) vkDestroyQueryPool(device,timestamps,nullptr);
            for(auto& structure:structures) if(structure.handle) destroyAcceleration(device,structure.handle,nullptr);
            for(auto& texture:textures)
            {
                if(texture.view) vkDestroyImageView(device,texture.view,nullptr);
                if(texture.image) vkDestroyImage(device,texture.image,nullptr);
                if(texture.memory) vkFreeMemory(device,texture.memory,nullptr);
            }
            for(auto& buffer:buffers)
            {
                if(buffer.mapped) vkUnmapMemory(device,buffer.memory);
                if(buffer.handle) vkDestroyBuffer(device,buffer.handle,nullptr);
                if(buffer.memory) vkFreeMemory(device,buffer.memory,nullptr);
            }
            if(commands) vkDestroyCommandPool(device,commands,nullptr);
            vkDestroyDevice(device,nullptr);
        }
        if(instance) vkDestroyInstance(instance,nullptr);
    }
    void removeBuffer(Buffer& target)
    {
        auto found = std::find_if(buffers.begin(), buffers.end(), [&](const Buffer& item) { return item.handle == target.handle; });
        if (found == buffers.end()) return;
        if (found->mapped) vkUnmapMemory(device, found->memory);
        if (found->handle) vkDestroyBuffer(device, found->handle, nullptr);
        if (found->memory) vkFreeMemory(device, found->memory, nullptr);
        buffers.erase(found);
        target = {};
    }
    void clearScene()
    {
        for (auto& structure : structures) if (structure.handle) destroyAcceleration(device, structure.handle, nullptr);
        structures.clear();
        for (auto& texture : textures)
        {
            if (texture.view) vkDestroyImageView(device, texture.view, nullptr);
            if (texture.image) vkDestroyImage(device, texture.image, nullptr);
            if (texture.memory) vkFreeMemory(device, texture.memory, nullptr);
        }
        textures.clear();
        while (!buffers.empty())
        {
            auto buffer = buffers.back();
            removeBuffer(buffer);
        }
    }
    HardwareRaySupport initialize(bool createDevice)
    {
        HardwareRaySupport result;
        VkApplicationInfo app {VK_STRUCTURE_TYPE_APPLICATION_INFO};
        app.pApplicationName="Eldridge Hardware Ray Tracing"; app.apiVersion=VK_API_VERSION_1_2;
        VkInstanceCreateInfo info {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; info.pApplicationInfo=&app;
        VkResult status=vkCreateInstance(&info,nullptr,&instance);
        if(status!=VK_SUCCESS) { result.reason="Vulkan 1.2 loader/driver unavailable"; return result; }
        uint32_t count=0; vkCheck(vkEnumeratePhysicalDevices(instance,&count,nullptr),"Enumerate Vulkan devices");
        std::vector<VkPhysicalDevice> devices(count); vkCheck(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"Read Vulkan devices");
        const char* required[]={VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,VK_KHR_RAY_QUERY_EXTENSION_NAME,VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME};
        for(auto candidate:devices)
        {
            VkPhysicalDeviceProperties candidateProperties; vkGetPhysicalDeviceProperties(candidate,&candidateProperties);
            if(candidateProperties.deviceType==VK_PHYSICAL_DEVICE_TYPE_CPU || candidateProperties.apiVersion<VK_API_VERSION_1_2) continue;
            uint32_t extensionCount=0; vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extensionCount,nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount); vkEnumerateDeviceExtensionProperties(candidate,nullptr,&extensionCount,extensions.data());
            bool found=true;
            for(auto name:required)
                if(std::none_of(extensions.begin(),extensions.end(),[&](auto& extension){return std::strcmp(extension.extensionName,name)==0;})) found=false;
            if(!found) continue;
            VkPhysicalDeviceRayQueryFeaturesKHR ray {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
            VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR}; acceleration.pNext=&ray;
            VkPhysicalDeviceVulkan12Features features12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; features12.pNext=&acceleration;
            VkPhysicalDeviceFeatures2 features {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext=&features12;
            vkGetPhysicalDeviceFeatures2(candidate,&features);
            if(!ray.rayQuery || !acceleration.accelerationStructure || !features12.bufferDeviceAddress) continue;
            uint32_t queueCount=0; vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queueCount,nullptr);
            std::vector<VkQueueFamilyProperties> queues(queueCount); vkGetPhysicalDeviceQueueFamilyProperties(candidate,&queueCount,queues.data());
            for(uint32_t i=0;i<queueCount;++i) if(queues[i].queueFlags&VK_QUEUE_COMPUTE_BIT)
            { physical=candidate; family=i; properties=candidateProperties; timestampBits=queues[i].timestampValidBits; break; }
            if(physical) break;
        }
        if(!physical) { result.reason="No GPU with Vulkan acceleration structures, ray queries and buffer device addresses was found."; return result; }
        result.available=true; result.device=QString::fromUtf8(properties.deviceName);
        if(!createDevice) return result;
        float priority=1;
        VkDeviceQueueCreateInfo queueInfo {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; queueInfo.queueFamilyIndex=family; queueInfo.queueCount=1; queueInfo.pQueuePriorities=&priority;
        VkPhysicalDeviceRayQueryFeaturesKHR ray {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR}; ray.rayQuery=VK_TRUE;
        VkPhysicalDeviceAccelerationStructureFeaturesKHR acceleration {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR}; acceleration.accelerationStructure=VK_TRUE; acceleration.pNext=&ray;
        VkPhysicalDeviceVulkan12Features features12 {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES}; features12.bufferDeviceAddress=VK_TRUE; features12.pNext=&acceleration;
        VkDeviceCreateInfo deviceInfo {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; deviceInfo.pNext=&features12;
        deviceInfo.queueCreateInfoCount=1; deviceInfo.pQueueCreateInfos=&queueInfo; deviceInfo.enabledExtensionCount=3; deviceInfo.ppEnabledExtensionNames=required;
        vkCheck(vkCreateDevice(physical,&deviceInfo,nullptr,&device),"Create ray-query device");
        vkGetDeviceQueue(device,family,0,&queue);
#define LOAD(name,member) member=reinterpret_cast<PFN_##name>(vkGetDeviceProcAddr(device,#name)); if(!member) throw std::runtime_error("Missing " #name)
        LOAD(vkCreateAccelerationStructureKHR,createAcceleration);
        LOAD(vkDestroyAccelerationStructureKHR,destroyAcceleration);
        LOAD(vkGetAccelerationStructureBuildSizesKHR,buildSizes);
        LOAD(vkCmdBuildAccelerationStructuresKHR,buildAcceleration);
        LOAD(vkGetAccelerationStructureDeviceAddressKHR,accelerationAddress);
#undef LOAD
        VkCommandPoolCreateInfo pool {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pool.queueFamilyIndex=family; pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCheck(vkCreateCommandPool(device,&pool,nullptr,&commands),"Create command pool");
        if (timestampBits)
        {
            VkQueryPoolCreateInfo query {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
            query.queryType = VK_QUERY_TYPE_TIMESTAMP; query.queryCount = 2;
            vkCheck(vkCreateQueryPool(device, &query, nullptr, &timestamps), "Create GPU timing queries");
        }
        return result;
    }
    uint32_t memoryType(uint32_t bits,VkMemoryPropertyFlags flags,VkMemoryPropertyFlags preferred=0)
    {
        VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical,&memory);
        // Upload memory is often write-combined. Readback should be CPU-cached.
        if (preferred) for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
            if ((bits & (1u << i)) && (memory.memoryTypes[i].propertyFlags & (flags | preferred)) == (flags | preferred)) return i;
        for(uint32_t i=0;i<memory.memoryTypeCount;++i) if((bits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&flags)==flags) return i;
        throw std::runtime_error("Required Vulkan memory type unavailable");
    }
    Buffer buffer(VkDeviceSize size,VkBufferUsageFlags usage,bool host=false,bool cached=false)
    {
        Buffer result; result.size=std::max<VkDeviceSize>(size,16);
        VkBufferCreateInfo info {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; info.size=result.size; info.usage=usage;
        vkCheck(vkCreateBuffer(device,&info,nullptr,&result.handle),"Create GPU buffer");
        buffers.push_back(result); auto& owned=buffers.back();
        VkMemoryRequirements requirements; vkGetBufferMemoryRequirements(device,result.handle,&requirements);
        VkMemoryAllocateFlagsInfo flags {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO};
        if(usage&VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT) flags.flags=VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT;
        VkMemoryAllocateInfo allocation {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; allocation.pNext=&flags; allocation.allocationSize=requirements.size;
        allocation.memoryTypeIndex=memoryType(requirements.memoryTypeBits,host?VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT:VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                             cached?VK_MEMORY_PROPERTY_HOST_CACHED_BIT:0);
        vkCheck(vkAllocateMemory(device,&allocation,nullptr,&owned.memory),"Allocate GPU buffer memory");
        vkCheck(vkBindBufferMemory(device,owned.handle,owned.memory,0),"Bind GPU buffer memory");
        if(host) vkCheck(vkMapMemory(device,owned.memory,0,owned.size,0,&owned.mapped),"Map host buffer");
        return owned;
    }
    VkDeviceAddress address(const Buffer& buffer)
    { VkBufferDeviceAddressInfo info {VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO}; info.buffer=buffer.handle; return vkGetBufferDeviceAddress(device,&info); }
    VkCommandBuffer begin()
    {
        VkCommandBufferAllocateInfo allocation {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; allocation.commandPool=commands; allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocation.commandBufferCount=1;
        VkCommandBuffer command; vkCheck(vkAllocateCommandBuffers(device,&allocation,&command),"Allocate command buffer");
        VkCommandBufferBeginInfo begin {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkCheck(vkBeginCommandBuffer(command,&begin),"Begin command buffer"); return command;
    }
    void submit(VkCommandBuffer command)
    {
        vkCheck(vkEndCommandBuffer(command),"End command buffer");
        VkFenceCreateInfo fenceInfo {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence;
        vkCheck(vkCreateFence(device,&fenceInfo,nullptr,&fence),"Create render fence");
        VkSubmitInfo submission {VK_STRUCTURE_TYPE_SUBMIT_INFO}; submission.commandBufferCount=1; submission.pCommandBuffers=&command;
        VkResult status=vkQueueSubmit(queue,1,&submission,fence);
        if(status==VK_SUCCESS) status=vkWaitForFences(device,1,&fence,VK_TRUE,60ull*1000*1000*1000);
        vkDestroyFence(device,fence,nullptr);
        vkCheck(status,"Submit/wait for GPU ray tracing");
        vkFreeCommandBuffers(device,commands,1,&command);
    }
    Acceleration build(VkAccelerationStructureTypeKHR type,const VkAccelerationStructureGeometryKHR& geometry,uint32_t primitives)
    {
        VkAccelerationStructureBuildGeometryInfoKHR info {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR};
        info.type=type; info.flags=VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR; info.mode=VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
        info.geometryCount=1; info.pGeometries=&geometry;
        VkAccelerationStructureBuildSizesInfoKHR sizes {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR};
        buildSizes(device,VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,&info,&primitives,&sizes);
        Acceleration result; result.storage=buffer(sizes.accelerationStructureSize,VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR);
        VkAccelerationStructureCreateInfoKHR create {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR}; create.buffer=result.storage.handle; create.size=sizes.accelerationStructureSize; create.type=type;
        vkCheck(createAcceleration(device,&create,nullptr,&result.handle),"Create acceleration structure"); structures.push_back(result);
        VkPhysicalDeviceAccelerationStructurePropertiesKHR properties {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR};
        VkPhysicalDeviceProperties2 deviceProperties {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2}; deviceProperties.pNext=&properties; vkGetPhysicalDeviceProperties2(physical,&deviceProperties);
        VkDeviceSize alignment=properties.minAccelerationStructureScratchOffsetAlignment;
        auto scratch=buffer(sizes.buildScratchSize+alignment,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
        auto raw=address(scratch); info.scratchData.deviceAddress=(raw+alignment-1)&~(alignment-1); info.dstAccelerationStructure=result.handle;
        VkAccelerationStructureBuildRangeInfoKHR range {}; range.primitiveCount=primitives; const auto* ranges=&range;
        auto command=begin(); buildAcceleration(command,1,&info,&ranges);
        VkMemoryBarrier barrier {VK_STRUCTURE_TYPE_MEMORY_BARRIER}; barrier.srcAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        barrier.dstAccessMask=VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        submit(command);
        removeBuffer(scratch);
        return result;
    }
    Texture textureArray(const std::vector<RayMaterial>& materials,int kind)
    {
        int count=std::max(1,int(materials.size())); const int side=256;
        auto staging=buffer(VkDeviceSize(side)*side*4*count,VK_BUFFER_USAGE_TRANSFER_SRC_BIT,true);
        auto* data=static_cast<uchar*>(staging.mapped);
        for(int i=0;i<count;++i)
        {
            QImage image(side,side,QImage::Format_RGBA8888); image.fill(kind==1?QColor(128,128,255):Qt::white);
            if(i<int(materials.size()))
            {
                const auto& material=materials[i];
                const QImage& source=kind==0?material.texture:material.normalTexture;
                if(kind<2 && !source.isNull()) image=source.mirrored().scaled(side,side).convertToFormat(QImage::Format_RGBA8888);
                if(kind==2 && (!material.metallicTexture.isNull() || !material.roughnessTexture.isNull()))
                {
                    auto metal=material.metallicTexture.isNull()?QImage():material.metallicTexture.mirrored().scaled(side,side);
                    auto rough=material.roughnessTexture.isNull()?QImage():material.roughnessTexture.mirrored().scaled(side,side);
                    for(int y=0;y<side;++y) for(int x=0;x<side;++x)
                        image.setPixelColor(x,y,QColor(metal.isNull()?255:metal.pixelColor(x,y).red(),rough.isNull()?255:rough.pixelColor(x,y).red(),255));
                }
            }
            std::memcpy(data+VkDeviceSize(i)*side*side*4,image.constBits(),side*side*4);
        }
        Texture result;
        VkImageCreateInfo create {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; create.imageType=VK_IMAGE_TYPE_2D; create.format=VK_FORMAT_R8G8B8A8_UNORM;
        create.extent={side,side,1}; create.mipLevels=1; create.arrayLayers=count; create.samples=VK_SAMPLE_COUNT_1_BIT; create.tiling=VK_IMAGE_TILING_OPTIMAL;
        create.usage=VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        vkCheck(vkCreateImage(device,&create,nullptr,&result.image),"Create ray-tracing texture array"); textures.push_back(result); auto& owned=textures.back();
        VkMemoryRequirements requirements; vkGetImageMemoryRequirements(device,result.image,&requirements);
        VkMemoryAllocateInfo memory {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; memory.allocationSize=requirements.size; memory.memoryTypeIndex=memoryType(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkCheck(vkAllocateMemory(device,&memory,nullptr,&owned.memory),"Allocate texture array"); vkCheck(vkBindImageMemory(device,result.image,owned.memory,0),"Bind texture array");
        auto command=begin();
        VkImageMemoryBarrier barrier {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.image=result.image; barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,uint32_t(count)};
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED; barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy {}; copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,uint32_t(count)}; copy.imageExtent={side,side,1};
        vkCmdCopyBufferToImage(command,staging.handle,result.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; barrier.newLayout=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,nullptr,0,nullptr,1,&barrier); submit(command);
        removeBuffer(staging);
        VkImageViewCreateInfo view {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; view.image=result.image; view.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY; view.format=VK_FORMAT_R8G8B8A8_UNORM; view.subresourceRange=barrier.subresourceRange;
        vkCheck(vkCreateImageView(device,&view,nullptr,&owned.view),"Create texture view"); return owned;
    }
};
QImage present(const GpuPixel* pixels,int width,int height,float exposure,bool denoise)
{
    QImage image(width,height,QImage::Format_RGB32);
    for(int y=0;y<height;++y) for(int x=0;x<width;++x)
    {
        const auto& center=pixels[y*width+x]; QVector3D color; float total=0;
        int radius=denoise?1:0;
        for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx)
        {
            int sx=std::clamp(x+dx,0,width-1),sy=std::clamp(y+dy,0,height-1); const auto& p=pixels[sy*width+sx];
            float weight=1;
            if(denoise)
            {
                float normalDot=p.normalDepth[0]*center.normalDepth[0]+p.normalDepth[1]*center.normalDepth[1]+p.normalDepth[2]*center.normalDepth[2];
                weight=std::exp(-std::abs(p.normalDepth[3]-center.normalDepth[3])*4.f)*std::pow(std::max(normalDot,0.f),16.f);
                if(dx==0&&dy==0) weight=1;
            }
            color+=QVector3D(p.sum[0],p.sum[1],p.sum[2])*(weight/std::max(p.sum[3],1.f)); total+=weight;
        }
        color/=std::max(total,0.0001f);
        for(int axis=0;axis<3;++axis)
        { float v=std::max(color[axis]*exposure,0.f); v=std::clamp((v*(2.51f*v+0.03f))/(v*(2.43f*v+0.59f)+0.14f),0.f,1.f); color[axis]=std::pow(v,1.f/2.2f); }
        image.setPixelColor(x,y,QColor::fromRgbF(color.x(),color.y(),color.z()));
    }
    return image;
}
}
#endif

HardwareRaySupport VulkanRayTracer::support()
{
#ifdef ENGINE_HAS_VULKAN
    try { Device device; return device.initialize(false); }
    catch(const std::exception& error) { return {false,QString(),error.what()}; }
#else
    return {false,QString(),"This build does not include Vulkan support."};
#endif
}

// Device/pipeline lifetime is independent of the scene and the camera.
struct VulkanRayTracer::Impl
{
#ifdef ENGINE_HAS_VULKAN
    Device gpu;
    Buffer output, outputGpu, cameraBuffer, filterA, filterB, packedGpu;
    int lightCount=0, emitterCount=0;
    bool sceneLights=false;
    LightData visibleSun;
    bool hasSun=false;
    VkDescriptorSet set = VK_NULL_HANDLE;
    uint32_t triangleCount = 0;
    int width = 0, height = 0;
    int sceneUploads = 0;
    double setupMilliseconds = 0;
    double gpuMilliseconds = -1;
    bool initialized = false, sceneReady = false;

    void initialize()
    {
        if (initialized) return;
        auto support = gpu.initialize(true);
        if (!support.available) throw std::runtime_error(support.reason.toStdString());
        VkSamplerCreateInfo sampler {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        sampler.magFilter = sampler.minFilter = VK_FILTER_LINEAR;
        sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
        vkCheck(vkCreateSampler(gpu.device, &sampler, nullptr, &gpu.sampler), "Create ray texture sampler");
        std::array<VkDescriptorSetLayoutBinding,12> bindings {};
        for(uint32_t i=0;i<12;++i)
        {
            bindings[i].binding=i; bindings[i].descriptorCount=1; bindings[i].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
            bindings[i].descriptorType=i==0?VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:i==4?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:(i>=5&&i<=7)?VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }
        VkDescriptorSetLayoutCreateInfo layout {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO}; layout.bindingCount=bindings.size(); layout.pBindings=bindings.data();
        vkCheck(vkCreateDescriptorSetLayout(gpu.device,&layout,nullptr,&gpu.descriptorLayout),"Create ray descriptor layout");
        VkDescriptorPoolSize sizes[]={{VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,7},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,3}};
        VkDescriptorPoolCreateInfo pool {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; pool.maxSets=1; pool.poolSizeCount=4; pool.pPoolSizes=sizes;
        vkCheck(vkCreateDescriptorPool(gpu.device,&pool,nullptr,&gpu.descriptors),"Create ray descriptor pool");
        VkDescriptorSetAllocateInfo allocate {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; allocate.descriptorPool=gpu.descriptors; allocate.descriptorSetCount=1; allocate.pSetLayouts=&gpu.descriptorLayout;
        vkCheck(vkAllocateDescriptorSets(gpu.device,&allocate,&set),"Allocate ray descriptor set");
        QFile code(":/shaders/hardware.comp.spv"); if(!code.open(QIODevice::ReadOnly)) throw std::runtime_error("Hardware SPIR-V shader is missing"); auto bytes=code.readAll();
        if(bytes.size()%4) throw std::runtime_error("Invalid hardware SPIR-V shader");
        VkShaderModuleCreateInfo shaderInfo {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; shaderInfo.codeSize=bytes.size(); shaderInfo.pCode=reinterpret_cast<const uint32_t*>(bytes.constData());
        vkCheck(vkCreateShaderModule(gpu.device,&shaderInfo,nullptr,&gpu.shader),"Create ray-query shader module");
        VkPushConstantRange push {VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(Push)};
        VkPipelineLayoutCreateInfo pipelineLayout {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; pipelineLayout.setLayoutCount=1; pipelineLayout.pSetLayouts=&gpu.descriptorLayout; pipelineLayout.pushConstantRangeCount=1; pipelineLayout.pPushConstantRanges=&push;
        vkCheck(vkCreatePipelineLayout(gpu.device,&pipelineLayout,nullptr,&gpu.pipelineLayout),"Create ray pipeline layout");
        VkComputePipelineCreateInfo pipeline {VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO}; pipeline.layout=gpu.pipelineLayout;
        pipeline.stage={VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}; pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module=gpu.shader; pipeline.stage.pName="main";
        vkCheck(vkCreateComputePipelines(gpu.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&gpu.pipeline),"Create hardware ray-query pipeline");
        QFile filterCode(":/shaders/denoise.comp.spv");
        if (!filterCode.open(QIODevice::ReadOnly)) throw std::runtime_error("Denoising SPIR-V shader is missing");
        auto filterBytes=filterCode.readAll();
        shaderInfo.codeSize=filterBytes.size(); shaderInfo.pCode=reinterpret_cast<const uint32_t*>(filterBytes.constData());
        vkCheck(vkCreateShaderModule(gpu.device,&shaderInfo,nullptr,&gpu.filterShader),"Create denoising shader");
        pipeline.stage.module=gpu.filterShader;
        vkCheck(vkCreateComputePipelines(gpu.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&gpu.filterPipeline),"Create denoising pipeline");
        initialized = true;
    }
    void upload(const RaySceneSnapshot& scene)
    {
        QElapsedTimer clock;
        clock.start();
        initialize();
        if (scene.materials.size() > std::min(256u, gpu.properties.limits.maxImageArrayLayers))
            throw std::runtime_error("Hardware lighting currently supports at most 256 materials.");
        sceneReady = false;
        // Every dispatch has finished before we replace these resources.
        gpu.clearScene();
        output = {}; outputGpu = {}; cameraBuffer = {}; filterA={}; filterB={}; packedGpu={};
        std::vector<GpuTriangle> triangles; triangles.reserve(std::max(size_t(1),scene.triangles.size()));
        std::vector<QVector4D> vertices;
        for(const auto& t:scene.triangles)
        {
            triangles.push_back({QVector4D(t.a,1),QVector4D(t.b,1),QVector4D(t.c,1),QVector4D(t.na,0),QVector4D(t.nb,0),QVector4D(t.nc,0),
                QVector4D(t.uvA.x(),t.uvA.y(),t.uvB.x(),t.uvB.y()),QVector4D(t.uvC.x(),t.uvC.y(),float(t.material),0)});
            vertices.insert(vertices.end(),{QVector4D(t.a,1),QVector4D(t.b,1),QVector4D(t.c,1)});
        }
        if(triangles.empty()) { triangles.resize(1); vertices.resize(3); }
        auto vertexBuffer=gpu.buffer(vertices.size()*sizeof(QVector4D),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,true);
        std::memcpy(vertexBuffer.mapped,vertices.data(),vertices.size()*sizeof(QVector4D));
        VkAccelerationStructureGeometryKHR geometry {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR}; geometry.geometryType=VK_GEOMETRY_TYPE_TRIANGLES_KHR; geometry.flags=VK_GEOMETRY_OPAQUE_BIT_KHR;
        auto& tri=geometry.geometry.triangles; tri.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
        tri.vertexFormat=VK_FORMAT_R32G32B32_SFLOAT; tri.vertexData.deviceAddress=gpu.address(vertexBuffer); tri.vertexStride=sizeof(QVector4D); tri.maxVertex=vertices.size()-1; tri.indexType=VK_INDEX_TYPE_NONE_KHR;
        auto bottom=gpu.build(VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,geometry,scene.triangles.size());
        VkAccelerationStructureDeviceAddressInfoKHR addressInfo {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR}; addressInfo.accelerationStructure=bottom.handle;
        VkAccelerationStructureInstanceKHR instance {}; instance.transform.matrix[0][0]=instance.transform.matrix[1][1]=instance.transform.matrix[2][2]=1;
        instance.mask=0xff; instance.flags=VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR; instance.accelerationStructureReference=gpu.accelerationAddress(gpu.device,&addressInfo);
        auto instanceBuffer=gpu.buffer(sizeof(instance),VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR|VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,true);
        std::memcpy(instanceBuffer.mapped,&instance,sizeof(instance));
        VkAccelerationStructureGeometryKHR topGeometry {VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR}; topGeometry.geometryType=VK_GEOMETRY_TYPE_INSTANCES_KHR;
        topGeometry.geometry.instances.sType=VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR; topGeometry.geometry.instances.data.deviceAddress=gpu.address(instanceBuffer);
        auto top=gpu.build(VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,topGeometry,1);
        gpu.removeBuffer(vertexBuffer);
        gpu.removeBuffer(instanceBuffer);
        if(scene.lights.size()>64) throw std::runtime_error("Vulkan lighting supports up to 64 enabled lights.");
        sceneLights=scene.sceneLights; hasSun=false;
        lightCount=int(scene.lights.size()); emitterCount=0;
        std::vector<GpuLight> lights;
        const float radians=3.14159265359f/180.f;
        for(const auto& l:scene.lights)
        {
            lights.push_back({QVector4D(l.position,float(l.type)),QVector4D(l.direction,l.range),
                QVector4D(l.color,l.intensity),QVector4D(l.right,l.width),QVector4D(l.up,l.height),
                QVector4D(l.radius,l.angle*radians*.5f,std::cos(l.innerCone*radians),std::cos(l.outerCone*radians)),
                QVector4D(l.shadows?1.f:0.f,0,0,0)});
            if(l.type==0&&!hasSun) { visibleSun=l; hasSun=true; }
        }
        // Weight emissive triangles by their area and brightness. Store a CDF for
        // direct sampling, and the same probability on the triangle for MIS.
        std::vector<double> weights(scene.triangles.size(),0);
        double total=0;
        for(size_t i=0;i<scene.triangles.size();++i)
        {
            const auto& t=scene.triangles[i];
            if(t.material<0||size_t(t.material)>=scene.materials.size()) continue;
            auto e=scene.materials[t.material].emission;
            float area=QVector3D::crossProduct(t.b-t.a,t.c-t.a).length()*.5f;
            weights[i]=area*std::max(0.f,QVector3D::dotProduct(e,QVector3D(.2126f,.7152f,.0722f)));
            total+=weights[i];
        }
        double cumulative=0;
        for(size_t i=0;i<weights.size();++i) if(weights[i]>0&&total>0)
        {
            const auto& t=scene.triangles[i]; float probability=float(weights[i]/total);
            cumulative+=weights[i]/total; triangles[i].uvCMaterial.setW(probability);
            GpuLight emitter{}; emitter.colorIntensity=QVector4D(scene.materials[t.material].emission,1);
            float area=QVector3D::crossProduct(t.b-t.a,t.c-t.a).length()*.5f;
            emitter.parameters=QVector4D(float(i),float(cumulative),probability,area);
            lights.push_back(emitter); ++emitterCount;
        }
        if(emitterCount) lights.back().parameters.setY(1.f);
        if(lights.empty()) lights.resize(1);
        auto lightBuffer=gpu.buffer(lights.size()*sizeof(GpuLight),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true);
        std::memcpy(lightBuffer.mapped,lights.data(),lights.size()*sizeof(GpuLight));
        auto triangleBuffer=gpu.buffer(triangles.size()*sizeof(GpuTriangle),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true);
        std::memcpy(triangleBuffer.mapped,triangles.data(),triangles.size()*sizeof(GpuTriangle));
        std::vector<GpuMaterial> materials;
        for(const auto& m:scene.materials) materials.push_back({QVector4D(m.color,m.roughness),QVector4D(m.emission,m.metallic),QVector4D(m.lit?1.f:0.f,m.normalStrength,m.clearCoat,m.normalTexture.isNull()?0.f:1.f)});
        if(materials.empty()) materials.resize(1);
        auto materialBuffer=gpu.buffer(materials.size()*sizeof(GpuMaterial),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,true); std::memcpy(materialBuffer.mapped,materials.data(),materials.size()*sizeof(GpuMaterial));
        output = gpu.buffer(16, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
        outputGpu = gpu.buffer(16, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        filterA=gpu.buffer(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        filterB=gpu.buffer(16,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        packedGpu=gpu.buffer(32,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        cameraBuffer = gpu.buffer(sizeof(CameraData), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true);
        std::array<Texture,3> textures {gpu.textureArray(scene.materials,0),gpu.textureArray(scene.materials,1),gpu.textureArray(scene.materials,2)};
        std::array<VkDescriptorSetLayoutBinding,12> bindings {};
        for (uint32_t i = 0; i < bindings.size(); ++i)
            bindings[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR :
                i == 4 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : (i >= 5 && i <= 7) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        VkWriteDescriptorSetAccelerationStructureKHR accelerationWrite {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR}; accelerationWrite.accelerationStructureCount=1; accelerationWrite.pAccelerationStructures=&top.handle;
        Buffer boundBuffers[]={outputGpu,triangleBuffer,materialBuffer,cameraBuffer,lightBuffer,filterA,filterB,packedGpu};
        VkDescriptorBufferInfo bufferInfos[8]; VkDescriptorImageInfo imageInfos[3]; std::array<VkWriteDescriptorSet,12> writes {};
        for(uint32_t i=0;i<12;++i)
        {
            auto& write=writes[i]; write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            write.dstSet=set; write.dstBinding=i; write.descriptorCount=1; write.descriptorType=bindings[i].descriptorType;
            if(i==0) write.pNext=&accelerationWrite;
            else if(i>=5&&i<=7) { imageInfos[i-5]={gpu.sampler,textures[i-5].view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL}; write.pImageInfo=&imageInfos[i-5]; }
            else
            {
                int index=i<5?i-1:i-4;
                bufferInfos[index]={boundBuffers[index].handle,0,boundBuffers[index].size}; write.pBufferInfo=&bufferInfos[index];
            }
        }
        vkUpdateDescriptorSets(gpu.device,writes.size(),writes.data(),0,nullptr);
        triangleCount = uint32_t(scene.triangles.size());
        ++sceneUploads;
        setupMilliseconds = clock.nsecsElapsed() / 1000000.0;
        sceneReady = true;
    }
    bool trace(const Camera& camera, int w, int h, int start, int samples, int bounces,
               bool hybrid, unsigned int seed, const HardwareRaySettings& settings, std::atomic<bool>& cancel)
    {
        if (cancel) return false;
        if (!sceneReady) throw std::runtime_error("Upload a scene before tracing.");
        width = std::clamp(w, 1, 1920);
        height = std::clamp(h, 1, 1080);
        VkDeviceSize required = VkDeviceSize(width) * height * sizeof(GpuPixel);
        if (output.size < required)
        {
            gpu.removeBuffer(output);
            gpu.removeBuffer(outputGpu);
            output = gpu.buffer(required, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, true);
            gpu.removeBuffer(filterA); gpu.removeBuffer(filterB); gpu.removeBuffer(packedGpu);
            outputGpu=gpu.buffer(required*2,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            filterA=gpu.buffer(required/2,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            filterB=gpu.buffer(required/2,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            packedGpu=gpu.buffer(required,VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            Buffer buffers[]={outputGpu,filterA,filterB,packedGpu};
            uint32_t bindingNumbers[]={1,9,10,11};
            for(int i=0;i<4;++i)
            {
                VkDescriptorBufferInfo info{buffers[i].handle,0,buffers[i].size};
                VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                write.dstSet=set; write.dstBinding=bindingNumbers[i]; write.descriptorCount=1;
                write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write.pBufferInfo=&info;
                vkUpdateDescriptorSets(gpu.device,1,&write,0,nullptr);
            }
        }
        CameraData data {};
        auto inverse = (camera.projectionMatrix(width, height) * camera.viewMatrix()).inverted();
        std::memcpy(data.inverseViewProjection, inverse.constData(), 64);
        data.eye = QVector4D(camera.position, 1);
        data.sun=QVector4D(hasSun?-visibleSun.direction:QVector3D(.4f,1,.3f).normalized(),sceneLights?0.f:settings.sunIntensity);
        data.options=QVector4D(.001f,hybrid?1.f:0.f,float(lightCount),float(emitterCount));
        float angle=(hasSun?visibleSun.angle:settings.sunAngle)*3.14159265359f/360.f;
        data.render=QVector4D(settings.skyIntensity,settings.globalIllumination?1.f:0.f,settings.indirectClamp,settings.sunAngle*3.14159265359f/360.f);
        data.filter=QVector4D(settings.denoiseStrength,0,0,0);
        data.visualSun=QVector4D(hasSun?visibleSun.color*visibleSun.intensity:QVector3D(1,1,1)*settings.sunIntensity,(hasSun||!sceneLights)?angle:0);
        std::memcpy(cameraBuffer.mapped, &data, sizeof(data));

        auto command = gpu.begin();
        if (gpu.timestamps) vkCmdResetQueryPool(command, gpu.timestamps, 0, 2);
        if (start == 0) vkCmdFillBuffer(command, outputGpu.handle, 0, required*2, 0);
        VkMemoryBarrier before {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        before.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        before.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_HOST_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
        if (gpu.timestamps) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, gpu.timestamps, 0);
        vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipeline);
        vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, gpu.pipelineLayout, 0, 1, &set, 0, nullptr);
        Push parameters {uint32_t(width), uint32_t(height), uint32_t(start), uint32_t(samples),
                         uint32_t(bounces), triangleCount, seed, 0};
        vkCmdPushConstants(command, gpu.pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(parameters), &parameters);
        vkCmdDispatch(command, (width + 7) / 8, (height + 7) / 8, 1);
        // Filtering reads accumulated radiance but never writes back into it.
        VkMemoryBarrier filterBarrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        filterBarrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        filterBarrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,gpu.filterPipeline);
        int passes=settings.denoise?std::clamp(settings.denoisePasses,1,4):0;
        for(int pass=0;pass<=passes;++pass)
        {
            vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&filterBarrier,0,nullptr,0,nullptr);
            parameters.pad1=pass==passes?128+passes:pass;
            vkCmdPushConstants(command,gpu.pipelineLayout,VK_SHADER_STAGE_COMPUTE_BIT,0,sizeof(parameters),&parameters);
            vkCmdDispatch(command,(width+7)/8,(height+7)/8,1);
        }
        VkMemoryBarrier after {VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        after.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; after.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 1, &after, 0, nullptr, 0, nullptr);
        if (gpu.timestamps) vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, gpu.timestamps, 1);
        VkBufferCopy copy {0, 0, required};
        vkCmdCopyBuffer(command, packedGpu.handle, output.handle, 1, &copy);
        after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &after, 0, nullptr, 0, nullptr);
        gpu.submit(command);
        if (gpu.timestamps)
        {
            uint64_t times[2] {};
            vkCheck(vkGetQueryPoolResults(gpu.device, gpu.timestamps, 0, 2, sizeof(times), times, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT), "Read GPU timing");
            uint64_t mask = gpu.timestampBits == 64 ? ~uint64_t(0) : (uint64_t(1) << gpu.timestampBits) - 1;
            gpuMilliseconds = double((times[1] - times[0]) & mask) * gpu.properties.limits.timestampPeriod / 1000000.0;
        }
        return !cancel;
    }
#endif
};

VulkanRayTracer::VulkanRayTracer() : impl_(std::make_unique<Impl>()) {}
VulkanRayTracer::~VulkanRayTracer() = default;

bool VulkanRayTracer::setScene(const RaySceneSnapshot& scene, QString& error)
{
    error.clear();
#ifdef ENGINE_HAS_VULKAN
    try { impl_->upload(scene); return true; }
    catch (const std::exception& exception) { error = QString::fromUtf8(exception.what()); }
#else
    error = "This build does not include Vulkan support.";
#endif
    return false;
}

RayLightingFrame VulkanRayTracer::traceLighting(const Camera& camera, int width, int height,
    unsigned int seed, std::atomic<bool>& cancel, QString& error)
{
    return traceFrame(camera, width, height, seed, 1, HardwareRaySettings{}, cancel, error);
}

RayLightingFrame VulkanRayTracer::traceFrame(const Camera& camera, int width, int height,
    unsigned int seed, int samples, const HardwareRaySettings& settings, std::atomic<bool>& cancel, QString& error, int sampleStart)
{
    error.clear();
    RayLightingFrame frame;
#ifdef ENGINE_HAS_VULKAN
    try
    {
        QElapsedTimer clock;
        clock.start();
        samples = std::clamp(samples, 1, 4);
        int bounces = settings.fullFrame ? std::clamp(settings.bounces, 1, 8) : 2;
        bool cumulative = settings.fullFrame && !settings.moving;
        int start = cumulative ? std::clamp(sampleStart, 0, 512) : 0;
        if (!impl_->trace(camera, width, height, start, samples, bounces, !settings.fullFrame, seed, settings, cancel)) return frame;
        frame.width = impl_->width; frame.height = impl_->height; frame.camera = camera;
        frame.fullFrame = settings.fullFrame;
        frame.moving = settings.moving;
        frame.cumulative = cumulative;
        frame.gpuDenoised=settings.denoise; frame.denoisePasses=settings.denoise?std::clamp(settings.denoisePasses,1,4):0;
        frame.historySamples = start + samples;
        frame.lighting.resize(frame.width * frame.height * 4 * sizeof(float));
        frame.surface.resize(frame.lighting.size());
        const auto* pixels = static_cast<const GpuPixel*>(impl_->output.mapped);
        char* lighting = frame.lighting.data();
        char* surface = frame.surface.data();
        for (int i = 0; i < frame.width * frame.height; ++i)
        {
            std::memcpy(lighting + i * 16, pixels[i].sum, 16);
            std::memcpy(surface + i * 16, pixels[i].normalDepth, 16);
        }
        frame.sceneUploads = impl_->sceneUploads;
        frame.setupMilliseconds = impl_->setupMilliseconds;
        frame.renderMilliseconds = clock.nsecsElapsed() / 1000000.0;
        frame.gpuMilliseconds = impl_->gpuMilliseconds;
    }
    catch (const std::exception& exception) { error = QString::fromUtf8(exception.what()); }
#else
    error = "This build does not include Vulkan support.";
#endif
    return frame;
}

QImage VulkanRayTracer::render(const RaySceneSnapshot& scene, const Camera& camera, int width, int height,
    const HardwareRaySettings& settings, std::atomic<bool>& cancel,
    const std::function<void(const QImage&, int)>& progress, QString& error)
{
    error.clear();
    if (cancel) return {};
    VulkanRayTracer renderer;
    if (!renderer.setScene(scene, error)) return {};
#ifdef ENGINE_HAS_VULKAN
    try
    {
        int samples = std::clamp(settings.samples, 1, 1024);
        QImage image;
        QElapsedTimer presentation; presentation.start();
        for (int sample = 0; sample < samples; sample += 4)
        {
            int batch = std::min(4, samples - sample);
            if (!renderer.impl_->trace(camera, width, height, sample, batch,
                                      std::clamp(settings.bounces, 1, 8), false, 0, settings, cancel)) return {};
            // Export preview still uses CPU tone mapping, at most ten updates/second.
            if (sample == 0 || sample + batch == samples || presentation.elapsed() >= 100)
            {
                image = present(static_cast<const GpuPixel*>(renderer.impl_->output.mapped),
                                renderer.impl_->width, renderer.impl_->height, settings.exposure, false);
                if (progress) progress(image, (sample + batch) * 100 / samples);
                presentation.restart();
            }
        }
        return image;
    }
    catch (const std::exception& exception) { error = QString::fromUtf8(exception.what()); }
#endif
    return {};
}
