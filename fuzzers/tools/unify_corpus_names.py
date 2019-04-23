import sys
import os
import string
import hashlib

if len(sys.argv) < 2:
    print('usage: unify_corpus_names.py <corpus-directory>\n')
    sys.exit(1)

root = sys.argv[1]
for name in os.listdir(root):
    f = os.path.join(root, name)

    # ignore directories
    if not os.path.isfile(f):
        continue

    # if the name already looks like a SHA-1 hash, ignore it
    if len(name) == 40 and all(c in string.hexdigits for c in name):
        continue

    new_name = hashlib.sha1(open(f, 'r').read()).hexdigest()
    print('%s -> %s' % (f, new_name))
    os.rename(f, os.path.join(root, new_name))
