#include <stdio.h>
#include <string.h>
#include <CoreAudio/CoreAudio.h>

const Boolean isOutput = 0;
const Boolean isInput = 1;


const char *errToString(OSStatus err)
{
   const char *errstring;
   switch (err) {
   case kAudioHardwareNoError:
      errstring = "No error.";
      break;
   case kAudioHardwareNotRunningError:
      errstring = "Hardware not running.";
      break;
   case kAudioHardwareUnspecifiedError:
      errstring = "Unspecified error.";
      break;
   case kAudioHardwareUnknownPropertyError:
      errstring = "Unknown hardware property.";
      break;
   case kAudioDeviceUnsupportedFormatError:
      errstring = "Unsupported audio format.";
      break;
   case kAudioHardwareBadPropertySizeError:
      errstring = "Bad hardware propery size.";
      break;
   case kAudioHardwareIllegalOperationError:
      errstring = "Illegal operation.";
      break;
   default:
      errstring = "Unknown error.";
   }
   return errstring;
}


char *
GetDeviceName(AudioDeviceID devID)
{
   UInt32 size = 0;

   OSStatus err = AudioDeviceGetPropertyInfo(devID,
                                             0,
                                             isOutput,  // arbitrary?
                                             kAudioDevicePropertyDeviceName,
                                             &size, NULL);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get device name property info for device %d.\n",
                                                                        devID);
      return NULL;
   }

   char *name = new char[size + 1];
   err = AudioDeviceGetProperty(devID,
                                0,
                                isOutput,    // arbitrary?
                                kAudioDevicePropertyDeviceName,
                                &size, name);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get device name property for device %d.\n", devID);
      return NULL;
   }

   return name;
}


char *
GetStreamName(AudioDeviceID devID, UInt32 chan, Boolean isInput)
{
   UInt32 size = 0;

   OSStatus err = AudioDeviceGetPropertyInfo(devID,
                                    chan,
                                    isInput,
                                    kAudioDevicePropertyChannelCategoryName,
                                    &size, NULL);
   if (err != kAudioHardwareNoError) {
#if 0  // It's okay for a device not to have channel category names.
      fprintf(stderr,
              "Can't get channel category name property info for device %d.\n",
                                                                        devID);
#endif
      return NULL;
   }

   char *name = new char[size + 1];
   err = AudioDeviceGetProperty(devID,
                                chan,
                                isInput,
                                kAudioDevicePropertyChannelCategoryName,
                                &size, name);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr,
            "Can't get channel category name property for device %d.\n", devID);
      return NULL;
   }

   return name;
}


char *
GetStreamChannelName(AudioDeviceID devID, UInt32 chan, Boolean isInput)
{
   UInt32 size = 0;

   OSStatus err = AudioDeviceGetPropertyInfo(devID,
                                             chan,
                                             isInput,
                                             kAudioDevicePropertyChannelName,
                                             &size, NULL);
   if (err != kAudioHardwareNoError) {
#if 0  // It's okay for a device not to have channel names.
      fprintf(stderr, "Can't get channel name property info for device %d.\n",
                                                                        devID);
#endif
      return NULL;
   }

   char *chanName = new char[size + 1];
   err = AudioDeviceGetProperty(devID,
                                chan,
                                isInput,
                                kAudioDevicePropertyChannelName,
                                &size, chanName);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get device name property for device %d.\n", devID);
      return NULL;
   }

#ifdef NOTYET // this was returning empty strings for MOTU 828
   err = AudioDeviceGetPropertyInfo(devID,
                                    chan,
                                    isInput,
                                    kAudioDevicePropertyChannelNumberName,
                                    &size, NULL);
   if (err != kAudioHardwareNoError) {
#if 1  // It's okay for a device not to have channel number names.
      fprintf(stderr,
               "Can't get channel number name property info for device %d.\n",
                                                                        devID);
#endif
      return NULL;
   }

   char *chanNumName = new char[size + 1];
   err = AudioDeviceGetProperty(devID,
                                chan,
                                isInput,
                                kAudioDevicePropertyChannelNumberName,
                                &size, chanNumName);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get device name property for device %d.\n", devID);
      return NULL;
   }

   printf("chanNumName: %s\n", chanNumName);
#endif

   return chanName;
}


AudioDeviceID
GetDefaultDeviceID(Boolean isInput)
{
   AudioDeviceID devID;
   AudioDevicePropertyID prop;

   prop = isInput ? kAudioHardwarePropertyDefaultInputDevice
                  : kAudioHardwarePropertyDefaultOutputDevice;
   UInt32 size = sizeof(devID);
   OSStatus err = AudioHardwareGetProperty(prop, &size, &devID);
   if (err != kAudioHardwareNoError || devID == kAudioDeviceUnknown) {
      fprintf(stderr, "Can't find default %s device: %s\n",
            isInput ? "input" : "output", errToString(err));
      return 0;
   }

   return devID;
}


int
PrintStreamList(AudioDeviceID devID, Boolean isInput, UInt32 *totChans)
{
   UInt32 size = 0;
   Boolean writeable = 0;

   // Get size of AudioBufferList, in bytes.
   OSStatus err = AudioDeviceGetPropertyInfo(devID,
                           0, isInput,
                           kAudioDevicePropertyStreamConfiguration,
                           &size,
                           &writeable);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get %s device (%d) property info: %s\n",
            isInput ? "input" : "output", devID, errToString(err));
      return -1;
   }

//printf("stream config prop size: %d, writeable: %d\n", size, writeable);

   // Fill list with description of buffers.
   AudioBufferList *list = new AudioBufferList [size / sizeof(AudioBufferList)];
   err = AudioDeviceGetProperty(devID,
                           0, isInput,
                           kAudioDevicePropertyStreamConfiguration,
                           &size,
                           list);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get %s device (%d) configuration: %s\n",
            isInput ? "input" : "output", devID, errToString(err));
      return -1;
   }

   printf("   %s device (%d) has %d stream%s:\n",
            isInput ? "Input" : "Output", devID, list->mNumberBuffers,
            list->mNumberBuffers == 1 ? "" : "s");

   // Print number of channels in each buffer.
   *totChans = 0;
   for (UInt32 i = 0; i < list->mNumberBuffers; i++) {
      UInt32 numChans = list->mBuffers[i].mNumberChannels;
      printf("      Stream %d has %d channels\n", i, numChans);
      *totChans += numChans;
   }
   printf("   Total number of channels: %d\n", *totChans);

   delete [] list;

   return 0;
}


int
PrintStreamChannelInformation(AudioDeviceID devID, UInt32 chan, Boolean isInput)
{
   UInt32 size = sizeof(AudioStreamBasicDescription);
   AudioStreamBasicDescription strDesc;

   strDesc.mSampleRate = 0;
   strDesc.mFormatID = 0;
   strDesc.mFormatFlags = 0;
   strDesc.mBytesPerPacket = 0;
   strDesc.mFramesPerPacket = 0;
   strDesc.mBytesPerFrame = 0;
   strDesc.mChannelsPerFrame = 0;
   strDesc.mBitsPerChannel = 0;
   strDesc.mReserved = 0;

   OSStatus err = AudioDeviceGetProperty(devID,
                           chan, isInput,
                           kAudioDevicePropertyStreamFormatMatch,
                           &size,
                           &strDesc);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get %s device stream format for chan %d: %s\n",
            isInput? "input" : "output", chan, errToString(err));
      return -1;
   }

#ifdef NOTYET
   // this wasn't too informative, but maybe we should investigate it more
   char *streamName = GetStreamName(devID, chan, isInput);
   if (streamName) {
      printf("Stream name: \"%s\"\n", streamName);
      delete [] streamName;
   }
#endif

   char *chanName = GetStreamChannelName(devID, chan, isInput);
   if (chanName) {
      if (chan == 1)    // first line for device section
         printf("   Stream containing chan %d has %d chans per frame (\"%s\")\n",
               chan, strDesc.mChannelsPerFrame, chanName);
      else
         printf("   \"         \"            %d     %d       \"         (\"%s\")\n",
               chan, strDesc.mChannelsPerFrame, chanName);
      delete [] chanName;
   }
   else {
      if (chan == 1)
         printf("   Stream containing chan %d has %d chans per frame\n",
               chan, strDesc.mChannelsPerFrame);
      else
         printf("   \"         \"            %d     %d       \"\n",
               chan, strDesc.mChannelsPerFrame);
   }

   return 0;
}


int
GetDeviceList(AudioDeviceID **devList, int *devCount)
{
   UInt32 size;

   OSStatus err = AudioHardwareGetPropertyInfo(
                        kAudioHardwarePropertyDevices,
                        &size, NULL);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get hardware device list property info.\n");
      return -1;
   }
   *devCount = size / sizeof(AudioDeviceID);
   *devList = new AudioDeviceID[*devCount];
   err = AudioHardwareGetProperty(
                        kAudioHardwarePropertyDevices,
                        &size, *devList);
   if (err != kAudioHardwareNoError) {
      fprintf(stderr, "Can't get hardware device list.\n");
      return -1;
   }

   return 0;
}


int
main(int argc, char *argv[])
{
   AudioDeviceID *devList;
   int devCount;

   if (GetDeviceList(&devList, &devCount) != 0)
      return -1;
   printf("\nNumber of devices: %d\n", devCount);

   AudioDeviceID defaultInDevID = GetDefaultDeviceID(isInput);
   if (defaultInDevID == 0)
      return -1;
   AudioDeviceID defaultOutDevID = GetDefaultDeviceID(isOutput);
   if (defaultOutDevID == 0)
      return -1;

   for (int i = 0; i < devCount; i++) {
      AudioDeviceID devID = devList[i];
      char *devName = GetDeviceName(devID);
      if (devName == NULL)
         return -1;
      printf("\n");
      printf("=============================================================\n");
      printf("Device ID %d: \"%s\"", devID, devName);
      if (devID == defaultInDevID)
         printf(" (default input)");
      if (devID == defaultOutDevID)
         printf(" (default output)");
      printf("\n\n");

      printf("Input Section -----------------------------------------------\n");
      UInt32 totChans;
      if (PrintStreamList(devID, isInput, &totChans) != 0)
         return -1;
      for (UInt32 n = 1; n <= totChans; n++)
         if (PrintStreamChannelInformation(devID, n, isInput) != 0)
            return -1;
      printf("\n");

      printf("Output Section ----------------------------------------------\n");
      if (PrintStreamList(devID, isOutput, &totChans) != 0)
         return -1;
      for (UInt32 n = 1; n <= totChans; n++)
         if (PrintStreamChannelInformation(devID, n, isOutput) != 0)
            return -1;

      delete [] devName;
   }

   delete [] devList;

   printf("\n");

   return 0;
}

