#include "pch.h"
#include "tasks.h"

namespace mob::tasks
{

namespace
{

url source_url()
{
	return
		"https://github.com/microsoft/DirectXTex/archive/refs/tags/" +
		directxtex::version() + ".zip";
}

cmake create_cmake_tool(
	const fs::path& src_path, cmake::ops o=cmake::ops::generate)
{
	return std::move(cmake(o)
		.generator(cmake::vs)
		.root(src_path)
		.prefix(src_path / "build")
		.def("BUILD_TOOLS", "OFF")
		.def("BUILD_SAMPLE", "OFF"));
}

fs::path solution_path()
{
	const auto build_path = create_cmake_tool(directxtex::source_path())
		.build_path();

	return build_path / "INSTALL.vcxproj";
}

msbuild create_msbuild_tool(msbuild::ops o=msbuild::ops::build)
{
	return std::move(msbuild(o)
		.solution(solution_path()));
}

}	// namespace


directxtex::directxtex()
	: basic_task("directxtex")
{
}

std::string directxtex::version()
{
	return conf().version().get("directxtex");
}

bool directxtex::prebuilt()
{
	// no prebuilts available
	return false;
}

fs::path directxtex::source_path()
{
	return conf().path().build() / ("directxtex-" + version());
}

void directxtex::do_clean(clean c)
{
	// delete download
	if (is_set(c, clean::redownload))
		run_tool(downloader(source_url(), downloader::clean));

	// delete the whole directory
	if (is_set(c, clean::reextract))
	{
		cx().trace(context::reextract, "deleting {}", source_path());
		op::delete_directory(cx(), source_path(), op::optional);

		// no need to do anything else
		return;
	}

	// cmake clean
	if (is_set(c, clean::reconfigure))
		run_tool(create_cmake_tool(source_path(), cmake::clean));

	// msbuild clean
	if (is_set(c, clean::rebuild))
		run_tool(create_msbuild_tool(msbuild::clean));
}

void directxtex::do_fetch()
{
	const auto file = run_tool(downloader(source_url()));

	run_tool(extractor()
		.file(file)
		.output(source_path()));
}

void directxtex::do_build_and_install()
{
	run_tool(create_cmake_tool(source_path()));
	run_tool(create_msbuild_tool());
}

}	// namespace
