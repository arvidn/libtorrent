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

#include <cstdlib> // for atoi
#include <cstring> // for strlen
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
	std::snprintf(buf, sizeof(buf), "%*d", width, v);
	return buf;
}

std::string add_suffix_float(double val, char const* suffix)
{
	if (val < 0.001)
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
		val /= 1000.;
		if (std::fabs(val) < 1000.) break;
	}
	char ret[100];
	std::snprintf(ret, sizeof(ret), "%4.*f%s%s", val < 99 ? 1 : 0, val, prefix[i], suffix ? suffix : "");
	return ret;
}

std::string color(std::string const& s, color_code c)
{
	if (c == col_none) return s;
	if (std::count(s.begin(), s.end(), ' ') == int(s.size())) return s;

	char buf[1024];
	std::snprintf(buf, sizeof(buf), "\x1b[3%dm%s\x1b[39m", c, s.c_str());
	return buf;
}

std::string const& progress_bar(int progress, int width, color_code c
	, char fill, char bg, std::string caption, int flags)
{
	static std::string bar;
	bar.clear();
	bar.reserve(size_t(width + 10));

	auto const progress_chars = static_cast<std::size_t>((progress * width + 500) / 1000);

	if (caption.empty())
	{
		char code[10];
		std::snprintf(code, sizeof(code), "\x1b[3%dm", c);
		bar = code;
		std::fill_n(std::back_inserter(bar), progress_chars, fill);
		std::fill_n(std::back_inserter(bar), std::size_t(width) - progress_chars, bg);
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
			std::snprintf(str, sizeof(str), "\x1b[%sm\x1b[37m%s\x1b[4%d;3%dm%s\x1b[49;39m"
				, background, caption.substr(0, progress_chars).c_str(), c, tc
				, caption.substr(progress_chars).c_str());
		else
			std::snprintf(str, sizeof(str), "\x1b[4%d;3%dm%s\x1b[%sm\x1b[37m%s\x1b[49;39m"
				, c, tc, caption.substr(0, progress_chars).c_str(), background
				, caption.substr(progress_chars).c_str());
		bar = str;
	}
	return bar;
}

std::string const& piece_bar(lt::bitfield const& p, int width)
{
#ifdef _WIN32
	int const table_size = 5;
#else
	int const table_size = 18;
	width *= 2; // we only print one character for every two "slots"
#endif

	double const piece_per_char = p.size() / double(width);
	static std::string bar;
	bar.clear();

	if (width <= 0) return bar;

	bar.reserve(std::size_t(width) * 6);
	bar += "[";
	if (p.size() == 0)
	{
		for (int i = 0; i < width; ++i) bar += ' ';
		bar += "]";
		return bar;
	}

	// the [piece, piece + pieces_per_char) range is the pieces that are represented by each character
	double piece = 0;

	// we print two blocks at a time, so calculate the color in pair
#ifndef _WIN32
	int color[2];
	int last_color[2] = { -1, -1};
#endif

	for (int i = 0; i < width; ++i, piece += piece_per_char)
	{
		int num_pieces = 0;
		int num_have = 0;
		int end = (std::max)(int(piece + piece_per_char), int(piece) + 1);
		for (int k = int(piece); k < end; ++k, ++num_pieces)
			if (p[k]) ++num_have;
		int const c = int(std::ceil(num_have / float((std::max)(num_pieces, 1)) * (table_size - 1)));

#ifndef _WIN32
		color[i & 1] = c;

		if ((i & 1) == 1)
		{
			// now, print color[0] and [1]
			// bg determines whether we're settings foreground or background color
			static int const bg[] = { 38, 48};
			for (int k = 0; k < 2; ++k)
			{
				if (color[k] != last_color[k])
				{
					char buf[40];
					std::snprintf(buf, sizeof(buf), "\x1b[%d;5;%dm", bg[k & 1], 232 + color[k]);
					last_color[k] = color[k];
					bar += buf;
				}
			}
			bar += "\u258C";
		}
#else
		static char const table[] = {' ', '\xb0', '\xb1', '\xb2', '\xdb'};
		bar += table[c];
#endif
	}
	bar += esc("0");
	bar += "]";
	return bar;
}

std::string avail_bar(lt::span<int> avail, int const width, int& pos)
{
	std::string ret;
	int const max_avail = (std::max)(1, *std::max_element(avail.begin(), avail.end()));
	int cursor = 0;
#ifndef _WIN32
	for (int piece = 0; piece < avail.size(); piece += 2)
	{
		int p[2];
		p[0] = avail[piece] * 22 / max_avail;
		p[1] = piece + 1 < avail.size() ? avail[piece + 1] * 22 / max_avail : 0;
		assert(p[0] >= 0);
		assert(p[0] < 23);
		assert(p[1] >= 0);
		assert(p[1] < 23);
		char buf[50];
		std::snprintf(buf, sizeof(buf), "\x1b[38;5;%dm\x1b[48;5;%dm\u258c"
			, 232 + p[0], 232 + p[1]);
		ret += buf;
		cursor += 1;
		if (cursor >= width)
		{
			cursor = 0;
			pos += 1;
			ret += "\n";
		}
	}
#else
	for (int piece = 0; piece < avail.size(); ++piece)
	{
		static char const table[] = {' ', '\xb0', '\xb1', '\xb2', '\xdb'};
		int const p = avail[piece] * 4 / max_avail;
		assert(p >= 0);
		assert(p < 5);
		ret += table[p];
		cursor += 1;
		if (cursor >= width)
		{
			cursor = 0;
			pos += 1;
			ret += "\n";
		}
	}
#endif
	if (cursor > 0)
		ret += "\x1b[K\n";
	return ret;
}

namespace {
int get_piece(lt::bitfield const& p, int index)
{
	if (index < 0 || index >= p.size()) return 0;
	return p.get_bit(index) ? 1 : 0;
}
}

#ifndef _WIN32
// this function uses the block characters that splits up the glyph in 4
// segments and provide all combinations of a segment lit or not. This allows us
// to print 4 pieces per character.
std::string piece_matrix(lt::bitfield const& p, int width, int* height)
{
	if (width <= 0) return {};

	// print two rows of pieces at a time
	int piece = 0;
	++*height;
	std::string ret;
	ret.reserve(std::size_t((p.size() + width * 2 - 1) / width / 2 * 4));
	while (piece < p.size())
	{
		if (piece > 0)
			ret += "\n";
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
		ret += "\x1b[K";
		++*height;
		piece += width * 2; // skip another row, as we've already printed it
	}
	return ret;
}
#else
// on MS-DOS terminals, we only have block characters for upper half and lower
// half. This lets us print two pieces per character.
std::string piece_matrix(lt::bitfield const& p, int width, int* height)
{
	// print two rows of pieces at a time
	int piece = 0;
	++*height;
	std::string ret;
	ret.reserve((p.size() + width * 2 - 1) / width);
	while (piece < p.size())
	{
		if (piece > 0)
			ret += '\n';
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
	COORD c = {SHORT(x), SHORT(y)};
	SetConsoleCursorPosition(out, c);
#else
	std::printf("\033[%d;%dH", y + 1, x + 1);
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
	std::printf("\033[2J");
#endif
}

void clear_rows(int y1, int y2)
{
	if (y1 > y2) return;

#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);

	COORD c = {0, SHORT(y1)};
	SetConsoleCursorPosition(out, c);
	CONSOLE_SCREEN_BUFFER_INFO si;
	GetConsoleScreenBufferInfo(out, &si);
	DWORD n;
	int num_chars = si.dwSize.X * (std::min)(si.dwSize.Y - y1, y2 - y1);
	FillConsoleOutputCharacter(out, ' ', num_chars, c, &n);
	FillConsoleOutputAttribute(out, 0x7, num_chars, c, &n);
#else
	for (int i = y1; i < y2; ++i)
		std::printf("\033[%d;1H\033[2K", i + 1);
#endif
}

std::pair<int, int> terminal_size()
{
	int width = 80;
	int height = 50;
#ifdef _WIN32
	HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
	CONSOLE_SCREEN_BUFFER_INFO coninfo;
	if (GetConsoleScreenBufferInfo(out, &coninfo))
	{
		width = coninfo.dwSize.X;
		height = coninfo.srWindow.Bottom - coninfo.srWindow.Top;
#else
	int tty = open("/dev/tty", O_RDONLY);
	if (tty < 0)
	{
		width = 190;
		height = 100;
		return {width, height};
	}
	winsize size;
	int ret = ioctl(tty, TIOCGWINSZ, reinterpret_cast<char*>(&size));
	close(tty);
	if (ret == 0)
	{
		width = size.ws_col;
		height = size.ws_row;
#endif

		if (width < 64)
			width = 64;
		if (height < 25)
			height = 25;
	}
	else
	{
		width = 190;
		height = 100;
	}
	return {width, height};
}

#ifdef _WIN32
void apply_ansi_code(WORD* attributes, bool* reverse, bool* support_chaining, int code)
{
	static const WORD color_table[8] =
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

	WORD current_attributes = 7;
	bool reverse = false;
	SetConsoleTextAttribute(out, current_attributes);

	char const* start = buf;
	DWORD written;
	while (*buf != 0)
	{
		if (*buf == '\033' && buf[1] == '[')
		{
			WriteFile(out, start, DWORD(buf - start), &written, nullptr);
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
	WriteFile(out, start, DWORD(buf - start), &written, nullptr);

#else
	fputs(buf, stdout);
#endif
}
