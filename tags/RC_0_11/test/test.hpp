#ifndef TEST_HPP
#define TEST_HPP

void report_failure(char const* str, char const* file, int line);

#if defined(_MSC_VER)
#define COUNTER_GUARD(x)
#else
#define COUNTER_GUARD(type) \
    struct BOOST_PP_CAT(type, _counter_guard) \
    { \
        ~BOOST_PP_CAT(type, _counter_guard()) \
        { \
            TEST_CHECK(counted_type<type>::count == 0); \
        } \
    } BOOST_PP_CAT(type, _guard)
#endif

#define TEST_REPORT_AUX(x, line, file) \
	report_failure(x, line, file)

#define TEST_CHECK(x) \
    if (!(x)) \
        TEST_REPORT_AUX("TEST_CHECK failed: \"" #x "\"", __FILE__, __LINE__)

#define TEST_ERROR(x) \
	TEST_REPORT_AUX((std::string("ERROR: \"") + x + "\"").c_str(), __FILE__, __LINE__)

#define TEST_NOTHROW(x) \
	try \
	{ \
		x; \
	} \
	catch (...) \
	{ \
		TEST_ERROR("Exception thrown: " #x); \
	}

#endif // TEST_HPP

