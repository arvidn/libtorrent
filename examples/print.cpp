#ifdef _WIN32

#include <windows.h>
#include <conio.h>

#else

#include <unistd.h> // for close()
#include <fcntl.h> // for open()
#include <sys/ioctl.h>

#endif

#include "libtorrent/config.hpp"

#include "print.hpp"

#include <stdlib.h> // for atoi
#include <string.h> // for strlen
#include <cmath>
#include <algorithm> // for std::min
#include <iterator> // for back_inserter

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
	if (val < 0.001f)
	{
		std::string ret;
		ret.resize(4 + 2, ' ');
		if (suffix) ret.resize(4 + 2 + strlen(suffix), ' ');
		return ret;
	}

	const char* prefix[] = {"kB", "MB", "GB", "TB", "PB"};
	const int num_prefix = sizeof(prefix) / sizeof(const char*);
	int i = 0;
	for (; i < num_prefix - 1; ++i)
	{
		val /= 1000.f;
		if (std::fabs(val) < 1000.f) break;
	}
	char ret[100];
	snprintf(ret, sizeof(ret), "%4.*f%s%s", val < 99 ? 1 : 0, val, prefix[i], suffix ? suffix : "");
	return ret;
}

std::string color(std::string const& s, color_code c)
{
	if (c == col_none) return s;
	if (std::count(s.begin(), s.end(), ' ') == int(s.size())) return s;

	char buf[1024];
	snprintf(buf, sizeof(buf), "\x1b[3%dm%s\x1b[39m", c, s.c_str());
	return buf;
}

std::string const& progress_bar(int progress, int width, color_code c
	, char fill, char bg, std::string caption, int flags)
{
	static std::string bar;
	bar.clear();
	bar.reserve(size_t(width + 10));

	int const progress_chars = (progress * width + 500) / 1000;

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

		caption.resize(size_t(width), ' ');

#ifdef _WIN32
		char const* background = "40";
#else
		char const* background = "48;5;238";
#endif

		char str[256];
		if (flags & progress_invert)
			snprintf(str, sizeof(str), "\x1b[%sm\x1b[37m%s\x1b[4%d;3%dm%s\x1b[49;39m"
				, background, caption.substr(0, progress_chars).c_str(), c, tc
				, caption.substr(progress_chars).c_str());
		else
			snprintf(str, sizeof(str), "\x1b[4%d;3%dm%s\x1b[%sm\x1b[37m%s\x1b[49;39m"
				, c, tc, caption.substr(0, progress_chars).c_str(), background
				, caption.substr(progress_chars).c_str());
		bar = str;
	}
	return bar;
}

bool get_piece(libtorrent::bitfield const& p, int index)
{
	if (index < 0 || index >= p.size()) return false;
	return p.get_bit(index);
}

#ifndef _WIN32
// this function uses the block characters that splits up the glyph in 4
// segments and provide all combinations of a segment lit or not. This allows us
// to print 4 pieces per character.
std::string piece_matrix(libtorrent::bitfield const& p, int width, int* height)
{
	// print two rows of pieces at a time
	int piece = 0;
	++*height;
	std::string ret;
	ret.reserve((p.size() + width * 2 - 1) / width / 2 * 4);
	while (piece < p.size())
	{
		for (int i = 0; i < width; ++i)
		{
			// each character has 4 pieces. store them in a byte to use for lookups
			int const c = get_piece(p, piece)
				| (get_piece(p, piece+1) << 1)
				| (get_piece(p, width*2+piece) << 2)
				| (get_piece(p, width*2+piece+1) << 3);

			// we have 4 bits, 16 different combinations
			static char const* const chars[] =
			{
				" ",      // no bit is set             0000
				"\u2598", // upper left                0001
				"\u259d", // upper right               0010
				"\u2580", // both top bits             0011
				"\u2596", // lower left                0100
				"\u258c", // both left bits            0101
				"\u259e", // upper right, lower left   0110
				"\u259b", // left and upper sides      0111
				"\u2597", // lower right               1000
				"\u259a", // lower right, upper left   1001
				"\u2590", // right side                1010
				"\u259c", // lower right, top side     1011
				"\u2584", // both lower bits           1100
				"\u2599", // both lower, top left      1101
				"\u259f", // both lower, top right     1110
				"\x1b[7m \x1b[27m" // all bits are set (full block)
			};

			ret += chars[c];
			piece += 2;
		}
		ret += '\n';
		++*height;
		piece += width * 2; // skip another row, as we've already printed it
	}
	return ret;
}
#else
// on MS-DOS terminals, we only have block characters for upper half and lower
// half. This lets us print two pieces per character.
std::string piece_matrix(libtorrent::bitfield const& p, int width, int* height)
{
	// print two rows of pieces at a time
	int piece = 0;
	++*height;
	std::string ret;
	ret.reserve((p.size() + width * 2 - 1) / width);
	while (piece < p.size())
	{
		for (int i = 0; i < width; ++i)
		{
			// each character has 8 pieces. store them in a byte to use for lookups
			// the ordering of these bits
			int const c = get_piece(p, piece)
				| (get_piece(p, width*2+piece) << 1);

			static char const* const chars[] =
			{
				" ",    // no piece     00
				"\xdf", // top piece    01
				"\xdc", // bottom piece 10
				"\xdb"  // both pieces  11
			};

			ret += chars[c];
			++piece;
		}
		ret += '\n';
		++*height;
		piece += width * 2; // skip another row, as we've already printed it
	}
	return ret;
}
#endif

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

void clear_rows(int y1, int y2)
{
	if (y1 > y2) return;

#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

	COORD c = {0, y1};
	SetConsoleCursorPosition(out, c);
	CONSOLE_SCREEN_BUFFER_INFO si;
	GetConsoleScreenBufferInfo(out, &si);
	DWORD n;
	int num_chars = si.dwSize.X * (std::min)(si.dwSize.Y - y1, y2 - y1);
	FillConsoleOutputCharacter(out, ' ', num_chars, c, &n);
	FillConsoleOutputAttribute(out, 0x7, num_chars, c, &n);
#else
	for (int i = y1; i < y2; ++i)
		printf("\033[%d;1H\033[2K", i + 1);
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
	if (tty < 0)
	{
		*terminal_width = 190;
		*terminal_height = 100;
		return;
	}
	winsize size;
	int ret = ioctl(tty, TIOCGWINSZ, reinterpret_cast<char*>(&size));
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
void apply_ansi_code(int* attributes, bool* reverse, bool* support_chaining, int code)
{
	static const int color_table[8] =
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
		foreground_mask = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
		background_mask = BACKGROUND_RED | BACKGROUND_GREEN | BACKGROUND_BLUE | BACKGROUND_INTENSITY
	};

	static const int fg_mask[2] = {foreground_mask, background_mask};
	static const int bg_mask[2] = {background_mask, foreground_mask};
	static const int fg_shift[2] = { 0, 4};
	static const int bg_shift[2] = { 4, 0};

	// default foreground
	if (code == 39) code = 37;

	// default background
	if (code == 49) code = 40;

	if (code == 0)
	{
		// reset
		*attributes = color_table[7];
		*reverse = false;
		*support_chaining = true;
	}
	else if (code == 1)
	{
		// intensity
		*attributes |= *reverse ? BACKGROUND_INTENSITY : FOREGROUND_INTENSITY;
		*support_chaining = true;
	}
	else if (code == 7)
	{
		// reverse video
		*support_chaining = true;
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
		*support_chaining = true;
	}
	else if (code >= 40 && code <= 47)
	{
		// background color
		*attributes &= ~bg_mask[*reverse];
		*attributes |= color_table[code - 40] << bg_shift[*reverse];
		*support_chaining = true;
	}
}
#endif
void print(char const* buf)
{
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

	int current_attributes = 7;
	bool reverse = false;
	SetConsoleTextAttribute(out, current_attributes);

	char const* start = buf;
	DWORD written;
	while (*buf != 0)
	{
		if (*buf == '\033' && buf[1] == '[')
		{
			WriteFile(out, start, buf - start, &written, NULL);
			buf += 2; // skip escape and '['
			start = buf;
			if (*buf == 0) break;
			if (*start == 'K')
			{
				// this means clear the rest of the line.
				CONSOLE_SCREEN_BUFFER_INFO sbi;
				if (GetConsoleScreenBufferInfo(out, &sbi))
				{
					COORD const pos = sbi.dwCursorPosition;
					int const width = sbi.dwSize.X;
					int const run = width - pos.X;
					DWORD n;
					FillConsoleOutputAttribute(out, 0x7, run, pos, &n);
					FillConsoleOutputCharacter(out, ' ', run, pos, &n);
				}
				++buf;
				start = buf;
				continue;
			}
			else if (*start == 'J')
			{
				// clear rest of screen
				CONSOLE_SCREEN_BUFFER_INFO sbi;
				if (GetConsoleScreenBufferInfo(out, &sbi))
				{
					COORD pos = sbi.dwCursorPosition;
					int width = sbi.dwSize.X;
					int run = (width - pos.X) + width * (sbi.dwSize.Y - pos.Y - 1);
					DWORD n;
					FillConsoleOutputAttribute(out, 0x7, run, pos, &n);
					FillConsoleOutputCharacter(out, ' ', run, pos, &n);
				}
				++buf;
				start = buf;
				continue;
			}
one_more:
			while (*buf != 'm' && *buf != ';' && *buf != 0) ++buf;

			// this is where we handle reset, color and reverse codes
			if (*buf == 0) break;
			int code = atoi(start);
			bool support_chaining = false;
			apply_ansi_code(&current_attributes, &reverse, &support_chaining, code);
			if (support_chaining)
			{
				if (*buf == ';')
				{
					++buf;
					start = buf;
					goto one_more;
				}
			}
			else
			{
				// ignore codes with multiple fields for now
				while (*buf != 'm' && *buf != 0) ++buf;
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
	fputs(buf, stdout);
#endif
}

