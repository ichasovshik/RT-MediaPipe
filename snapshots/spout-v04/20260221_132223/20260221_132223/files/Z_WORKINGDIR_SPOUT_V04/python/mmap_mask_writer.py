import mmap
import struct
import time
import math

W, H = 1280, 720
NAME = r"Global\SEG_MASK_TO_TD_BGRA_1280x720"

HEADER_FMT = "<IIII"  # width, height, frameCounter, reserved
HEADER_SIZE = struct.calcsize(HEADER_FMT)
PIXEL_BYTES = W * H * 4
TOTAL = HEADER_SIZE + PIXEL_BYTES

def main():
    mm = mmap.mmap(-1, TOTAL, tagname=NAME, access=mmap.ACCESS_WRITE)
    print("Writing test pattern to", NAME)

    counter = 0
    t0 = time.time()

    while True:
        counter += 1
        t = time.time() - t0
        bar_x = int((math.sin(t) * 0.5 + 0.5) * (W - 1))

        mm.seek(0)
        mm.write(struct.pack(HEADER_FMT, W, H, counter, 0))

        mm.seek(HEADER_SIZE)
        for _y in range(H):
            row = bytearray(W * 4)
            for x in range(W):
                i = x * 4
                m = (x * 255) // (W - 1)
                if abs(x - bar_x) < 12:
                    m = 255
                row[i + 0] = m
                row[i + 1] = m
                row[i + 2] = m
                row[i + 3] = 255
            mm.write(row)

        time.sleep(1 / 30)

if __name__ == "__main__":
    main()