#include "pch.h"
#include "tasks.h"

namespace mob::tasks
{

namespace
{

url source_url()
{
	return
		"https://github.com/Ryan-rsm-McKenzie/bsa/archive/refs/tags/" +
		bsa::version() + ".zip";
}

cmake create_cmake_tool(
	const fs::path& src_path, cmake::ops o=cmake::ops::generate)
{
	std::string prefixPath;
	const auto prefix = [&](const std::filesystem::path& p) {
		if (!prefixPath.empty())
			prefixPath += ';';
		prefixPath += p.string();
	};

	prefix(binary_io::source_path() / "build");
	prefix(directxtex::source_path() / "build");
	prefix(mmio::source_path() / "build");
	prefix(zlib::source_path());

	return std::move(cmake(o)
		.generator(cmake::vs)
		.root(src_path)
		.prefix(src_path / "build")
		.def("BUILD_TESTING", "OFF")
		.def("CMAKE_PREFIX_PATH", prefixPath)
		.def("LZ4_INCLUDE_DIR:PATH", lz4::source_path() / "lib")
		.def("LZ4_LIBRARY_RELEASE:PATH", lz4::source_path() / "bin" / "liblz4.lib"));
}

fs::path solution_path()
{
	const auto build_path = create_cmake_tool(bsa::source_path())
		.build_path();

	return build_path / "INSTALL.vcxproj";
}

msbuild create_msbuild_tool(msbuild::ops o=msbuild::ops::build)
{
	return std::move(msbuild(o)
		.solution(solution_path()));
}

}	// namespace


bsa::bsa()
	: basic_task("bsa")
{
}

std::string bsa::version()
{
	return conf().version().get("bsa");
}

bool bsa::prebuilt()
{
	// no prebuilts available
	return false;
}

fs::path bsa::source_path()
{
	return conf().path().build() / ("bsa-" + version());
}

void bsa::do_clean(clean c)
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

void bsa::do_fetch()
{
	const auto file = run_tool(downloader(source_url()));

	run_tool(extractor()
		.file(file)
		.output(source_path()));
}

void bsa::do_build_and_install()
{
	run_tool(create_cmake_tool(source_path()));
	run_tool(create_msbuild_tool());
}

}	// namespace
