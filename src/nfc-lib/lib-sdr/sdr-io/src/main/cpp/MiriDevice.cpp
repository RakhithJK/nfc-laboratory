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

#include <mirisdr.h>

#include <rt/Logger.h>

#include <sdr/SignalType.h>
#include <sdr/SignalBuffer.h>
#include <sdr/MiriDevice.h>

namespace sdr {

#define MIRI_SUCCESS 0

#define MAX_QUEUE_SIZE 4

#define ASYNC_BUF_NUMBER 32
#define ASYNC_BUF_LENGTH (16 * 16384)

int process_transfer(unsigned char *buf, uint32_t len, void *ctx);

struct MiriDevice::Impl
{
   rt::Logger log {"AirspyDevice"};

   std::string deviceName;
   std::string deviceVersion;
   int fileDesc = 0;
   int centerFreq = 0;
   int sampleRate = 0;
   int sampleSize = 16;
   int sampleType = RadioDevice::Float;
   int gainMode = 0;
   int gainValue = 0;
   int tunerAgc = 0;
   int mixerAgc = 0;
   int decimation = 0;
   int streamTime = 0;

   mirisdr_dev_t *deviceHandle = nullptr;

   std::mutex streamMutex;
   std::queue<SignalBuffer> streamQueue;
   RadioDevice::StreamHandler streamCallback;

   long samplesReceived = 0;
   long samplesDropped = 0;

   explicit Impl(std::string name) : deviceName(std::move(name))
   {
      log.debug("created MiriDevice for name [{}]", {this->deviceName});
   }

   explicit Impl(int fileDesc) : fileDesc(fileDesc)
   {
      log.debug("created MiriDevice for file descriptor [{}]", {fileDesc});
   }

   ~Impl()
   {
      log.debug("destroy MiriDevice");

      close();
   }

   static std::vector<std::string> listDevices()
   {
      std::vector<std::string> result;

      unsigned int count = mirisdr_get_device_count();

      for (int i = 0; i < count; i++)
      {
         char buffer[256];

         const char *name = mirisdr_get_device_name(i);

         snprintf(buffer, sizeof(buffer), "miri://%s", name);

         result.emplace_back(buffer);
      }

      return result;
   }

   bool open(SignalDevice::OpenMode mode)
   {
      mirisdr_dev_t *handle;

      if (deviceName.find("://") != -1 && deviceName.find("miri://") == -1)
      {
         log.warn("invalid device name [{}]", {deviceName});
         return false;
      }

      close();

      int index = 0;

      if (mirisdr_open(&handle, index) == MIRI_SUCCESS)
      {
         deviceHandle = handle;

         char vendor[256], product[256], serial[32];

         // read board serial
         if (mirisdr_get_device_usb_strings(index, vendor, product, serial) != MIRI_SUCCESS)
            log.warn("failed mirisdr_get_device_usb_strings!");

         // set HW flavour
         if (mirisdr_set_hw_flavour(handle, MIRISDR_HW_DEFAULT) != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_hw_flavour!");

         // set bandwidth
         if (mirisdr_set_bandwidth(handle, 8000000) != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_bandwidth!");

         // set sample format, 10+2 bit
         if (mirisdr_set_sample_format(handle, (char *) "384_S16") != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_sample_format!");

         // set USB transfer type
         if (mirisdr_set_transfer(handle, (char *) "ISOC") != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_transfer!");

         // set IF mode
         if (mirisdr_set_if_freq(handle, 0) != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_if_freq!");

         // configure frequency
         setCenterFreq(centerFreq);

         // configure samplerate
         setSampleRate(sampleRate);

         // configure gain mode
         setGainMode(gainMode);

         // configure gain value
         setGainValue(gainValue);

         log.info("openned miri device {}, vendor {} product {} serial {}", {deviceName, std::string(vendor), std::string(product), std::string(serial)});

         return true;
      }

      log.warn("failed mirisdr_open!");

      return false;
   }

   void close()
   {
      if (deviceHandle)
      {
         // stop streaming if active...
         stop();

         log.info("close device {}", {deviceName});

         // close device
         if (mirisdr_close(deviceHandle) != MIRI_SUCCESS)
            log.warn("failed mirisdr_close!");

         deviceName = "";
         deviceVersion = "";
         deviceHandle = nullptr;
      }
   }

   int start(RadioDevice::StreamHandler handler)
   {
      if (deviceHandle)
      {
         log.info("start streaming for device {}", {deviceName});

         // clear counters
         samplesDropped = 0;
         samplesReceived = 0;

         // reset stream status
         streamCallback = std::move(handler);
         streamQueue = std::queue<SignalBuffer>();

         // Reset endpoint before we start reading from it (mandatory)
         if (mirisdr_reset_buffer(deviceHandle) != MIRI_SUCCESS)
            log.warn("failed mirisdr_reset_buffer!");

         // start reception
         if (mirisdr_read_async(deviceHandle, reinterpret_cast<mirisdr_read_async_cb_t>(process_transfer), this, ASYNC_BUF_NUMBER, ASYNC_BUF_LENGTH) != MIRI_SUCCESS)
         {
            log.warn("failed mirisdr_read_async!");

            // clear callback to disable receiver
            streamCallback = nullptr;

            // start failed!
            return -1;
         }

         // sets stream start time
         streamTime = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

         return 0;
      }

      return -1;
   }

   int stop()
   {
      if (deviceHandle && streamCallback)
      {
         log.info("stop streaming for device {}", {deviceName});

         // stop reception
         if (mirisdr_cancel_async(deviceHandle) != MIRI_SUCCESS)
            log.warn("failed mirisdr_cancel_async!");

         // disable stream callback and queue
         streamCallback = nullptr;
         streamQueue = std::queue<SignalBuffer>();
         streamTime = 0;

         return 0;
      }

      return -1;
   }

   bool isOpen() const
   {
      return deviceHandle;
   }

   bool isEof() const
   {
      return !deviceHandle || !streamCallback;
   }

   bool isReady() const
   {
      return deviceHandle;
   }

   bool isStreaming() const
   {
      return deviceHandle && streamCallback;
   }

   int setCenterFreq(long value)
   {
      centerFreq = value;

      if (deviceHandle)
      {
         if (mirisdr_set_center_freq(deviceHandle, centerFreq) != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_center_freq!");

         return 0;
      }

      return -1;
   }

   int setSampleRate(long value)
   {
      sampleRate = value;

      if (deviceHandle)
      {
         if (mirisdr_set_sample_rate(deviceHandle, sampleRate) != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_sample_rate!");

         return 0;
      }

      return -1;
   }

   int setGainMode(int mode)
   {
      gainMode = mode;

      if (deviceHandle)
      {
         if (mirisdr_set_tuner_gain_mode(deviceHandle, mode) != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_tuner_gain_mode!");

         if (gainMode == MiriDevice::Manual)
         {
            return setGainValue(gainValue);
         }
      }

      return -1;
   }

   int setGainValue(int value)
   {
      gainValue = value;

      if (deviceHandle)
      {
         if (mirisdr_set_tuner_gain(deviceHandle, gainValue) != MIRI_SUCCESS)
            log.warn("failed mirisdr_set_tuner_gain!");

         return 0;
      }

      return -1;
   }

   int setTunerAgc(int value)
   {
      tunerAgc = value;

      return -1;
   }

   int setMixerAgc(int value)
   {
      mixerAgc = value;

      return 0;
   }

   int setDecimation(int value)
   {
      decimation = value;

      return 0;
   }

   int setTestMode(int value)
   {
      log.warn("test mode not supported on this device!");

      return -1;
   }

   std::map<int, std::string> supportedSampleRates() const
   {
      std::map<int, std::string> result;

      result[5000000] = "000000"; // 5 MSPS
      result[10000000] = "10000000"; // 10 MSPS

      return result;
   }

   std::map<int, std::string> supportedGainModes() const
   {
      std::map<int, std::string> result;

      result[MiriDevice::Auto] = "Auto";
      result[MiriDevice::Manual] = "Manual";

      return result;
   }

   std::map<int, std::string> supportedGainValues() const
   {
      int gains[512];

      std::map<int, std::string> result;

      int count = mirisdr_get_tuner_gains(deviceHandle, gains);

      for (int i = 0; i < count; i++)
      {
         int value = gains[i];

         char buffer[64];

         snprintf(buffer, sizeof(buffer), "%d db", value);

         result[value] = buffer;
      }

      return result;
   }

   int read(SignalBuffer &buffer)
   {
      // lock buffer access
      std::lock_guard<std::mutex> lock(streamMutex);

      if (!streamQueue.empty())
      {
         buffer = streamQueue.front();

         streamQueue.pop();

         return buffer.limit();
      }

      return -1;
   }

   int write(SignalBuffer &buffer)
   {
      log.warn("write not supported on this device!");

      return -1;
   }

};

MiriDevice::MiriDevice(const std::string &name) : impl(std::make_shared<Impl>(name))
{
}

MiriDevice::MiriDevice(int fd) : impl(std::make_shared<Impl>(fd))
{
}

std::vector<std::string> MiriDevice::listDevices()
{
   return Impl::listDevices();
}

const std::string &MiriDevice::name()
{
   return impl->deviceName;
}

const std::string &MiriDevice::version()
{
   return impl->deviceVersion;
}

bool MiriDevice::open(SignalDevice::OpenMode mode)
{
   return impl->open(mode);
}

void MiriDevice::close()
{
   impl->close();
}

int MiriDevice::start(RadioDevice::StreamHandler handler)
{
   return impl->start(handler);
}

int MiriDevice::stop()
{
   return impl->stop();
}

bool MiriDevice::isOpen() const
{
   return impl->isOpen();
}

bool MiriDevice::isEof() const
{
   return impl->isEof();
}

bool MiriDevice::isReady() const
{
   return impl->isReady();
}

bool MiriDevice::isStreaming() const
{
   return impl->isStreaming();
}

int MiriDevice::sampleSize() const
{
   return impl->sampleSize;;
}

int MiriDevice::setSampleSize(int value)
{
   return impl->setSampleRate(value);
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
   return Float;
}

int MiriDevice::setSampleType(int value)
{
   impl->log.warn("setSampleType has no effect!");

   return -1;
}

long MiriDevice::streamTime() const
{
   return impl->streamTime;
}

int MiriDevice::setStreamTime(long value)
{
   return 0;
}

long MiriDevice::centerFreq() const
{
   return impl->centerFreq;
}

int MiriDevice::setCenterFreq(long value)
{
   return impl->setCenterFreq(value);
}

int MiriDevice::tunerAgc() const
{
   return impl->tunerAgc;
}

int MiriDevice::setTunerAgc(int value)
{
   return impl->setTunerAgc(value);
}

int MiriDevice::mixerAgc() const
{
   return impl->mixerAgc;
}

int MiriDevice::setMixerAgc(int value)
{
   return impl->setMixerAgc(value);
}

int MiriDevice::gainMode() const
{
   return impl->gainMode;
}

int MiriDevice::setGainMode(int value)
{
   return impl->setGainMode(value);
}

int MiriDevice::gainValue() const
{
   return impl->gainValue;
}

int MiriDevice::setGainValue(int value)
{
   return impl->setGainValue(value);
}

int MiriDevice::decimation() const
{
   return impl->decimation;
}

int MiriDevice::setDecimation(int value)
{
   return impl->setDecimation(value);
}

int MiriDevice::testMode() const
{
   return 0;
}

int MiriDevice::setTestMode(int value)
{
   return impl->setTestMode(value);
}

long MiriDevice::samplesReceived()
{
   return impl->samplesReceived;
}

long MiriDevice::samplesDropped()
{
   return impl->samplesDropped;
}

std::map<int, std::string> MiriDevice::supportedSampleRates() const
{
   return impl->supportedSampleRates();
}

std::map<int, std::string> MiriDevice::supportedGainValues() const
{
   return impl->supportedGainValues();
}

std::map<int, std::string> MiriDevice::supportedGainModes() const
{
   return impl->supportedGainModes();
}

int MiriDevice::read(SignalBuffer &buffer)
{
   return impl->read(buffer);
}

int MiriDevice::write(SignalBuffer &buffer)
{
   return impl->write(buffer);
}

int process_transfer(unsigned char *buf, uint32_t len, void *ctx)
{
   // check device validity
   if (auto *device = static_cast<MiriDevice::Impl *>(ctx))
   {
      SignalBuffer buffer;

//      SignalBuffer buffer = SignalBuffer((float *) transfer->samples, transfer->sample_count * 2, 2, device->sampleRate, device->samplesReceived, 0, SignalType::SAMPLE_IQ);

      // update counters
      device->samplesReceived += len;

      // stream to buffer callback
      if (device->streamCallback)
      {
         device->streamCallback(buffer);
      }

         // or store buffer in receive queue
      else
      {
         // lock buffer access
         std::lock_guard<std::mutex> lock(device->streamMutex);

         // discard oldest buffers
         if (device->streamQueue.size() >= MAX_QUEUE_SIZE)
         {
            device->samplesDropped += device->streamQueue.front().elements();
            device->streamQueue.pop();
         }

         // queue new sample buffer
         device->streamQueue.push(buffer);
      }

      // continue streaming
      return 0;
   }

   return -1;
}

}