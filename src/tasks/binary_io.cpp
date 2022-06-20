#include "pch.h"
#include "tasks.h"

namespace mob::tasks
{

namespace
{

url source_url()
{
	return
		"https://github.com/Ryan-rsm-McKenzie/binary_io/archive/refs/tags/" +
		binary_io::version() + ".zip";
}

cmake create_cmake_tool(
	const fs::path& src_path, cmake::ops o=cmake::ops::generate)
{
	return std::move(cmake(o)
		.generator(cmake::vs)
		.root(src_path)
		.prefix(src_path / "build")
		.def("BUILD_TESTING", "OFF"));
}

fs::path solution_path()
{
	const auto build_path = create_cmake_tool(binary_io::source_path())
		.build_path();

	return build_path / "INSTALL.vcxproj";
}

msbuild create_msbuild_tool(msbuild::ops o=msbuild::ops::build)
{
	return std::move(msbuild(o)
		.solution(solution_path()));
}

}	// namespace


binary_io::binary_io()
	: basic_task("binary_io")
{
}

std::string binary_io::version()
{
	return conf().version().get("binary_io");
}

bool binary_io::prebuilt()
{
	// no prebuilts available
	return false;
}

fs::path binary_io::source_path()
{
	return conf().path().build() / ("binary_io-" + version());
}

void binary_io::do_clean(clean c)
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

void binary_io::do_fetch()
{
	const auto file = run_tool(downloader(source_url()));

	run_tool(extractor()
		.file(file)
		.output(source_path()));
}

void binary_io::do_build_and_install()
{
	run_tool(create_cmake_tool(source_path()));
	run_tool(create_msbuild_tool());
}

}	// namespace
