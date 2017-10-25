/*
 *  Copyright 2017 Rey Tucker <git@reytucker.us>
 *
 *  This file is part of Subarufobrob
 *
 *  Subarufobrob is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  Subarufobrob is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with Subarufobrob.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

#include <getopt.h>
#include <libbladeRF.h>

#include "hex.h"
#include "protocol.h"
#include "runningavg.h"
#include "filter.h"
#include "demodulator.h"

const int DEFAULT_GAIN = 42;

/* The RX and TX channels are configured independently for these parameters */
struct channel_config {
    bladerf_channel channel;
    unsigned int frequency;
    unsigned int bandwidth;
    unsigned int samplerate;
    int gain;
    bool agc;
};

typedef struct
{
    float i;
    float q;
} ComplexSample;

void handlePacket(unsigned char* packet)
{
    unsigned char hexString[21];

    hexify(hexString, packet, 10);

    fprintf(stderr, "Valid packet received\n");
    fprintf(stderr, " * Code: %s\n", hexString);
    fprintf(stderr, " * Command: %s\n", commandName(getCommand(packet)));
    fprintf(stderr, " * Rolling code: %d\n", getCode(packet));

    FILE* f = fopen("latestcode.txt", "w");
    fprintf(f, "%s\n", hexString);
    fclose(f);

    f = fopen("receivedcodes.txt", "a");
    fprintf(f, "%s\n", hexString);
    fclose(f);
}

int configure_channel(struct bladerf *dev, struct channel_config *c)
{
    int status;
    status = bladerf_set_frequency(dev, c->channel, c->frequency);
    if (status != 0) {
        fprintf(stderr, "Failed to set frequency = %u: %s\n",
                c->frequency, bladerf_strerror(status));
        return status;
    }
    status = bladerf_set_sample_rate(dev, c->channel, c->samplerate, NULL);
    if (status != 0) {
        fprintf(stderr, "Failed to set samplerate = %u: %s\n",
                c->samplerate, bladerf_strerror(status));
        return status;
    }
    status = bladerf_set_bandwidth(dev, c->channel, c->bandwidth, NULL);
    if (status != 0) {
        fprintf(stderr, "Failed to set bandwidth = %u: %s\n",
                c->bandwidth, bladerf_strerror(status));
        return status;
    }
    status = bladerf_set_gain(dev, c->channel, c->gain);
    if (status != 0) {
        fprintf(stderr, "Failed to set gain: %s\n",
                bladerf_strerror(status));
        return status;
    }
    status = bladerf_set_gain_mode(dev, c->channel, (c->agc ? BLADERF_GAIN_FASTATTACK_AGC : BLADERF_GAIN_MGC));
    if (status != 0) {
        fprintf(stderr, "Failed to set gain mode: %s\n",
                bladerf_strerror(status));
        return status;
    }

    return status;
}

void printHelp(char *s)
{
    fprintf(stderr, "usage %s [options]\n", s);
    fprintf(stderr, "\t-h, --help                   : this\n");
    fprintf(stderr, "\t-d, --device <string>        : specify a bladeRF device identifier string\n");
    //fprintf(stderr, "\t-p, --ppm <ppm>              : set the frequency correction\n");
    fprintf(stderr, "\t-a, --agc                    : enable autogain\n");
    fprintf(stderr, "\t-t, --tunergain <gain value> : set system gain (default: %d)\n", DEFAULT_GAIN);
    //fprintf(stderr, "\t-v, --verbose <int>          : debug stuff. Look in the code if you're interested\n");
}

bool testClipping(int16_t sample)
{
    const float thresh = 0.95;
    return (sample > 2047*thresh || sample < -2048*thresh);
}

int main(int argc, char *argv[])
{
    int status;
    struct channel_config config;
    struct bladerf *dev = NULL;

    const unsigned int num_buffers   = 16;
    const unsigned int buffer_size   = 8192;  /* Must be a multiple of 1024 */
    const unsigned int num_transfers = 8;
    const unsigned int timeout_ms    = 3500;

    config.channel = BLADERF_CHANNEL_RX(0);
    config.frequency = 433.92e6;
    config.bandwidth = 200e3;
    config.samplerate = 4*972500;
    config.gain = DEFAULT_GAIN;
    config.agc = false;

    int const decimation1 = 20; // decimate to ~200Khz
    int const samplesPerSymbol = 200; // at ~200Khz we're getting exactly 200 samples per bit
    int const minPreambleBits = 42;
    int const maxPreambleTimingError = 40; // Might be a little high but reduces false positives by a LOT

    char* dev_str = NULL;
    //int ppm = 0;
    //int debug = 0;
    int decimator = 0;
    bool done = false;

    /* Parse options */
    while (1) {
        int option_index = 0;
        int c;

        static struct option long_options[] = {
            {"device",     required_argument, 0,  'd' },
            {"ppm",        required_argument, 0,  'p' },
            {"tunergain",  required_argument, 0,  't' },
            {"agc",        no_argument,       0,  'a' },
            {"help",       no_argument,       0,  'h' },
            {"verbose",    required_argument, 0,  'v' },
            {0,            0,                 0,  0 }
        };

        c = getopt_long(argc, argv, "p:t:ahd:v:",
                long_options, &option_index);
        if (c == -1)
            break;

        switch (c) {
            default:
            case 'h':
            case 0:
                printHelp(argv[0]);
                return c == 'h' ? 0 : -1;

            case 'd':
                dev_str = strdup(optarg);
                if (NULL == dev_str) {
                    perror("malloc");
                    goto out;
                }

                printf("selecting device '%s'\n", dev_str);
                break;

            // case 'p':
            //     ppm = atoi(optarg);
            //     break;

            case 't':
                config.gain = atoi(optarg);
                break;

            case 'a':
                config.agc = true;
                break;

            // case 'v':
            //     debug = atoi(optarg);
            //     break;
        }
    }

    /* Initialize handlers */
    SampleFilter lpfi1;
    SampleFilter lpfq1;
    SampleFilter_init(&lpfi1);
    SampleFilter_init(&lpfq1);

    DemodContext demodCtx;
    demodInit(&demodCtx, samplesPerSymbol, minPreambleBits, maxPreambleTimingError, &handlePacket);

    /* Request a device with the provided dev_str, if available.
     * Invalid strings should simply fail to match a device. */
    status = bladerf_open(&dev, dev_str);
    if (status != 0) {
        fprintf(stderr, "Unable to open device: %s\n",
                bladerf_strerror(status));
        return 1;
    }

    /* Set up RX channel parameters */
    status = configure_channel(dev, &config);
    if (status != 0) {
        fprintf(stderr, "Failed to configure RX channel. Exiting.\n");
        goto out;
    }

    /* Synchronous interface */
    status = bladerf_sync_config(dev, BLADERF_RX_X1, BLADERF_FORMAT_SC16_Q11,
                num_buffers, buffer_size, num_transfers, timeout_ms);
    if (status != 0) {
        fprintf(stderr, "Failed to configure RX sync interface: %s\n",
                bladerf_strerror(status));
        goto out;
    }

    status = bladerf_enable_module(dev, BLADERF_RX, true);
    if (status != 0) {
        fprintf(stderr, "Failed to enable RX: %s\n",
                bladerf_strerror(status));
        goto out;
    }

    /* "User" samples buffers and their associated sizes, in units of samples.
     * Recall that one sample = two int16_t values. */
    int16_t *rx_samples = NULL;
    const unsigned int samples_len = 10000; /* May be any (reasonable) size */

    /* Allocate a buffer to store received samples in */
    rx_samples = malloc(samples_len * 2 * 1 * sizeof(int16_t));
    if (rx_samples == NULL) {
        perror("malloc");
        goto out;
    }

    size_t clipping_warning_count = 0;

    while (status == 0 && !done) {
        /* Receive samples */
        status = bladerf_sync_rx(dev, rx_samples, samples_len, NULL, 5000);
        if (status == 0) {
            /* Process these samples */
            bool clipping = false;

            for (size_t i = 0; i < samples_len; ++i) {
                clipping = testClipping(rx_samples[2*i]) || testClipping(rx_samples[2*i+1]);
                ComplexSample cSample;
                cSample.i = (float)rx_samples[2*i];
                cSample.q = (float)rx_samples[2*i+1];

                SampleFilter_put(&lpfi1, cSample.i);
                SampleFilter_put(&lpfq1, cSample.q);

                if (decimation1 > ++decimator) {
                    continue;
                }

                decimator = 0;
                double si = SampleFilter_get(&lpfi1);
                double sq = SampleFilter_get(&lpfq1);

                /* Convert complex sample to magnitude squared */
                double sample = si*si + sq*sq;

                demodSample(&demodCtx, sample);
            }

            if (clipping) {
                // instead of spamming the console, do some crude rate-limiting
                if (clipping_warning_count == 0) {
                    fprintf(stderr, "Signal may be clipping! Try reducing gain\n");
                    clipping_warning_count += 100;
                }
            } else if (clipping_warning_count > 0) {
                clipping_warning_count--;
            }
        } else {
            fprintf(stderr, "Failed to RX samples: %s\n",
                    bladerf_strerror(status));
        }
    }
    if (status == 0) {
        /* Wait a few seconds for any remaining TX samples to finish
         * reaching the RF front-end */
        sleep(2000);
    }

out:
    /* Disable RX, shutting down our underlying RX stream */
    status = bladerf_enable_module(dev, BLADERF_RX, false);
    if (status != 0) {
        fprintf(stderr, "Failed to disable RX: %s\n",
                bladerf_strerror(status));
    }

    bladerf_close(dev);
    free(dev_str);
    return status;
}
