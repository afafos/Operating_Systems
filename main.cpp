#include <iostream>

#include <limits.h>
#include <unistd.h>

#include "demon.h"

int main()
{
	static const char config[] = "config.cfg";
	char config_file[PATH_MAX];

	if (realpath(config, config_file) == NULL)
	{
		std::cerr << "realpath failed\n";
		return 1;
	}

	int daemon_pid = fork();

	if (daemon_pid == 0)
	{
		return demon::start(config_file) ? 0 : 1;
	}
	else if (daemon_pid < 0)
	{
		std::cerr << "fork failed\n";
		return 1;
	}

	return 0;
}
