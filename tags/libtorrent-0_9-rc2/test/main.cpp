#include <iostream>

int test_main();

bool tests_failure = false;

void report_failure(char const* err, char const* file, int line)
{
	std::cerr << file << ":" << line << "\"" << err << "\"\n";
	tests_failure = true;
}

int main()
{
	try
	{
		test_main();
		return tests_failure ? 1 : 0;
	}
	catch (std::exception const& e)
	{
		std::cerr << "Terminated with exception: \"" << e.what() << "\"\n";
		return 1;
	}
	catch (...)
	{
		std::cerr << "Terminated with unknown exception\n";
		return 1;
	}
}

