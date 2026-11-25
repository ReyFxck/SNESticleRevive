#include "snobc1.h"
#include <cassert>
#include <cstdio>
int main() {
    SNOBC1 chip;
    unsigned cases = 0;
    for (unsigned b=0; b<2; ++b) {
        unsigned base = b ? 0x7800 : 0x7c00;
        chip.Write(0x7ff5, (Uint8)(0xa0|b));
        assert(chip.Read(0x7ff5)==(0xa0|b));
        for (unsigned index=0; index<256; ++index) {
            chip.Write(0x7ff6, (Uint8)index);
            assert(chip.Read(0x7ff6)==index);
            unsigned obj = index & 127;
            for (unsigned field=0;field<4;++field) {
                Uint8 data=(Uint8)(index*7+field*71);
                chip.Write(0x7ff0+field,data);
                assert(chip.Read(base+obj*4+field)==data);
                assert(chip.Read(0x7ff0+field)==data);
            }
            unsigned high=base+0x200+obj/4, shift=(obj&3)*2;
            for (unsigned value=0;value<256;++value) {
                Uint8 before=(Uint8)(value*13+index);
                chip.Write(high,before);
                chip.Write(0x7ff4,(Uint8)value);
                Uint8 expected=(Uint8)((before&~(3u<<shift))|((value&3u)<<shift));
                assert(chip.Read(high)==expected);
                assert(chip.Read(0x7ff4)==expected);
                ++cases;
            }
        }
    }
    std::printf("OBC1 sprite ports and readback: %u cases PASS\n",cases);
}
