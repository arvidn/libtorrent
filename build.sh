bjam -j2 encryption=openssl link=static invariant-checks=off
bjam -j2 encryption=openssl link=static release debug-symbols=on cflags=-fno-omit-frame-pointer

