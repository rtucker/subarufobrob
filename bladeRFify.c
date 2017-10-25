// This is a modified rpitxify.c

#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <math.h>

#include "hex.h"
#include "protocol.h"
#include "manchester.h"

/*
 * bladeRF-cli -e "set frequency tx 433820000; set samplerate tx 4000000; \
 * tx config file=bleh.txt format=csv; tx start" 
 */

const double CARRIER_FREQ = 100e3;
const double SAMPLE_RATE = 4000000;
const int16_t MAGNITUDE_0 = 0;
const int16_t MAGNITUDE_1 = 0.9*2047;

void modulate(double* i, double* q, int a)
{
    static size_t count = 0;
    const double OMEGA = 2.0*M_PI*CARRIER_FREQ/SAMPLE_RATE;

    *i = a*sin(OMEGA*count);
    *q = a*cos(OMEGA*count);

    ++count;
}

void writeAm(int fd, int a, uint32_t Timing)
{
    const size_t NS_PER_SAMPLE = 1e9/SAMPLE_RATE;
    size_t samples = Timing/NS_PER_SAMPLE;

    for (size_t i = 0; i < samples; ++i) {
        double i, q;
        modulate(&i, &q, a);

        if (0 > dprintf(fd, "%d,%d\n", (int)i, (int)q)) {
            fprintf(stderr, "Unable to write sample\n");
            return;
        }
    }
}

int main(int argc, char** argv)
{
    int nsecsPerBit = 1013210; // Measures. Might not be 100% accurate, but it works
    if (argc < 3)
    {
        fprintf(stderr, "usage, %s <hex string code> <tx.csv>\n", argv[0]);
        return -1;
    }

    char* hexString = argv[1];
    char* outFile = argv[2];

    if (strlen(hexString) != 20)
    {
        fprintf(stderr, "hex string must be 20 digits (10 byte) long\n");
        return -1;
    }

    /* Turn hex string into binary */
    unsigned char decoded[10];
    dehexify(decoded, (unsigned char*)hexString, 10);

    /* Manchester encode it */
    unsigned char encoded[20];
    manchester_encode(encoded, decoded, 10);
    
    int fd = open(outFile, O_WRONLY|O_CREAT, 0644);

    /* test padding */
    //writeAm(fd, 0, 5000000);
    //writeAm(fd, 1, 5000000);
    //writeAm(fd, 0, 5000000);

    /* preamble */
    for (int i = 0; i < 128; i++)
    {
        writeAm(fd, MAGNITUDE_0, nsecsPerBit);
        writeAm(fd, MAGNITUDE_1, nsecsPerBit);
    }
    printf("\n");
    writeAm(fd, 0, 4 * nsecsPerBit);

    /* Code */
    printf("Bit code: ");
    for (int i= 0; i < 160; i++) 
    {
        int bit = encoded[i / 8] & ( 1 << ( 7 - (i % 8)));
        writeAm(fd, bit ? MAGNITUDE_1 : MAGNITUDE_0, nsecsPerBit);
        printf("%d", bit ? 1 : 0);
        if (i % 8 == 7) {
            printf(" ");
        }
    }
    printf("\n");

    /* Padding */
    writeAm(fd, 0, 8 * nsecsPerBit);

    /* Code */
    for (int i= 0; i < 160; i++) 
    {
        int bit = encoded[i / 8] & ( 1 << ( 7 - (i % 8)));
        writeAm(fd, bit ? MAGNITUDE_1 : MAGNITUDE_0, nsecsPerBit);
    }

    close(fd);

    return 0;
}

