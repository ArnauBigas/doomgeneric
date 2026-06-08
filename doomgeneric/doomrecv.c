#include <stdint.h>

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <X11/Xlib.h>
#include <stdio.h>

#define SRC_W 320
#define SRC_H 200

#define SCALE 4

#define WIDTH (SRC_W * SCALE)
#define HEIGHT (SRC_H * SCALE)

#define FRAME_SIZE (SRC_W * SRC_H)

#define CHUNK_PAYLOAD SRC_W*2

struct ChunkPacket {
    uint32_t magic;
    uint32_t seq;
    uint16_t chunk_id;
    uint16_t chunk_count;
    uint16_t payload_len;
    uint8_t payload[CHUNK_PAYLOAD];
};

struct PalettePacket {
    uint32_t magic;
    uint32_t rgb[256];
};

union Packet {
    struct ChunkPacket chunk;
    struct PalettePacket palette;
};

static uint8_t framebuffer[FRAME_SIZE];
static uint32_t rgbfb[WIDTH * HEIGHT];

static uint32_t palette[256];

static uint8_t received[128];

static uint32_t current_seq = 0;

static int chunks_received = 0;
static int expected_chunks = 0;

static Display *dpy;
static Window win;
static GC gc;
static XImage *img;

static int sock;

void NetInit() {
    printf("NetInit()\n");

    sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in addr = {0};

    addr.sin_family = AF_INET;
    addr.sin_port = htons(6969);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(sock,
        (struct sockaddr*)&addr,
        sizeof(addr));
}

void XInit() {
    printf("XInit()\n");

    dpy = XOpenDisplay(NULL);

    int screen = DefaultScreen(dpy);

    win =
        XCreateSimpleWindow(
            dpy,
            RootWindow(dpy, screen),
            0,
            0,
            WIDTH,
            HEIGHT,
            0,
            0,
            0);

    XMapWindow(dpy, win);

    gc = XCreateGC(dpy, win, 0, NULL);

    img =
    XCreateImage(
        dpy,
        DefaultVisual(dpy, screen),
        24,
        ZPixmap,
        0,
        (char*)rgbfb,
        WIDTH,
        HEIGHT,
        32,
        0);
}

static void ConvertFrame(void)
{
    /*for (int i = 0; i < FRAME_SIZE; i++)
    {
        uint8_t idx = framebuffer[i];

        rgbfb[i] = palette[idx];

        //printf("Pixel %d palette[%d] -> %lx\n", i, idx, palette[idx]);
    }*/

    for (int y = 0; y < SRC_H; y++)
    {
        for (int x = 0; x < SRC_W; x++)
        {
            uint32_t c = received[y/2] ? palette[framebuffer[y*SRC_W+x]] : 0x00ff0000;

            for (int dy = 0; dy < SCALE; dy++)
            {
                uint32_t *row =
                    &rgbfb[(y*SCALE+dy)*WIDTH];

                for (int dx = 0; dx < SCALE; dx++)
                {
                    row[x*SCALE+dx] = c;
                }
            }
        }
    }
}

static void DrawFrame(void)
{
    //printf("DrawFrame seq=%d\n", current_seq);

    ConvertFrame();

    XPutImage(
        dpy,
        win,
        gc,
        img,
        0,
        0,
        0,
        0,
        WIDTH,
        HEIGHT);

    XFlush(dpy);
}

static void ProcessChunk(struct ChunkPacket *pkt) {
    //printf("Packet received seq=%d chunk_id=%d\n", pkt.seq, pkt.chunk_id);
    
    if (pkt->seq > current_seq)
    {
        if (chunks_received < expected_chunks) {
            printf("Frame seq=%d dropped (got %d/%d chunks)\n", current_seq, chunks_received, expected_chunks);
            DrawFrame();
        }

        current_seq = pkt->seq;

        memset(received,
            0,
            sizeof(received));

        chunks_received = 0;
        expected_chunks = pkt->chunk_count;
    }

    if (pkt->seq < current_seq)
        return;

    if (!received[pkt->chunk_id])
    {
        int offset =
            pkt->chunk_id * pkt->payload_len;

        memcpy(framebuffer + offset,
            pkt->payload,
            pkt->payload_len);

        received[pkt->chunk_id] = 1;

        chunks_received++;
    }

    if (chunks_received == expected_chunks)
    {
        DrawFrame();
    }
}

static void ProcessPalette(struct PalettePacket *pkt) {
    printf("Got Palette packet\n");
    memcpy(palette, pkt->rgb, 256*4);
}

static void MainLoop() {
    while (1)
    {
        union Packet pkt;

        ssize_t len =
            recv(sock,
                &pkt,
                sizeof(pkt),
                0);

        if (len <= 0) {
            printf("Problem during recv len=%d\n", len);
            continue;
        }

        if (pkt.chunk.magic == 0x444f4f4d) ProcessChunk(&pkt.chunk);
        else if (pkt.chunk.magic == 0x444f4f4e) ProcessPalette(&pkt.palette);
        else printf("Unknown magic %llx\n", pkt.chunk.magic);
    }
}

void main() {
    XInit();
    NetInit();
    MainLoop();
}