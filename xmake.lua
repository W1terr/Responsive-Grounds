-- include subprojects
includes("lib/commonlibsse-ng")

-- set project constants
set_project("ResponsiveGrounds")
set_version("1.0.0")
set_license("GPL-3.0")
set_languages("c++23")
set_warnings("allextra")

-- add common rules
add_rules("mode.debug", "mode.releasedbg")

add_requires("nlohmann_json")

-- define targets
target("ResponsiveGrounds")
    set_version("1.0.0")
    add_rules("commonlibsse-ng.plugin", {
        name = "ResponsiveGrounds",
        author = "W1ter",
        description = "The ground answers to everything that moves on it: leaves, dust and pebbles fly up"
    })

    add_packages("nlohmann_json")

    add_files("src/**.cpp")
    add_headerfiles("src/**.h")
    add_includedirs("src")
    set_pcxxheader("src/pch.h")

    -- third party: SKSE Menu Framework API (header only)
    add_headerfiles("lib/SKSEMenuFramework/*.h")
    add_includedirs("lib/SKSEMenuFramework")
