#ifndef PRINT_HPP_
#define PRINT_HPP_

#include <string>

enum color_code
{
	col_none = -1,
	col_black = 0,
	col_red = 1,
	col_green = 2,
	col_yellow = 3,
	col_blue = 4,
	col_magenta = 5,
	col_cyan = 6,
	col_white = 7,
};

char const* esc(char const* code);

std::string to_string(int v, int width);

std::string add_suffix(float val, char const* suffix = 0);

std::string color(std::string const& s, color_code c);

enum { progress_invert = 1};

std::string const& progress_bar(int progress, int width, color_code c = col_green
	, char fill = '#', char bg = '-', std::string caption = "", int flags = 0);

void set_cursor_pos(int x, int y);

void clear_screen();

void clear_rows(int y1, int y2);

void terminal_size(int* terminal_width, int* terminal_height);

void print(char const* str);

#endif // PRINT_HPP_

