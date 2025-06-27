#define GC9A01_DRIVER
#define TFT_WIDTH 240
#define TFT_HEIGHT 240

#define TFT_CS D8   // GPIO2
#define TFT_DC D4   // GPIO0
#define TFT_RST D0  // GPIO16
#define TFT_SCLK D5 // GPIO14
#define TFT_MOSI D7 // GPIO12

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY 40000000