#include "auth.hpp"
#include <termios.h>
#include <stdio.h>

using namespace libtorrent;

int main(int argc, char* argv[])
{
	if (argc != 3 || atoi(argv[2]) < 0 || atoi(argv[2]) > 10000)
	{
		fprintf(stderr, "usage:\n"
			"add_user username group-number\n\n"
			"the user is added to users.conf in\n"
			"current working directory.\n"
			"group numbers may not be negative.\n");
		return 1;
	}

	auth authorizer;

	char password[1024];
	printf("enter password: ");
	fflush(stdout);
	if (fgets(password, 1024, stdin) == NULL)
		return 1;

	int pwdlen = strlen(password);
	while (pwdlen > 0 && password[pwdlen-1] == '\n')
	{
		--pwdlen;
		password[pwdlen] = '\0';
	}

	error_code ec;
	authorizer.load_accounts("./users.conf", ec);
	authorizer.add_account(argv[1], password, atoi(argv[2]));
	ec.clear();
	authorizer.save_accounts("./users.conf", ec);
	if (ec)
	{
		fprintf(stderr, "failed to save users file: %s\n", ec.message().c_str());
	}
	return 0;
}

