#include "pms5003.h"
#include <Arduino.h>

namespace PMS5003
{
bool isMeasuring = false;
Stream *serialStream = nullptr;
static pms5003data storage = {};
pms5003data *lastData = nullptr;

uint8_t passiveModeCmd[] = {0x42, 0x4D, 0xE1, 0x00, 0x00, 0x01, 0x70};
uint8_t requestReadCmd[] = {0x42, 0x4D, 0xE2, 0x00, 0x00, 0x01, 0x71};
uint8_t pmsSleepCmd[] = {0x42, 0x4D, 0xE4, 0x00, 0x00, 0x01, 0x73};
uint8_t pmsWakeCmd[] = {0x42, 0x4D, 0xE4, 0x00, 0x01, 0x01, 0x74};

void flushInput(Stream *stream)
{
    DEBUG("Clear " + String(serialStream->available()) + " bytes");
    while (stream->available())
    {
        stream->read();
    }
}

void initPMS5003(Stream *s)
{
    lastData = &storage;
    serialStream = s;
    delay(50);
    serialStream->write(passiveModeCmd, sizeof(passiveModeCmd));
    delay(50);
    flushInput(serialStream);
}

bool readPMSdata(Stream *s)
{
    if (!s->available())
    {
        return false;
    }

    // Now read all 32 bytes
    if (s->available() < 32)
    {
        return false;
    }

    // Read a byte at a time until we get to the special '0x42' start-byte
    if (s->peek() != 0x42)
    {
        s->read();
        return false;
    }

    uint8_t buffer[32];
    uint16_t sum = 0;
    s->readBytes(buffer, 32);

    // get checksum ready
    for (uint8_t i = 0; i < 30; i++)
    {
        sum += buffer[i];
    }

    // The data comes in endian'd, this solves it so it works on all platforms
    uint16_t buffer_u16[15];
    for (uint8_t i = 0; i < 15; i++)
    {
        buffer_u16[i] = buffer[2 + i * 2 + 1];
        buffer_u16[i] += (buffer[2 + i * 2] << 8);
    }

    // Put it into a nice struct :)
    memcpy((void *)lastData, (void *)buffer_u16, 30);

    if (sum != lastData->checksum)
    {
        isMeasuring = false;
#ifdef DEBUG_ENABLE
        DEBUG("Checksum failure");
        String result = "";
        for (int i = 0; i < sizeof(buffer); i++)
        {
            if (buffer[i] < 16)
                result += "0";
            result += String(buffer[i], HEX) + " ";
        }
        DEBUG(result);
#endif
        return false;
    }
    // success!
    return true;
}

void startMeasure()
{
    if (isMeasuring)
        return;
    isMeasuring = true;
    serialStream->write(pmsWakeCmd, sizeof(pmsWakeCmd));
    delay(50);
    serialStream->write(passiveModeCmd, sizeof(passiveModeCmd));
    delay(50);
    DEBUG("PMS5003 measuring started");
}

pms5003data *endMeasure()
{
    if (!isMeasuring)
        return lastData;

    if (serialStream->available() > 0)
        flushInput(serialStream);

    serialStream->write(requestReadCmd, sizeof(requestReadCmd));
    delay(50);
    uint8_t retries = 0;
    do
    {
        delay(20);
        retries++;
    } while (retries < 10 && serialStream->available() < 32);

    DEBUG("Buffer has " + String(serialStream->available()) + " bytes");
    if (readPMSdata(serialStream))
    {
        isMeasuring = false;
        serialStream->write(pmsSleepCmd, sizeof(pmsSleepCmd));
        return lastData;
    }
    return nullptr;
}
} // namespace PMS5003