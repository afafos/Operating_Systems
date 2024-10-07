#include "demon.h"

#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <unordered_set>
#include <vector>

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <syslog.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace demon
{

struct config_line
{
	fs::path from;
	fs::path to;
	std::string ext;
};

static const std::string IDENT("demon");
static const std::string PID_FILE("/var/run/" + IDENT + ".pid");
static std::string CONFIG_FILE = {};

static bool kill_existing_process(const std::string& pid_file)
{
	std::ifstream fin(pid_file);

	if (!fin.is_open())
	{
		return true;
	}

	int pid;
	fin >> pid;

	if (fin.fail())
	{
		fin.close();
		return false;
	}
	
	fin.close();
	errno = 0;

	if (kill(pid, SIGTERM) < 0 && errno != ESRCH)
	{
		return false;
	}

	std::ofstream out(pid_file);

	if (!out.is_open())
	{
		return true;
	}

	out << getpid() << std::endl;
	out.close();

	return true;
}

static bool read_config(std::vector<config_line>& config)
{
	std::ifstream fin(CONFIG_FILE);

	if (!fin.is_open())
	{
		return true;
	}

	std::string line;

	while (std::getline(fin, line))
	{
		config_line config_line;
		std::stringstream ss(line);
		ss >> config_line.from >> config_line.to >> config_line.ext;

		if (ss.fail())
		{
			return false;
		}

		syslog(LOG_DEBUG, "Read config line: '%s' -> '%s' (%s)",
			   config_line.from.c_str(), config_line.to.c_str(), config_line.ext.c_str());
		config.push_back(std::move(config_line));
	}

	fin.close();

	return true;
}

inline bool ends_with(const std::string& value, const std::string& ending)
{
    return value.size() > ending.size() &&
		   std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

static void work(const std::vector<config_line>& config)
{
	std::unordered_set<fs::path> cleaned_dirs;

	for (const auto& config_line : config)
	{
		try
		{
			if (cleaned_dirs.find(config_line.to) == cleaned_dirs.end())
			{
				for (const auto& entry : fs::directory_iterator(config_line.to))
				{
					fs::remove(entry.path());
				}

				cleaned_dirs.insert(config_line.to);
			}

			for (const auto& entry : fs::directory_iterator(config_line.from))
			{
				if (!entry.is_regular_file() ||
					!ends_with(entry.path(), config_line.ext))
				{
					continue;
				}

				syslog(LOG_DEBUG, "Copy '%s' -> '%s'", entry.path().c_str(), config_line.to.c_str());

				try
				{
					if (!fs::copy_file(entry.path(), config_line.to / entry.path().filename()))
					{
						syslog(LOG_ERR, "Failed copying '%s'", entry.path().c_str());
					}
				}
				catch(const std::exception& e)
				{
					syslog(LOG_ERR, "%s", e.what());
				}
			}
		}
		catch (const std::exception& e)
		{
			syslog(LOG_ERR, "%s", e.what());
		}
	}
}

static void sighup_fun(int sig)
{
	syslog(LOG_NOTICE, "Reload config");

	std::vector<config_line> config;

	if (!read_config(config))
	{
		syslog(LOG_ERR, "Failed reloading config");
		closelog();
		exit(1);
	}

	work(config);
	syslog(LOG_DEBUG, "Work done");
}

static void sigterm_fun(int sig)
{
	syslog(LOG_NOTICE, "Terminate");
	closelog();
	exit(0);
}

bool start(const std::string& config_file)
{
	CONFIG_FILE = config_file;

	openlog(IDENT.c_str(), LOG_CONS | LOG_PID | LOG_NDELAY, LOG_LOCAL0);
	syslog(LOG_NOTICE, "Start");
	setsid();

	if (chdir("/root") == -1)
	{
		syslog(LOG_ERR, "chdir failed");
		return false;
	}

	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	if (!kill_existing_process(PID_FILE))
	{
		return false;
	}

	struct sigaction sighup_action;
	memset(&sighup_action, 0, sizeof(sighup_action));
	sighup_action.sa_handler = &sighup_fun;

	struct sigaction sigterm_action;
	memset(&sigterm_action, 0, sizeof(sigterm_action));
	sigterm_action.sa_handler = &sigterm_fun;

	sigaction(SIGHUP, &sighup_action, NULL);
	sigaction(SIGTERM, &sigterm_action, NULL);

	std::vector<config_line> config;

	if (!read_config(config))
	{
		return false;
	}

	work(config);
	sleep(UINT_MAX);

	return true;
}

} // namespace demon
