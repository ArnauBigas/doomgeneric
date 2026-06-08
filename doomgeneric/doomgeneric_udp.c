#include "doomkeys.h"

#include "doomgeneric.h"

#include <sys/time.h>
#include <unistd.h>

#include <pthread.h>

#include <stddef.h>
#include <string.h>
#include <stdio.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

uint64_t num_frames = 0;
uint64_t num_frames_last_report = 0;
uint64_t last_seq = 0;
uint64_t num_packets = 0;
uint64_t bytes_sent = 0;
pthread_t net_thread;
static int g_sock;
static struct sockaddr_in g_dst;

// Maximum one frame in buffer
#define NET_THROTTLE_MAX_BYTES (DOOMGENERIC_RESX*DOOMGENERIC_RESY)

#if DOOMGENERIC_RESX == 320
    // If original 320x200, 2 lines per chunk
    #define CHUNK_PAYLOAD (2*DOOMGENERIC_RESX)
#else
    // Else one line per chunk
    #define CHUNK_PAYLOAD DOOMGENERIC_RESX
#endif


struct ChunkPacket {
    uint32_t magic;
    uint32_t seq;
    uint16_t chunk_id;
    uint16_t chunk_count;
    uint16_t payload_len;
    uint8_t  payload[CHUNK_PAYLOAD];
};

struct PalettePacket {
    uint32_t magic;
    uint32_t pallet[256];
};

static int NetCanSend(void)
{
    int outq = 0;

    if (ioctl(g_sock, TIOCOUTQ, &outq) < 0)
        return 0;

    return outq < NET_THROTTLE_MAX_BYTES;
}

static void SendFrame(uint32_t seq)
{
    uint8_t* fb = (uint8_t*)DG_ScreenBuffer;

    const int frame_size = DOOMGENERIC_RESX * DOOMGENERIC_RESY;

    int nchunks = frame_size / CHUNK_PAYLOAD;

    for (int i = 0; i < nchunks; i++)
    {
        struct ChunkPacket pkt;

        pkt.magic       = 0x444f4f4d;
        pkt.seq         = seq;
        pkt.chunk_id    = i;
        pkt.chunk_count = nchunks;

        int offset = i * CHUNK_PAYLOAD;

        int len = frame_size - offset;

        if (len > CHUNK_PAYLOAD)
            len = CHUNK_PAYLOAD;

        pkt.payload_len = len;

        memcpy(pkt.payload, fb + offset, len);

        sendto(g_sock,
               &pkt,
               offsetof(struct ChunkPacket, payload) + len,
               0,
               (struct sockaddr*)&g_dst,
               sizeof(g_dst));

        num_packets++;
        bytes_sent += offsetof(struct ChunkPacket, payload) + len;
    }
}

static void *NetworkThread(void *arg) {
    uint64_t cur_time;
    uint64_t prev_time = DG_GetTicksMs();

    // Wait for rendering to begin
    while(num_frames == 0) sched_yield();

    while (1) {
        uint32_t cur_frame = num_frames - 1;

        if (cur_frame == last_seq)
        {
            sched_yield();
            continue;
        }

        if (!NetCanSend())
        {
            sched_yield();
            continue;
        }

        SendFrame(cur_frame);

        //printf("Sent frame seq=%d\n", cur_frame);
        cur_time = DG_GetTicksMs();
        if ((cur_time - prev_time) > 1000) {
            printf(
                "TX: %.1f KB/s, %llu pkt/s, %llu fps\n",
                bytes_sent / 1024.0,
                num_packets,
                num_frames - num_frames_last_report
            );
            num_frames_last_report = num_frames;
            prev_time = cur_time;
            bytes_sent = 0;
            num_packets = 0;
        }

        last_seq = cur_frame;

        DG_SleepMs(300);
    }

    return NULL;
}

static void NetworkInit(char *addr) {
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);

    int sndbuf = 8192;
    setsockopt(g_sock,
               SOL_SOCKET,
               SO_SNDBUF,
               &sndbuf,
               sizeof(sndbuf));

    memset(&g_dst, 0, sizeof(g_dst));
    g_dst.sin_family = AF_INET;
    g_dst.sin_port = htons(6969);
    inet_aton(addr, &g_dst.sin_addr);

    pthread_create(&net_thread, NULL, NetworkThread, NULL);
}

void DG_PaletteUpdate(uint32_t *colors) {
    struct PalettePacket pkt;

    pkt.magic       = 0x444f4f4e;

    memcpy(pkt.pallet, colors, 256 * 4);

    sendto(g_sock,
            &pkt,
            256*4 + 4,
            0,
            (struct sockaddr*)&g_dst,
            sizeof(g_dst));
}

void DG_Init()
{   
    NetworkInit("localhost");
}

void DG_DrawFrame()
{
    num_frames++;
}

void DG_SleepMs(uint32_t ms)
{
    usleep (ms * 1000);
}

uint32_t DG_GetTicksMs()
{
    struct timeval  tp;

    gettimeofday(&tp, NULL);

    return (tp.tv_sec * 1000) + (tp.tv_usec / 1000); /* return milliseconds */
}

int DG_GetKey(int* pressed, unsigned char* doomKey)
{
}

void DG_SetWindowTitle(const char * title)
{
}

int main(int argc, char **argv)
{
    doomgeneric_Create(argc, argv);

    while(1)
    {
      doomgeneric_Tick(); 
    }

    return 0;
}
