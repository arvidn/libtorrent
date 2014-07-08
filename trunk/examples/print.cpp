#ifdef _WIN32

#include <windows.h>
#include <conio.h>

#else

#include <unistd.h> // for close()
#include <fcntl.h> // for open()
#include <sys/ioctl.h>

#endif

#include "print.hpp"

#include <cmath>

char const* esc(char const* code)
{
	// this is a silly optimization
	// to avoid copying of strings
	enum { num_strings = 200 };
	static char buf[num_strings][20];
	static int round_robin = 0;
	char* ret = buf[round_robin];
	++round_robin;
	if (round_robin >= num_strings) round_robin = 0;
	ret[0] = '\033';
	ret[1] = '[';
	int i = 2;
	int j = 0;
	while (code[j]) ret[i++] = code[j++];
	ret[i++] = 'm';
	ret[i++] = 0;
	return ret;
}

std::string to_string(int v, int width)
{
	char buf[100];
	snprintf(buf, sizeof(buf), "%*d", width, v);
	return buf;
}

std::string add_suffix(float val, char const* suffix)
{
	std::string ret;
	if (val == 0)
	{
		ret.resize(4 + 2, ' ');
		if (suffix) ret.resize(4 + 2 + strlen(suffix), ' ');
		return ret;
	}

	const char* prefix[] = {"kB", "MB", "GB", "TB"};
	const int num_prefix = sizeof(prefix) / sizeof(const char*);
	for (int i = 0; i < num_prefix; ++i)
	{
		val /= 1000.f;
		if (std::fabs(val) < 1000.f)
		{
			ret = to_string(val, 4);
			ret += prefix[i];
			if (suffix) ret += suffix;
			return ret;
		}
	}
	ret = to_string(val, 4);
	ret += "PB";
	if (suffix) ret += suffix;
	return ret;
}

std::string color(std::string const& s, color_code c)
{
	if (c == col_none) return s;

	char buf[1024];
	snprintf(buf, sizeof(buf), "\x1b[3%dm%s\x1b[39m", c, s.c_str());
	return buf;
}

std::string const& progress_bar(int progress, int width, color_code c
	, char fill, char bg, std::string caption)
{
	static std::string bar;
	bar.clear();
	bar.reserve(width + 10);

	int progress_chars = (progress * width + 500) / 1000;

	if (caption.empty())
	{
		char code[10];
		snprintf(code, sizeof(code), "\x1b[3%dm", c);
		bar = code;
		std::fill_n(std::back_inserter(bar), progress_chars, fill);
		std::fill_n(std::back_inserter(bar), width - progress_chars, bg);
		bar += esc("39");
	}
	else
	{
		// foreground color (depends a bit on background color)
		color_code tc = col_black;
		if (c == col_black || c == col_blue)
			tc = col_white;

		caption.resize(width, ' ');

		char str[256];
		snprintf(str, sizeof(str), "\x1b[4%d;3%dm%s\x1b[48;5;238m\x1b[37m%s\x1b[49;39m"
			, c, tc, caption.substr(0, progress_chars).c_str(), caption.substr(progress_chars).c_str());
		bar = str;
	}
	return bar;
}

void set_cursor_pos(int x, int y)
{
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	COORD c = {x, y};
	SetConsoleCursorPosition(out, c);
#else
	printf("\033[%d;%dH", y + 1, x + 1);
#endif
}

void clear_screen()
{
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

	COORD c = {0, 0};
	CONSOLE_SCREEN_BUFFER_INFO si;
	GetConsoleScreenBufferInfo(out, &si);
	DWORD n;
	FillConsoleOutputCharacter(out, ' ', si.dwSize.X * si.dwSize.Y, c, &n);
	FillConsoleOutputAttribute(out, 0x7, si.dwSize.X * si.dwSize.Y, c, &n);
#else
	printf("\033[2J");
#endif
}

void clear_below(int y)
{
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

	COORD c = {0, y};
	SetConsoleCursorPosition(out, c);
	CONSOLE_SCREEN_BUFFER_INFO si;
	GetConsoleScreenBufferInfo(out, &si);
	DWORD n;
	FillConsoleOutputCharacter(out, ' ', si.dwSize.X * (si.dwSize.Y - y), c, &n);
	FillConsoleOutputAttribute(out, 0x7, si.dwSize.X * (si.dwSize.Y - y), c, &n);
#else
	printf("\033[%d;1H\033[J", y + 1);
#endif

}

void terminal_size(int* terminal_width, int* terminal_height)
{
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO coninfo;
	if (GetConsoleScreenBufferInfo(out, &coninfo))
	{
		*terminal_width = coninfo.dwSize.X;
		*terminal_height = coninfo.srWindow.Bottom - coninfo.srWindow.Top;
#else
	int tty = open("/dev/tty", O_RDONLY);
	winsize size;
	int ret = ioctl(tty, TIOCGWINSZ, (char*)&size);
	close(tty);
	if (ret == 0)
	{
		*terminal_width = size.ws_col;
		*terminal_height = size.ws_row;
#endif

		if (*terminal_width < 64)
			*terminal_width = 64;
		if (*terminal_height < 25)
			*terminal_height = 25;
	}
	else
	{
		*terminal_width = 190;
		*terminal_height = 100;
	}
}

#ifdef _WIN32
void apply_ansi_code(int* attributes, bool* reverse, int code)
{
	const static int color_table[8] =
	{
		0, // black
		FOREGROUND_RED, // red
		FOREGROUND_GREEN, // green
		FOREGROUND_RED | FOREGROUND_GREEN, // yellow
		FOREGROUND_BLUE, // blue
		FOREGROUND_RED | FOREGROUND_BLUE, // magenta
		FOREGROUND_BLUE | FOREGROUND_GREEN, // cyan
		FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE // white
	};

	enum
	{
		foreground_mask = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
		background_mask = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE
	};

	const static int fg_mask[2] = {foreground_mask, background_mask};
	const static int bg_mask[2] = {background_mask, foreground_mask};
	const static int fg_shift[2] = { 0, 4};
	const static int bg_shift[2] = { 4, 0};

	if (code == 0)
	{
		// reset
		*attributes = color_table[7];
		*reverse = false;
	}
	else if (code == 7)
	{
		if (*reverse) return;
		*reverse = true;
		int fg_col = *attributes & foreground_mask;
		int bg_col = (*attributes & background_mask) >> 4;
		*attributes &= ~(foreground_mask + background_mask);
		*attributes |= fg_col << 4;
		*attributes |= bg_col;
	}
	else if (code >= 30 && code <= 37)
	{
		// foreground color
		*attributes &= ~fg_mask[*reverse];
		*attributes |= color_table[code - 30] << fg_shift[*reverse];
	}
	else if (code >= 40 && code <= 47)
	{
		// foreground color
		*attributes &= ~bg_mask[*reverse];
		*attributes |= color_table[code - 40] << bg_shift[*reverse];
	}
}
#endif
void print(char const* str)
{
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

	char* buf = (char*)str;

	int current_attributes = 7;
	bool reverse = false;
	SetConsoleTextAttribute(out, current_attributes);

	char* start = buf;
	DWORD written;
	while (*buf != 0)
	{
		if (*buf == '\033' && buf[1] == '[')
		{
			*buf = 0;
			WriteFile(out, start, buf - start, &written, NULL);
			buf += 2; // skip escape and '['
			start = buf;
		one_more:
			while (*buf != 'm' && *buf != ';' && *buf != 0) ++buf;
			if (*buf == 0) break;
			int code = atoi(start);
			apply_ansi_code(&current_attributes, &reverse, code);
			if (*buf == ';')
			{
				++buf;
				start = buf;
				goto one_more;
			}
			SetConsoleTextAttribute(out, current_attributes);
			++buf; // skip 'm'
			start = buf;
		}
		else
		{
			++buf;
		}
	}
	WriteFile(out, start, buf - start, &written, NULL);

#else
	puts(str);
#endif
}

