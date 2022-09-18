/*

  Copyright (c) 2021 Jose Vicente Campos Martinez - <josevcm@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

*/

#include <queue>
#include <mutex>
#include <chrono>

#include <airspy.h>

#include <rt/Logger.h>

#include <sdr/SignalType.h>
#include <sdr/SignalBuffer.h>
#include <sdr/MiriDevice.h>

namespace sdr {

struct MiriDevice::Impl
{
};

MiriDevice::MiriDevice(const std::string &name)
{

}

std::vector<std::string> MiriDevice::listDevices()
{
   return std::vector<std::string>();
}

const std::string &MiriDevice::name()
{
   return "";
}

const std::string &MiriDevice::version()
{
   return "";
}

bool MiriDevice::open(SignalDevice::OpenMode mode)
{
   return false;
}

void MiriDevice::close()
{

}

int MiriDevice::start(RadioDevice::StreamHandler handler)
{
   return 0;
}

int MiriDevice::stop()
{
   return 0;
}

bool MiriDevice::isOpen() const
{
   return false;
}

bool MiriDevice::isEof() const
{
   return false;
}

bool MiriDevice::isReady() const
{
   return false;
}

bool MiriDevice::isStreaming() const
{
   return false;
}

int MiriDevice::sampleSize() const
{
   return 0;
}

int MiriDevice::setSampleSize(int value)
{
   return 0;
}

long MiriDevice::sampleRate() const
{
   return 0;
}

int MiriDevice::setSampleRate(long value)
{
   return 0;
}

int MiriDevice::sampleType() const
{
   return 0;
}

int MiriDevice::setSampleType(int value)
{
   return 0;
}

long MiriDevice::streamTime() const
{
   return 0;
}

int MiriDevice::setStreamTime(long value)
{
   return 0;
}

long MiriDevice::centerFreq() const
{
   return 0;
}

int MiriDevice::setCenterFreq(long value)
{
   return 0;
}

int MiriDevice::tunerAgc() const
{
   return 0;
}

int MiriDevice::setTunerAgc(int value)
{
   return 0;
}

int MiriDevice::mixerAgc() const
{
   return 0;
}

int MiriDevice::setMixerAgc(int value)
{
   return 0;
}

int MiriDevice::gainMode() const
{
   return 0;
}

int MiriDevice::setGainMode(int value)
{
   return 0;
}

int MiriDevice::gainValue() const
{
   return 0;
}

int MiriDevice::decimation() const
{
   return 0;
}

int MiriDevice::setGainValue(int value)
{
   return 0;
}

int MiriDevice::setDecimation(int value)
{
   return 0;
}

int MiriDevice::testMode() const
{
   return 0;
}

int MiriDevice::setTestMode(int value)
{
   return 0;
}

long MiriDevice::samplesReceived()
{
   return 0;
}

long MiriDevice::samplesDropped()
{
   return 0;
}

std::map<int, std::string> MiriDevice::supportedSampleRates() const
{
   return std::map<int, std::string>();
}

std::map<int, std::string> MiriDevice::supportedGainValues() const
{
   return std::map<int, std::string>();
}

std::map<int, std::string> MiriDevice::supportedGainModes() const
{
   return std::map<int, std::string>();
}

int MiriDevice::read(SignalBuffer &buffer)
{
   return 0;
}

int MiriDevice::write(SignalBuffer &buffer)
{
   return 0;
}

}