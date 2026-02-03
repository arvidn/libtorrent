#include "file_utils.hpp"
#include "torrent_utils.hpp"
#include <fstream>
#include <cstring>
#include <cstdlib>

#ifdef TORRENT_WINDOWS
#include <direct.h>
#include <windows.h>
#else
#include <dirent.h>
#include <unistd.h>
#endif

namespace piece_cache {

bool load_file(std::string const& filename, std::vector<char>& v, int limit)
{
    std::fstream f(filename, std::ios_base::in | std::ios_base::binary);
    f.seekg(0, std::ios_base::end);
    auto const s = f.tellg();
    if (s > limit || s < 0) return false;
    f.seekg(0, std::ios_base::beg);
    v.resize(static_cast<std::size_t>(s));
    if (s == std::fstream::pos_type(0)) return !f.fail();
    f.read(v.data(), int(v.size()));
    return !f.fail();
}

int save_file(std::string const& filename, std::vector<char> const& v)
{
    std::fstream f(filename, std::ios_base::trunc | std::ios_base::out | std::ios_base::binary);
    f.write(v.data(), int(v.size()));
    return !f.fail();
}

bool is_absolute_path(std::string const& f)
{
    if (f.empty()) return false;
#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
    int i = 0;
    // match the xx:\ or xx:/ form
    while (f[i] && strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVXYZ", f[i])) ++i;
    if (i < int(f.size()-1) && f[i] == ':' && (f[i+1] == '\\' || f[i+1] == '/'))
        return true;

    // match the \\ form
    if (int(f.size()) >= 2 && f[0] == '\\' && f[1] == '\\')
        return true;
    return false;
#else
    if (f[0] == '/') return true;
    return false;
#endif
}

std::string path_append(std::string const& lhs, std::string const& rhs)
{
    if (lhs.empty() || lhs == ".") return rhs;
    if (rhs.empty() || rhs == ".") return lhs;

#if defined(TORRENT_WINDOWS) || defined(TORRENT_OS2)
#define TORRENT_SEPARATOR "\\"
    bool need_sep = lhs[lhs.size()-1] != '\\' && lhs[lhs.size()-1] != '/';
#else
#define TORRENT_SEPARATOR "/"
    bool need_sep = lhs[lhs.size()-1] != '/';
#endif
    return lhs + (need_sep?TORRENT_SEPARATOR:"") + rhs;
}

std::string make_absolute_path(std::string const& p)
{
    if (is_absolute_path(p)) return p;
    std::string ret;
#if defined TORRENT_WINDOWS
    char* cwd = ::_getcwd(nullptr, 0);
    ret = path_append(cwd, p);
    std::free(cwd);
#else
    char* cwd = ::getcwd(nullptr, 0);
    ret = path_append(cwd, p);
    std::free(cwd);
#endif
    return ret;
}

std::vector<std::string> list_dir(
    std::string path,
    bool (*filter_fun)(lt::string_view),
    lt::error_code& ec)
{
    std::vector<std::string> ret;
#ifdef TORRENT_WINDOWS
    if (!path.empty() && path[path.size()-1] != '\\') path += "\\*";
    else path += "*";

    WIN32_FIND_DATAA fd;
    HANDLE handle = FindFirstFileA(path.c_str(), &fd);
    if (handle == INVALID_HANDLE_VALUE)
    {
        ec.assign(GetLastError(), boost::system::system_category());
        return ret;
    }

    do
    {
        lt::string_view p = fd.cFileName;
        if (filter_fun(p))
            ret.push_back(p.to_string());

    } while (FindNextFileA(handle, &fd));
    FindClose(handle);
#else

    if (!path.empty() && path[path.size()-1] == '/')
        path.resize(path.size()-1);

    DIR* handle = opendir(path.c_str());
    if (handle == nullptr)
    {
        ec.assign(errno, boost::system::system_category());
        return ret;
    }

    struct dirent* de;
    while ((de = readdir(handle)))
    {
        lt::string_view p(de->d_name);
        if (filter_fun(p))
            ret.push_back(p.to_string());
    }
    closedir(handle);
#endif
    return ret;
}

void scan_dir(std::string const& dir_path, lt::session& ses)
{
    using namespace lt;

    error_code ec;
    std::vector<std::string> ents = list_dir(dir_path,
        [](lt::string_view p) { return p.size() > 8 && p.substr(p.size() - 8) == ".torrent"; }, ec);
    if (ec)
    {
        std::fprintf(stderr, "failed to list directory: (%s : %d) %s\n",
            ec.category().name(), ec.value(), ec.message().c_str());
        return;
    }

    for (auto const& e : ents)
    {
        std::string const file = path_append(dir_path, e);

        // there's a new file in the monitor directory, load it up
        if (add_torrent(ses, file))
        {
            if (::remove(file.c_str()) < 0)
            {
                std::fprintf(stderr, "failed to remove torrent file: \"%s\"\n", file.c_str());
            }
        }
    }
}

bool is_resume_file(std::string const& s)
{
    static std::string const hex_digit = "0123456789abcdef";
    if (s.size() != 40 + 7) return false;
    if (s.substr(40) != ".resume") return false;
    for (char const c : s.substr(0, 40))
    {
        if (hex_digit.find(c) == std::string::npos) return false;
    }
    return true;
}

} // namespace piece_cache
