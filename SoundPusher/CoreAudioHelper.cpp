//
//  CoreAudioHelper.cpp
//  VirtualSound
//
//  Created by Daniel Vollmer on 14/12/2015.
//
//

#include "CoreAudioHelper.hpp"

#include <string>
#include <cstring>
#include <cctype>

static std::string GetOSStatusAsString(const OSStatus error)
{
  union
  {
    OSStatus error;
    char fcc[5]; // one extra for terminating 0
  } both;

  both.error = CFSwapInt32HostToBig(error);
  // see if it appears to be a 4-char-code
  if (std::isprint(both.fcc[0]) && std::isprint(both.fcc[1]) && std::isprint(both.fcc[2]) && std::isprint(both.fcc[3]))
  {
    both.fcc[4] = '\0';
    return std::string(both.fcc);
  }
  return std::to_string(error);
}

namespace CAHelper {

CoreAudioException::CoreAudioException(const OSStatus error)
: std::runtime_error(GetOSStatusAsString(error))
{ }

//==================================================================================================
#pragma mark -
#pragma mark Device queries
//==================================================================================================

std::vector<AudioStreamRangedDescription> GetStreamPhysicalFormats(const AudioStreamID stream)
{
  UInt32 dataSize = 0;
  OSStatus status = noErr;

  const AudioObjectPropertyAddress physicalFormatsAddress = { kAudioStreamPropertyAvailablePhysicalFormats, kAudioObjectPropertyScopeGlobal, 0 };
  // num physical formats
  status = AudioObjectGetPropertyDataSize(stream, &physicalFormatsAddress, 0, NULL, &dataSize);
  if (status != noErr)
    throw CoreAudioException(status);

  // get physical formats
  std::vector<AudioStreamRangedDescription> formats;
  formats.resize(dataSize / sizeof formats.front());
  status = AudioObjectGetPropertyData(stream, &physicalFormatsAddress, 0, NULL, &dataSize, formats.data());
  if (status != noErr)
    throw CoreAudioException(status);
  formats.resize(dataSize / sizeof formats.front());
  return formats;
}

std::vector<AudioStreamID> GetStreams(const AudioDeviceID device, const bool input)
{
  UInt32 dataSize = 0;
  OSStatus status = noErr;

  const AudioObjectPropertyAddress streamsAddress = { kAudioDevicePropertyStreams, input ? kAudioDevicePropertyScopeInput : kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMaster };
  // num streams
  status = AudioObjectGetPropertyDataSize(device, &streamsAddress, 0, NULL, &dataSize);
  if (dataSize == 0 || status != noErr)
    throw CoreAudioException(status);
  // get streams
  std::vector<AudioStreamID> streams;
  streams.resize(dataSize / sizeof streams.front());
  status = AudioObjectGetPropertyData(device, &streamsAddress, 0, NULL, &dataSize, streams.data());
  if (status != noErr)
    throw CoreAudioException(status);
  streams.resize(dataSize / sizeof streams.front()); // may not have returned full buffer
  return streams;
}

std::vector<AudioDeviceID> GetDevices()
{
  UInt32 dataSize = 0;
  OSStatus status = noErr;

  const AudioObjectPropertyAddress audioDevicesAddress = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };

  // num devices
  status = AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &audioDevicesAddress, 0, NULL, &dataSize);
  if (dataSize == 0 || status != noErr)
    throw CoreAudioException(status);
  // get devices
  std::vector<AudioDeviceID> devices;
  devices.resize(dataSize / sizeof devices.front());
  status = AudioObjectGetPropertyData(kAudioObjectSystemObject, &audioDevicesAddress, 0, NULL, &dataSize, devices.data());
  if (status != noErr)
    throw CoreAudioException(status);
  devices.resize(dataSize / sizeof devices.front()); // may not have returned full buffer
  return devices;
}

//==================================================================================================
#pragma mark -
#pragma mark DeviceHogger
//==================================================================================================

static const AudioObjectPropertyAddress DeviceHogModeAddress = { kAudioDevicePropertyHogMode, kAudioDevicePropertyScopeOutput, kAudioObjectPropertyElementMaster };

DeviceHogger::DeviceHogger(const AudioDeviceID device)
: _device(device), _hog_pid(-1)
{
  const auto pid = getpid();
  OSStatus status = 0;
  UInt32 dataSize = 0;

  dataSize = sizeof _hog_pid;
  status = AudioObjectGetPropertyData(_device, &DeviceHogModeAddress, 0, NULL, &dataSize, &_hog_pid);
  if (status != noErr)
    _hog_pid = -1;
  if (_hog_pid == pid)
  {
    _hog_pid = -1;
    throw std::logic_error("Device already hogged by me");
  }
  // could be either a different process, or -1: either way, attempt to get it
  _hog_pid = pid;
  dataSize = sizeof _hog_pid;
  status = AudioObjectSetPropertyData(_device, &DeviceHogModeAddress, 0, NULL, dataSize, &_hog_pid);
  if (status != noErr || _hog_pid != pid)
    throw std::runtime_error("Could not obtain exclusive access to device");
  printf("Hogging device %u\n", _device);
}

DeviceHogger::~DeviceHogger()
{
  if (_hog_pid != getpid())
    return;
  _hog_pid = -1; // don't want to try again
  // release hog-mode
  OSStatus status = 0;
  pid_t pid = -1;
  UInt32 dataSize = sizeof pid;
  status = AudioObjectSetPropertyData(_device, &DeviceHogModeAddress, 0, NULL, dataSize, &pid);
  if (status != noErr || pid != -1)
  {
    printf("Could not release exclusive access to device\n");
    return;
  }
  printf("No longer hogging device %u\n", _device);
}

//==================================================================================================
#pragma mark -
#pragma mark MixingSetter
//==================================================================================================

static const AudioObjectPropertyAddress DeviceSupportsMixingAddress = { kAudioDevicePropertySupportsMixing , kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };

MixingSetter::MixingSetter(const AudioDeviceID device, bool supportMixing)
: _device(device), _originalState(-1), _didChange(false)
{
  if (!AudioObjectHasProperty(_device, &DeviceSupportsMixingAddress))
    return;
  OSStatus status = 0;
  Boolean isSettable = false;
  status = AudioObjectIsPropertySettable(_device, &DeviceSupportsMixingAddress, &isSettable);
  if (status != noErr || !isSettable)
    return;
  UInt32 dataSize = 0;
  status = AudioObjectGetPropertyDataSize(_device, &DeviceSupportsMixingAddress, 0, NULL, &dataSize);
  if (status != noErr || dataSize != sizeof _originalState)
    return;
  status = AudioObjectGetPropertyData(_device, &DeviceSupportsMixingAddress, 0, NULL, &dataSize, &_originalState);
  if (status != noErr || !isSettable || _originalState != 0)
    return;
  UInt32 value = supportMixing;
  status = AudioObjectSetPropertyData(_device, &DeviceSupportsMixingAddress, 0, NULL, dataSize, &value);
  if (status != noErr)
    return;
  _didChange = true;
  printf("Changed mixing for device %u to %u\n", _device, value);
}

MixingSetter::~MixingSetter()
{
  if (!_didChange)
    return;
  _didChange = false;
  // restore original mixing
  OSStatus status = 0;
  UInt32 dataSize = sizeof _originalState;
  status = AudioObjectSetPropertyData(_device, &DeviceSupportsMixingAddress, 0, NULL, dataSize, &_originalState);
  if (status != noErr)
  {
    printf("Could not release exclusive access to device\n");
    return;
  }
  printf("Restored mixing for device %u to %u\n", _device, _originalState);
}

//==================================================================================================
#pragma mark -
#pragma mark FormatSetter
//==================================================================================================

static const AudioObjectPropertyAddress StreamPhysicalFormatAddress = { kAudioStreamPropertyPhysicalFormat, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMaster };

FormatSetter::FormatSetter(const AudioStreamID stream, const AudioStreamBasicDescription &format)
: _stream(stream), _originalFormat(), _didChange(false)
{
  OSStatus status = 0;
  UInt32 dataSize = sizeof _originalFormat;
  status = AudioObjectGetPropertyData(_stream, &StreamPhysicalFormatAddress, 0, NULL, &dataSize, &_originalFormat);
  if (status != noErr)
    throw std::runtime_error("Could not get current format");

  if (std::memcmp(&_originalFormat, &format, sizeof _originalFormat) == 0)
    return;

  dataSize = sizeof format;
  status = AudioObjectSetPropertyData(_stream, &StreamPhysicalFormatAddress, 0, NULL, dataSize, &format);
  if (status != noErr)
    throw std::runtime_error("Could not set new format for stream %u\n");
  _didChange = true;
  printf("Changed format for stream %u\n", _stream);
}

FormatSetter::~FormatSetter()
{
  if (!_didChange)
    return;
  _didChange = false;
  // restore original format
  OSStatus status = 0;
  UInt32 dataSize = sizeof _originalFormat;
  status = AudioObjectSetPropertyData(_stream, &StreamPhysicalFormatAddress, 0, NULL, dataSize, &_originalFormat);
  if (status != noErr)
  {
    printf("Could not restore original format on stream\n");
    return;
  }
  printf("Restored original format on stream %u\n", _stream);
}

} // end namespace