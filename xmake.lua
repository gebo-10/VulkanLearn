set_project("VulkanLearnProject")
set_version("0.0.1")

add_rules("plugin.compile_commands.autoupdate", {outputdir = ".vscode"}) 

add_rules("mode.debug")
--add_rules("mode.release")

set_languages("c++17")
set_allowedarchs("x64")

if is_plat("windows") then
    add_cxxflags("/utf-8", {force = true})
end

set_targetdir("output")
set_rundir("$(projectdir)")

add_requires("spdlog", { alias = "spdlog"})
add_requires("glm", { alias = "glm"})
add_requires("glfw",{alias = "glfw",configs = { build = "missing"} })
add_requires("stb", {alias = "stb", configs = {build = "missing"}})
--add_requires("SPIRV-Reflect")

--includes("external/klib/core/xmake.lua")
includes("src/imgui/xmake.lua")

add_defines("DEBUG")
add_defines("SPDLOG_ACTIVE_LEVEL=0")

target("VulkanLearn")
    set_kind("binary")
    add_files("src/*.cpp")
    add_includedirs("src")

    add_headerfiles("src/**.h")

    --add_deps("klib.core")
    add_deps("klib.imgui-glfw-vulkan")

    add_packages("spdlog", "glfw", "glm", "stb")

    -- Use the SDK configured by the Vulkan installer, with the local SDK as fallback.
    local vulkan_sdk = os.getenv("VULKAN_SDK") or "D:/VulkanSDK/1.4.363.0"
    -- 头文件
    add_includedirs(vulkan_sdk .. "/Include")

    -- 链接库目录
    add_linkdirs(vulkan_sdk .. "/Lib")

    -- 链接 Vulkan 导入库
    add_links("vulkan-1")

    add_runenvs("PATH", vulkan_sdk .. "/Bin")

    before_build(function (target)
        local compiler = path.join(vulkan_sdk, "Bin", "slangc.exe")
        if not os.isfile(compiler) then
            raise("Slang shader compiler was not found: " .. compiler)
        end

        local shader_output = path.join(target:targetdir(), "shaders")
        os.mkdir(shader_output)
        local shaders = {
            {entry = "vertexMain",
             stage = "vertex", output = "triangle.vert.spv"},
            {entry = "fragmentMain",
             stage = "fragment", output = "triangle.frag.spv"}
        }
        for _, shader in ipairs(shaders) do
            local source = path.join(os.projectdir(), "assets", "shaders", "triangle.slang")
            local output = path.join(shader_output, shader.output)
            os.execv(compiler, {
                source, "-target", "spirv", "-profile", "spirv_1_4",
                "-entry", shader.entry, "-stage", shader.stage, "-o", output
            })
        end
    end)

    add_syslinks("Cfgmgr32","version","winmm","user32","Shell32","ws2_32","advapi32","Bcrypt","Advapi32","Iphlpapi","Psapi","userenv","d3dcompiler")
    add_syslinks("dwmapi","Shcore","Gdi32","opengl32","Cfgmgr32","version","winmm","user32","Shell32","ws2_32","advapi32","Bcrypt","Advapi32","Iphlpapi","Psapi","userenv","d3dcompiler")
