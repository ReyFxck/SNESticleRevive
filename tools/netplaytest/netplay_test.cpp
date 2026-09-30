#include <cstdio>
#include <cstring>
#include <cstdint>
#include "types.h"
#include "netplay.h"
/* netpacket.h only needs a pointer to this socket type; the test exercises
   the wire codec without opening a socket on the host. */
typedef struct NetSocket_t NetSocketT;
typedef struct NetSocketAddr_dummy NetSocketAddrT;
#include "netinput_codec.h"
#include "netplay_input_merge.h"
extern "C" {
#include "netqueue.h"
}


static int errors = 0;
static void check(bool ok, const char *name)
{
    if (!ok) { std::fprintf(stderr, "netplay_test: FAIL %s\n", name); errors++; }
}
static NetPlayFrameInputT frame(Uint16 p0, Uint16 p1, Uint16 payload0,
                                Uint16 payload1, Uint16 tag)
{
    NetPlayFrameInputT f = {{p0, p1, payload0, payload1, tag}};
    return f;
}
static bool equal(const NetPlayFrameInputT &a, const NetPlayFrameInputT &b)
{
    return !std::memcmp(&a, &b, sizeof(a));
}
static void codecTests()
{
    static_assert(sizeof(NetPlayFrameInputT) == 10, "five exact 16-bit words");
    static_assert(sizeof(NetPacketInputRunT) == 16, "fixed-size wire run");
    static_assert(12 + NETPACKET_INPUT_RUNS_MAX *
                  sizeof(NetPacketInputRunT) <= NETPACKET_MAX_SIZE,
                  "bounded packet size");
    static_assert(NETPLAY_VERSION == 0x101, "versioned protocol");

    NetPlayFrameInputT source[64], decoded[128];
    NetPacketInputRunT runs[NETPACKET_INPUT_RUNS_MAX];
    for (Uint32 i = 0; i < 64; i++)
        source[i] = frame((Uint16)(0x100 + i), (Uint16)(i << 4),
                          (Uint16)(i | (i << 8)), (Uint16)(0xFFFF - i),
                          (Uint16)((i & 3) + 1));
    Uint32 total = 0;
    while (total < 64)
    {
        Uint32 used = 0, got = 0;
        Uint32 n = NetInputEncodeRuns(runs, NETPACKET_INPUT_RUNS_MAX,
                                      source + total, 64 - total, &used);
        check(n > 0 && n <= NETPACKET_INPUT_RUNS_MAX &&
              used > 0 && used <= 64 - total, "bounded unique run packing");
        if (!n || !used) break;
        check(NetInputDecodeRuns(decoded, 128, runs, n, &got) && got == used,
              "unique frame decode");
        for (Uint32 i = 0; i < got; i++)
            check(equal(decoded[i], source[total + i]), "byte-exact frame");
        total += used;
    }
    check(total == 64, "multiple packets cover all unique frames");

    for (int i = 0; i < 64; i++)
        source[i] = frame(0, 0, 0x1173, 0x992A, EMUSYS_SNES_SPECIAL_SUPERSCOPE);
    Uint32 used = 0, got = 0;
    check(NetInputEncodeRuns(runs, NETPACKET_INPUT_RUNS_MAX,
                            source, 64, &used) == 1 && used == 64,
          "repeat compression");
    check(NetInputDecodeRuns(decoded, 128, runs, 1, &got) && got == 64,
          "repeated frames restored");
    for (Uint32 i=0; i < got; i++)
        check(equal(decoded[i], source[i]), "repeated payload intact");

    std::memset(decoded, 0x5A, sizeof(decoded));
    runs[0].uLength = 0;
    check(!NetInputDecodeRuns(decoded, 128, runs, 1, &got) &&
          got == 0 && decoded[0].uPad[0] == 0x5A5A,
          "reject zero run without writing");
    runs[0].uLength = 129;
    check(!NetInputDecodeRuns(decoded, 128, runs, 1, &got) &&
          got == 0 && decoded[0].uPad[0] == 0x5A5A,
          "reject oversized run without writing");
    runs[0].uLength = 1;
    runs[0].uReserved = 1;
    check(!NetInputDecodeRuns(decoded, 128, runs, 1, &got) &&
          got == 0 && decoded[0].uPad[0] == 0x5A5A,
          "reject unsupported flags");
    runs[0].uReserved = 0;
    runs[1].uLength = 128;
    runs[1].uReserved = 0;
    check(!NetInputDecodeRuns(decoded, 128, runs, 2, &got) &&
          got == 0 && decoded[0].uPad[0] == 0x5A5A,
          "reject total overflow atomically");
}

static void queueTests()
{
    NetQueueT queue;
    NetQueueNew(&queue);

    for (int i = 0; i < NETQUEUE_SIZE; i++)
    {
        NetQueueElementT item = frame((Uint16)i, (Uint16)(0x1000 + i),
            (Uint16)(i ^ 0xAA), (Uint16)(i ^ 0x55),
            EMUSYS_SNES_SPECIAL_MOUSE);
        check(NetQueueEnqueue(&queue, &item), "enqueue bounded frame");
    }
    check(NetQueueGetCount(&queue) == NETQUEUE_SIZE,
          "backlog contains 128 full frames");
    NetQueueElementT overflow = frame(0,0,0,0,0);
    check(!NetQueueEnqueue(&queue, &overflow), "full queue rejects writes");

    NetQueueElementT guard[66];
    std::memset(guard, 0xA5, sizeof(guard));
    int n = NetQueueFetchRange(&queue, queue.uHead, queue.uTail,
                               guard + 1, 64);
    check(n == 64, "128-frame backlog fetch bounded to 64");
    check(guard[0].uPad[0] == 0xA5A5 &&
          guard[65].uPad[0] == 0xA5A5,
          "bounded fetch preserves both guard frames");
    for (int i = 0; i < n; i++)
        check(guard[i+1].uPad[0] == (Uint16)i &&
              guard[i+1].uPad[1] == (Uint16)(0x1000+i),
              "complete queue frame preserved");

    check(NetQueueFetchRange(&queue, 0, 128, guard+1, 0) == 0,
          "zero-capacity fetch is safe");

    NetQueueElementT backwards[3];
    n = NetQueueFetchRange(&queue, 12, 9, backwards, 3);
    check(n == 3 && backwards[0].uPad[0] == 11 &&
          backwards[1].uPad[0] == 10 &&
          backwards[2].uPad[0] == 9,
          "reverse fetch walks backward without overread");
}

static void mergeTests()
{
    NetPlayFrameInputT peers[4], before[4];
    Emu::SysInputT out;
    peers[0] = frame(0x1110, 0xAAAA, 0xFFFF, 0xFFFF, 0xFFFF);
    peers[1] = frame(0x2220, 0xBBBB, 0xFFFF, 0xFFFF, 0xFFFF);
    peers[2] = frame(0xFFFF, 0, 0xFFFF, 0xFFFF, 0xFFFF);
    peers[3] = frame(0xFFFF, 0, 0xFFFF, 0xFFFF, 0xFFFF);

    NetPlayMergeInputs(&out, peers, TRUE);
    check(out.uPad[0]==0x1110 && out.uPad[1]==0x2220 &&
          out.uPad[2]==0xAAAA && out.uPad[3]==0xBBBB &&
          out.uPad[4]==0xFFFF, "legacy two-peer pad fallbacks");

    peers[2].uPad[0]=0x3330;
    peers[3].uPad[0]=0x4440;
    NetPlayMergeInputs(&out, peers, TRUE);
    check(out.uPad[2]==0x3330 && out.uPad[3]==0x4440,
          "four independent peers unchanged");

    peers[2] = frame(0x5550, 0x0000, 0xFB07, 0x0003, EMUSYS_SNES_SPECIAL_MOUSE);
    std::memcpy(before, peers, sizeof(peers));
    NetPlayMergeInputs(&out, peers, TRUE);
    check(out.uPad[0]==0x5550 && out.uPad[1]==0x2220 &&
          out.uPad[2]==0xFB07 && out.uPad[3]==0x0003 &&
          out.uPad[4]==EMUSYS_SNES_SPECIAL_MOUSE,
          "mouse payload owns port 1");
    check(!std::memcmp(before,peers,sizeof(peers)), "no peer mutation");

    peers[2]=frame(0x5550, 0x9990, 0x7B4D, 0x000F,
                   EMUSYS_SNES_SPECIAL_SUPERSCOPE);
    NetPlayMergeInputs(&out, peers, TRUE);
    check(out.uPad[0]==0x1110 && out.uPad[1]==0x9990 &&
          out.uPad[2]==0x7B4D && out.uPad[3]==0x000F &&
          out.uPad[4]==EMUSYS_SNES_SPECIAL_SUPERSCOPE,
          "Super Scope payload owns port 2");

    peers[2]=frame(0x5550, 0xFFF7, 0xC85B, 0x3C44,
                   EMUSYS_SNES_SPECIAL_JUSTIFIERS);
    NetPlayMergeInputs(&out, peers, TRUE);
    check(out.uPad[0]==0x1110 && out.uPad[1]==0xFFF7 &&
          out.uPad[2]==0xC85B && out.uPad[3]==0x3C44 &&
          out.uPad[4]==EMUSYS_SNES_SPECIAL_JUSTIFIERS,
          "chained Justifier keeps two aims/buttons");

    peers[1]=frame(0x2220, 0xFFF1, 0xAA77, 0xFFFF,
                   EMUSYS_SNES_SPECIAL_JUSTIFIER);
    NetPlayMergeInputs(&out, peers, TRUE);
    check(out.uPad[1]==0xFFF1 && out.uPad[2]==0xAA77 &&
          out.uPad[3]==0xFFFF &&
          out.uPad[4]==EMUSYS_SNES_SPECIAL_JUSTIFIER,
          "lower peer index wins conflicting special tags");

    NetPlayMergeInputs(&out, peers, FALSE);
    check(out.uPad[4]==0xFFFF && out.uPad[0]==0x1110 &&
          out.uPad[1]==0x2220 && out.uPad[2]==0x5550,
          "NES/plain input ignores SNES peripheral tags");
}
int main()
{
    codecTests();
    queueTests();
    mergeTests();
    if(errors){std::fprintf(stderr,"netplay_test: %d failure(s)\n",errors);return 1;}
    std::puts("netplay_test: OK");
    return 0;
}
