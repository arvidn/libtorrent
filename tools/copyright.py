#!/usr/bin/python

# essentially copy pasted from http://0pointer.de/blog/projects/copyright.html

from subprocess import Popen, PIPE
from datetime import datetime


def pretty_years(s):

    li = list(s)
    li.sort()

    start = None
    prev = None
    r = []

    for x in li:
        if prev is None:
            start = x
            prev = x
            continue

        if x == prev + 1:
            prev = x
            continue

        if prev == start:
            r.append("%i" % prev)
        else:
            r.append("%i-%i" % (start, prev))

        start = x
        prev = x

    if prev is not None:
        if prev == start:
            r.append("%i" % prev)
        else:
            r.append("%i-%i" % (start, prev))

    return ", ".join(r)


def order_by_year(a, b):

    la = list(a[2])
    la.sort()

    lb = list(b[2])
    lb.sort()

    if la[0] < lb[0]:
        return -1
    elif la[0] > lb[0]:
        return 1
    else:
        return 0


author_map = {
    'arvidn': 'Arvid Norberg',
    'pavel.pimenov': 'Pavel Pimenov',
    'd_komarov': 'd-komarov',
    'Chocobo1': 'Mike Tzou',
    'unsh': 'Un Shyam',
    'toinetoine': 'Antoine Dahan'
}


def map_author(a):
    if a in author_map:
        return author_map[a]
    else:
        return a


def get_authors(f):

    print("File: %s" % f)

    commits = []
    data = {}

    for ln in Popen(["git", "blame", "--incremental", f], stdout=PIPE).stdout:

        if ln.startswith("filename "):
            if len(data) > 0:
                commits.append(data)
            data = {}

        elif ln.startswith("author "):
            data["author"] = map_author(ln[7:].strip())

        elif ln.startswith("author-mail <"):
            data["author-mail"] = ln[12:].strip()

        elif ln.startswith("author-time "):
            data["author-time"] = ln[11:].strip()

        elif ln.startswith("author-tz "):
            data["author-tz"] = ln[9:].strip()

    by_author = {}

    for c in commits:
        try:
            if c['author'] == 'Not Committed Yet':
                continue
            n = by_author[c["author"]]
        except KeyError:
            n = (c["author"], c["author-mail"], set())
            by_author[c["author"]] = n

        # FIXME: Handle time zones properly
        year = datetime.fromtimestamp(int(c["author-time"])).year

        n[2].add(year)

    for an, a in list(by_author.iteritems()):
        for bn, b in list(by_author.iteritems()):
            if a is b:
                continue

            if a[1] == b[1]:
                a[2].update(b[2])

                if an in by_author and bn in by_author:
                    del by_author[bn]

    copyright = list(by_author.itervalues())
    copyright.sort(order_by_year)

    ret = ''
    for name, mail, years in copyright:
        ret += "Copyright (c) %s, %s\n" % (pretty_years(years), name)
    return ret
