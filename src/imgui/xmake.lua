local install_dir="build/tmp/imgui/"
local prefix_dir=os.projectdir().."//build/tmp/"

add_requires("glfw",{alias = "glfw",configs = { build = "missing"} })

target("klib.imgui-glfw-vulkan")
    set_group("klib")
    set_kind("static")

    on_load(function (target) 
        --print(os.exists(install_dir))
        if not os.exists(install_dir) then
            print("git clone imgui!")
            local cmd="git clone --depth=1 -b docking https://github.com/ocornut/imgui.git "..install_dir
            os.run(cmd)
        end
    end)

    add_defines("IMGUI_DEFINE_MATH_OPERATORS")

    add_files(prefix_dir.."imgui/*.cpp")
    add_headerfiles(prefix_dir.."imgui/*.h")

    add_files(prefix_dir.."imgui/backends/imgui_impl_glfw.cpp")
    add_headerfiles(prefix_dir.."imgui/backends/imgui_impl_glfw.h")
    add_packages("glfw",{public =true})

    add_files(prefix_dir.."imgui/backends/imgui_impl_vulkan.cpp")
    add_headerfiles(prefix_dir.."imgui/backends/imgui_impl_vulkan.h")

    add_includedirs(prefix_dir.."imgui",{public=true})
    add_includedirs(prefix_dir.."imgui/backends",{public=true})

    -- Use the SDK configured by the Vulkan installer, with the local SDK as fallback.
    local vulkan_sdk = os.getenv("VULKAN_SDK") or "D:/VulkanSDK/1.4.363.0"
    -- 头文件
    add_includedirs(vulkan_sdk .. "/Include")

    -- 链接库目录
    add_linkdirs(vulkan_sdk .. "/Lib")

    -- 链接 Vulkan 导入库
    add_links("vulkan-1")

    add_runenvs("PATH", vulkan_sdk .. "/Bin")