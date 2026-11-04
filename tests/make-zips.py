# Makes small Lively-style packages for test_online.cpp in the folder given.
import sys
import zipfile

out = sys.argv[1]
with zipfile.ZipFile(out + "/video.zip", "w") as z:
    z.writestr("preview.jpg", b"j" * 100)
    z.writestr("LivelyInfo.json", '{"Type": 1, "FileName": "big.mp4"}')
    z.writestr("small.mp4", b"s" * 1000)
    z.writestr("big.mp4", b"b" * 3000)
with zipfile.ZipFile(out + "/gif.zip", "w") as z:
    z.writestr("LivelyInfo.json", "{}")
    z.writestr("rain.gif", b"g" * 500)
with zipfile.ZipFile(out + "/web.zip", "w") as z:
    z.writestr("LivelyInfo.json", "{}")
    z.writestr("index.html", "<html></html>")
# Awkward names: two spaces, brackets and a star, inside a folder; and a
# bigger symlink entry that must not be picked.
with zipfile.ZipFile(out + "/odd.zip", "w") as z:
    z.writestr("sub dir/My  Wall [4K]*.mp4", b"o" * 2000)
    link = zipfile.ZipInfo("link.mp4")
    link.create_system = 3
    link.external_attr = 0o120777 << 16
    z.writestr(link, "/" + "x" * 5000)
