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
