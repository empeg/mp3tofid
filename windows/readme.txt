Sorry, no Windows port yet. I was hoping to use cygwin
to port mp3tofid to Windows, but the biggest problem
with cygwin, surprisingly, is not its support for
symbolic links on which mp3tofid heavily relies, but
its inode emulation.

mp3tofid relies on inode numbers that don't change
when files are moved ore renamed, that are allocated
by the filesystem from zero and up.

Windows filesystems do not have inodes, so cygwin
needs to emulate them. Unfortunately, these numbers
in cygwin appear as random 32-bit unsigned integers,
which can not easily relate to Empeg FID numbers.

FID number will probably overflow at 28 bits, and
even much lower FID numbers would create a huge
player database, as it would need an empty record
for each skipped FID.

And worst of all, cygwin inode numbers change when
the corresponding file is moved.

I might come up with some kind of inode-to-fid
translation table stored in a file, but right now
I wanted to get this release out.

In the mean time, you might try to get acquainted
with cygwin. instsrv.sh is  script that will install
cygwin's rsync port as an NT service, which will be
useful once a cygwin port of mp3tofid is there.
