#include "pch.h"
#include "process.h"
#include "conf.h"
#include "net.h"
#include "context.h"

namespace mob
{

async_pipe::async_pipe()
	: pending_(false)
{
	std::memset(buffer_, 0, sizeof(buffer_));
	std::memset(&ov_, 0, sizeof(ov_));
}

handle_ptr async_pipe::create()
{
	// creating pipe
	handle_ptr out(create_pipe());
	if (out.get() == INVALID_HANDLE_VALUE)
		return {};

	ov_.hEvent = ::CreateEvent(nullptr, TRUE, FALSE, nullptr);

	if (ov_.hEvent == NULL)
	{
		const auto e = GetLastError();
		bail_out("CreateEvent failed", e);
	}

	event_.reset(ov_.hEvent);

	return out;
}

std::string async_pipe::read()
{
	if (pending_)
		return check_pending();
	else
		return try_read();
}

HANDLE async_pipe::create_pipe()
{
	const std::string pipe_name_prefix = "\\\\.\\pipe\\mob_pipe";
	const std::string pipe_name = pipe_name_prefix + std::to_string(rand());

	SECURITY_ATTRIBUTES sa = {};
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.bInheritHandle = TRUE;

	handle_ptr pipe;

	// creating pipe
	{
		HANDLE pipe_handle = ::CreateNamedPipeA(
			pipe_name.c_str(), PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED,
			PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT,
			1, 50'000, 50'000, pipe_timeout, &sa);

		if (pipe_handle == INVALID_HANDLE_VALUE)
		{
			const auto e = GetLastError();
			bail_out("CreateNamedPipe failed", e);
		}

		pipe.reset(pipe_handle);
	}

	{
		// duplicating the handle to read from it
		HANDLE output_read = INVALID_HANDLE_VALUE;

		const auto r = DuplicateHandle(
			GetCurrentProcess(), pipe.get(), GetCurrentProcess(), &output_read,
			0, TRUE, DUPLICATE_SAME_ACCESS);

		if (!r)
		{
			const auto e = GetLastError();
			bail_out("DuplicateHandle for pipe", e);
		}

		stdout_.reset(output_read);
	}


	// creating handle to pipe which is passed to CreateProcess()
	HANDLE output_write = ::CreateFileA(
		pipe_name.c_str(), FILE_WRITE_DATA|SYNCHRONIZE, 0,
		&sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);

	if (output_write == INVALID_HANDLE_VALUE)
	{
		const auto e = GetLastError();
		bail_out("CreateFileW for pipe failed", e);
	}

	return output_write;
}

std::string async_pipe::try_read()
{
	DWORD bytes_read = 0;

	if (!::ReadFile(stdout_.get(), buffer_, buffer_size, &bytes_read, &ov_))
	{
		const auto e = GetLastError();

		switch (e)
		{
			case ERROR_IO_PENDING:
			{
				pending_ = true;
				break;
			}

			case ERROR_BROKEN_PIPE:
			{
				// broken pipe probably means lootcli is finished
				break;
			}

			default:
			{
				bail_out("async_pipe read failed", e);
				break;
			}
		}

		return {};
	}

	return {buffer_, buffer_ + bytes_read};
}

std::string async_pipe::check_pending()
{
	DWORD bytes_read = 0;

	const auto r = WaitForSingleObject(event_.get(), pipe_timeout);

	if (r == WAIT_FAILED) {
		const auto e = GetLastError();
		bail_out("WaitForSingleObject in async_pipe failed", e);
	}

	if (!::GetOverlappedResult(stdout_.get(), &ov_, &bytes_read, FALSE))
	{
		const auto e = GetLastError();

		switch (e)
		{
			case ERROR_IO_INCOMPLETE:
			{
				break;
			}

			case WAIT_TIMEOUT:
			{
				break;
			}

			case ERROR_BROKEN_PIPE:
			{
				// broken pipe probably means lootcli is finished
				break;
			}

			default:
			{
				bail_out("GetOverlappedResult failed in async_pipe", e);
				break;
			}
		}

		return {};
	}

	::ResetEvent(event_.get());
	pending_ = false;

	return {buffer_, buffer_ + bytes_read};
}


process::impl::impl(const impl& i)
	: interrupt(i.interrupt.load())
{
}

process::impl& process::impl::operator=(const impl& i)
{
	interrupt = i.interrupt.load();
	return *this;
}


process::process()
	: flags_(process::noflags), code_(0)
{
}

process::~process()
{
	try
	{
		join();
	}
	catch(...)
	{
	}
}

process process::raw(const std::string& cmd)
{
	process p;
	p.raw_ = cmd;
	return p;
}

process& process::name(const std::string& name)
{
	name_ = name;
	return *this;
}

const std::string& process::name() const
{
	return name_;
}

process& process::binary(const fs::path& p)
{
	bin_ = p;
	return *this;
}

const fs::path& process::binary() const
{
	return bin_;
}

process& process::cwd(const fs::path& p)
{
	cwd_ = p;
	return *this;
}

const fs::path& process::cwd() const
{
	return cwd_;
}

process& process::flags(flags_t f)
{
	flags_ = f;
	return *this;
}

process::flags_t process::flags() const
{
	return flags_;
}

process& process::env(const mob::env& e)
{
	env_ = e;
	return *this;
}

std::string process::make_name() const
{
	if (!name_.empty())
		return name_;

	return make_cmd();
}

std::string process::make_cmd() const
{
	if (!raw_.empty())
		return raw_;

	std::string s = "\"" + bin_.string() + "\" " + cmd_;

	if (flags_ & stdout_is_verbose)
	{
		if (!conf::verbose())
			s += " > NUL";
	}

	return s;
}

void process::pipe_into(const process& p)
{
	raw_ = make_cmd() + " | " + p.make_cmd();
}

void process::run()
{
	if (!cwd_.empty())
		debug("> cd " + cwd_.string());

	const auto what = make_cmd();
	debug("> " + what);

	if (conf::dry())
		return;

	do_run(what);
}

void process::do_run(const std::string& what)
{
	STARTUPINFOA si = { .cb=sizeof(si) };
	PROCESS_INFORMATION pi = {};

	auto process_stdout = impl_.stdout_pipe.create();
	si.hStdOutput = process_stdout.get();

	auto process_stderr = impl_.stderr_pipe.create();
	si.hStdError = process_stderr.get();

	si.dwFlags = STARTF_USESTDHANDLES;

	const std::string cmd = this_env::get("COMSPEC");
	const std::string args = "/C \"" + what + "\"";

	const char* cwd_p = nullptr;
	std::string cwd_s;

	if (!cwd_.empty())
	{
		create_directories(cwd_);
		cwd_s = cwd_.string();
		cwd_p = (cwd_s.empty() ? nullptr : cwd_s.c_str());
	}

	const auto r = ::CreateProcessA(
		cmd.c_str(), const_cast<char*>(args.c_str()),
		nullptr, nullptr, TRUE, CREATE_NEW_PROCESS_GROUP,
		env_.get_pointers(), cwd_p, &si, &pi);

	if (!r)
	{
		const auto e = GetLastError();
		bail_out("failed to start '" + cmd + "'", e);
	}

	::CloseHandle(pi.hThread);
	impl_.handle.reset(pi.hProcess);
}

void process::interrupt()
{
	impl_.interrupt = true;
}

void process::join()
{
	if (!impl_.handle)
		return;

	bool interrupted = false;

	for (;;)
	{
		const auto r = WaitForSingleObject(impl_.handle.get(), 100);

		if (r == WAIT_OBJECT_0)
		{
			// done
			GetExitCodeProcess(impl_.handle.get(), &code_);

			if ((flags_ & allow_failure) || impl_.interrupt)
				break;

			if (code_ != 0)
			{
				impl_.handle = {};
				bail_out(make_name() + " returned " + std::to_string(code_));
			}

			break;
		}

		if (r == WAIT_TIMEOUT)
		{
			std::string s = impl_.stdout_pipe.read();
			//if (!s.empty())
			//	std::cout << "stdin: " << s << "\n";

			s = impl_.stderr_pipe.read();
			//if (!s.empty())
			//	std::cout << "stderr: " << s << "\n";


			if (impl_.interrupt && !interrupted)
			{
				const auto pid = GetProcessId(impl_.handle.get());

				if (pid == 0)
				{
					error("process id is 0, terminating instead");
					::TerminateProcess(impl_.handle.get(), 0xffff);
					break;
				}
				else
				{
					debug("sending sigint to " + std::to_string(pid));
					GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, pid);
				}

				interrupted = true;
			}

			continue;
		}

		const auto e = GetLastError();
		impl_.handle = {};
		bail_out("failed to wait on process", e);
	}

	impl_.handle = {};
}

int process::exit_code() const
{
	return static_cast<int>(code_);
}

void process::add_arg(const std::string& k, const std::string& v, arg_flags f)
{
	if ((f & quiet) && conf::verbose())
		return;

	if (k.empty() && v.empty())
		return;

	if (k.empty())
		cmd_ += " " + v;
	else if ((f & nospace) || k.back() == '=')
		cmd_ += " " + k + v;
	else
		cmd_ += " " + k + " " + v;
}

std::string process::arg_to_string(const char* s, bool force_quote)
{
	if (force_quote)
		return "\"" + std::string(s) + "\"";
	else
		return s;
}

std::string process::arg_to_string(const std::string& s, bool force_quote)
{
	if (force_quote)
		return "\"" + std::string(s) + "\"";
	else
		return s;
}

std::string process::arg_to_string(const fs::path& p, bool)
{
	return "\"" + p.string() + "\"";
}

std::string process::arg_to_string(const url& u, bool force_quote)
{
	if (force_quote)
		return "\"" + u.string() + "\"";
	else
		return u.string();
}

}	// namespace